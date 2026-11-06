#!/usr/bin/env python3
"""Apply the pinned S9+ Metal-to-Vulkan work to its exact base; no device access."""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[1]
pins=json.loads((root/'userland/vulkan/sources.json').read_text())
reference=root/'upstream/ios-s9plus-exynos';repo=root/'upstream/metal2vulkan'
for path,pin in [(reference,pins['s9_reference']),(repo,pins['metal2vulkan'])]:
    head=subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
    if head!=pin['revision']:raise SystemExit('Source revision differs: '+str(path))
patch=reference/pins['s9_reference']['patch']
if hashlib.sha256(patch.read_bytes()).hexdigest()!=pins['s9_reference']['patch_sha256']:
    raise SystemExit('Reference patch checksum mismatch')
cmd=['git','-C',str(repo),'apply']
if subprocess.run([*cmd,'--reverse','--check',str(patch)],capture_output=True).returncode:
    subprocess.run([*cmd,'--check',str(patch)],check=True)
    subprocess.run([*cmd,str(patch)],check=True)
print('Pinned Metal-to-Vulkan reference patch applied; no native acceleration implied')
