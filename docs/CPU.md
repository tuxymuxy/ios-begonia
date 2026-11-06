# Begonia CPU bring-up

The current iOS payload deliberately exposes one Cortex-A55 core and passes
`cpus=1`. It does not yet have MT6785 clock/voltage scaling. The common optional
CPU-frequency driver targets Qualcomm EPSS at 0x18591000; it is incompatible
with MT6785 and must remain disabled. CPU instructions/scheduling work already;
additional cores and power management are separate bring-up tasks.

The captured Linux tree provides six A55 and two A76 cores with MPIDRs
0x000 through 0x700 in steps of0x100. The independent capture/watchdog supervisor
currently owns physical CPU1 (MPIDR0x100). It must never be included in XNU's CPU
nodes while that supervisor is running. Do not simply change cpus=1 to cpus=8.
At most seven cores remain available without redesigning recovery.

The common kernel already contains generic PSCI SMC CPU_ON/OFF and GICv3 IPI
support. First experiment should expose only CPU0 and CPU2 (two A55s), retain
the supervisor and existing clock settings, and check both scheduler entry and
recovery. Confirm the per-core reset path and 16KiB granule support before adding
the A76s. A higher requested CPU count alone is not evidence of active cores.

For DVFS, use the actual MT6785 clock/OPP/rail relationships captured from Linux.
The live tree has CPU clock/intermediate-clock pairs and a CCI clock domain;
Viva's MT6781 voltage tables and PLL values cannot be copied. Establish current
CPU/CCI frequencies, PLL register format, PMIC buck identities, voltage bounds,
rail transition ordering and thermal limits before any frequency writes.

Next evidence collection can use tools/collect_linux_performance.py under Linux.
It reads CPU policy, GPU devfreq, thermal and clock summary files; it changes no
clock, regulator, power-domain or governor settings. Keep device output private.

Opt-in native observation lives in kernel/bsd/dev/begonia_perf.c, linked through
patch0010 and enabled only with begonia_perf_probe=1. It reads APMIXED0x1000c000
LL/BIG/CCI CON1 offsets0x204/0x214/0x2a4 and MCUCFG0x0c530000 divider offsets
0xa2a0/0xa2a4/0xa2e0. Raw mux/divider data is retained because an active PLL
frequency alone does not establish the CPU's selected clock. No clock writes or
voltage changes occur. Source reference: MiCode commit pinned in board.json,
drivers/misc/mediatek/base/power/cpufreq_v1/src/mach/mt6785/mtk_cpufreq_platform.c.

Native v27 observation has LL CON1=0x8116989d/div=0x03110aff, BIG
0x0114dd89/div=0x03110aff, CCI0x821789d8/div=0x83100aff. PLL-path estimates:
1.175GHz,1.085GHz,612MHz respectively (mux field1). Actual clock switching and
rail handling remain unimplemented. Two-core payload builder --cpus2 prepares
CPU0+CPU2, explicitly reserving CPU1. v28 hardware result pending; default1.

The pinned MT6785 mtk_cpufreq_config.h confirms mux1=ARMPLL. Thus v27 selected
A55~1.175GHz, A76~1.085GHz and CCI~612MHz, rather than Linux's later DVFS settings.
The decoder now exposes selected_hz only for this confirmed source. Raising the
PLL without knowing/controlling current VPROC/VSRAM and interconnect state is not
implemented. Owner explicitly authorizes clock correction; the prerequisite is
matching rail controls, not further permission.

MT6359 PMIC readback verified in nativev30: identity5920, A55 VPROC2 DA selector64
at400mV+6.25mV/step=>800mV; SRAM_PROC2 DA selector64 at500mV base=>900mV.
The captured OPP table permits A55 1275MHz/800mV with CCI875MHz/781.25mV.
Opt-in --cpu-boost (requires --perf-probe and one CPU) tests these two frequencies
without regulator writes. Exact initial PLL, divider, PMIC identity and rail
bounds are required. Park on CLKSQ, write PCW,20us settle, restoreARMPLL; CCI
raised first. No maximum-clock setting, automatic governor or thermal governor
is implemented. Host clock-policy tests pass; native v31 result pending.
