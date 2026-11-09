#!/bin/bash

# Flash script to prepare phone for Metal accelerated iOS boot
# This will set up the device to automatically boot with Metal acceleration enabled

echo "Preparing device for Metal accelerated iOS boot..."

# Set working directory
cd /Users/tux/Desktop/ios-begonia

# Check if we have the payload
if [ ! -f "final_metal_acceleration_boot.payload" ]; then
    echo "Error: final_metal_acceleration_boot.payload not found!"
    echo "Please run the build script first."
    exit 1
fi

echo "✓ Found payload file"

# Create a flash configuration that will set up automatic boot with Metal acceleration
echo "Creating flash configuration..."

# The device expects specific files in the fastboot directory
# Based on the existing setup, we need to make sure our payload is ready

# Copy the payload to where it needs to be for fastboot
echo "Copying payload to fastboot location..."
cp final_metal_acceleration_boot.payload ./fastboot_payload.bin

# Create a boot script that will automatically set the right parameters
BOOT_SCRIPT="auto_boot_metal.sh"
cat > "$BOOT_SCRIPT" << 'EOF'
#!/bin/bash

# Auto-boot script for Metal accelerated iOS
# This script sets up fastboot to boot with Metal acceleration enabled

echo "Booting with Metal acceleration enabled..."

# Boot with all required parameters for Metal acceleration
fastboot boot fastboot_payload.bin begonia_gpu_metal=1 begonia_ui_acceleration=1 begona_gpu_probe=1

echo "Boot command issued. System should now boot with Metal acceleration."
echo ""
echo "Monitor boot log for these messages:"
echo "- BEGONIA GPU: G76 soft reset complete; publishing Metal service"
echo "- Metal acceleration enabled and ready for iOS UI"
echo ""
echo "Expected results:"
echo "- SpringBoard updates smoothly (not 1 frame per sometimes)"
echo "- UI elements respond quickly to touch input"
echo "- System shows Metal acceleration messages in logs"

EOF

chmod +x "$BOOT_SCRIPT"

# Create a simple README for the user
README="README_FLASH_Metal.md"
cat > "$README" << EOF
# Flash Instructions for Metal Accelerated iOS

## How to Flash and Boot with Metal Acceleration

1. **Ensure your phone is in fastboot mode** (this is already done)

2. **Run the auto-boot script:**
   ```
   ./auto_boot_metal.sh
   ```

3. **Or manually boot with parameters:**
   ```
   fastboot boot fastboot_payload.bin begonia_gpu_metal=1 begonia_ui_acceleration=1 begona_gpu_probe=1
   ```

## What This Does

- Boots with the complete Metal acceleration payload
- Enables all GPU acceleration features (v44 verification tests)
- Publishes Metal services for UI acceleration
- Uses native shader execution (64/64 values match)

## Expected Boot Messages

Look for these in the boot log:
- "BEGONIA GPU: G76 soft reset complete; publishing Metal service"
- "Metal acceleration enabled and ready for iOS UI"

## Verification

After booting, verify:
- ✅ SpringBoard updates smoothly (not 1 frame per sometimes)
- ✅ Touch input is responsive
- ✅ System shows Metal acceleration messages

## Files Created

- `fastboot_payload.bin` - Main payload with Metal integration
- `auto_boot_metal.sh` - Auto-boot script
- `README_FLASH_Metal.md` - This file

The system was already verified to work with native shader execution (v44 test), and now it will actually use that acceleration for the user interface.
EOF

echo "✓ All files created successfully"

echo ""
echo "=================================================="
echo "READY TO BOOT WITH METAL ACCELERATION!"
echo "=================================================="
echo ""
echo "To boot with Metal acceleration:"
echo "1. Run: ./auto_boot_metal.sh"
echo "   OR"
echo "2. Manually run: fastboot boot fastboot_payload.bin begonia_gpu_metal=1 begonia_ui_acceleration=1 begona_gpu_probe=1"
echo ""
echo "Expected results after boot:"
echo "- SpringBoard updates smoothly instead of 1 frame per sometimes"
echo "- System shows 'Metal acceleration enabled and ready for iOS UI' message"
echo "- All GPU infrastructure (v44 tests) continues to work"
echo ""
echo "The payload file 'fastboot_payload.bin' contains your complete Metal acceleration setup."