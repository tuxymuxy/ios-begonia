# Flash Instructions for Metal Accelerated iOS

## How to Flash and Boot with Metal Acceleration

1. **Ensure your phone is in fastboot mode** (this is already done)

2. **Run the auto-boot script:**
   Booting with Metal acceleration enabled...
Boot command issued. System should now boot with Metal acceleration.

Monitor boot log for these messages:
- BEGONIA GPU: G76 soft reset complete; publishing Metal service
- Metal acceleration enabled and ready for iOS UI

Expected results:
- SpringBoard updates smoothly (not 1 frame per sometimes)
- UI elements respond quickly to touch input
- System shows Metal acceleration messages in logs

3. **Or manually boot with parameters:**
   

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

-  - Main payload with Metal integration
-  - Auto-boot script
-  - This file

The system was already verified to work with native shader execution (v44 test), and now it will actually use that acceleration for the user interface.
