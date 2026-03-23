#include "MaxHeap.h"
#include <cassert>
#include <cstdlib>
#include <cfloat>
#include <algorithm>
#include <iostream>

using namespace std;

void MaxHeap::percolateUp ( int pos ){
  int node=maxHeap[pos], parentNode;
  while (pos > 1) {                    // not yet at top of the maxHeap
    parentNode = maxHeap[pos/2];
    if (nodeIsGreater(parentNode,node)) break;
    placeNode(parentNode,pos);
    pos = pos/2;
  }
  placeNode(node,pos);  
}

MaxHeap::MaxHeap ( int nElems ):
  maxHeap(nElems+2),
  value(nElems+1),
  heapPositions(nElems+2),
  maxHeapLast(0),
  numElems(nElems){
    maxHeap[0]=0;  
    for (int elem=1;elem<=nElems;elem++){
      maxHeap[elem]=elem;
      value[elem]=1;
      heapPositions[elem]=elem;
    }
    maxHeapLast=nElems;
}

void MaxHeap::print( ){
  for (int i = 1; i <= maxHeapLast; ++i)
    cout << "v" << maxHeap[i] << " --> " << value[maxHeap[i]] << "; ";
  cout << endl;
}

void MaxHeap::reset() { for (int elem = 1; elem <= numElems; elem++ ) value[elem]=0;  }
	  
void MaxHeap::insertElement ( int elem ){
  if (heapPositions[elem]!=0) return;
  int pos = ++maxHeapLast;
  maxHeap[pos]=elem;
  percolateUp(pos);
}

bool MaxHeap::increaseValueBy ( int elem, double increment ){
  assert(increment >= 0);
  double newVal = value[elem]+increment;
  if (newVal > DBL_MAX) return true;  // i.e., newVal is "infty": overflow.
  value[elem]+=increment;
  int pos = heapPositions[elem];
  if (pos) percolateUp(pos); //if in heap percolate up
  return false;
}

void MaxHeap::removeMax ( ){
  int      resultVar;
  int      pos=1, childPos=2;
  int      node, childNode, rchildNode;
  assert(maxHeapLast != 0);                 // Heap must be non-empty
  resultVar = maxHeap[1];
  heapPositions[resultVar] = 0;             // out of heap
  node = maxHeap[maxHeapLast--];            // now sink node until its place:
  while (childPos <= maxHeapLast) {         // i.e., while lchild exists
    childNode = maxHeap[childPos];
    if (childPos < maxHeapLast) {           // if rchild also exists, make
      rchildNode = maxHeap[childPos+1];     //   childNode the largest of both:
      if (nodeIsGreater(rchildNode,childNode)) {childNode=rchildNode; childPos++;}
    }
    if (nodeIsGreater(node,childNode)) break;  // no need to sink any further
    placeNode(childNode,pos);
    pos = childPos;
    childPos = pos*2;
  }
  if (maxHeapLast) placeNode(node,pos);
}

void MaxHeap::reduceScore(int v) { // used after chosing this var for decision based on score
  if (heapPositions[v]==0) // v is not in heap
    value[v] *= 0.9;
  else {
    assert( v == consultMax() );
    removeMax();
    value[v] *= 0.9;
    insertElement(v);
  }
}  

