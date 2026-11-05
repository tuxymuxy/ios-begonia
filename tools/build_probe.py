#!/usr/bin/env python3
"""Build raw Linux Image diagnostic. Does not produce or flash an Android boot image."""
import argparse, hashlib, json, pathlib, shutil, struct, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--qemu', action='store_true', help='enable QEMU virt PL011; NEVER use this image on a phone')
p.add_argument('--mirror-test',action='store_true',help='QEMU only: exercise the upper-RAM log mirror')
p.add_argument('--chain', action='store_true', help='build chainloader probe; requires pack_chain.py and the original kernel')
p.add_argument('--ramoops', action='store_true', help='write verified begonia ramoops console (hardware only)')
p.add_argument('--xnu', action='store_true', help='build an unvalidated XNU loader; requires --chain --ramoops')
p.add_argument('--force-xnu-test',action='store_true',help='explicit diagnostic: enter XNU until a matching upper-RAM log capture exists')
p.add_argument('--board',action='store_true',help='read-only inherited UFS/display register inventory on begonia')
p.add_argument('--ufs-read',action='store_true',help='allow TUR/capacity/first-8KiB read from inherited UFS LUN2; no media writes')
a = p.parse_args()
if a.mirror_test and (not a.qemu or not a.xnu): p.error('--mirror-test requires --qemu --xnu')
if a.ufs_read and not a.board: p.error('--ufs-read requires --board')
if a.board and (a.qemu or not a.chain or not a.ramoops): p.error('--board requires hardware --chain --ramoops')
if a.force_xnu_test and (not a.xnu or a.qemu): p.error('--force-xnu-test requires hardware --xnu')
if a.xnu and (not a.chain or not a.ramoops): p.error('--xnu requires --chain --ramoops')
if a.ramoops and not a.chain: p.error('--ramoops requires --chain')
zig = ROOT / '.tools/python/ziglang/zig'
if not zig.exists():
    candidate = shutil.which('zig')
    if not candidate: p.error('install ziglang==0.13.0 into .tools/python or provide zig on PATH')
    zig = pathlib.Path(candidate)
clang = shutil.which('clang')
if not clang: p.error('clang is required')
out = ROOT / 'out' / (('qemu' if a.qemu else 'begonia') + ('-chain' if a.chain else '') + ('-xnu' if a.xnu else '') + ('-forced' if a.force_xnu_test else '') + ('-board' if a.board else '') + ('-ufs' if a.ufs_read else ''))
out.mkdir(parents=True, exist_ok=True)
flags = ['--target=aarch64-none-elf', '-ffreestanding', '-nostdlib', '-fno-builtin', '-fno-stack-protector', '-fno-pic', '-mgeneral-regs-only', '-mstrict-align', '-O2', '-Wall', '-Wextra', '-Werror']
if a.qemu: flags += ['-DQEMU_UART=1']
if a.mirror_test: flags += ['-DTEST_LOG_MIRROR=1']
if a.chain: flags += ['-DCHAIN_BOOT=1']
if a.ramoops: flags += ['-DRAMOOPS_STAGE0=1']
if a.xnu: flags += ['-DXNU_BOOT=1']
if a.force_xnu_test: flags += ['-DXNU_FORCE_TEST=1']
if a.board: flags += ['-DBOARD_PROBE=1']
if a.ufs_read: flags += ['-DUFS_READ_PROBE=1']
objects = []
for name in ['start.S', 'probe.c'] + (['xnu_loader.c','watcher.c'] if a.xnu else []) + (['board_probe.c'] if a.board else []) + (['ufs_probe.c'] if a.ufs_read else []):
    obj = out / (name + '.o')
    subprocess.run([clang, *flags, '-c', str(ROOT/'boot'/name), '-o', str(obj)], check=True)
    objects.append(str(obj))
base = [str(zig), 'ld.lld', '-T', str(ROOT/'boot/link.ld'), *objects]
subprocess.run([*base, '-o', str(out/'probe.elf')], check=True)
subprocess.run([*base, '--oformat=binary', '-o', str(out/'Image')], check=True)
image = bytearray((out/'Image').read_bytes())
assert image[56:60] == b'ARM\x64'
size = struct.unpack_from('<Q', image, 16)[0]
assert len(image) <= size < 1024*1024
image.extend(bytes(size-len(image)))
(out/'Image').write_bytes(image)
manifest = dict(stage=('native XNU diagnostic with captured-log warm fallback; cold resets may retry' if a.force_xnu_test else 'experimental XNU loader; userland depends on appended payload' if a.xnu else 'architectural diagnostic, NOT XNU/iOS'), target='qemu-virt-only' if a.qemu else 'begonia-unvalidated', size=len(image), sha256=hashlib.sha256(image).hexdigest(), phone_tested=False)
(out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(json.dumps(manifest, indent=2))
