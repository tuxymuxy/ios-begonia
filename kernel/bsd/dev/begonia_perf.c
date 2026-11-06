/* MT6785 clock/power observation and opt-in guarded CPU/CCI correction.
 * No regulator or GPU register writes.
 * Register locations: pinned Xiaomi MT6785 clk/cpufreq sources (docs/CPU.md).
 * Enable explicitly with begonia_perf_probe=1. This is not a DVFS governor.
 */
#include <kern/clock.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include "begonia_clock_policy.h"
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

static uint32_t
rd(vm_offset_t base, unsigned offset)
{
    return *(volatile uint32_t *)(base + offset);
}

/* AP WACS2 command channel: only read commands (bit31 clear), no PMIC writes. */
static void pw_wr(vm_offset_t base, unsigned offset, uint32_t value)
{
    *(volatile uint32_t *)(base+offset)=value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static int pmic_read(vm_offset_t base, unsigned address, uint32_t *value)
{
    if ((address&1) || address>0xfffe) return -1;
    uint32_t state=rd(base,0xc24);
    if (!(state&(1u<<22)) || ((state>>16)&7)!=0) return -1;
    pw_wr(base,0xc20,(address>>1)<<16);
    for (unsigned i=0;i<1000;i++) {
        state=rd(base,0xc24);
        if (((state>>16)&7)==6) {
            *value=state&0xffff;
            pw_wr(base,0xc28,1);
            return 0;
        }
        delay(10);
    }
    return -1;
}
static void pmic_probe(void)
{
    vm_offset_t base=ml_io_map(0x1000d000,0x1000);
    uint32_t hw=0,sw=0;
    if (!base || pmic_read(base,8,&hw) || pmic_read(base,10,&sw)) {
        printf("BEGONIA PMIC: read channel unavailable; no regulator changes\n");return;
    }
    printf("BEGONIA PMIC: HWCID=%04x SWCID=%04x\n",hw,sw);
    if ((hw&0xff00)!=0x5900) {
        printf("BEGONIA PMIC: identity differs from MT6359; voltage reads skipped\n");return;
    }
    static const unsigned addresses[]={0x1726,0x1728,0x17a6,0x17a8,0x1e8e,0x1eac,0x15a6,0x15a8};
    for (unsigned i=0;i<sizeof(addresses)/sizeof(addresses[0]);i++) {
        uint32_t value=0;
        if (pmic_read(base,addresses[i],&value)) {
            printf("BEGONIA PMIC: read timeout at %04x; stopped\n",addresses[i]);return;
        }
        printf("BEGONIA PMIC: reg %04x=%04x\n",addresses[i],value);
    }
}

static void clock_correct(vm_offset_t pll, vm_offset_t div)
{
    uint32_t enabled=0;
    if(!PE_parse_boot_argn("begonia_cpu_boost",&enabled,sizeof(enabled)) || enabled!=1)return;
    vm_offset_t pmic=ml_io_map(0x1000d000,0x1000);
    uint32_t hw=0,proc=0,sram=0,a=0,b=0;
    if(!pmic || pmic_read(pmic,8,&hw) || hw!=0x5920 ||
       pmic_read(pmic,0x17a6,&proc) || pmic_read(pmic,0x1eac,&sram)) {
        printf("BEGONIA CLOCK: voltage/identity unavailable; unchanged\n");return;
    }
    uint32_t cpu_uv=400000+(proc&127)*6250;
    uint32_t sram_uv=500000+((sram>>8)&127)*6250;
    uint32_t ll=rd(pll,0x204),cci=rd(pll,0x2a4);
    uint32_t ll_div=rd(div,0x2a0),cci_div=rd(div,0x2e0);
    if(ll_div!=0x03110aff || cci_div!=0x83100aff ||
       begonia_clock_values(cpu_uv,sram_uv,ll,cci,&a,&b)) {
        printf("BEGONIA CLOCK: unexpected clock/rail state; unchanged\n");return;
    }
    /* Park on CLKSQ (mux0) while changing PCW, then restore ARMPLL (mux1).
     * Preserve divider/post-divider; register encoding/20us settle from MT6785.
     * Raise interconnect first. No PMIC write commands are issued. */
    boolean_t interrupts=ml_set_interrupts_enabled(FALSE);
    pw_wr(div,0x2e0,cci_div&~(3u<<9));
    pw_wr(pll,0x2a4,b);delay(20);
    pw_wr(div,0x2e0,cci_div);
    pw_wr(div,0x2a0,ll_div&~(3u<<9));
    pw_wr(pll,0x204,a);delay(20);
    pw_wr(div,0x2a0,ll_div);
    ml_set_interrupts_enabled(interrupts);
    printf("BEGONIA CLOCK: A55 %08x->%08x CCI %08x->%08x rails %u/%u uV unchanged\n",
       ll,rd(pll,0x204),cci,rd(pll,0x2a4),cpu_uv,sram_uv);
}

void
begonia_perf_init(void)
{
    uint32_t enabled = 0;
    if (!PE_parse_boot_argn("begonia_perf_probe", &enabled, sizeof(enabled)) || enabled != 1)
        return;
    vm_offset_t pll = ml_io_map(0x1000c000, 0x1000);
    vm_offset_t div = ml_io_map(0x0c53a000, 0x1000);
    vm_offset_t spm = ml_io_map(0x10006000, 0x1000);
    if (!pll || !div || !spm) {
        printf("BEGONIA PERF: mapping failed; no register access\n");
        return;
    }
    /* Report raw mux/divider state: PLL output alone is not CPU clock rate. */
    printf("BEGONIA PERF: MPIDR=%llx MIDR=%llx CNTFRQ=%llu\n",
        __builtin_arm_rsr64("MPIDR_EL1"), __builtin_arm_rsr64("MIDR_EL1"),
        __builtin_arm_rsr64("CNTFRQ_EL0"));
    printf("BEGONIA PERF: LL pll=%08x div=%08x BIG pll=%08x div=%08x CCI pll=%08x div=%08x\n",
        rd(pll, 0x204), rd(div, 0x2a0), rd(pll, 0x214), rd(div, 0x2a4),
        rd(pll, 0x2a4), rd(div, 0x2e0));
    printf("BEGONIA PERF: MFG pll_con0=%08x pll_con1=%08x pll_power=%08x SPM=%08x/%08x\n",
        rd(pll, 0x250), rd(pll, 0x254), rd(pll, 0x25c), rd(spm, 0x160), rd(spm, 0x164));
    /* Do not read 0x13040000: a powered-down Mali can fault/hang the CPU.
     * Power/clock/bus-protection ownership must be established first. */
    printf("BEGONIA PERF: Mali MMIO untouched; no DVFS or power-state changes\n");
    pmic_probe();
    clock_correct(pll,div);
    uint32_t gpu_probe=0;
    if(PE_parse_boot_argn("begonia_gpu_probe",&gpu_probe,sizeof(gpu_probe)) && gpu_probe==1) {
        extern void begonia_mali_probe(uint32_t,uint32_t,int);
        vm_offset_t pw=ml_io_map(0x1000d000,0x1000);
        uint32_t hw=0,gv=0,ge=0,sv=0,se=0;
        if(pw && !pmic_read(pw,8,&hw) && hw==0x5920 &&
           !pmic_read(pw,0x15a6,&gv) && !pmic_read(pw,0x15a8,&ge) &&
           !pmic_read(pw,0x14a6,&sv) && !pmic_read(pw,0x14a8,&se)) {
            begonia_mali_probe(400000+(gv&127)*6250,400000+(sv&127)*6250,(ge&se&1)!=0);
        } else printf("BEGONIA GPU: PMIC identity/readback failed; untouched\n");
    }
}
