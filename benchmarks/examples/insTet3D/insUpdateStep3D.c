#include "ins3D.h"

// complete a time step using LSERK4
void insUpdateStep3D(ins_t *ins, iint tstep, iint haloBytes,
				       dfloat * sendBuffer, dfloat * recvBuffer,
				        char   * options){

  mesh3D *mesh = ins->mesh;
  dfloat t = tstep*ins->dt + ins->dt;

  iint offset = (mesh->Nelements+mesh->totalHaloPairs);

  if(mesh->totalHaloPairs>0){

    ins->pressureHaloExtractKernel(mesh->Nelements,
                                  mesh->totalHaloPairs,
                                  mesh->o_haloElementList,
                                  ins->o_PI,
                                  ins->o_pHaloBuffer);

    // copy extracted halo to HOST
    ins->o_pHaloBuffer.copyTo(sendBuffer);

    // start halo exchange
    meshHaloExchangeStart(mesh,
                         mesh->Np*sizeof(dfloat),
                         sendBuffer,
                         recvBuffer);
  }

  // Compute Volume Contribution of gradient of pressure gradient
  ins->gradientVolumeKernel(mesh->Nelements,
                            mesh->o_vgeo,
                            mesh->o_DrT,
                            mesh->o_DsT,
                            mesh->o_DtT,
                            0,  //no offset
                            ins->o_PI,
                            ins->o_PIx,
                            ins->o_PIy,
                            ins->o_PIz);

  if(mesh->totalHaloPairs>0){
    meshHaloExchangeFinish(mesh);

    ins->o_pHaloBuffer.copyFrom(recvBuffer);

    ins->pressureHaloScatterKernel(mesh->Nelements,
                                    mesh->totalHaloPairs,
                                    mesh->o_haloElementList,
                                    ins->o_PI,
                                    ins->o_pHaloBuffer);
  }
  
  const iint solverid = 1; 
  // Compute Surface Contribution of gradient of pressure increment
  ins->gradientSurfaceKernel(mesh->Nelements,
                              mesh->o_sgeo,
                              mesh->o_LIFTT,
                              mesh->o_vmapM,
                              mesh->o_vmapP,
                              mesh->o_EToB,
                              mesh->o_x,
                              mesh->o_y,
                              mesh->o_z,
                              t,
                              ins->dt,
                              ins->c0,
                              ins->c1,
                              ins->c2,
                              ins->index,
                              offset,
                              solverid, // pressure increment BCs
                              ins->o_P,
                              ins->o_PI,
                              ins->o_PIx,
                              ins->o_PIy,
                              ins->o_PIz);


  
  // U <= U - dt/g0 * d(pressure increment)/dx
  const dfloat dt_invg0 = ins->dt/ins->g0;
  ins->updateUpdateKernel(mesh->Nelements,
                              dt_invg0,
			                        ins->c0,
                              ins->c1,
                              ins->c2,
                              ins->o_PI,
                              ins->o_PIx,
                              ins->o_PIy,
                              ins->o_PIz,
                              ins->index,
                              offset,
                              ins->o_U,
                              ins->o_V,
                              ins->o_W,
                              ins->o_P);

  ins->index = (ins->index+1)%3; //hard coded for 3 stages
}
