#!/usr/bin/env python3
"""Validate and decompile a captured DTB locally; never accesses a phone."""
import argparse, pathlib, struct, subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('dtb', type=pathlib.Path)
p.add_argument('--output', type=pathlib.Path, default=pathlib.Path('private/live.dts'))
a = p.parse_args()
data = a.dtb.read_bytes()
if len(data) < 40: p.error('truncated DTB header')
magic, total = struct.unpack_from('>II', data)
if magic != 0xd00dfeed or total > len(data) or total < 40: p.error('invalid DTB magic/length')
if a.output.exists(): p.error('output exists; select a new output')
a.output.parent.mkdir(parents=True, exist_ok=True)
r = subprocess.run(['dtc', '-I', 'dtb', '-O', 'dts', str(a.dtb)], capture_output=True)
if r.returncode: p.error(r.stderr.decode(errors='replace'))
a.output.write_bytes(r.stdout)
print('Decompiled DTB to', a.output)
print('Review /memory, /reserved-memory, memreserve entries, /chosen, framebuffer, PSCI and interrupt controllers.')
