#!/usr/bin/env python3
"""Boot the diagnostic on QEMU; this does not test Redmi hardware or XNU."""
import pathlib, re, subprocess
root = pathlib.Path(__file__).resolve().parents[1]
subprocess.run(['python3', str(root/'tools/build_probe.py'), '--qemu'], check=True)
cmd = ['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a55', '-m', '256M', '-nographic', '-monitor', 'none', '-no-reboot', '-kernel', str(root/'out/qemu/Image')]
proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
try:
    data, _ = proc.communicate(timeout=5)
except subprocess.TimeoutExpired:
    proc.terminate()
    data, _ = proc.communicate(timeout=5)
log = data.decode(errors='replace')
(root/'out/qemu/serial.log').write_text(log)
print(log)
assert 'PROBE_COMPLETE' in log, 'probe did not complete'
midr = int(re.search(r'MIDR=([0-9a-f]+)', log)[1], 16)
assert (midr & 0xff00fff0) == 0x4100d050, 'unexpected QEMU CPU implementer or part'
assert 'EL=0000000000000001' in log, 'unexpected entry EL'
assert 'TGran16=0000000000000001' in log, 'unexpected QEMU 16K granule support'
print('PASS: emulator entry, register report, completion (not a phone boot)')
