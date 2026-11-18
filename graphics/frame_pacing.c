#include <stdint.h>
#include "frame_pacing.h"

/*
 * Read the CPU timestamp counter.
 *
 * RDTSC returns a monotonically increasing counter on
 * suitable x86-64 systems.
 *
 * We use it for lightweight frame pacing.
 */
static uint64_t read_tsc(void)
{
    uint32_t low;
    uint32_t high;

    __asm volatile ("rdtsc" : "=a"(low), "=d"(high));

    return ((uint64_t)high << 32) | low;
}

/*
 * Estimated CPU timestamp-counter frequency.
 *
 * IMPORTANT:
 * This value is only a temporary estimate.
 * For accurate pacing, we should calibrate the TSC
 * against Tiny64's timer.
 */
#define TSC_FREQUENCY 2000000000ULL

#define FRAME_INTERVAL \
    (TSC_FREQUENCY / FRAME_TARGET_FPS)

static uint64_t last_frame_tsc = 0;

void frame_pacing_init(void)
{
    last_frame_tsc = read_tsc();
}

int frame_pacing_ready(void)
{
    uint64_t now = read_tsc();

    if (now - last_frame_tsc < FRAME_INTERVAL)
        return 0;

    /*
     * Reset to the current time instead of attempting
     * to catch up on missed frames.
     *
     * This avoids bursts of redraws after a slow frame.
     */
    last_frame_tsc = now;

    return 1;
}