/* Bounds checks shared by the native driver and host tests. */
#ifndef BEGONIA_EXTENT_POLICY_H
#define BEGONIA_EXTENT_POLICY_H
#include <stdint.h>
struct begonia_extent { uint64_t logical, physical, count; };
static int begonia_extent_contains(const struct begonia_extent *map,uint32_t n,uint64_t lba,uint64_t count) {
 if(!count)return 0;
 for(uint32_t i=0;i<n;i++)if(lba>=map[i].physical && lba-map[i].physical<map[i].count && count<=map[i].count-(lba-map[i].physical))return 1;
 return 0;
}
static int begonia_extents_disjoint(const struct begonia_extent *a,uint32_t n,const struct begonia_extent *b,uint32_t m) {
 for(uint32_t i=0;i<n;i++)for(uint32_t j=0;j<m;j++) {
  if(a[i].physical<=b[j].physical) {if(b[j].physical-a[i].physical<a[i].count)return 0;}
  else if(a[i].physical-b[j].physical<b[j].count)return 0;
 }
 return 1;
}
#endif
