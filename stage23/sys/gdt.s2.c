#include <stdint.h>
#include <sys/gdt.h>

static struct gdt_desc gdt_descs[] = {
    {0},

    {
        .limit       = 0xffff,
        .base_low    = 0x0000,
        .base_mid    = 0x00,
        .access      = 0b10011010,
        .granularity = 0b00000000,
        .base_hi     = 0x00
    },

    {
        .limit       = 0xffff,
        .base_low    = 0x0000,
        .base_mid    = 0x00,
        .access      = 0b10010010,
        .granularity = 0b00000000,
        .base_hi     = 0x00
    },

    {
        .limit       = 0xffff,
        .base_low    = 0x0000,
        .base_mid    = 0x00,
        .access      = 0b10011010,
        .granularity = 0b11001111,
        .base_hi     = 0x00
    },

    {
        .limit       = 0xffff,
        .base_low    = 0x0000,
        .base_mid    = 0x00,
        .access      = 0b10010010,
        .granularity = 0b11001111,
        .base_hi     = 0x00
    },

    {
        .limit       = 0x0000,
        .base_low    = 0x0000,
        .base_mid    = 0x00,
        .access      = 0b10011010,
        .granularity = 0b00100000,
        .base_hi     = 0x00
    },

    {
        .limit       = 0x0000,
        .base_low    = 0x0000,
        .base_mid    = 0x00,
        .access      = 0b10010010,
        .granularity = 0b00000000,
        .base_hi     = 0x00
    }
};

#if bios == 1
__attribute__((section(".realmode")))
#endif
struct gdtr gdt = {
    sizeof(gdt_descs) - 1,
    (uintptr_t)gdt_descs,
#if defined (__i386__)
    0
#endif
};
