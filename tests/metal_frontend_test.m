/* SPDX-License-Identifier: MIT
 * Native Metal Frontend diagnostic: MTLCreateSystemDefaultDevice, blits, and IOSurface textures.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurfaceRef.h>
#include <mach/mach.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "native_gpu_diagnostics.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IOLBF, 0);
    setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);
    native_gpu_install_crash_diagnostics();
    alarm(120);

    puts("BEGONIA METAL FRONTEND: begin");
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            fprintf(stderr, "BEGONIA METAL FRONTEND: MTLCreateSystemDefaultDevice returned nil\n");
            return 1;
        }
        printf("BEGONIA METAL FRONTEND: device=%s registryID=%llu\n",
               [device.name UTF8String], (unsigned long long)device.registryID);

        // 1. Buffer creation and CPU access
        const NSUInteger bufSize = 4096;
        id<MTLBuffer> bufA = [device newBufferWithLength:bufSize options:MTLResourceStorageModeShared];
        id<MTLBuffer> bufB = [device newBufferWithLength:bufSize options:MTLResourceStorageModeShared];
        if (!bufA || !bufB) {
            fprintf(stderr, "BEGONIA METAL FRONTEND: failed to allocate test buffers\n");
            return 1;
        }

        // 2. Command queue and blit operations (fill + copy)
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) {
            fprintf(stderr, "BEGONIA METAL FRONTEND: failed to create command queue\n");
            return 1;
        }

        id<MTLCommandBuffer> cb = [queue commandBuffer];
        if (!cb) {
            fprintf(stderr, "BEGONIA METAL FRONTEND: failed to create command buffer\n");
            return 1;
        }

        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        if (!blit) {
            fprintf(stderr, "BEGONIA METAL FRONTEND: failed to create blit encoder\n");
            return 1;
        }

        // Fill bufA with 0x5a
        [blit fillBuffer:bufA range:NSMakeRange(0, bufSize) value:0x5a];
        // Copy bufA to bufB
        [blit copyFromBuffer:bufA sourceOffset:0 toBuffer:bufB destinationOffset:0 size:bufSize];
        [blit endEncoding];

        puts("BEGONIA METAL FRONTEND: committing blit commands");
        [cb commit];
        [cb waitUntilCompleted];

        if (cb.status != MTLCommandBufferStatusCompleted) {
            fprintf(stderr, "BEGONIA METAL FRONTEND: blit command buffer status %lu error: %s\n",
                    (unsigned long)cb.status, cb.error ? [cb.error.localizedDescription UTF8String] : "none");
            return 1;
        }

        const uint8_t *ptrB = (const uint8_t *)bufB.contents;
        unsigned blit_matches = 0;
        for (NSUInteger i = 0; i < bufSize; i++) {
            if (ptrB[i] == 0x5a) blit_matches++;
        }
        printf("BEGONIA METAL FRONTEND: blit fill/copy matches=%u/%u\n", blit_matches, (unsigned)bufSize);
        if (blit_matches != bufSize) {
            fprintf(stderr, "BEGONIA METAL FRONTEND: blit byte mismatch\n");
            return 1;
        }

        // 3. IOSurface texture wrapping and render clear
        NSDictionary *surfaceProps = @{
            (id)kIOSurfaceWidth: @64,
            (id)kIOSurfaceHeight: @64,
            (id)kIOSurfaceBytesPerElement: @4,
            (id)kIOSurfaceBytesPerRow: @256,
            (id)kIOSurfaceAllocSize: @16384,
            (id)kIOSurfacePixelFormat: @(0x42475241) // 'BGRA'
        };
        IOSurfaceRef surface = IOSurfaceCreate((__bridge CFDictionaryRef)surfaceProps);
        if (!surface) {
            fprintf(stderr, "BEGONIA METAL FRONTEND: failed to create IOSurface\n");
            return 1;
        }

        // Initial pattern in IOSurface
        if (IOSurfaceLock(surface, 0, NULL) != 0) return 1;
        uint32_t *surfacePixels = (uint32_t *)IOSurfaceGetBaseAddress(surface);
        for (int i = 0; i < 64 * 64; i++) {
            surfacePixels[i] = 0xcafe0000;
        }
        IOSurfaceUnlock(surface, 0, NULL);

        MTLTextureDescriptor *texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                          width:64
                                                                                         height:64
                                                                                      mipmapped:NO];
        texDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        id<MTLTexture> surfaceTex = [device newTextureWithDescriptor:texDesc iosurface:surface plane:0];
        if (!surfaceTex) {
            fprintf(stderr, "BEGONIA METAL FRONTEND: failed to wrap IOSurface in MTLTexture\n");
            CFRelease(surface);
            return 1;
        }
        printf("BEGONIA METAL FRONTEND: IOSurface texture wrapped successfully (%p)\n", (__bridge void *)surfaceTex);

        // Perform RenderPass Clear on IOSurface
        MTLRenderPassDescriptor *passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.colorAttachments[0].texture = surfaceTex;
        passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        // RGBA: (0.25, 0.5, 0.75, 1.0) -> in BGRA8 bytes: B ~ 191 (0xbf), G ~ 128 (0x80), R ~ 64 (0x40), A = 255 (0xff)
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.25, 0.5, 0.75, 1.0);

        id<MTLCommandBuffer> renderCb = [queue commandBuffer];
        id<MTLRenderCommandEncoder> renderEnc = [renderCb renderCommandEncoderWithDescriptor:passDesc];
        if (!renderEnc) {
            fprintf(stderr, "BEGONIA METAL FRONTEND: failed to create render command encoder\n");
            CFRelease(surface);
            return 1;
        }
        [renderEnc endEncoding];

        puts("BEGONIA METAL FRONTEND: committing render clear on IOSurface");
        [renderCb commit];
        [renderCb waitUntilCompleted];

        if (renderCb.status != MTLCommandBufferStatusCompleted) {
            fprintf(stderr, "BEGONIA METAL FRONTEND: render command buffer status %lu error: %s\n",
                    (unsigned long)renderCb.status, renderCb.error ? [renderCb.error.localizedDescription UTF8String] : "none");
            CFRelease(surface);
            return 1;
        }

        // Readback IOSurface pixels directly
        if (IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL) != 0) return 1;
        const uint8_t *rawBytes = (const uint8_t *)IOSurfaceGetBaseAddress(surface);
        // Pixel at (32, 32): offset = (32 * 256) + (32 * 4) = 8192 + 128 = 8320
        const uint8_t b = rawBytes[8320 + 0];
        const uint8_t g = rawBytes[8320 + 1];
        const uint8_t r = rawBytes[8320 + 2];
        const uint8_t a = rawBytes[8320 + 3];
        IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL);

        printf("BEGONIA METAL FRONTEND: IOSurface clear readback BGRA=%u/%u/%u/%u\n", b, g, r, a);
        int clearOk = (b >= 190 && b <= 193) && (g >= 126 && g <= 129) && (r >= 62 && r <= 65) && (a == 255);
        if (clearOk) {
            puts("BEGONIA METAL FRONTEND: IOSurface render clear PASS");
        } else {
            fprintf(stderr, "BEGONIA METAL FRONTEND: IOSurface render clear FAIL (expected ~191/128/64/255)\n");
            CFRelease(surface);
            return 1;
        }

        CFRelease(surface);
        puts("BEGONIA METAL FRONTEND: ALL PASS");
    }
    alarm(0);
    return 0;
}
