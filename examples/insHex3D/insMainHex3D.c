#include "insHex3D.h"

int main(int argc, char **argv){

  // start up MPI
  MPI_Init(&argc, &argv);

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  // SET OPTIONS
  // out  = VTU, SLICE, CONTOUR
  // adv  = CUBATURE, COLLOCATION
  //int Ns = 0; // no-subcycling 
  int Ns = 4; 
  if(argc==5)
    Ns = atoi(argv[4]); // Number of substeps
  char *options; 
  if(Ns==0)
    options = strdup("out=VTU, adv=CUBATURE");
  else
    options = strdup("out=VTU, adv=CUBATURE,SUBCYCLING");  //pres=PRESSURE_HISTORY


  char *velSolverOptions =
    strdup("solver=PCG method=IPDG basis=NODAL preconditioner=JACOBI");
  char *velParAlmondOptions =
    strdup("solver= smoother= partition=");

  char *prSolverOptions =
    strdup("solver=PCG,FLEXIBLE method=CONTINUOUS basis=NODAL preconditioner=MULTIGRID,HALFDOFS smoother=DAMPEDJACOBI,CHEBYSHEV");
    //strdup("solver=PCG,FLEXIBLE, method=CONTINUOUS basis=NODAL preconditioner=FULLALMOND");

  char *prParAlmondOptions =
    strdup("solver=KCYCLE smoother=CHEBYSHEV partition=STRONGNODES");

  if(argc!=3 && argc!=4 && argc!=5){
    printf("usage 1: ./main meshes/cavityH005.msh N\n");
    printf("usage 2: ./main meshes/cavityH005.msh N insUniformFlowBoundaryConditions.h\n");
    printf("usage 3: ./main meshes/cavityH005.msh N insUniformFlowBoundaryConditions.h Nsubstep\n");
    exit(-1);
  }
  // int specify polynomial degree
  int N = atoi(argv[2]);

  // set up mesh stuff
  mesh3D *mesh = meshSetupHex3D(argv[1], N); 

  // capture header file
  char *boundaryHeaderFileName;
  if(argc==3)
    boundaryHeaderFileName = strdup(DHOLMES "/examples/insHex3D/insUniform3D.h"); // default
  else
    boundaryHeaderFileName = strdup(argv[3]);

  if (rank==0) printf("Setup INS Solver: \n");
  ins_t *ins = insSetupHex3D(mesh,Ns,options, velSolverOptions,   velParAlmondOptions,
                          prSolverOptions, prParAlmondOptions, boundaryHeaderFileName);
  
  if (rank==0) printf("Running INS solver\n");
  insRunHex3D(ins,options);
    
  // close down MPI
  MPI_Finalize();

  exit(0);
  return 0;
}