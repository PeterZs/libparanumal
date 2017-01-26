#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"
#include "mesh2D.h"

typedef struct {
  iint v1, v2; // vertices on face
  iint element, face, rank;    // face info
  iint elementN, faceN, rankN; // N for neighbor face info

}parallelFace_t;

// comparison function that orders vertices 
// based on their combined vertex indices
int parallelCompareVertices(const void *a, 
			    const void *b){

  parallelFace_t *fa = (parallelFace_t*) a;
  parallelFace_t *fb = (parallelFace_t*) b;

  if(fa->v1 < fb->v1) return -1;
  if(fa->v1 > fb->v1) return +1;

  if(fa->v2 < fb->v2) return -1;
  if(fa->v2 > fb->v2) return +1;

  return 0;
}

/* comparison function that orders element/face
   based on their indexes */
int parallelCompareFaces(const void *a, 
			 const void *b){

  parallelFace_t *fa = (parallelFace_t*) a;
  parallelFace_t *fb = (parallelFace_t*) b;

  if(fa->rank < fb->rank) return -1;
  if(fa->rank > fb->rank) return +1;

  if(fa->element < fb->element) return -1;
  if(fa->element > fb->element) return +1;

  if(fa->face < fb->face) return -1;
  if(fa->face > fb->face) return +1;

  return 0;

}


// compute destination rank for sorting
iint destinationRank(iint size, iint v1, iint v2){
  //return (mymin(v1%size,v2%size));
  //return (mymin(v1,v2))%size;
  return hash(mymin(v1,v2))%size;
}
  
// mesh is the local partition
void meshParallelConnect2D(mesh2D *mesh){

  int rank, size;

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  // serial connectivity on each process
  meshConnect2D(mesh);

  // count # of elements to send to each rank based on
  // minimum {vertex id % size}
  iint *Nsend = (iint*) calloc(size, sizeof(iint));
  iint *Nrecv = (iint*) calloc(size, sizeof(iint));
  iint *sendOffsets = (iint*) calloc(size, sizeof(iint));
  iint *recvOffsets = (iint*) calloc(size, sizeof(iint));
    
  iint allNsend = 0;
  for(iint e=0;e<mesh->Nelements;++e){
    for(iint f=0;f<mesh->Nfaces;++f){
      if(mesh->EToE[e*mesh->Nfaces+f]==-1){
	// find vertices on face
	iint v1 = mesh->EToV[e*mesh->Nfaces+f];
	iint v2 = mesh->EToV[e*mesh->Nfaces+(f+1)%mesh->Nverts];

	// find rank of destination for sorting based on max(v1,v2)%size
	iint destRank = destinationRank(size, v1,v2);

	// increment send size for 
	++Nsend[destRank];
	++allNsend;
      }
    }
  }
  
  // find send offsets
  for(iint r=1;r<size;++r)
    sendOffsets[r] = sendOffsets[r-1] + Nsend[r-1];
  
  // reset counters
  for(iint r=0;r<size;++r)
    Nsend[r] = 0;

  // buffer for outgoing data
  parallelFace_t *sendFaces = (parallelFace_t*) calloc(allNsend, sizeof(parallelFace_t));

  // pack face data
  for(iint e=0;e<mesh->Nelements;++e){
    for(iint f=0;f<mesh->Nfaces;++f){
      if(mesh->EToE[e*mesh->Nfaces+f]==-1){
	// find vertices on face
	iint v1 = mesh->EToV[e*mesh->Nfaces+f];
	iint v2 = mesh->EToV[e*mesh->Nfaces+(f+1)%mesh->Nverts];

	// find rank of destination for sorting based on max(v1,v2)%size
	iint destRank = destinationRank(size, v1,v2);
	
	// populate face to send out staged in segment of sendFaces array
	iint id = sendOffsets[destRank]+Nsend[destRank];

	sendFaces[id].element = e;
	sendFaces[id].face = f;
	sendFaces[id].v1 = mymax(v1,v2);
	sendFaces[id].v2 = mymin(v1,v2);	
	sendFaces[id].rank = rank;

	sendFaces[id].elementN = -1;
	sendFaces[id].faceN = -1;
	sendFaces[id].rankN = -1;
	
	++Nsend[destRank];
      }
    }
  }

  // exchange byte counts 
  MPI_Alltoall(Nsend, 1, MPI_IINT,
	       Nrecv, 1, MPI_IINT,
	       MPI_COMM_WORLD);
  
  // count incoming faces
  iint allNrecv = 0;
  for(iint r=0;r<size;++r){
    allNrecv += Nrecv[r];
    Nrecv[r] *= sizeof(parallelFace_t);
    Nsend[r] *= sizeof(parallelFace_t);
    sendOffsets[r] *= sizeof(parallelFace_t);
  }

  // find offsets for recv data
  for(iint r=1;r<size;++r)
    recvOffsets[r] = recvOffsets[r-1] + Nrecv[r-1]; // byte offsets
  
  // buffer for incoming face data
  parallelFace_t *recvFaces = (parallelFace_t*) calloc(allNrecv, sizeof(parallelFace_t));
  
  // exchange parallel faces
  MPI_Alltoallv(sendFaces, Nsend, sendOffsets, MPI_CHAR,
		recvFaces, Nrecv, recvOffsets, MPI_CHAR,
		MPI_COMM_WORLD);
  
  // local sort allNrecv received faces
  qsort(recvFaces, allNrecv, sizeof(parallelFace_t), parallelCompareVertices);

  // find matches
  for(iint n=0;n<allNrecv-1;++n){
    // since vertices are ordered we just look for pairs
    if(!parallelCompareVertices(recvFaces+n, recvFaces+n+1)){
      recvFaces[n].elementN = recvFaces[n+1].element;
      recvFaces[n].faceN = recvFaces[n+1].face;
      recvFaces[n].rankN = recvFaces[n+1].rank;
      
      recvFaces[n+1].elementN = recvFaces[n].element;
      recvFaces[n+1].faceN = recvFaces[n].face;
      recvFaces[n+1].rankN = recvFaces[n].rank;
    }
  }

  // sort back to original ordering
  qsort(recvFaces, allNrecv, sizeof(parallelFace_t), parallelCompareFaces);

  // send faces back from whence they came
  MPI_Alltoallv(recvFaces, Nrecv, recvOffsets, MPI_CHAR,
		sendFaces, Nsend, sendOffsets, MPI_CHAR,
		MPI_COMM_WORLD);
  
  // extract connectivity info
  mesh->EToP = (iint*) calloc(mesh->Nelements*mesh->Nfaces, sizeof(iint));
  for(int cnt=0;cnt<mesh->Nelements*mesh->Nfaces;++cnt)
    mesh->EToP[cnt] = -1;
  
  for(iint cnt=0;cnt<allNsend;++cnt){
    iint e = sendFaces[cnt].element;
    iint f = sendFaces[cnt].face;
    iint eN = sendFaces[cnt].elementN;
    iint fN = sendFaces[cnt].faceN;
    iint rN = sendFaces[cnt].rankN;
    
    if(e>=0 && f>=0 && eN>=0 && fN>=0){
      mesh->EToE[e*mesh->Nfaces+f] = eN;
      mesh->EToF[e*mesh->Nfaces+f] = fN;
      mesh->EToP[e*mesh->Nfaces+f] = rN;
    }
  }
  
  free(sendFaces);
  free(recvFaces);
}