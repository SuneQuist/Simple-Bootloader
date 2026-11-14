#include <stdint.h>

static int rdrand_supported(void) {
    uint32_t eax = 1, ebx, ecx = 0, edx;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    return (ecx & (1u << 30)) != 0;
}

int account_random_salt(unsigned char salt[16]) {
    if (!rdrand_supported())
        return 0;

    for (unsigned offset = 0; offset < 16; offset += 8) {
        unsigned char ok = 0;
        uint64_t word;
        for (unsigned tries = 0; tries < 10 && !ok; ++tries)
            __asm__ volatile ("rdrand %0; setc %1"
                              : "=r"(word), "=qm"(ok) : : "cc");
        if (!ok)
            return 0;
        for (unsigned j = 0; j < 8; ++j)
            salt[offset + j] = (unsigned char)(word >> (8 * j));
    }
    return 1;
}

