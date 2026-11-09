# Metal Acceleration Integration Plan

Based on the analysis, here's how to get full iOS GPU acceleration working:

## Current Status Analysis

From the documentation and build logs, we know:
1. ✅ GPU acceleration is verified - native shader execution passes (64/64 values match)
2. ✅ Metal-to-Vulkan runtime builds successfully 
3. ✅ The `libmetal2vulkan_bridge.dylib` exists and is signed
4. ❌ But the UI is not using this acceleration - it's still software-rendered

## Integration Steps

### 1. Update XNU Kernel Configuration
We need to modify the kernel to load and initialize the Metal-to-Vulkan bridge when GPU acceleration is enabled.

### 2. Create a Service/Bundle Integration
The system needs to properly install and register the Metal driver bundle so SpringBoard can use it.

### 3. Update Boot Parameters 
Add parameters to enable Metal acceleration in the boot process.

## Implementation Plan

### Step 1: Create Updated Kernel Integration
We need to modify `begonia_mali_probe.c` to actually publish a Metal service:

```c
// In begonia_mali_probe.c, add Metal service publication:
void begonia_mali_probe(uint32_t gpu_uv,uint32_t sram_uv,int rails_enabled) {
    // ... existing code ...
    
    printf("BEGONIA GPU: G76 soft reset complete; publishing Metal service\n");
    
    // Add Metal service publication here
    if(write_value_test(gpu) && mapped_write_test(gpu))shader_compute_test(gpu);
    
    // Publish the Metal service so SpringBoard can use it
    // This is where we'd call functions to register the Metal driver
}
```

### Step 2: Create a Build Script
Create a script that:
1. Builds the Metal-to-Vulkan runtime (already done)
2. Creates proper integration files
3. Updates kernel configuration
4. Builds new payload with GPU acceleration enabled

### Step 3: Modify Boot Parameters
Ensure that boot parameters enable Metal acceleration:
```
begonia_gpu_metal=1
```

## Immediate Fix Required

Looking at the current build, I need to create a new build that specifically integrates this Metal-to-Vulkan runtime into the actual UI path. The key files are:
- `out/metal2vulkan-target-ios/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib`
- The existing GPU infrastructure in `kernel/bsd/dev/begonia_*.c`

The issue is that while we have all the pieces, we're missing the integration step that actually tells iOS to use this GPU acceleration for UI rendering.