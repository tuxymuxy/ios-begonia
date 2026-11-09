/*
 * Begonia (Redmi Note 8 Pro / MT6785) Hardware Buttons Driver.
 *
 * Monitors:
 *   - Volume Up & Volume Down keys via MT6785 Keypad (KPD) controller at 0x10010000.
 *     Register 0x10010004 (KPD_MEM) bits:
 *       bit 0: Volume Up (active-low)
 *       bit 1: Volume Down (active-low)
 *   - Power / Lock key via MT6359 PMIC TOPSTATUS (0x2a) over PWRAP (0x1000d000).
 *       bit 1 (0x2): Power key (active-low)
 *
 * Dispatches HAKIOS_BUTTON_DOWN / HAKIOS_BUTTON_UP to hakios_touch_post(),
 * which inject.c reads from /dev/hakios-touch and translates into IOHIDEvents
 * (HID_USAGE_VOLUME_UP, HID_USAGE_VOLUME_DOWN, HID_USAGE_POWER).
 */

#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_time.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/hakios.input.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern kern_return_t kernel_thread_start(thread_continue_t continuation, void *parameter, thread_t *new_thread);
extern void thread_set_thread_name(thread_t th, const char *name);
extern void thread_deallocate(thread_t thread);
extern kern_return_t thread_terminate(thread_t thread);
extern void delay(int);
extern void delay_for_interval(unsigned int time, unsigned int unit);

#define KPD_PHYS_BASE   0x10010000u
#define PWRAP_PHYS_BASE 0x1000d000u
#define PMIC_TOPSTATUS  0x2a

static inline uint32_t
rd(vm_offset_t b, unsigned o)
{
    return *(volatile uint32_t *)(b + o);
}

static inline void
wr(vm_offset_t b, unsigned o, uint32_t v)
{
    *(volatile uint32_t *)(b + o) = v;
    __asm__ volatile("dsb sy" ::: "memory");
}

/* Read a 16-bit register from MT6359 PMIC via PWRAP AP WACS2 channel */
static int
pmic_read_reg(vm_offset_t base, unsigned address, uint32_t *value)
{
    if ((address & 1) || address > 0xfffe) return -1;
    uint32_t state = rd(base, 0xc24);
    if (!(state & (1u << 22)) || ((state >> 16) & 7) != 0) return -1;
    wr(base, 0xc20, (address >> 1) << 16);
    for (unsigned i = 0; i < 1000; i++) {
        state = rd(base, 0xc24);
        if (((state >> 16) & 7) == 6) {
            *value = state & 0xffff;
            wr(base, 0xc28, 1);
            return 0;
        }
        delay(10);
    }
    return -1;
}

static void
buttons_thread(__unused void *arg, __unused wait_result_t result)
{
    vm_offset_t kpd = ml_io_map(KPD_PHYS_BASE, 0x1000);
    vm_offset_t pwrap = ml_io_map(PWRAP_PHYS_BASE, 0x1000);

    if (!kpd || !pwrap) {
        printf("BEGONIA BUTTONS: failed to map KPD (0x10010000) or PWRAP (0x1000d000)\n");
        thread_terminate(current_thread());
        return;
    }

    printf("BEGONIA BUTTONS: Hardware buttons driver started (KPD + PMIC PWRKEY)\n");

    /* Initial state sampling */
    uint32_t kpd_val = rd(kpd, 0x04);
    bool state_vol_up = !((kpd_val >> 0) & 1);
    bool state_vol_down = !((kpd_val >> 1) & 1);
    bool state_power = false;

    uint32_t pmic_val = 0;
    if (pmic_read_reg(pwrap, PMIC_TOPSTATUS, &pmic_val) == 0) {
        state_power = !((pmic_val >> 1) & 1);
    }

    for (;;) {
        /* 1. Poll Volume Keys from KPD controller */
        kpd_val = rd(kpd, 0x04);
        bool cur_vol_up = !((kpd_val >> 0) & 1);
        bool cur_vol_down = !((kpd_val >> 1) & 1);

        if (cur_vol_up != state_vol_up) {
            state_vol_up = cur_vol_up;
            hakios_touch_post(cur_vol_up ? HAKIOS_BUTTON_DOWN : HAKIOS_BUTTON_UP,
                              HAKIOS_BUTTON_VOLUME_UP, 0, 0);
            printf("BEGONIA BUTTONS: Volume Up %s\n", cur_vol_up ? "DOWN" : "UP");
        }

        if (cur_vol_down != state_vol_down) {
            state_vol_down = cur_vol_down;
            hakios_touch_post(cur_vol_down ? HAKIOS_BUTTON_DOWN : HAKIOS_BUTTON_UP,
                              HAKIOS_BUTTON_VOLUME_DOWN, 0, 0);
            printf("BEGONIA BUTTONS: Volume Down %s\n", cur_vol_down ? "DOWN" : "UP");
        }

        /* 2. Poll Power / Lock Key from MT6359 PMIC */
        if (pmic_read_reg(pwrap, PMIC_TOPSTATUS, &pmic_val) == 0) {
            bool cur_power = !((pmic_val >> 1) & 1);
            if (cur_power != state_power) {
                state_power = cur_power;
                hakios_touch_post(cur_power ? HAKIOS_BUTTON_DOWN : HAKIOS_BUTTON_UP,
                                  HAKIOS_BUTTON_LOCK, 0, 0);
                printf("BEGONIA BUTTONS: Power / Lock %s\n", cur_power ? "DOWN" : "UP");
            }
        }

        /* 20ms poll interval (50 Hz) gives <20ms latency and negligible CPU usage */
        delay_for_interval(20, NSEC_PER_MSEC);
    }
}

void
begonia_buttons_init(void)
{
    thread_t thread;
    if (kernel_thread_start(buttons_thread, NULL, &thread) == KERN_SUCCESS) {
        thread_set_thread_name(thread, "begonia-buttons");
        thread_deallocate(thread);
    }
}
