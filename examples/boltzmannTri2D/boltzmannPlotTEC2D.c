#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"

#include "mesh2D.h"

// interpolate data to plot nodes and save to file (one per process
void boltzmannPlotTEC2D(mesh2D *mesh, char *fileName, dfloat time){

    
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  FILE *fp;
  
  fp = fopen(fileName, "a");

  fprintf(fp,"#ELEMENT NUMBER        =%d\n",mesh->Nelements);
  fprintf(fp,"#SOLUTION ORDER        =%d\n",mesh->N);
  fprintf(fp,"#POSTPROCESS NODES     =%d\n",mesh->plotNp);
  fprintf(fp,"#POSTPROCESS Elements  =%d\n",mesh->plotNelements);
  fprintf(fp,"#SPEED of SOUND        =%f\n",mesh->sqrtRT);

  fprintf(fp,"VARIABLES=x,y,u,v,p,w\n");

  iint TotalPoints = mesh->Nelements*mesh->plotNp;
  iint TotalCells  = mesh->Nelements*mesh->plotNelements;

  fprintf(fp, "ZONE  N = %d  E = %d  F=FEPOINT , ET=TRIANGLE , STRANDID=1, SOLUTIONTIME = %.8f \n", TotalPoints,TotalCells,time);

  


  dfloat *curlU   = (dfloat *) calloc(mesh->Np, sizeof(dfloat));
  dfloat *divU    = (dfloat *) calloc(mesh->Np, sizeof(dfloat));
  // Write data
  // compute plot node coordinates on the fly
  for(iint e=0;e<mesh->Nelements;++e){
    // First compute the curl and div
    for(iint n=0;n<mesh->Np;++n){
      dfloat dUdr = 0, dUds = 0, dVdr = 0, dVds = 0;
      for(iint m=0;m<mesh->Np;++m){
        iint base = mesh->Nfields*(m + e*mesh->Np);
        dfloat rho = mesh->q[base + 0];
        dfloat u = mesh->q[1 + base]*mesh->sqrtRT/rho;
        dfloat v = mesh->q[2 + base]*mesh->sqrtRT/rho;
        dUdr += mesh->Dr[n*mesh->Np+m]*u;
        dUds += mesh->Ds[n*mesh->Np+m]*u;
        dVdr += mesh->Dr[n*mesh->Np+m]*v;
        dVds += mesh->Ds[n*mesh->Np+m]*v;
      }

      dfloat rx = mesh->vgeo[e*mesh->Nvgeo+RXID];
      dfloat ry = mesh->vgeo[e*mesh->Nvgeo+RYID];
      dfloat sx = mesh->vgeo[e*mesh->Nvgeo+SXID];
      dfloat sy = mesh->vgeo[e*mesh->Nvgeo+SYID];

      dfloat dUdx = rx*dUdr + sx*dUds;
      dfloat dUdy = ry*dUdr + sy*dUds;
      dfloat dVdx = rx*dVdr + sx*dVds;
      dfloat dVdy = ry*dVdr + sy*dVds;

      curlU[n] = dVdx-dUdy;
      divU[n]  = dUdx+dVdy;
    }   

    for(iint n=0;n<mesh->plotNp;++n){
      dfloat plotxn = 0, plotyn = 0, plotun=0;
      dfloat plotvn = 0, plotpn =0,  plotwn=0, plotdn=0;

      for(iint m=0;m<mesh->Np;++m){

        plotxn += mesh->plotInterp[n*mesh->Np+m]*mesh->x[m+e*mesh->Np];
        plotyn += mesh->plotInterp[n*mesh->Np+m]*mesh->y[m+e*mesh->Np];
        //
        iint base = mesh->Nfields*(m + e*mesh->Np);
        dfloat rho = mesh->q[base];
        dfloat pm = mesh->sqrtRT*mesh->sqrtRT*rho; 
        dfloat um = mesh->q[1 + base]*mesh->sqrtRT/rho;
        dfloat vm = mesh->q[2 + base]*mesh->sqrtRT/rho;
        //
        dfloat wz = curlU[m];
        dfloat du = divU[m];
        //
        plotpn  += mesh->plotInterp[n*mesh->Np+m]*pm;
        plotun  += mesh->plotInterp[n*mesh->Np+m]*um;
        plotvn  += mesh->plotInterp[n*mesh->Np+m]*vm;
        plotwn  += mesh->plotInterp[n*mesh->Np+m]*wz;
        plotdn  += mesh->plotInterp[n*mesh->Np+m]*du;
     }

      fprintf(fp,"%.10e\t%.10e\t%.10e\t%.10e\t%.10e\t%.10e\t%.10e\n",plotxn,plotyn,plotun,plotvn,plotpn,plotwn, plotdn);
    }
}


  // Write Connectivity
   for(iint e=0;e<mesh->Nelements;++e){
    for(iint n=0;n<mesh->plotNelements;++n){
      for(int m=0;m<mesh->plotNverts;++m){
        fprintf(fp, "%9d\t ", 1 + e*mesh->plotNp + mesh->plotEToV[n*mesh->plotNverts+m]);
      }
      fprintf(fp, "\n");
    }
  }

  fclose(fp);


  free(curlU);
  free(divU);

}
