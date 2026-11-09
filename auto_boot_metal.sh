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

