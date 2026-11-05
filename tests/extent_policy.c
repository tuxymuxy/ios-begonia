#include <assert.h>
#include <stdio.h>
#include "../kernel/bsd/dev/begonia_extent_policy.h"
int main(void) {
 struct begonia_extent root[]={{0,100,10}},data[]={{0,200,4},{4,300,8}};
 assert(begonia_extents_disjoint(root,1,data,2));
 assert(begonia_extent_contains(data,2,200,4));assert(begonia_extent_contains(data,2,307,1));
 assert(!begonia_extent_contains(data,2,199,2));assert(!begonia_extent_contains(data,2,203,2));
 assert(!begonia_extent_contains(data,2,204,1));assert(!begonia_extent_contains(data,2,200,0));
 assert(!begonia_extent_contains(data,2,200,UINT64_MAX));assert(!begonia_extent_contains(data,2,UINT64_MAX,1));
 struct begonia_extent overlap[]={{0,109,5}},adjacent[]={{0,110,5}};
 assert(!begonia_extents_disjoint(root,1,overlap,1));assert(begonia_extents_disjoint(root,1,adjacent,1));
 puts("Extent policy tests passed: boundary, overlap, gap, and overflow cases");
}
