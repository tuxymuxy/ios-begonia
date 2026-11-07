#!/usr/bin/env python3
"""Build a private begonia support image with SpringBoard logging and test swipes."""
from pathlib import Path
import argparse,plistlib,shutil,subprocess,sys
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--base-root',type=Path,help='reuse an existing tested support tree without rebuilding its userland')
parser.add_argument('--output-dir',type=Path)
parser.add_argument('--gpu-service-test',type=Path)
parser.add_argument('--physical-touch',action='store_true')
parser.add_argument('--quiet',action='store_true')
parser.add_argument('--with-metal',action='store_true',help='stage PanVK, metal2vulkan, HakiosMetal bundle and GPU test binaries')
parser.add_argument('--metal-client',action='append',default=[],help='opt in an additional named process; default is diagnostics only')
args=parser.parse_args()
if args.metal_client and (not args.with_metal or any(not s or not all(c.isascii() and (c.isalnum() or c in '._-') for c in s) for s in args.metal_client)):
    parser.error('--metal-client requires --with-metal and a literal process name')

root=Path(__file__).resolve().parents[1];repo=root/'upstream/hakios-xnu-kernel'
out=args.output_dir or root/'out/begonia-ui-support';dest=out/'root'
source=args.base_root or repo/'userland/root'
if source.resolve()==dest.resolve() or source.resolve() in dest.resolve().parents or dest.resolve() in source.resolve().parents:
    parser.error('base and output must be separate trees')
if dest.exists():
    shutil.rmtree(dest)
shutil.copytree(source,dest,symlinks=True)

# Build init from the recorded patch, rather than trusting a stale private binary.
if not args.base_root:
    subprocess.run([sys.executable,str(root/'tools/prepare_userland.py')],check=True)
    shutil.copy2(repo/'userland/obj/init',dest/'sbin/init')
    shutil.copy2(repo/'userland/obj/libhakios.dylib',dest/'lib/libhakios.dylib')

for label in ['com.apple.SpringBoard','com.apple.lsd']:
    p=dest/'LaunchDaemons'/(label+'.plist');job=plistlib.loads(p.read_bytes())
    if args.quiet:job.get('EnvironmentVariables',{}).pop('OS_ACTIVITY_DT_MODE',None)
    else:job.setdefault('EnvironmentVariables',{})['OS_ACTIVITY_DT_MODE']='YES'
    p.write_bytes(plistlib.dumps(job,fmt=plistlib.FMT_BINARY))

p=dest/'LaunchDaemons/com.apple.backboardd.plist';job=plistlib.loads(p.read_bytes())
job.get('EnvironmentVariables',{}).pop('OS_ACTIVITY_DT_MODE',None)
p.write_bytes(plistlib.dumps(job,fmt=plistlib.FMT_BINARY))

shutil.copy2(root/'devices/begonia/userland/rc.jobs.ui-diagnostic',dest/'etc/rc.jobs')
if args.physical_touch:
    jobs=dest/'etc/rc.jobs'
    jobs.write_text(''.join(line for line in jobs.read_text().splitlines(True) if '/bin/rwtouch ' not in line))

if args.gpu_service_test:
    shutil.copy2(args.gpu_service_test,dest/'bin/gpu_service_test')
    jobs=dest/'etc/rc.jobs';jobs.write_text('/redwood/bin/gpu_service_test\n'+jobs.read_text())

if args.with_metal:
    # 0. Stage default.metallib fallback and updated inject libraries
    metallib_src = root/'upstream/hakios-xnu-kernel/sysimage/rootfs/System/Library/Extensions/AppleMetalGLRenderer.bundle/default.metallib'
    if metallib_src.exists():
        shutil.copy2(metallib_src, dest/'lib/default.metallib')

    for dylib_name in ['libhakios.dylib', 'libhakios.sb.dylib']:
        dylib_src = root/'upstream/hakios-xnu-kernel/userland/obj'/dylib_name
        if dylib_src.exists():
            dylib_dst = dest/'lib'/dylib_name
            shutil.copy2(dylib_src, dylib_dst)
            subprocess.run(['xcrun', 'strip', '-S', '-x', str(dylib_dst)], check=True)
            subprocess.run(['codesign', '-s', '-', '-f', str(dylib_dst)], check=True)

    # 1. Stage PanVK Vulkan ICD and metal2vulkan bridge
    for source_file, name in [
        (root/'out/panvk-ios-build/src/panfrost/vulkan/libvulkan_panfrost.dylib', 'libvulkan_panfrost.dylib'),
        (root/'out/metal2vulkan-target-ios/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib', 'libmetal2vulkan.dylib')
    ]:
        target = dest/'lib'/name
        shutil.copy2(source_file, target)
        subprocess.run(['xcrun', 'strip', '-S', '-x', str(target)], check=True)
        subprocess.run(['codesign', '-s', '-', '-f', str(target)], check=True)
        subprocess.run(['codesign', '--verify', '--strict', str(target)], check=True)

    # 2. Stage HakiosMetal bundle
    bundle_bin = dest/'lib/HakiosMetal.bundle/HakiosMetal'
    if (root/'out/metal-frontend-ios/HakiosMetal').exists():
        bundle_bin.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root/'out/metal-frontend-ios/HakiosMetal', bundle_bin)
        subprocess.run(['xcrun', 'strip', '-S', '-x', str(bundle_bin)], check=True)
        subprocess.run(['codesign', '-s', '-', '-f', str(bundle_bin)], check=True)
        subprocess.run(['codesign', '--verify', '--strict', str(bundle_bin)], check=True)

    # 3. Write HakiosMetal.clients (Metal shown to SpringBoard and tests; backboardd uses GLES)
    clients=dict.fromkeys(['metal_frontend_test','metal_test','metal_vulkan_compute_test',*args.metal_client])
    (dest/'lib/HakiosMetal.clients').write_text(''.join(name+'\n' for name in clients))

    # 4. Stage AIR compute shader
    (dest/'fw').mkdir(exist_ok=True)
    if (root/'out/begonia-own-compute.air').exists():
        shutil.copy2(root/'out/begonia-own-compute.air', dest/'fw/begonia-own-compute.air')

    # 5. Stage GPU test binaries into bin/
    gpu_tests = ['gpu_service_test', 'gpu_surface_test', 'vulkan_device_test', 'metal_frontend_test']
    for t in gpu_tests:
        src = root/'out/native-gpu-tests-v59'/t
        if src.exists():
            shutil.copy2(src, dest/'bin'/t)

    # 6. Configure rc.jobs: run tests first, then GUI diagnostics and swipes timed for 180s capture
    test_calls = '\n'.join('/redwood/bin/' + t for t in gpu_tests if (dest/'bin'/t).exists())
    rc_content = f"""{test_calls}
/redwood/bin/vol_ls fs /private/var
/redwood/bin/vol_ls cat /private/var/begonia-persistence-test 4096
/redwood/bin/var_store stamp /private/var/begonia-persistence-test
/redwood/bin/hello_dyn started by launchd
/redwood/bin/svc_test
/redwood/bin/surf_test
/bin/ps -A
/redwood/bin/rwtouch wait 25 swipe 0.5 0.985 0.5 0.3 250 wait 50 swipe 0.5 0.985 0.5 0.3 250 wait 50 swipe 0.5 0.985 0.5 0.3 250 &
sleep 30
/bin/ps -A
/redwood/bin/vol_ls fs /private/var
sleep 45
/bin/ps -A
sleep 45
/bin/ps -A
"""
    (dest/'etc/rc.jobs').write_text(rc_content)

subprocess.run([sys.executable,str(repo/'tools/apfs/mkapfs.py'),'-z','--slack','128','--plain','fw','-o',str(out/'root.apfs'),'redwood='+str(dest)],check=True)
print('Built private support image; the staged system APFS file is unchanged.')
