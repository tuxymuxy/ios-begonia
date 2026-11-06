/* Exercise Mesa's real Mach-O LC_UUID lookup and cache key representation. */
#include "util/build_id.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
 const struct build_id_note *note=build_id_find_nhdr_for_addr(main);assert(note);
 assert(build_id_length(note)==16 && build_id_length(note)>=BUILD_ID_EXPECTED_HASH_LENGTH);
 uint8_t id[BLAKE3_KEY_LEN];copy_build_id_to_sha1(id,note);
 assert(!memcmp(id,build_id_data(note),16));
 for(unsigned i=16;i<sizeof id;i++)assert(!id[i]);
 puts("PASS: Mach-O UUID accepted as real driver identity; cache key correctly padded");
}
