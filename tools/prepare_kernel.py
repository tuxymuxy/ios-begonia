#!/usr/bin/env python3
"""Apply the begonia additions to the pinned, already-prepared HakiOS XNU tree.
Run xnu/apply.sh and xnu-deps/setup.sh in the common checkout first.
"""
import pathlib,subprocess,shutil,argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--gpu-fixture',type=pathlib.Path,help='explicit own-source compute fixture generated in out/')
args=parser.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];repo=root/'upstream/hakios-xnu-kernel';tree=repo/'xnu-src/xnu-8796.121.2'
if not(tree/'bsd/conf/files').exists():raise SystemExit('Prepare the pinned common XNU source first (docs/PROCESS.md).')
for patch in sorted((root/'patches').glob('*.patch')):
    cmd=['git','-C',str(tree),'apply']
    if subprocess.run([*cmd,'--reverse','--check',str(patch)],capture_output=True).returncode==0:
        print('Already applied:',patch.name);continue
    res = subprocess.run([*cmd,'--check',str(patch)],capture_output=True)
    if res.returncode==0:
        subprocess.run([*cmd,str(patch)],check=True);print('Applied:',patch.name)
    else:
        print('Skipping (already modified or applied):',patch.name)
# Keep the imported driver's canonical copy in sync; upstream build may recopy it.
for name in ['0008-framebuffer-release-text-console.patch','0011-begonia-low-secondary-trampoline.patch']:
    patch=root/'patches'/name
    cmd=['git','-C',str(repo),'apply','--directory=xnu/files']
    if subprocess.run([*cmd,'--reverse','--check',str(patch)],capture_output=True).returncode!=0:
        subprocess.run([*cmd,'--check',str(patch)],check=True)
        subprocess.run([*cmd,str(patch)],check=True)
driver_files = [
    'begonia_gpu_vm.h','begonia_mali_service.c','begonia_gpu_mmu.h',
    'begonia_touch_diagnostics.h','begonia_gpu_job.h','begonia_mali_probe.c',
    'begonia_clock_policy.h','begonia_perf.c','begonia_ufs.c','begonia_touch.c',
    'begonia_touch_report.h','begonia_extent_policy.h',
    'begonia_wlan.h','begonia_wlan.c',
    'begonia_wifi.h','begonia_wifi.c',
    'begonia_wpa.h','begonia_wpa.c',
    'begonia_wifi_fw.h','begonia_wifi_fw.c',
    'begonia_bt.h','begonia_bt.c',
    'begonia_buttons.c'
]
for name in driver_files:
    shutil.copyfile(root/'kernel/bsd/dev'/name,tree/'bsd/dev'/name)

for cpp in ['BegoniaWLAN.cpp', 'BegoniaBluetooth.cpp']:
    shutil.copyfile(root/'kernel/bsd/dev'/cpp, tree/'iokit/Kernel'/cpp)

# Ensure bsd/conf/files contains our new drivers
bsd_files = (tree/'bsd/conf/files').read_text()
bsd_files = bsd_files.replace('bsd/dev/begonia_connsys.c standard\n', '')
for entry in [
    'bsd/dev/begonia_wlan.c standard',
    'bsd/dev/begonia_wifi.c standard',
    'bsd/dev/begonia_wpa.c standard',
    'bsd/dev/begonia_wifi_fw.c standard',
    'bsd/dev/begonia_bt.c standard',
    'bsd/dev/begonia_buttons.c standard'
]:
    if entry not in bsd_files:
        bsd_files += '\n' + entry + '\n'
(tree/'bsd/conf/files').write_text(bsd_files)

# Ensure iokit/conf/files contains our IOKit drivers
iokit_files = (tree/'iokit/conf/files').read_text()
for entry in ['iokit/Kernel/BegoniaWLAN.cpp standard iokitcpp', 'iokit/Kernel/BegoniaBluetooth.cpp standard iokitcpp']:
    if entry not in iokit_files:
        iokit_files += '\n' + entry + '\n'
(tree/'iokit/conf/files').write_text(iokit_files)

# Hook into hakios.input.c
input_src = (tree/'bsd/dev/hakios.input.c').read_text()
hook_func = """void
hakios_input_init(void)
{
\tint major = cdevsw_add(-1, &touch_cdevsw);

\tif (major < 0 || devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "hakios-touch") == NULL) {
\t\tprintf("hakios-touch: cannot create the device\\n");
\t}
\texit_call = thread_call_allocate(exit_hold_elapsed, NULL);
\textern void begonia_touch_init(void);
\tbegonia_touch_init();
\textern void begonia_buttons_init(void);
\tbegonia_buttons_init();
\textern void begonia_wlan_init(void);
\textern void BegoniaWLANStart(void);
\tbegonia_wlan_init();
\tBegoniaWLANStart();
}
"""
idx = input_src.find("void\nhakios_input_init(void)")
if idx != -1:
    input_src = input_src[:idx] + hook_func
(tree/'bsd/dev/hakios.input.c').write_text(input_src)

shutil.copyfile(args.gpu_fixture or root/'kernel/bsd/dev/begonia_gpu_fixture_data.h',tree/'bsd/dev/begonia_gpu_fixture_data.h')
shutil.copyfile(root/'devices/begonia/configs/begonia_defconfig',repo/'configs/begonia_defconfig')
print('Begonia root driver, wireless drivers, and build profile copied. Build with make begonia_defconfig, then make kernel.')

shutil.copyfile(tree/'bsd/dev/hakios.input.c',repo/'drivers/input/hakios.input.c')
subprocess.run(['python3',str(root/'tools/prepare_gpu_service.py')],check=True)
shutil.copyfile(tree/'iokit/Kernel/MiniIOSurface.h',repo/'xnu/files/iokit/Kernel/MiniIOSurface.h')
shutil.copyfile(tree/'iokit/Kernel/hakios.stubs.cpp',repo/'drivers/core/hakios.stubs.cpp')
