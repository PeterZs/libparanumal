#include "insBenchmark2D.h"

int main(int argc, char **argv){

  // start up MPI
  MPI_Init(&argc, &argv);

  if(argc<4){
    printf("usage 1: ./insBenchmark2D insSubCycleCubatureSurface2D.okl ../../meshes/cavityH005.msh N\n");
    printf("usage 2: ./insBenchmark2D insSubCycleCubatureSurface2D.okl ../../meshes/cavityH005.msh N Nblocks Nnodes\n");
    exit(-1);
  }
  // int specify polynomial degree
  int N = atoi(argv[3]);
  int Nblocks = 1;
  int Nnodes = 1;

  if (argc == 6) {
    Nblocks = atoi(argv[4]);
    Nnodes = atoi(argv[5]);
  }

  char *kernelFileName = strdup(argv[1]);

  // set up mesh
  mesh2D *mesh = meshSetupTri2D(argv[2], N);

  // capture header file
  char *boundaryHeaderFileName = strdup(DHOLMES "/examples/insTri2D/insUniform2D.h"); // default
  char *options = strdup("method = ALGEBRAIC, grad-div= BROKEN, out=REPORT+VTU, adv=CUBATURE, disc = DISCONT_GALERKIN"); // SUBCYCLING

  // SET OPTIONS
  // method = ALGEBRAIC, STIFFLYSTABLE (default for now)
  // grad-div   = WEAK, NODAL (default nodal)
  // out  = REPORT, REPORT+VTU
  // adv  = CUBATURE, COLLOCATION
  // disc = DISCONT_GALERKIN, CONT_GALERKIN
  //char *options = strdup("method = ALGEBRAIC, grad-div= BROKEN, out=REPORT, adv=CUBATURE, disc = DISCONT_GALERKIN"); // SUBCYCLING

  char *velSolverOptions =
    strdup("solver=PCG method=IPDG preconditioner=MASSMATRIX");
  char *velParAlmondOptions =
    strdup("solver= smoother= partition=");

  char *prSolverOptions =
    strdup("solver=PCG,FLEXIBLE method=IPDG preconditioner=MULTIGRID, HALFDOFS smoother=DAMPEDJACOBI,CHEBYSHEV");
    //strdup("solver=PCG,FLEXIBLE,VERBOSE method=IPDG preconditioner=NONE");
   // strdup("solver=PCG,FLEXIBLE,method=IPDG  preconditioner=FULLALMOND");
    //strdup("solver=PCG,FLEXIBLE, method=IPDG preconditioner=OMS,APPROXPATCH coarse=COARSEGRID,ALMOND");

  char *prParAlmondOptions =
    strdup("solver=KCYCLE smoother=CHEBYSHEV partition=STRONGNODES");

  int Ns = 0;

  printf("Setup INS Solver: \n");
  occa::kernelInfo kernelInfo;
  ins_t *ins = insSetup2D(mesh,Ns,options, Nblocks, Nnodes,
                          velSolverOptions,velParAlmondOptions,
                          prSolverOptions, prParAlmondOptions,
                          boundaryHeaderFileName, kernelInfo);

  insRunBenchmark2D(ins,options,kernelInfo,kernelFileName,Nblocks,Nnodes);

  // close down MPI
  MPI_Finalize();
  return 0;
}
