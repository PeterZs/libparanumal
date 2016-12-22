#include "mpi.h"
#include <math.h>
#include "mesh2D.h"

// NBN: toggle use of 2nd stream
#define USE_2_STREAMS
// #undef USE_2_STREAMS

void meshBoltzmannSetup2D(mesh2D *mesh){

  mesh->Nfields = 8;
  
  // compute samples of q at interpolation nodes
  mesh->q    = (dfloat*) calloc((mesh->totalHaloPairs+mesh->Nelements)*mesh->Np*mesh->Nfields,
				sizeof(dfloat));
  mesh->rhsq = (dfloat*) calloc(mesh->Nelements*mesh->Np*mesh->Nfields,
				sizeof(dfloat));
  mesh->resq = (dfloat*) calloc(mesh->Nelements*mesh->Np*mesh->Nfields,
				sizeof(dfloat));

  mesh->wx    = (dfloat*) calloc((mesh->totalHaloPairs+mesh->Nelements)*mesh->Np*mesh->Nfields,
				sizeof(dfloat));
  mesh->rhswx = (dfloat*) calloc(mesh->Nelements*mesh->Np*mesh->Nfields,
				 sizeof(dfloat));
  mesh->reswx = (dfloat*) calloc(mesh->Nelements*mesh->Np*mesh->Nfields,
				sizeof(dfloat));
  mesh->sigmax= (dfloat*) calloc(mesh->Nelements,
				 sizeof(dfloat));
  
  // set temperature, gas constant, wave speeds
  mesh->RT = 9.;
  mesh->sqrtRT = sqrt(mesh->RT);  
  
  // initial conditions
  // uniform flow
  dfloat rho = 1, u = 1.f, v = 0;
  dfloat sigma11 = 0, sigma12 = 0, sigma22 = 0;
  dfloat q1bar = rho;
  dfloat q2bar = rho*u/mesh->sqrtRT;
  dfloat q3bar = rho*v/mesh->sqrtRT;
  dfloat q4bar = (rho*u*v - sigma12)/(mesh->sqrtRT*mesh->sqrtRT);
  dfloat q5bar = (rho*u*u - sigma11)/(sqrt(2.)*mesh->sqrtRT*mesh->sqrtRT);
  dfloat q6bar = (rho*v*v - sigma22)/(sqrt(2.)*mesh->sqrtRT*mesh->sqrtRT);

  iint cnt = 0;
  for(iint e=0;e<mesh->Nelements;++e){
    for(iint n=0;n<mesh->Np;++n){
      dfloat t = 0;
      dfloat x = mesh->x[n + mesh->Np*e];
      dfloat y = mesh->y[n + mesh->Np*e];

#if 0
      boltzmannCavitySolution2D(x, y, t,
				mesh->q+cnt, mesh->q+cnt+1, mesh->q+cnt+2);
#endif

#if 0
      boltzmannGaussianPulse2D(x, y, t,
			       mesh->q+cnt,
			       mesh->q+cnt+1,
			       mesh->q+cnt+2,
			       mesh->q+cnt+3,
			       mesh->q+cnt+4,
			       mesh->q+cnt+5);
#endif
      mesh->q[cnt+0] = q1bar; // uniform density, zero flow
      mesh->q[cnt+1] = q2bar;
      mesh->q[cnt+2] = q3bar;
      mesh->q[cnt+3] = q4bar;
      mesh->q[cnt+4] = q5bar;
      mesh->q[cnt+5] = q6bar;
      cnt += mesh->Nfields;
    }
  }

  // set BGK collision relaxation rate
  // nu = R*T*tau
  // 1/tau = RT/nu
  dfloat nu = 3e-3;
  mesh->tauInv = mesh->RT/nu; // TW
  
  // set penalty parameter
  //  mesh->Lambda2 = 0.5/(sqrt(3.)*mesh->sqrtRT);
  mesh->Lambda2 = 0.5/(mesh->sqrtRT);

  // find elements with center inside PML zone
  dfloat xmin = -1, xmax = 6, xsigma = 0; // TW
  
  for(iint e=0;e<mesh->Nelements;++e){
    dfloat cx = 0, cy = 0;
    for(iint n=0;n<mesh->Nverts;++n){
      cx += mesh->EX[e*mesh->Nverts+n];
    }
    cx /= mesh->Nverts;
    
    // add element outside [xmin,xmax]x[ymin,ymax] to pml
    if((cx>xmin-1 && cx<xmin) || (cx>xmax && cx<xmax+1)){
      printf("element %d in layer at %g,%g\n", e, cx, cy);
      mesh->sigmax[e] = xsigma;
    }
  }

  // set time step
  dfloat hmin = 1e9;
  for(iint e=0;e<mesh->Nelements;++e){  

    for(iint f=0;f<mesh->Nfaces;++f){
      iint sid = mesh->Nsgeo*(mesh->Nfaces*e + f);
      dfloat sJ   = mesh->sgeo[sid + SJID];
      dfloat invJ = mesh->sgeo[sid + IJID];

      // sJ = L/2, J = A/2,   sJ/J = L/A = L/(0.5*h*L) = 2/h
      // h = 0.5/(sJ/J)
      
      dfloat hest = .5/(sJ*invJ);

      hmin = mymin(hmin, hest);
    }
  }

  dfloat cfl = .4; // depends on the stability region size

  // dt ~ cfl (h/(N+1)^2)/(Lambda^2*fastest wave speed)
  dfloat dt = cfl*hmin/((mesh->N+1.)*(mesh->N+1.)
			*mymax(mesh->Lambda2*mesh->RT*sqrt(2.), sqrt(3.)*mesh->sqrtRT));

  printf("hmin = %g\n", hmin);
  printf("cfl = %g\n", cfl);
  printf("dt = %g\n", dt);
  printf("max wave speed = %g\n", sqrt(3.)*mesh->sqrtRT);
  

  
  // MPI_Allreduce to get global minimum dt
  MPI_Allreduce(&dt, &(mesh->dt), 1, MPI_DFLOAT, MPI_MIN, MPI_COMM_WORLD);

  //
  mesh->finalTime = 20;
  mesh->NtimeSteps = mesh->finalTime/mesh->dt;
  mesh->dt = mesh->finalTime/mesh->NtimeSteps;

  // errorStep
  mesh->errorStep = 2000;

  printf("dt = %g\n", mesh->dt);

  // OCCA build stuff
  char deviceConfig[BUFSIZ];
  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  // use rank to choose DEVICE
  sprintf(deviceConfig, "mode = CUDA, deviceID = %d", 0);
  //  sprintf(deviceConfig, "mode = OpenCL, deviceID = 0, platformID = 1");
  //  sprintf(deviceConfig, "mode = OpenMP, deviceID = %d", 1);
  mesh->device.setup(deviceConfig);

  // build Dr, Ds, LIFT transposes
  dfloat *DrT = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
  dfloat *DsT = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
  for(int n=0;n<mesh->Np;++n){
    for(int m=0;m<mesh->Np;++m){
      DrT[n+m*mesh->Np] = mesh->Dr[n*mesh->Np+m];
      DsT[n+m*mesh->Np] = mesh->Ds[n*mesh->Np+m];
    }
  }

  dfloat *LIFTT = (dfloat*) calloc(mesh->Np*mesh->Nfaces*mesh->Nfp, sizeof(dfloat));
  for(int n=0;n<mesh->Np;++n){
    for(int m=0;m<mesh->Nfaces*mesh->Nfp;++m){
      LIFTT[n+m*mesh->Np] = mesh->LIFT[n*mesh->Nfp*mesh->Nfaces+m];
    }
  }

  // find elements that have all neighbors on this process
  iint *internalElementIds = (iint*) calloc(mesh->Nelements, sizeof(iint));
  iint *notInternalElementIds = (iint*) calloc(mesh->Nelements, sizeof(iint));

  iint Ninterior = 0, NnotInterior = 0;
  for(iint e=0;e<mesh->Nelements;++e){
    iint flag = 0;
    for(iint f=0;f<mesh->Nfaces;++f)
      if(mesh->EToP[e*mesh->Nfaces+f]!=-1)
	flag = 1;
    if(!flag)
      internalElementIds[Ninterior++] = e;
    else
      notInternalElementIds[NnotInterior++] = e;
  }

  printf("NinteriorElements = %d, NnotInternalElements = %d\n", Ninterior, NnotInterior);
  
  mesh->NinternalElements = Ninterior;
  mesh->NnotInternalElements = NnotInterior;
  mesh->o_internalElementIds    = mesh->device.malloc(Ninterior*sizeof(iint), internalElementIds);
  if(NnotInterior>0)
    mesh->o_notInternalElementIds = mesh->device.malloc(NnotInterior*sizeof(iint), notInternalElementIds);
  
  // OCCA allocate device memory (remember to go back for halo)
  mesh->o_q =
    mesh->device.malloc(mesh->Np*(mesh->totalHaloPairs+mesh->Nelements)*mesh->Nfields*sizeof(dfloat), mesh->q);
  mesh->o_rhsq =
    mesh->device.malloc(mesh->Np*mesh->Nelements*mesh->Nfields*sizeof(dfloat), mesh->rhsq);
  mesh->o_resq =
    mesh->device.malloc(mesh->Np*mesh->Nelements*mesh->Nfields*sizeof(dfloat), mesh->resq);

  // pml variables
  mesh->o_wx =    
    mesh->device.malloc(mesh->Np*(mesh->totalHaloPairs+mesh->Nelements)*mesh->Nfields*sizeof(dfloat), mesh->wx);
  mesh->o_rhswx =
    mesh->device.malloc(mesh->Np*mesh->Nelements*mesh->Nfields*sizeof(dfloat), mesh->rhswx);
  mesh->o_reswx =
    mesh->device.malloc(mesh->Np*mesh->Nelements*mesh->Nfields*sizeof(dfloat), mesh->reswx);

  mesh->o_sigmax =
    mesh->device.malloc(mesh->Nelements*sizeof(dfloat), mesh->sigmax);
  
  mesh->o_Dr = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
				   mesh->Dr);

  mesh->o_Ds = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
				   mesh->Ds);

  mesh->o_DrT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
				    DrT);

  mesh->o_DsT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat),
				    DsT);

  mesh->o_LIFT =
    mesh->device.malloc(mesh->Np*mesh->Nfaces*mesh->Nfp*sizeof(dfloat),
			mesh->LIFT);

  mesh->o_LIFTT =
    mesh->device.malloc(mesh->Np*mesh->Nfaces*mesh->Nfp*sizeof(dfloat),
			LIFTT);

  
  mesh->o_vgeo =
    mesh->device.malloc(mesh->Nelements*mesh->Nvgeo*sizeof(dfloat),
			mesh->vgeo);
  
  mesh->o_sgeo =
    mesh->device.malloc(mesh->Nelements*mesh->Nfaces*mesh->Nsgeo*sizeof(dfloat),
			mesh->sgeo);

  mesh->o_vmapM =
    mesh->device.malloc(mesh->Nelements*mesh->Nfp*mesh->Nfaces*sizeof(iint),
			mesh->vmapM);

  mesh->o_vmapP =
    mesh->device.malloc(mesh->Nelements*mesh->Nfp*mesh->Nfaces*sizeof(iint),
			mesh->vmapP);

  mesh->o_EToB =
    mesh->device.malloc(mesh->Nelements*mesh->Nfaces*sizeof(iint),
			mesh->EToB);

  mesh->o_x =
    mesh->device.malloc(mesh->Nelements*mesh->Np*sizeof(dfloat),
			mesh->x);

  mesh->o_y =
    mesh->device.malloc(mesh->Nelements*mesh->Np*sizeof(dfloat),
			mesh->y);
  
  if(mesh->totalHaloPairs>0){
    // copy halo element list to DEVICE
    mesh->o_haloElementList =
      mesh->device.malloc(mesh->totalHaloPairs*sizeof(iint), mesh->haloElementList);
    
    // temporary DEVICE buffer for halo (maximum size Nfields*Np for dfloat)
    mesh->o_haloBuffer =
      mesh->device.malloc(mesh->totalHaloPairs*mesh->Np*mesh->Nfields*sizeof(dfloat));
  }

  
  //-------------------------------------
  // NBN: 2 streams for async MPI updates
  // {Vol, Surf, update}  run on q[0]
  // {halo-get, copy} run on q[1]
  //-------------------------------------
  mesh->stream0 = mesh->device.getStream();
#ifdef USE_2_STREAMS
  mesh->stream1 = mesh->device.createStream();  // NBN: second stream
#else
  mesh->stream1 = mesh->stream0;                // NBN: stream1 == stream0
#endif
  mesh->device.setStream(mesh->stream0);
  //-------------------------------------
  
  occa::kernelInfo kernelInfo;

  kernelInfo.addDefine("p_Nfields", mesh->Nfields);
  kernelInfo.addDefine("p_N", mesh->N);
  kernelInfo.addDefine("p_Np", mesh->Np);
  kernelInfo.addDefine("p_Nfp", mesh->Nfp);
  kernelInfo.addDefine("p_Nfaces", mesh->Nfaces);
  kernelInfo.addDefine("p_NfacesNfp", mesh->Nfp*mesh->Nfaces);
  kernelInfo.addDefine("p_Nvgeo", mesh->Nvgeo);
  kernelInfo.addDefine("p_Nsgeo", mesh->Nsgeo);

  int maxNodes = mymax(mesh->Np, (mesh->Nfp*mesh->Nfaces));
  kernelInfo.addDefine("p_maxNodes", maxNodes);

  int NblockV = 512/mesh->Np; // works for CUDA
  kernelInfo.addDefine("p_NblockV", NblockV);

  int NblockS = 512/maxNodes; // works for CUDA
  kernelInfo.addDefine("p_NblockS", NblockS);

  // physics 
  kernelInfo.addDefine("p_Lambda2", 0.5f);
  kernelInfo.addDefine("p_sqrtRT", mesh->sqrtRT);
  kernelInfo.addDefine("p_sqrt2", (float)sqrt(2.));
  kernelInfo.addDefine("p_invsqrt2", (float)sqrt(1./2.));
  kernelInfo.addDefine("p_tauInv", mesh->tauInv);


  kernelInfo.addDefine("p_q1bar", q1bar);
  kernelInfo.addDefine("p_q2bar", q2bar);
  kernelInfo.addDefine("p_q3bar", q3bar);
  kernelInfo.addDefine("p_q4bar", q4bar);
  kernelInfo.addDefine("p_q5bar", q5bar);
  kernelInfo.addDefine("p_q6bar", q6bar);
  kernelInfo.addDefine("p_alpha0", .01);
  if(sizeof(dfloat)==4){
    kernelInfo.addDefine("dfloat","float");
    kernelInfo.addDefine("dfloat4","float4");
    kernelInfo.addDefine("dfloat8","float8");
  }
  if(sizeof(dfloat)==8){
    kernelInfo.addDefine("dfloat","double");
    kernelInfo.addDefine("dfloat4","double4");
    kernelInfo.addDefine("dfloat8","double8");
  }

  if(sizeof(iint)==4){
    kernelInfo.addDefine("iint","int");
  }
  if(sizeof(iint)==8){
    kernelInfo.addDefine("iint","long long int");
  }

  if(mesh->device.mode()=="CUDA"){ // add backend compiler optimization for CUDA
    kernelInfo.addCompilerFlag("--ftz=true");
    kernelInfo.addCompilerFlag("--prec-div=false");
    kernelInfo.addCompilerFlag("--prec-sqrt=false");
    kernelInfo.addCompilerFlag("--use_fast_math");
    kernelInfo.addCompilerFlag("--fmad=true"); // compiler option for cuda
  }

  mesh->boltzmannVolumeKernel =
    mesh->device.buildKernelFromSource("src/meshBoltzmannVolume2D.okl",
				       "meshBoltzmannVolume2D",
				       kernelInfo);
  printf("starting surface\n");
  mesh->boltzmannSurfaceKernel =
    mesh->device.buildKernelFromSource("src/meshBoltzmannSurface2D.okl",
				       "meshBoltzmannSurface2D",
				       kernelInfo);
  printf("ending surface\n");
#if 0
  mesh->boltzmannPartialSurfaceKernel =
    mesh->device.buildKernelFromSource("src/meshBoltzmannPartialSurface2D.okl",
				       "meshBoltzmannPartialSurface2D",
				       kernelInfo);
#endif

  mesh->boltzmannUpdateKernel =
    mesh->device.buildKernelFromSource("src/meshBoltzmannUpdate2D.okl",
				       "meshBoltzmannUpdate2D",
				       kernelInfo);

  mesh->boltzmannPmlVolumeKernel =
    mesh->device.buildKernelFromSource("src/meshBoltzmannPmlVolume2D.okl",
				       "meshBoltzmannPmlVolume2D",
				       kernelInfo);
  
  mesh->boltzmannPmlSurfaceKernel =
    mesh->device.buildKernelFromSource("src/meshBoltzmannPmlSurface2D.okl",
				       "meshBoltzmannPmlSurface2D",
				       kernelInfo);
  mesh->boltzmannPmlUpdateKernel =
    mesh->device.buildKernelFromSource("src/meshBoltzmannPmlUpdate2D.okl",
				       "meshBoltzmannPmlUpdate2D",
				       kernelInfo);

  mesh->haloExtractKernel =
    mesh->device.buildKernelFromSource("src/meshHaloExtract2D.okl",
				       "meshHaloExtract2D",
				       kernelInfo);
  
}