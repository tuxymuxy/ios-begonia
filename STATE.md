# Current state — 2026-11-06

## Current phone: v32 UI run

v32 was installed over verified v31, partition readback verified, reboot requested
successfully. It runs iOS for1800seconds then captures logs and returns to PMOS.
No new flash/reboot while this UI run is active. Owner feedback is pending.

- Image: out/begonia-chain-xnu-forced/ios16-clock-ui-v32.img,49768448bytes
- SHA256:2808eb48475edd05143cc63a9f028b007aeb393b89568281060f93ff29dcb2d5
- Payload: out/native-clock-ui-v32.payload
- Kernel: out/kernel.begonia-stock-clock
- Physical/direct touch, original one-record reads, console released at graphics,
  touch tracing, persistent16GiB data, one XNU core, guarded CPU/CCI correction.
- Evidence: private/device/native-v32-install.txt

Owner wants continuous progress without repeated interruptions and explicitly
authorizes CPU clock correction. Do not claim stable GUI touch or GPU acceleration.

## Verified CPU work

Native v27/v30 showed A55~1.175GHz,A76~1.085GHz,CCI~612MHz. Mux1=ARMPLL,
confirmed in pinned MT6785 configuration. Linux later reports2GHz/2.05GHz,
CCI1.4GHz; these Linux rates are not inherited by XNU.

Read-only MT6785 PWRAP WACS2 works: MT6359 HWCID/SWCID5920, VPROC2 selector64
=>800mV; SRAM_PROC2 selector64=>900mV. No regulator-write commands implemented.
Stock captured OPP permits A551275MHz/800mV and requiredCCI875MHz/781.25mV.

Opt-in begonia_cpu_boost=1/--cpu-boost: exact PLL/divider, PMIC/voltage guards;
raiseCCI first, CLKSQ parking during PCW update,20us settle, restoreARMPLL;
no voltage writes. v31 capture validated request/checksums and readback:
A55 8116989d->011884ec,CCI821789d8->0221a762. Native file-root boot continued;
7500touch polls/0invalid. Longer stability/performance remains pending v32.
Host tests/clock_policy.c passes rate/mask/rail/unknown-state checks.

SMP is NOT working. v28 registers physical0/0x200 but1of2 processors run;
secondary acceptsCPU_ON but never reports reaching kernel startup/scheduler.
v29 lowRAM br-x0 trampoline0x400801c0 stillfails. Keep cpus=1 for UI.
PhysicalCPU1/MPIDR0x100 belongs to recovery supervisor; NEVER expose it to XNU.
Opt-in --cpus2 maps logical0/1 to physical0/2 only. Trampoline experiment remains
opt-in; high reset-address hypothesis is not sufficient to explain failure.

## Touch / GUI evidence

- Native iOS16.5(20F66)/XNU22.5 reached SpringBoard, owner photographed v10,
  again confirmed v18. v24 console-release build allowed real Settings taps.
- v24 was very slow/stalled; clock later advanced. Input tracing showed backlog.
- v25 (quiet/batched reads) failed GUI touch. Capture:22500polls/13684invalid/
  58events, later30000/21184/58. Root cause unproven, batching now opt-in only.
- v27:7500polls/0invalid/387events; v28:0invalid/649events. Controller-level
  failure fromv25 not reproduced. Coordinates are normalized0..65535;30k/40k
  values are valid, not physical-panel coordinates.
- Decoder classifies negative reasons: recovery77,watchdogFD/FE,invalidID,
  duplicates,bounds. Rate-limited prefixes added; no native rejection observed
  in v27/v28. Firmware reload recovery is NOT implemented.
- Verified NT36672A SPI5 at11019000; resetGPIO2 LOW; firmware all8 readbacks
  match; polling8ms. Exact32bit MMIO, CFG1 beforeFIFO, STATUS1 idle (not repeated
  read-clearSTATUS0). Private firmware stays private. Do not regress this path.

Known partially working fallback: out/begonia-chain-xnu-forced/ios16-no-overlay-v24.img
49768448bytes,SHA256a4abe35aca60b440339f1d813a8c7f51fbfd64cc85836a10fce3f6f264669886.
It is retained, NOT currently installed. v24 payload also retained.

## GPU work: compiler verified, native probe untested

Software rendering/framebuffer remains in use. Linux Panfrost measured G76
product7211,shader0xf,AS0xff,jobslots0x7. Native power/MMU/jobs/Metal unported.

Built Mesa26.2.6 commit8e87a503ebd91408c88596d1b9eeb5f4c0b2088d using pinned
viva-gpu patches. Host LLVM23.1.5,SPIRV-LLVM-Translator23.1.3,SPIRV-Tools1.4.363.0
and pkgconf installed. tools/test_g76_compiler.py passes: architecture7,
Bifrost ISA,176-byte fragment shader. No physical GPU execution implied.

kernel/bsd/dev/begonia_mali_probe.c compiled as opt-in --gpu-probe: actualMT6785
SPM/MFG clock/bus-protection addresses; measured enabled MT6359 rails required;
26MHz crystal; bounded power/SRAM ACK waits; G76 identity/soft-reset only.
No jobs/MMU/Metal service. It has NOT run on hardware and is NOT enabled v32.
Prepared v33 NOT INSTALLED: out/begonia-chain-xnu-forced/ios16-gpu-probe-v33.img,
49770496bytes (different length fromv32),SHA256
9db1d49948c5f9d63832e25bcfd21804091bf2d0146f45dd3137fc2b7fff43c2.
Payload out/native-gpu-v33.payload; kernel out/kernel.begonia-gpu-probe.
90sec deadline, oneCPU, noCPUboost; isolates GPU power/reset. Do NOT install
before v32 capture/review. Verify old/new image lengths separately on install.
DefaultUI remains unchanged. Extended query_linux_gpu.py --all prepared to
capture missing hardware capabilities instead of copying G57 values.

Next: retrieve v32 after recovery (validate exact request/checksums), assess UI
and timing. Preserve UI baseline. Then a short <=180sec opt-in native GPU probe,
plus extended Linux capability/rail baseline. Do not interrupt active v32.

## Storage and recovery

System: /home/user/ios-begonia/system.apfs (read-only file-backed UFS root).
Data: /home/user/ios-begonia/data.hfs, fully allocated16GiB journaledHFS+ with
130 verified disjoint extents. No physical repartitioning. Multiple markers,
caches and app-container metadata persisted. Preserve data; do not reinitialize.
Immutable Linux inode pins extents but does not prevent native block writes.
HFS mountuid/gid/mask VNOVAL fix essential; recurring group deletion removed.
No blanket AMFI flags by default. No synthetic touch in physical-touch support.

PMOS SSH user@192.168.1.124; USB172.16.42.1 with verified host alias can work.
Use normal /Users/tux/.ssh/known_hosts, strict checking. Fastboot target private
in private/device/fastboot-target.txt; always select exact target. Temporary
fastboot boot unreliable: only backed-up boot writes, verify current/staged and
partition readback. No GPT/bootloader/modem/persist changes.

Original PMOS backup private/device/backups/boot.img,67108864bytes,SHA256
115e3e00624de812b99e83703768bf90c44d5e9196415381c036629c226e7ee7.
Cold/early manual reset can retry iOS. To leave PMOS safely, restore exact backup.

Log mirror:0x1b7e00000/2MiB, matching no-map PMOS recoveryDTB, BEGLOG02 format.
Copy out/read-log-mirror to PMOS/tmp after reboot, retrieve binary privately,
parse with tools/parse_log_mirror.py and compare request_id(exactpayload).
Captures private/device/native-v24/v25/v27/v28/v29/v30/v31-mirror.bin and decoded
log/JSON. v26 was prepared but never installed. v32 capture still pending.

No Apple assets, firmware, images, identifiers or private device maps/captures
published. Mu menu handoff remains deferred. Process: docs/PROCESS.md;
CPU/GPU/touch/storage details in docs/. Source repo git.tuxy.dev/tuxy/ios-begonia.
