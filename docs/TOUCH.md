# Begonia physical touch

The physical controller is Novatek NT36672A on SPI5 (`0x11019000`). Linux calls
it `spi1.0`. GPIO1 is its IRQ line; GPIO2 is reset. The captured board DT and
working Linux GPIO state require GPIO2 LOW during operation. Do not infer this
board's polarity from another phone's generic TP_RESX pin name.

The native driver is `kernel/bsd/dev/begonia_touch.c`, opt-in with
`begonia_touch=1`. It selects the 26 MHz SPI source and 1 MHz bus timing, uploads
private firmware to controller SRAM, verifies section readbacks, starts firmware
with hardware CRC, validates its parameters, and polls reports every 8 ms.
It never programs controller flash. The private firmware is supplied in the ADT
and is not published in this repository.

Two important transport corrections came from tests on the physical phone:

1. Program the current packet length **before** writing TX FIFO words.
2. Wait through the known wire time and poll STATUS1's idle bit; acknowledge
   read-clear STATUS0 only after completion. Repeatedly polling STATUS0 lost
   completions during long firmware uploads on this controller.

FIFO accesses must be exact 32-bit loads/stores. Python `struct`/byte-copy
accesses are unsuitable; the diagnostic harness uses `tools/mmio_access.c`.
The native driver uses volatile 32-bit accesses and barriers.

The report decoder tracks up to ten contacts, normalizes panel coordinates to
0..65535 and posts down/move/up records to `/dev/hakios-touch`. The existing
HakiOS backboardd injection code turns those records into iOS HID events. Empty
reports release previous contacts; host tests cover that transition and reject
invalid/recovery packets. This port uses polling rather than a native SPI/GPIO
interrupt driver.

## Validation

- Linux SPI control: all eight firmware section samples match; firmware boots.
- Corrected polled FIFO on Linux: same readbacks and boot result; a one-minute
  physical capture recorded 334 changing samples and 42 empty/release samples,
  including multiple simultaneous fingers.
- **Native XNU v21**: capture checksums and request ID verified; all eight section
  samples match, firmware status A0 then A1, parameters 1080x2340, and real
  physical down/move events reach the kernel input queue.
- v22 ten-minute UI test: owner could not open apps despite the home screen.
  The validated capture includes kernel down/move/release and backboardd reads.
  Recorded digitizer mode is wake-only0xfe, with no active0x1 visible in retained
  data. Wake-only handling in inject.c consumes finger sequences as wake-button
  events. Investigate that path and display state next; the circular log does
  not prove that mode never changed. No synthetic gestures were enabled.
  The complete GUI touch path is NOT working/verified.

## Linux bring-up tools

`probe_touch_linux.py` temporarily binds spidev and can test either Linux SPI or
polled FIFO against the privately provided firmware. It requires root, an exact
Begonia match and `/tmp/mmio-access.so`, built for aarch64 Linux from the C helper.
The validated FIFO profile uses `--native-fifo --idle-poll --chunk 28 --crc-bytes 2`.
The tool can capture physical reports with `--touch-seconds 60`.

On this kernel, detaching/rebinding changes the GPIO IRQ mapping and may require
a normal PMOS reboot to restore the original Novatek driver. The test series
used `--leave-unbound`, then rebooted and verified the original binding. Never
leave a test phone unbound without recovery. Raw legacy/latch profiles are only
for reproducing the investigation, not the production driver.

`read_touch_config.c` skips SPI registers when the controller is suspended;
GPIO and clock snapshots alone are safe in that state. Do not read a gated SPI
register block merely to inspect it.

Source references: the pinned MT6785 Linux tree in `docs/sources.lock.json`,
`novatek-nvt-ts-core.c`, `novatek-nvt-ts-spi.c`, `spi-mt65xx.c`, and the MT6785
clock/pin definitions. No Linux source or firmware blob is embedded in the port.
