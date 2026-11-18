#ifndef TINY64_TLS_CLOCK_H
#define TINY64_TLS_CLOCK_H
#include <stdint.h>

/* UTC is never implicitly trusted: the operator must approve RTC time or
 * explicitly supply UTC from an independent, trusted source. */
int tls_clock_read_rtc(uint32_t *days, uint32_t *seconds);
int tls_clock_parse_utc(const char *date, const char *time,
                        uint32_t *days, uint32_t *seconds);
int tls_clock_set_utc(uint32_t days, uint32_t seconds);
int tls_clock_approve_rtc(void);
/* Explicit local-RTC conversion for Europe/Copenhagen (CET/CEST).
 * Rejects nonexistent and ambiguous DST transition hours. */
int tls_clock_convert_copenhagen(uint32_t local_days, uint32_t local_seconds,
                                 uint32_t *utc_days, uint32_t *utc_seconds);
int tls_clock_approve_copenhagen(void);
int tls_clock_now(uint32_t *days, uint32_t *seconds);
/* Print a validated BearSSL day/seconds pair as YYYY-MM-DD HH:MM:SS UTC. */
void tls_clock_print_utc(uint32_t days, uint32_t seconds);
void tls_clock_show(void);
#endif
