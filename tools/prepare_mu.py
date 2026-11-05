#!/usr/bin/env python3
"""Install our reviewable begonia overlay into the pinned Mu checkout. No flashing."""
from pathlib import Path
import shutil, subprocess, json
ROOT = Path(__file__).resolve().parents[1]
repo = ROOT / 'upstream/Mu-Silicium'
expected = json.loads((ROOT / 'docs/sources.lock.json').read_text())['Mu-Silicium']['commit']
actual = subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
if actual != expected:
    raise SystemExit('Mu checkout does not match the pinned revision')
for source in (ROOT / 'firmware/mu').rglob('*'):
    if source.is_file():
        target = repo / source.relative_to(ROOT / 'firmware/mu')
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
print('Installed begonia overlay; firmware is experimental and not flashed.')
