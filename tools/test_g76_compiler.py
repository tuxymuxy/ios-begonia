#!/usr/bin/env python3
"""Compile a G76 Bifrost shader on the host. Does not run GPU work."""
from pathlib import Path
import shlex,subprocess
root=Path(__file__).resolve().parents[1]
package=root/'upstream/viva-gpu/mesa/out/macos'
out=root/'out';out.mkdir(exist_ok=True)
if not (package/'cflags.txt').exists():
    raise SystemExit('Build the pinned viva-gpu Mesa macos package first (docs/GPU.md).')
command=['clang','-O1','-g',*shlex.split((package/'cflags.txt').read_text()),
         str(root/'tests/g76_compiler.c'),'-o',str(out/'test-g76-compiler'),
         *shlex.split((package/'ldflags.txt').read_text())]
result=subprocess.run(command,capture_output=True,text=True)
(out/'g76-compiler-build.log').write_text(result.stdout+result.stderr)
result.check_returncode()
result=subprocess.run([str(out/'test-g76-compiler')],capture_output=True,text=True)
(out/'g76-compiler-test.log').write_text(result.stdout+result.stderr)
result.check_returncode()
print(result.stdout.splitlines()[0])
