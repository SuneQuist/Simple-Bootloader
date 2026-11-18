#include <stdint.h>
#include "timer.h"

#define PIT_HZ       1193182ULL
#define CALIBRATE_MS 50ULL

static uint64_t tsc_hz = 0;
static uint64_t tsc_start = 0;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;

    __asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));

    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;

    __asm volatile ("rdtsc" : "=a"(lo), "=d"(hi) : : "memory");

    return ((uint64_t)hi << 32) | lo;
}

int timer_init(void) {
    if (tsc_hz)
        return 1;

    /*
     * PIT channel 2, mode 0 (one-shot).
     * Gate is controlled by port 0x61 bit 0.
     * Bit 5 reports OUT2.
     */
    uint16_t divisor =
        (uint16_t)((PIT_HZ * CALIBRATE_MS) / 1000);

    uint8_t saved = inb(0x61);

    /* Gate low, speaker disabled. */
    outb(0x61, (saved & (uint8_t)~0x03));

    /* Channel 2, low/high byte, mode 0, binary. */
    outb(0x43, 0xB0);

    outb(0x42, (uint8_t)(divisor & 0xff));
    outb(0x42, (uint8_t)(divisor >> 8));

    /* Raise gate to start the one-shot countdown. */
    outb(0x61, (saved & (uint8_t)~0x02) | 0x01);

    uint64_t start = rdtsc();

    /* Wait for PIT channel 2 OUT to become high. */
    while (!(inb(0x61) & 0x20)) {
        __asm volatile ("pause");
    }

    uint64_t end = rdtsc();

    /* Restore the original speaker/gate control. */
    outb(0x61, saved);

    if (end <= start)
        return 0;

    uint64_t elapsed_cycles = end - start;

    tsc_hz = (elapsed_cycles * 1000ULL) / CALIBRATE_MS;

    if (tsc_hz < 1000) {
        tsc_hz = 0;
        return 0;
    }

    tsc_start = end;

    return 1;
}

uint64_t net_millis(void) {
    if (tsc_hz < 1000)
        return 0;

    uint64_t cycles_per_ms = tsc_hz / 1000;

    return (rdtsc() - tsc_start) / cycles_per_ms;
}