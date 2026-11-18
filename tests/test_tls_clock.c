#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../tls/tls_clock.h"
static uint64_t ms = 123456;
int timer_init(void) { return 1; }
uint64_t net_millis(void) { return ms; }
static char output[256];
static size_t output_len;
void console_putc(char c) {
    assert(output_len + 1 < sizeof output);
    output[output_len++] = c;
    output[output_len] = 0;
}
void console_print(const char *s) {
    while (*s) console_putc(*s++);
}
static void clear_output(void) { output_len = 0; output[0] = 0; }
int main(void)
{
    uint32_t d, s;
    assert(!tls_clock_now(&d, &s)); /* fail closed without approval */
    assert(tls_clock_parse_utc("2026-11-18", "20:37:45", &d, &s));
    assert(d == 739982u + 321u); /* Nov 18: day 321 of nonleap year */
    assert(s == 20u*3600u + 37u*60u + 45u);
    clear_output();
    tls_clock_print_utc(d, s);
    assert(strcmp(output, "2026-11-18 20:37:45 UTC\n") == 0);
    assert(tls_clock_parse_utc("2024-02-29", "00:00:00", &d, &s));
    clear_output();
    tls_clock_print_utc(d, s);
    assert(strcmp(output, "2024-02-29 00:00:00 UTC\n") == 0);
    assert(tls_clock_parse_utc("2099-12-31", "23:59:59", &d, &s));
    clear_output();
    tls_clock_print_utc(d, s);
    assert(strcmp(output, "2099-12-31 23:59:59 UTC\n") == 0);
    clear_output();
    tls_clock_print_utc(0, 0);
    assert(strcmp(output, "Invalid UTC\n") == 0);
    assert(!tls_clock_parse_utc("2025-02-29", "12:00:00", &d, &s));
    assert(tls_clock_parse_utc("2024-02-29", "00:00:00", &d, &s));
    assert(!tls_clock_parse_utc("2024-02-30", "00:00:00", &d, &s));
    assert(!tls_clock_parse_utc("2026-11-18", "24:00:00", &d, &s));
    assert(!tls_clock_parse_utc("2026-11-18", "12:00:00junk", &d, &s));
    assert(!tls_clock_parse_utc("2026-11-18x", "12:00:00", &d, &s));
    assert(tls_clock_parse_utc("2026-11-18", "23:59:58", &d, &s));
    assert(tls_clock_set_utc(d, s));
    assert(tls_clock_now(&d, &s) && s == 86398u);
    ms += 3000u;
    assert(tls_clock_now(&d, &s) && s == 1u);
    assert(d == 739982u + 322u);
    uint32_t ud,us;
    assert(tls_clock_parse_utc("2026-11-18","21:37:41",&d,&s));
    assert(tls_clock_convert_copenhagen(d,s,&ud,&us));
    assert(ud==d && us==20u*3600u+37u*60u+41u);
    assert(tls_clock_parse_utc("2026-07-18","21:37:41",&d,&s));
    assert(tls_clock_convert_copenhagen(d,s,&ud,&us));
    assert(ud==d && us==19u*3600u+37u*60u+41u);
    assert(tls_clock_parse_utc("2026-03-29","02:30:00",&d,&s));
    assert(!tls_clock_convert_copenhagen(d,s,&ud,&us));
    assert(tls_clock_parse_utc("2026-10-25","02:30:00",&d,&s));
    assert(!tls_clock_convert_copenhagen(d,s,&ud,&us));
    assert(tls_clock_parse_utc("2026-10-25","03:30:00",&d,&s));
    assert(tls_clock_convert_copenhagen(d,s,&ud,&us));
    assert(us==2u*3600u+30u*60u);
    puts("PASS: UTC approval, Gregorian dates, Copenhagen CET/CEST and DST ambiguity");
    return 0;
}
