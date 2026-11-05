#!/usr/bin/env python3
"""Read-only bring-up evidence. Requires a connected, authorized Android ADB device.
No reboot, unlocking, partition writes or firmware changes. Root is opt-in.
"""
import argparse, hashlib, json, pathlib, subprocess, datetime
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True, help='explicit adb serial, never stored in report')
p.add_argument('--root', action='store_true', help='use existing su permission to READ DTB and boot partition')
p.add_argument('--output', default='private/device')
a = p.parse_args()
def adb(*args):
    r = subprocess.run(['adb', '-s', a.serial, *args], capture_output=True, timeout=60)
    if r.returncode: raise RuntimeError(r.stderr.decode(errors='replace').strip() or 'ADB read failed')
    return r.stdout
props = ['ro.product.device', 'ro.product.board', 'ro.hardware', 'ro.boot.hardware', 'ro.boot.slot_suffix', 'ro.boot.flash.locked', 'ro.boot.verifiedbootstate', 'ro.build.fingerprint']
info = {k: adb('shell', 'getprop', k).decode().strip() for k in props}
if info['ro.product.device'] not in ('begonia', 'begoniain'):
    p.error('device is not begonia/begoniain; collection stopped')
out = pathlib.Path(a.output)
out.mkdir(parents=True, exist_ok=False)
report = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'properties': info, 'files': {}, 'errors': {}}
for name, command in [('meminfo.txt', 'cat /proc/meminfo'), ('partitions.txt', 'cat /proc/partitions'), ('block-links.txt', 'ls -l /dev/block/by-name')]:
    try: (out/name).write_bytes(adb('shell', command))
    except RuntimeError as e: report['errors'][name] = str(e)
if a.root:
    slot = info['ro.boot.slot_suffix']
    if slot not in ('', '_a', '_b'): p.error('unexpected slot suffix')
    for name, command in [('live.dtb', 'cat /sys/firmware/fdt'), ('boot.img', 'cat /dev/block/by-name/boot'+slot)]:
        try:
            data = adb('exec-out', 'su', '-c', command)
            magic = b'\xd0\x0d\xfe\xed' if name.endswith('.dtb') else b'ANDROID!'
            if not data.startswith(magic): raise RuntimeError('unexpected file header; not saved')
            (out/name).write_bytes(data)
            report['files'][name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        except RuntimeError as e: report['errors'][name] = str(e)
(out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
print('Read-only evidence saved to', out)
if report['errors']: print('Some reads unavailable; inspect report.json')
