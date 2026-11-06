#!/usr/bin/env python3
"""Apply recorded G76 frontend adaptations to the pinned viva-gpu checkout."""
from pathlib import Path
import shutil,subprocess
root=Path(__file__).resolve().parents[1]
repo=root/'upstream/viva-gpu'
for patch in sorted((root/'userland/gpu/patches').glob('*.patch')):
    cmd=['git','-C',str(repo),'apply']
    if subprocess.run([*cmd,'--reverse','--check',str(patch)],capture_output=True).returncode==0:continue
    subprocess.run([*cmd,'--check',str(patch)],check=True)
    subprocess.run([*cmd,str(patch)],check=True)
shutil.copyfile(root/'userland/gpu/begonia_gpu_support.h',repo/'driver/begonia_gpu_support.h')
shutil.copyfile(root/'userland/gpu/begonia_gpu_params.h',repo/'vgpu/begonia_gpu_params.h')
print('G76 uses Mesa/NIR; direct ISA compiler remains limited to known G57 products.')
