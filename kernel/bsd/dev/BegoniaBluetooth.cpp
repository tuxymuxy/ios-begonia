#include <sys/types.h>

extern "C" int BegoniaBluetoothPublish(const uint8_t address[6], const uint8_t commands[64])
{
    (void)address;
    (void)commands;
    return 0;
}

extern "C" void BegoniaBluetoothSetReady(bool ready)
{
    (void)ready;
}
