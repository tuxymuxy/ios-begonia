#!/usr/bin/env python3
"""Adapt pinned S9+ IOKit transport to begonia's existing MiniIOSurface layout."""
from pathlib import Path
import shutil,subprocess
root=Path(__file__).resolve().parents[1];reference=root/'upstream/ios-s9plus-exynos'
if subprocess.check_output(['git','-C',str(reference),'rev-parse','HEAD'],text=True).strip()!='2d092398040f98685b0c5a07776df5e9f199b9dc':
    raise SystemExit('S9+ reference revision mismatch')
source=reference/'core/drivers/gpu';tree=root/'upstream/hakios-xnu-kernel/xnu-src/xnu-8796.121.2'
text=(source/'hakios.mali.cpp').read_text()
def change(before,after):
    global text
    if text.count(before)!=1:raise SystemExit('Reference context changed: '+before)
    text=text.replace(before,after)
change('&& s->scanPhys == 0','')
change('|| surface->scanPhys ||','||')
change('memory = s->scan != NULL ? IOSubMemoryDescriptor::withSubRange(s->scan, 0, size, kIODirectionInOut) :\n\t    IOSubMemoryDescriptor::withSubRange(s->memory, PAGE_SIZE, size, kIODirectionInOut);',
       'memory = IOSubMemoryDescriptor::withSubRange(s->memory, PAGE_SIZE, size, kIODirectionInOut);')
change('b = s->scan != NULL ? newBuffer(s->scan, 0, size, &request->handle, PANFROST_BO_NOEXEC) :\n\t    newBuffer(s->memory, PAGE_SIZE, size, &request->handle, PANFROST_BO_NOEXEC);',
       'b = newBuffer(s->memory, PAGE_SIZE, size, &request->handle, PANFROST_BO_NOEXEC);')
change('*options = b->surface != NULL && b->surface->scan != NULL ? kIOMapWriteCombineCache : 0;', '*options = 0;')
change('\t\tfSubmits++;','''        if (!r.submit.jc || !r.submit.bo_handle_count || r.submit.bo_handle_count > MALI_BUFFERS ||
            (r.submit.requirements & ~PANFROST_JD_REQ_FS)) {
            ret = kIOReturnBadArgument; break;
        }
        bool ownsChain = false;
        for (uint32_t i = 0; i < r.submit.bo_handle_count; i++) {
            uint32_t h;
            if (copyin((user_addr_t)(r.submit.bo_handles + i * sizeof(h)), &h, sizeof(h)) ||
                (b = buffer(h)) == NULL) { ret = kIOReturnBadArgument; break; }
            if (b->size >= 32 && r.submit.jc >= b->address && r.submit.jc - b->address <= b->size - 32)
                ownsChain = true;
        }
        if (ret != kIOReturnSuccess) break;
        if (!ownsChain) { ret = kIOReturnBadArgument; break; }
\t\tfSubmits++;''')
change('\t\tr.submit.jc = seq;', '\t\tr.submit.jc = seq;\n        if (!seq) ret = kIOReturnIOError;')
change('\thakios_mali_unmap(b->address, b->size);','''\tif (!hakios_mali_ready()) {
        // A failed GPU reset must never release pages it can still write.
        // Keep descriptor/surface references until the phone resets.
        bzero(b, sizeof(*b));
        return;
    }
\thakios_mali_unmap(b->address, b->size);''')
(tree/'iokit/Kernel/begonia.mali.cpp').write_text(text)
for name in ['hakios.mali_va.h','hakios.mali_cpu_cache.h']:
    shutil.copyfile(source/name,tree/'bsd/dev'/name)
shutil.copyfile(source/'hakios.mali_cpu_cache.h',tree/'iokit/Kernel/hakios.mali_cpu_cache.h')
print('Prepared attributed S9+ transport; Exynos power/clock driver excluded')
