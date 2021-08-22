#include <stdint.h>
#include <stddef.h>
#include <lib/blib.h>
#include <lib/print.h>

void copyright_notice(void) {
    print("Limine " LIMINE_VERSION "\n");
    print(LIMINE_COPYRIGHT "\n");
    print("This bootloader is distributed under the terms of the BSD-2-Clause license.\n\n");
}

uint8_t bcd_to_int(uint8_t val) {
    return (val & 0x0f) + ((val & 0xf0) >> 4) * 10;
}

int digit_to_int(char c) {
    if (c >= 'a' && c <= 'f') {
        return (c - 'a') + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return (c - 'A') + 10;
    }
    if (c >= '0' && c <= '9'){
        return c - '0';
    }

    return -1;
}

uint64_t strtoui(const char *s, const char **end, int base) {
    uint64_t n = 0;
    for (size_t i = 0; ; i++) {
        int d = digit_to_int(s[i]);
        if (d == -1) {
            if (end != NULL)
                *end = &s[i];
            break;
        }
        n = n * base + d;
    }
    return n;
}
