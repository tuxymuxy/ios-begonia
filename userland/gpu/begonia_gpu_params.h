#ifndef BEGONIA_GPU_PARAMS_H
#define BEGONIA_GPU_PARAMS_H
#include <stdint.h>
/* Measured G76 MC4 Panfrost capability parameters0..42. No identifiers.
 * Parameter41 timestamp is simulated by the mock runtime.43/44 unsupported. */
static inline int begonia_gpu_mock_param(uint32_t parameter,uint64_t *value)
{
    static const uint64_t parameters[]={
        0x7211ull, /* product_id */
        0x0ull, /* revision */
        0xfull, /* shader_present */
        0x1ull, /* tiler_present */
        0x1ull, /* l2_present */
        0xfull, /* stack_present */
        0xffull, /* address_spaces */
        0x7ull, /* job_slots */
        0x7130206ull, /* l2_features */
        0x0ull, /* core_features */
        0x809ull, /* tiler_features */
        0x101ull, /* memory_features */
        0x2830ull, /* mmu_features */
        0xa046000ull, /* thread_features */
        0x300ull, /* max_threads */
        0x180ull, /* thread_max_workgroup_size */
        0x180ull, /* thread_max_barrier_size */
        0x0ull, /* coherency_features */
        0xf7fe03feull, /* texture_features0 */
        0xc3fff7ffull, /* texture_features1 */
        0xbfe1ff9full, /* texture_features2 */
        0x10c6ull, /* texture_features3 */
        0x20eull, /* job_slot_features0 */
        0x4feull, /* job_slot_features1 */
        0x7eull, /* job_slot_features2 */
        0x0ull, /* job_slot_features3 */
        0x0ull, /* job_slot_features4 */
        0x0ull, /* job_slot_features5 */
        0x0ull, /* job_slot_features6 */
        0x0ull, /* job_slot_features7 */
        0x0ull, /* job_slot_features8 */
        0x0ull, /* job_slot_features9 */
        0x0ull, /* job_slot_features10 */
        0x0ull, /* job_slot_features11 */
        0x0ull, /* job_slot_features12 */
        0x0ull, /* job_slot_features13 */
        0x0ull, /* job_slot_features14 */
        0x0ull, /* job_slot_features15 */
        0x1ull, /* core_groups */
        0x0ull, /* thread_tls_alloc */
        0x0ull, /* afbc_features */
        0x0ull, /* system_timestamp */
        0xc65d40ull, /* system_timestamp_frequency */
    };
    if(!value || parameter>=sizeof(parameters)/sizeof(parameters[0]))return -1;
    *value=parameters[parameter];return 0;
}
#endif
