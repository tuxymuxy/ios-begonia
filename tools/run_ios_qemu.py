#!/usr/bin/env python3
"""Local, private firmware-derived iOS userland validation. Not a phone emulator profile.
Requires the common tree's locally built system/root APFS images and real-identity EFI loader.
"""
import argparse,pathlib,subprocess,shutil,time,os
root=pathlib.Path(__file__).resolve().parents[1];repo=root/'upstream/hakios-xnu-kernel';out=root/'out/ios16-qemu'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--seconds',type=int,default=540);p.add_argument('--window',action='store_true');p.add_argument('--verbose',action='store_true');p.add_argument('--support-root',type=pathlib.Path,default=repo/'userland/root.apfs');p.add_argument('--cpu',choices=['cortex-a55','cortex-a76'],default='cortex-a76');p.add_argument('--output-dir',type=pathlib.Path,default=out);a=p.parse_args();out=a.output_dir;out.mkdir(parents=True,exist_ok=True)
image=repo/'userland/system.apfs';payload=out/'payload.bin'
subprocess.run([str(repo/'xnuloader/mkpayload.py'),'-o',str(payload),'kernel='+str(repo/'hakios-kernel'),'ramdisk='+str(a.support_root),'--rdres',format(image.stat().st_size,'x'),'--cmdline','cpus=1 loader_return_after='+str(a.seconds-40)+' epoch='+str(int(time.time()))+(' -v' if a.verbose else '')],check=True)
esp=out/'esp/EFI/BOOT';esp.mkdir(parents=True,exist_ok=True);shutil.copyfile(repo/'xnuloader/loader.efi',esp/'BOOTAA64.EFI');shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd',out/'vars.fd')
backing=root/'out/ios16-qemu/ram.img'
if not backing.exists() or backing.stat().st_mtime<image.stat().st_mtime:
    with backing.open('wb') as dst, image.open('rb') as src:
        dst.truncate(7<<30);dst.seek(0xFC000000-0x40000000);shutil.copyfileobj(src,dst,8<<20)
cmd=['qemu-system-aarch64','-M','virt,gic-version=3','-cpu',a.cpu,'-smp','1','-m','7G','-accel','tcg,tb-size=256','-drive','if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-aarch64-code.fd','-drive','if=pflash,format=raw,file='+str(out/'vars.fd'),'-drive','file=fat:rw:'+str(out/'esp')+',format=raw,if=virtio','-device','loader,file='+str(payload)+',addr=0xF0000000,force-raw=on','-object','memory-backend-file,id=ram,size='+str(7<<30)+',mem-path='+str(backing)+',share=off','-machine','memory-backend=ram','-device','ramfb','-display',('cocoa' if a.window else 'none'),'-serial','file:'+str(out/'serial.log'),'-monitor','unix:'+str(out/'monitor.sock')+',server=on,wait=off','-no-reboot']
with (out/'supervisor.log').open('wb') as log:
 proc=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT);(out/'pid').write_text(str(proc.pid))
 try:proc.wait(timeout=a.seconds)
 except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=5)
print('Emulator stopped; return:',proc.returncode)
