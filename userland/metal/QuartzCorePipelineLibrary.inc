// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 the HakiOS authors
// Adapted from HakiOS userland/src/HakiosMetalCA.m for the native Metal bridge.

extern "C" const void *_dyld_get_shared_cache_range(size_t *length);

- (const struct BegoniaMTLTargetDeviceArch *)targetDeviceInfo {
    static char name[] = "BegoniaG76";
    static const BegoniaMTLTargetDeviceArch target = {0, 0, name};
    return &target;
}

- (id)newPipelineLibraryWithFilePath:(NSString *)path error:(NSError **)error {
    // Patch missing entry in QuartzCore ogl-metal shader table at 0x1d5b53db0 (table[2])
    {
        size_t cache_len = 0;
        const void *cache_base = _dyld_get_shared_cache_range(&cache_len);
        if (cache_base != NULL) {
            uintptr_t cache_slide = (uintptr_t)cache_base - 0x180000000UL;
            uintptr_t *stable = (uintptr_t *)(0x1d5b53db0UL + cache_slide);
            if (stable[2] == 0) {
                vm_address_t page = (vm_address_t)&stable[2] & ~0x3fffUL;
                kern_return_t kr = vm_protect(mach_task_self(), page, 0x4000, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
                if (kr == KERN_SUCCESS) {
                    stable[2] = stable[1] != 0 ? stable[1] : stable[3];
                    NSLog(@"[BegoniaMetal] Patched QuartzCore shader_table[2] = %p (was NULL)", (void *)stable[2]);
                } else {
                    NSLog(@"[BegoniaMetal] Failed to make QuartzCore shader table writable: %d", kr);
                }
            }
        }
    }
    NSString *quartzCore = @"/System/Library/Frameworks/QuartzCore.framework/default.pipelinelib";
    if (![path isEqualToString:quartzCore]) {
        if (error) *error = nbError([NSString stringWithFormat:@"no pipeline library on this GPU: %@", path.lastPathComponent]);
        return nil;
    }
    Method original = class_getInstanceMethod(class_getSuperclass(object_getClass(self)), _cmd);
    if (!original) {
        if (error) *error = nbError(@"Metal pipeline library loader is unavailable");
        return nil;
    }
    typedef id (*OpenLibrary)(id, SEL, NSString *, NSError **) __attribute__((ns_returns_retained));
    OpenLibrary openLibrary = (OpenLibrary)method_getImplementation(original);
    NSError *first = nil;
    id library = openLibrary(self, _cmd, path, &first);
    NSString *cacheName = @"pipelines.BegoniaG76";
    if (library ||
        ![first.localizedDescription containsString:@"Could not open pipeline cache file:"] ||
        ![first.localizedDescription containsString:cacheName]) {
        NSLog(@"Begonia QuartzCore pipeline library %@", library ? @"opened" : first);
        if (error) *error = first;
        return library;
    }

    // Metal creates its own cache format. An empty cache lets it take the
    // normal miss path and build the pipelines from the library descriptions.
    NSFileManager *files = NSFileManager.defaultManager;
    NSString *temp = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    NSString *copy = [temp stringByAppendingPathComponent:path.lastPathComponent];
    NSError *failure = nil;
    if ([files createDirectoryAtPath:copy withIntermediateDirectories:YES attributes:nil error:&failure]) {
        for (NSString *entry in @[@"functions", @"pipelines.desc"]) {
            if (![files createSymbolicLinkAtPath:[copy stringByAppendingPathComponent:entry]
                             withDestinationPath:[path stringByAppendingPathComponent:entry]
                                            error:&failure]) break;
        }
        if (!failure) {
            [files createSymbolicLinkAtPath:[temp stringByAppendingPathComponent:@"default.metallib"]
                        withDestinationPath:@"/redwood/lib/default.metallib" error:&failure];
        }
    }
    if (!failure) {
        id cache = [NSClassFromString(@"_MTLPipelineCache") alloc];
        SEL initializer = sel_registerName("initWithFilePath:readOnly:deviceInfo:");
        if ([cache respondsToSelector:initializer]) {
            cache = ((id (*)(id, SEL, NSString *, BOOL, const void *))objc_msgSend)(
                cache, initializer, [copy stringByAppendingPathComponent:cacheName],
                NO, [self targetDeviceInfo]);
            SEL writeCache = sel_registerName("write");
            if ([cache respondsToSelector:writeCache]) {
                ((void (*)(id, SEL))objc_msgSend)(cache, writeCache);
                library = openLibrary(self, _cmd, copy, &failure);
            }
        }
    }
    if (!library) [files removeItemAtPath:temp error:nil];
    NSLog(@"Begonia QuartzCore pipeline library %@", library ? @"opened" : (failure ?: first));
    if (error) *error = library ? nil : (failure ?: first);
    return library;
}
