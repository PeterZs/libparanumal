#include "massHex3D.h"

void massParallelGatherScatter(mesh3D *mesh, ogs_t *ogs, occa::memory &o_q, occa::memory &o_gsq, const char *type, const char *op){

  // use gather map for gather and scatter
  occaTimerTic(mesh->device,"meshParallelGatherScatter3D");
  meshParallelGatherScatter(mesh, ogs, o_q, o_gsq, type, op);
  occaTimerToc(mesh->device,"meshParallelGatherScatter3D");
}


void massHaloGatherScatter(solver_t *solver, 
			       ogs_t *halo, 
			       occa::memory &o_v,
			       const char *type,
			       const char *op){


  mesh3D *mesh = solver->mesh;

  if(halo->Ngather){
    // rough way to make sure 
    mesh->device.finish();
    
    // set stream to halo stream
    //    mesh->device.setStream(solver->dataStream);
    mesh->device.finish();
    
    // gather halo nodes on DEVICE
    mesh->gatherKernel(halo->Ngather, halo->o_gatherOffsets, halo->o_gatherLocalIds, o_v, halo->o_gatherTmp);

    mesh->device.finish();
    
    // copy partially gathered halo data from DEVICE to HOST
    halo->o_gatherTmp.asyncCopyTo(halo->gatherTmp);

    mesh->device.finish();
    
    // wait for async copy
    occa::streamTag tag = mesh->device.tagStream();
    mesh->device.waitFor(tag);
    
    // gather across MPI processes then scatter back
    gsParallelGatherScatter(halo->gatherGsh, halo->gatherTmp, dfloatString, op); // danger on hardwired type

    mesh->device.finish();
    
    // copy totally gather halo data back from HOST to DEVICE
    halo->o_gatherTmp.asyncCopyFrom(halo->gatherTmp); 

    mesh->device.finish();
    
    tag = mesh->device.tagStream();
    mesh->device.waitFor(tag);
    
    // do scatter back to local nodes
    mesh->scatterKernel(halo->Ngather, halo->o_gatherOffsets, halo->o_gatherLocalIds, halo->o_gatherTmp, o_v);
    mesh->device.finish();
    
    // revert back to default stream
    mesh->device.setStream(solver->defaultStream);
  }
  
}


void massNonHaloGatherScatter(solver_t *solver, 
				  ogs_t *nonHalo, 
				  occa::memory &o_v,
				  const char *type,
				  const char *op){


  mesh3D *mesh = solver->mesh;

  // set stream to default stream
  //  mesh->device.setStream(solver->defaultStream);
  
  // unified gather-scatter operation on DEVICE for non-halo nodes
  mesh->gatherScatterKernel(nonHalo->Ngather, nonHalo->o_gatherOffsets, nonHalo->o_gatherLocalIds, o_v);
  
}
