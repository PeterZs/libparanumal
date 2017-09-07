#include "ellipticTet3D.h"

void ellipticOperator3D(solver_t *solver, dfloat lambda, occa::memory &o_q, occa::memory &o_Aq, const char *options);
dfloat ellipticScaledAdd(solver_t *solver, dfloat alpha, occa::memory &o_a, dfloat beta, occa::memory &o_b);

void AxTet3D(void **args, occa::memory &o_x, occa::memory &o_Ax) {

  solver_t *solver = (solver_t *) args[0];
  dfloat *lambda = (dfloat *) args[1];
  char *options = (char *) args[2];

  ellipticOperator3D(solver,*lambda,o_x,o_Ax,options);
}

void coarsenTet3D(void **args, occa::memory &o_x, occa::memory &o_Rx) {

  solver_t *solver = (solver_t *) args[0];
  occa::memory *o_V = (occa::memory *) args[1];

  mesh_t *mesh = solver->mesh;
  precon_t *precon = solver->precon;

  precon->coarsenKernel(mesh->Nelements, *o_V, o_x, o_Rx);
}

void prolongateTet3D(void **args, occa::memory &o_x, occa::memory &o_Px) {

  solver_t *solver = (solver_t *) args[0];
  occa::memory *o_V = (occa::memory *) args[1];

  mesh_t *mesh = solver->mesh;
  precon_t *precon = solver->precon;

  precon->prolongateKernel(mesh->Nelements, *o_V, o_x, o_Px);
}

dfloat *buildCoarsenerTet3D(mesh3D** meshLevels, int N, int Nc) {

  //TODO We can build the coarsen matrix either from the interRaise or interpLower matrices. Need to check which is better

  //use the Raise for now (essentally an L2 projection)

  int NpFine   = meshLevels[N]->Np;
  int NpCoarse = meshLevels[Nc]->Np;

  dfloat *P    = (dfloat *) calloc(NpFine*NpCoarse,sizeof(dfloat));
  dfloat *Ptmp = (dfloat *) calloc(NpFine*NpCoarse,sizeof(dfloat));

  //initialize P as identity
  for (int i=0;i<NpCoarse;i++) P[i*NpCoarse+i] = 1.0;

  for (int n=Nc;n<N;n++) {

    int Npp1 = meshLevels[n+1]->Np;
    int Np   = meshLevels[n]->Np;

    //copy P
    for (int i=0;i<Np*NpCoarse;i++) Ptmp[i] = P[i];

    //Multiply by the raise op
    for (int i=0;i<Npp1;i++) {
      for (int j=0;j<NpCoarse;j++) {
        P[i*NpCoarse + j] = 0.;
        for (int k=0;k<Np;k++) {
          P[i*NpCoarse + j] += meshLevels[n]->interpRaise[i*Np+k]*Ptmp[k*NpCoarse + j];
        }
      }
    }
  }

  //the coarsen matrix is P^T
  dfloat *R    = (dfloat *) calloc(NpFine*NpCoarse,sizeof(dfloat));
  for (int i=0;i<NpCoarse;i++) {
    for (int j=0;j<NpFine;j++) {
      R[i*NpFine+j] = P[j*NpCoarse+i];
    }
  }

  free(P); free(Ptmp);

  return R;
}

void ellipticMultiGridSetupTet3D(solver_t *solver, precon_t* precon,
                                dfloat tau, dfloat lambda, iint *BCType,
                                const char *options, const char *parAlmondOptions) {

  iint rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  mesh3D *mesh = solver->mesh;

  //read all the nodes files and load them in a dummy mesh array
  mesh3D **meshLevels = (mesh3D**) calloc(mesh->N+1,sizeof(mesh3D*));
  for (int n=1;n<mesh->N+1;n++) {
    meshLevels[n] = (mesh3D *) calloc(1,sizeof(mesh3D));
    meshLevels[n]->Nverts = mesh->Nverts;
    meshLevels[n]->Nfaces = mesh->Nfaces;
    meshLoadReferenceNodesTet3D(meshLevels[n], n);
  }

  //set the number of MG levels and their degree
  int numLevels;
  int *levelDegree;

  if (strstr(options,"ALLDEGREES")) {
    numLevels = mesh->N;
    levelDegree= (int *) calloc(numLevels,sizeof(int));
    for (int n=0;n<numLevels;n++) levelDegree[n] = mesh->N - n; //all degrees
  } else if (strstr(options,"HALFDEGREES")) {
    numLevels = floor(mesh->N/2.)+1;
    levelDegree= (int *) calloc(numLevels,sizeof(int));
    for (int n=0;n<numLevels;n++) levelDegree[n] = mesh->N - 2*n; //decrease by two
    levelDegree[numLevels-1] = 1; //ensure the last level is degree 1
  } else if (strstr(options,"HALFDOFS")) {
    // pick the degrees so the dofs of each level halfs (roughly)
    //start by counting the number of levels neccessary
    numLevels = 1;
    int degree = mesh->N;
    int dofs = meshLevels[degree]->Np;
    while (dofs>4) {
      numLevels++;
      for (;degree>0;degree--)
        if (meshLevels[degree]->Np<=dofs/2)
          break;
      dofs = meshLevels[degree]->Np;
    }
    levelDegree= (int *) calloc(numLevels,sizeof(int));
    degree = mesh->N;
    numLevels = 1;
    levelDegree[0] = degree;
    dofs = meshLevels[degree]->Np;
    while (dofs>4) {
      for (;degree>0;degree--)
        if (meshLevels[degree]->Np<=dofs/2)
          break;
      dofs = meshLevels[degree]->Np;
      levelDegree[numLevels] = degree;
      numLevels++;
    }
  }

  //storage for lambda parameter
  dfloat *vlambda = (dfloat *) calloc(1,sizeof(dfloat));
  *vlambda = lambda;

  //storage for restriction matrices
  dfloat **R = (dfloat **) calloc(numLevels,sizeof(dfloat*));
  occa::memory *o_R = (occa::memory *) calloc(numLevels,sizeof(occa::memory));

  //maually build multigrid levels
  precon->parAlmond = parAlmondInit(mesh, parAlmondOptions);
  agmgLevel **levels = precon->parAlmond->levels;

  for (int n=0;n<numLevels;n++) {
    solver_t *solverL;

    if (n==0) {
      solverL = solver;
    } else {
      //build ops for this level
      printf("=============BUIDLING MULTIGRID LEVEL OF DEGREE %d==================\n", levelDegree[n]);
      solverL = ellipticBuildMultigridLevelTet3D(solver,levelDegree,n,options);

      //set the normalization constatnt for the allNeumann POisson problem on this coarse mesh
      iint totalElements = 0;
      MPI_Allreduce(&(mesh->Nelements), &totalElements, 1, MPI_IINT, MPI_SUM, MPI_COMM_WORLD);
      solverL->allNeumannScale = 1.0/sqrt(solverL->mesh->Np*totalElements);
    }

    //check if we're at the degree 1 problem
    if (n==numLevels-1) {
      // build degree 1 matrix problem
      nonZero_t *coarseA;
      iint nnzCoarseA;
      hgs_t *coarsehgs;
      dfloat *V1;

      iint *coarseGlobalStarts = (iint*) calloc(size+1, sizeof(iint));

      ellipticCoarsePreconditionerSetupTet3D(mesh, precon, tau, lambda, BCType,
                                             &V1, &coarseA, &nnzCoarseA,
                                             &coarsehgs, coarseGlobalStarts, options);

      precon->o_V1  = mesh->device.malloc(mesh->Nverts*mesh->Np*sizeof(dfloat), V1);

      iint *Rows = (iint *) calloc(nnzCoarseA, sizeof(iint));
      iint *Cols = (iint *) calloc(nnzCoarseA, sizeof(iint));
      dfloat *Vals = (dfloat*) calloc(nnzCoarseA,sizeof(dfloat));

      for (iint n=0;n<nnzCoarseA;n++) {
        Rows[n] = coarseA[n].row;
        Cols[n] = coarseA[n].col;
        Vals[n] = coarseA[n].val;
      }

      // build amg starting at level 1
      parAlmondAgmgSetup(precon->parAlmond,
                         coarseGlobalStarts,
                         nnzCoarseA,
                         Rows,
                         Cols,
                         Vals,
                         solver->allNeumann,
                         solver->allNeumannPenalty,
                         coarsehgs);

      free(coarseA); free(Rows); free(Cols); free(Vals);
    } else {
      //build the level manually
      precon->parAlmond->numLevels++;
      levels[n] = (agmgLevel *) calloc(1,sizeof(agmgLevel));
    }

    //use the matrix free Ax
    levels[n]->AxArgs = (void **) calloc(3,sizeof(void*));
    levels[n]->AxArgs[0] = (void *) solverL;
    levels[n]->AxArgs[1] = (void *) vlambda;
    levels[n]->AxArgs[2] = (void *) options;
    levels[n]->device_Ax = AxTet3D;

    levels[n]->smoothArgs = (void **) calloc(2,sizeof(void*));
    levels[n]->smoothArgs[0] = (void *) solverL;
    levels[n]->smoothArgs[1] = (void *) levels[n];

    levels[n]->Nrows = mesh->Nelements*solverL->mesh->Np;
    levels[n]->Ncols = (mesh->Nelements+mesh->totalHaloPairs)*solverL->mesh->Np;

    if (strstr(options,"CHEBYSHEV")) {
      levels[n]->device_smooth = smoothChebyshevTet3D;

      levels[n]->smootherResidual = (dfloat *) calloc(levels[n]->Ncols,sizeof(dfloat));

      // extra storage for smoothing op
      levels[n]->o_smootherResidual = mesh->device.malloc(levels[n]->Ncols*sizeof(dfloat),levels[n]->smootherResidual);
      levels[n]->o_smootherResidual2 = mesh->device.malloc(levels[n]->Ncols*sizeof(dfloat),levels[n]->smootherResidual);
      levels[n]->o_smootherUpdate = mesh->device.malloc(levels[n]->Ncols*sizeof(dfloat),levels[n]->smootherResidual);
    } else {
      levels[n]->device_smooth = smoothTet3D;

      // extra storage for smoothing op
      levels[n]->o_smootherResidual = mesh->device.malloc(levels[n]->Ncols*sizeof(dfloat));
    }

    levels[n]->smootherArgs = (void **) calloc(1,sizeof(void*));
    levels[n]->smootherArgs[0] = (void *) solverL;


    dfloat rateTolerance;    // 0 - accept not approximate patches, 1 - accept all approximate patches
    if(strstr(options, "EXACT")){
      rateTolerance = 0.0;
    } else {
      rateTolerance = 1.0;
    }


    //set up the fine problem smoothing
    if (strstr(options, "IPDG")) {
      if(strstr(options, "OVERLAPPINGPATCH")){
        ellipticSetupSmootherOverlappingPatchIpdg(solverL, solverL->precon, levels[n], tau, lambda, BCType, options);
      } else if(strstr(options, "FULLPATCH")){
        ellipticSetupSmootherFullPatchIpdg(solverL, solverL->precon, levels[n], tau, lambda, BCType, rateTolerance, options);
      } else if(strstr(options, "FACEPATCH")){
        ellipticSetupSmootherFacePatchIpdg(solverL, solverL->precon, levels[n], tau, lambda, BCType, rateTolerance, options);
      } else if(strstr(options, "LOCALPATCH")){
        ellipticSetupSmootherLocalPatchIpdg(solverL, solverL->precon, levels[n], tau, lambda, BCType, rateTolerance, options);
      } else { //default to damped jacobi
        ellipticSetupSmootherDampedJacobiIpdg(solverL, solverL->precon, levels[n], tau, lambda, BCType, options);
      }
    }

    if (n==0) continue; //dont need restriction and prolongation ops at top level

    //build coarsen and prologation ops
    int N = levelDegree[n-1];
    int Nc = levelDegree[n];

    R[n] = buildCoarsenerTet3D(meshLevels, N, Nc);
    o_R[n] = mesh->device.malloc(meshLevels[N]->Np*meshLevels[Nc]->Np*sizeof(dfloat), R[n]);

    levels[n]->coarsenArgs = (void **) calloc(2,sizeof(void*));
    levels[n]->coarsenArgs[0] = (void *) solverL;
    levels[n]->coarsenArgs[1] = (void *) &(o_R[n]);
    levels[n]->device_coarsen = coarsenTet3D;

    levels[n]->prolongateArgs = levels[n]->coarsenArgs;
    levels[n]->device_prolongate = prolongateTet3D;
  }

  //report the multigrid levels
  if (strstr(options,"VERBOSE")) {
    if(rank==0) {
      printf("------------------Multigrid Report---------------------\n");
      printf("-------------------------------------------------------\n");
      printf("level|  Degree  |    dimension   |      Smoother       \n");
      printf("     |  Degree  |  (min,max,avg) |      Smoother       \n");
      printf("-------------------------------------------------------\n");
    }

    for(int lev=0; lev<numLevels; lev++){

      iint Nrows = levels[lev]->Nrows;

      iint minNrows=0, maxNrows=0, totalNrows=0;
      dfloat avgNrows;
      MPI_Allreduce(&Nrows, &maxNrows, 1, MPI_IINT, MPI_MAX, MPI_COMM_WORLD);
      MPI_Allreduce(&Nrows, &totalNrows, 1, MPI_IINT, MPI_SUM, MPI_COMM_WORLD);
      avgNrows = (dfloat) totalNrows/size;

      if (Nrows==0) Nrows=maxNrows; //set this so it's ignored for the global min
      MPI_Allreduce(&Nrows, &minNrows, 1, MPI_IINT, MPI_MIN, MPI_COMM_WORLD);

      char *smootherString;
      if(strstr(options, "OVERLAPPINGPATCH")){
        smootherString = strdup("OVERLAPPINGPATCH");
      } else if(strstr(options, "FULLPATCH")){
        smootherString = strdup("FULLPATCH");
      } else if(strstr(options, "FACEPATCH")){
        smootherString = strdup("FACEPATCH");
      } else if(strstr(options, "LOCALPATCH")){
        smootherString = strdup("LOCALPATCH");
      } else { //default to damped jacobi
        smootherString = strdup("DAMPEDJACOBI");
      }

      char *smootherOptions1 = strdup(" ");
      char *smootherOptions2 = strdup(" ");
      if (strstr(options,"EXACT")) {
        smootherOptions1 = strdup("EXACT");
      }
      if (strstr(options,"CHEBYSHEV")) {
        smootherOptions2 = strdup("CHEBYSHEV");
      }

      if (rank==0){
        printf(" %3d |   %3d    |    %10.2f  |   %s  \n",
          lev, levelDegree[lev], (dfloat)minNrows, smootherString);
        printf("     |          |    %10.2f  |   %s %s  \n", (dfloat)maxNrows, smootherOptions1, smootherOptions2);
        printf("     |          |    %10.2f  |   \n", avgNrows);
      }
    }
    if(rank==0)
      printf("-------------------------------------------------------\n");
  }

  for (int n=1;n<mesh->N+1;n++) free(meshLevels[n]);
  free(meshLevels);
}