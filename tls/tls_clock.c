/* Phase 10.3: explicitly approved UTC for BearSSL certificate validation.
 * CMOS RTC may be configured to local time or have an incorrect date.
 * It is NOT automatically trusted. `tlsclock` shows the candidate;
 * `tlsclock accept` explicitly approves it, or `tlsclock set ...` accepts
 * UTC supplied out-of-band by the operator. This is a development-only
 * time policy; there is no authenticated NTP or anti-rollback protection.
 */
#include <stddef.h>
#include <stdint.h>
#include "../console/console.h"
#include "../networking/timer.h"
#include "tls_clock.h"

static struct {
    int approved;
    uint32_t days, seconds;
    uint64_t monotonic_ms;
} clock_state;

static inline uint8_t clock_inb(uint16_t port)
{
    uint8_t v;
    __asm volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void clock_outb(uint16_t port, uint8_t v)
{
    __asm volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

/* Protect the shared CMOS index register from interrupt handlers. */
static uint8_t cmos_read(uint8_t reg)
{
    uint64_t flags;
    __asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    /* Port 0x70 is not reliably readable. Keep NMI enabled and avoid
     * changing the NMI mask; interrupts cannot change the index here. */
    clock_outb(0x70, (uint8_t)(reg & 0x7fu));
    uint8_t value = clock_inb(0x71);
    __asm volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
    return value;
}

static int leap(unsigned y)
{
    return (y % 4u == 0u) && (y % 100u != 0u || y % 400u == 0u);
}

static int valid_date(unsigned y, unsigned m, unsigned d)
{
    static const uint8_t lengths[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (y < 2024 || y > 2099 || m < 1 || m > 12 || d < 1) return 0;
    return d <= (unsigned)lengths[m] + (unsigned)(m == 2 && leap(y));
}

static int to_bearssl_time(unsigned y, unsigned mo, unsigned d,
                           unsigned h, unsigned mi, unsigned s,
                           uint32_t *days, uint32_t *seconds)
{
    static const unsigned cumulative[] = {0,0,31,59,90,120,151,181,212,243,273,304,334};
    if (!days || !seconds || !valid_date(y, mo, d) ||
        h > 23 || mi > 59 || s > 59) return 0;
    /* Proleptic Gregorian days since 0000-01-01. Unix epoch = 719528. */
    *days = 365u * y + (y + 3u) / 4u - (y + 99u) / 100u +
            (y + 399u) / 400u + cumulative[mo] + d - 1u +
            (unsigned)(mo > 2 && leap(y));
    *seconds = h * 3600u + mi * 60u + s;
    return 1;
}

static unsigned digit(char c)
{
    return (unsigned)(c - '0');
}

static int dec(char c)
{
    return c >= '0' && c <= '9';
}

int tls_clock_parse_utc(const char *date, const char *time,
                        uint32_t *days, uint32_t *seconds)
{
    if (!date || !time) return 0;
    /* Exact formats: YYYY-MM-DD and HH:MM:SS; reject trailing garbage. */
    for (unsigned i = 0; i < 10; i++) {
        if (!date[i]) return 0;
        if (i == 4 || i == 7) { if (date[i] != '-') return 0; }
        else if (!dec(date[i])) return 0;
    }
    if (date[10]) return 0;
    for (unsigned i = 0; i < 8; i++) {
        if (!time[i]) return 0;
        if (i == 2 || i == 5) { if (time[i] != ':') return 0; }
        else if (!dec(time[i])) return 0;
    }
    if (time[8]) return 0;
    unsigned y = digit(date[0])*1000u + digit(date[1])*100u + digit(date[2])*10u + digit(date[3]);
    unsigned mo = digit(date[5])*10u + digit(date[6]);
    unsigned d = digit(date[8])*10u + digit(date[9]);
    unsigned h = digit(time[0])*10u + digit(time[1]);
    unsigned mi = digit(time[3])*10u + digit(time[4]);
    unsigned s = digit(time[6])*10u + digit(time[7]);
    return to_bearssl_time(y, mo, d, h, mi, s, days, seconds);
}

static unsigned bcd(uint8_t v)
{
    return (unsigned)(v & 15u) + 10u * (unsigned)(v >> 4);
}

struct rtc_snapshot { uint8_t second, minute, hour, day, month, year, status; };

static int rtc_snapshot(struct rtc_snapshot *r)
{
    if (cmos_read(0x0a) & 0x80u) return 0; /* update in progress */
    r->second = cmos_read(0x00);
    r->minute = cmos_read(0x02);
    r->hour = cmos_read(0x04);
    r->day = cmos_read(0x07);
    r->month = cmos_read(0x08);
    r->year = cmos_read(0x09);
    r->status = cmos_read(0x0b);
    return !(cmos_read(0x0a) & 0x80u);
}

int tls_clock_read_rtc(uint32_t *days, uint32_t *seconds)
{
    struct rtc_snapshot a, b;
    for (unsigned tries = 0; tries < 12; tries++) {
        if (!rtc_snapshot(&a) || !rtc_snapshot(&b)) continue;
        if (a.second != b.second || a.minute != b.minute ||
            a.hour != b.hour || a.day != b.day ||
            a.month != b.month || a.year != b.year ||
            a.status != b.status) continue;
        unsigned s = a.second, mi = a.minute, h = a.hour & 0x7fu;
        unsigned d = a.day, mo = a.month, y = a.year;
        if (!(a.status & 0x04u)) { /* BCD format */
            s = bcd((uint8_t)s); mi = bcd((uint8_t)mi);
            h = bcd((uint8_t)h); d = bcd((uint8_t)d);
            mo = bcd((uint8_t)mo); y = bcd((uint8_t)y);
        }
        if (!(a.status & 0x02u)) { /* 12-hour clock */
            if (h < 1 || h > 12) return 0;
            h = (h % 12u) + ((a.hour & 0x80u) ? 12u : 0u);
        }
        /* Tiny64 targets modern VM dates; require 2000..2099. */
        return to_bearssl_time(2000u + y, mo, d, h, mi, s, days, seconds);
    }
    return 0;
}

int tls_clock_set_utc(uint32_t days, uint32_t seconds)
{
    if (days < 739251u || days > 767009u || seconds >= 86400u ||
        !timer_init()) return 0;
    clock_state.days = days;
    clock_state.seconds = seconds;
    clock_state.monotonic_ms = net_millis();
    clock_state.approved = 1;
    return 1;
}

int tls_clock_approve_rtc(void)
{
    uint32_t days, seconds;
    if (!tls_clock_read_rtc(&days, &seconds)) return 0;
    return tls_clock_set_utc(days, seconds);
}

/* EU daylight saving rules: last Sunday in March at 02:00 local
 * (jump to 03:00), last Sunday in October at 03:00 local
 * (repeat 02:00). Reject 02:xx on both transition dates because the
 * RTC alone cannot disambiguate the repeated autumn hour. */
static uint32_t last_sunday(unsigned year, unsigned month)
{
    uint32_t last_day, ignored;
    if (!to_bearssl_time(year, month, 31, 0, 0, 0, &last_day, &ignored))
        return 0;
    /* Gregorian days from year 0000: (day + 5) % 7 is Monday=0. */
    return last_day - ((last_day + 6u) % 7u);
}

int tls_clock_convert_copenhagen(uint32_t local_days, uint32_t local_seconds,
                                 uint32_t *utc_days, uint32_t *utc_seconds)
{
    if (!utc_days || !utc_seconds || local_seconds >= 86400u) return 0;
    unsigned year=2024;
    uint32_t jan, ignored;
    if (!to_bearssl_time(year,1,1,0,0,0,&jan,&ignored) || local_days<jan) return 0;
    while (year<2100) {
        uint32_t next;
        if (year==2099) break;
        if (!to_bearssl_time(year+1,1,1,0,0,0,&next,&ignored)) return 0;
        if (local_days<next) break;
        year++;
    }
    uint32_t last;
    if (year>2099 ||
        !to_bearssl_time(2099,12,31,0,0,0,&last,&ignored) ||
        local_days>last) return 0;
    uint32_t spring=last_sunday(year,3), autumn=last_sunday(year,10);
    if (!spring || !autumn) return 0;
    if ((local_days==spring || local_days==autumn) &&
        local_seconds>=7200u && local_seconds<10800u) return 0;
    int summer=(local_days>spring && local_days<autumn) ||
        (local_days==spring && local_seconds>=10800u) ||
        (local_days==autumn && local_seconds<7200u);
    unsigned offset=summer?7200u:3600u;
    if (local_seconds<offset) {
        if (!local_days) return 0;
        *utc_days=local_days-1;
        *utc_seconds=local_seconds+86400u-offset;
    } else {
        *utc_days=local_days;
        *utc_seconds=local_seconds-offset;
    }
    return 1;
}

int tls_clock_approve_copenhagen(void)
{
    uint32_t local_days,local_seconds,utc_days,utc_seconds;
    if (!tls_clock_read_rtc(&local_days,&local_seconds) ||
        !tls_clock_convert_copenhagen(local_days,local_seconds,&utc_days,&utc_seconds))
        return 0;
    return tls_clock_set_utc(utc_days,utc_seconds);
}

int tls_clock_now(uint32_t *days, uint32_t *seconds)
{
    if (!clock_state.approved || !days || !seconds) return 0;
    uint64_t elapsed = (net_millis() - clock_state.monotonic_ms) / 1000u;
    uint64_t total = (uint64_t)clock_state.seconds + elapsed;
    if (total / 86400u > 366u) return 0; /* Require periodic re-approval. */
    *days = clock_state.days + (uint32_t)(total / 86400u);
    *seconds = (uint32_t)(total % 86400u);
    return 1;
}

static void print_dec(unsigned value, unsigned width)
{
    char digits[16];
    unsigned n = 0;
    do { digits[n++] = (char)('0' + value % 10u); value /= 10u; }
    while (value && n < sizeof digits);
    while (n < width) digits[n++] = '0';
    while (n) console_putc(digits[--n]);
}

void tls_clock_print_utc(uint32_t days, uint32_t seconds)
{
    /* Start at 2024, the first year accepted by to_bearssl_time().
     * Previously this used 2000; validation rejected that date and left
     * base == 0, producing bogus years/days such as 2100-12-703445. */
    unsigned y = 2024;
    uint32_t base = 0, dummy;
    if (!to_bearssl_time(y, 1, 1, 0, 0, 0, &base, &dummy) ||
        days < base || days > 767009u || seconds >= 86400u) {
        console_print("Invalid UTC\n");
        return;
    }
    while (y < 2100 && base + (leap(y) ? 366u : 365u) <= days) {
        base += leap(y) ? 366u : 365u;
        y++;
    }
    unsigned mo = 1;
    static const uint8_t lengths[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t day_index = days - base;
    while (mo < 12) {
        unsigned length = lengths[mo] + (unsigned)(mo == 2 && leap(y));
        if (day_index < length) break;
        day_index -= length;
        mo++;
    }
    print_dec(y, 4); console_putc('-'); print_dec(mo, 2);
    console_putc('-'); print_dec(day_index + 1u, 2);
    console_putc(' '); print_dec(seconds / 3600u, 2);
    console_putc(':'); print_dec((seconds / 60u) % 60u, 2);
    console_putc(':'); print_dec(seconds % 60u, 2);
    console_print(" UTC\n");
}

void tls_clock_show(void)
{
    uint32_t d, s;
    if (tls_clock_read_rtc(&d, &s)) {
        console_print("CMOS RTC (unverified; may be local time): ");
        tls_clock_print_utc(d, s);
    } else console_print("CMOS RTC: unavailable or invalid\n");
    if (tls_clock_now(&d, &s)) {
        console_print("TLS operator-approved UTC: ");
        tls_clock_print_utc(d, s);
    } else console_print("TLS UTC: NOT APPROVED\n");
    console_print("Verify UTC independently, then use 'tlsclock accept'\n");
    console_print("or 'tlsclock copenhagen' (local CET/CEST RTC)\n");
    console_print("or 'tlsclock set YYYY-MM-DD HH:MM:SS'.\n");
}
