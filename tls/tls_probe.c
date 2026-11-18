/* Phase 10.5: shared, single-flight TLS 1.2 client for the HTTPS browser
 * and the existing tlstest diagnostic. No insecure verification bypass. */
#include <stddef.h>
#include <stdint.h>
#include <bearssl.h>
#include "../console/console.h"
#include "../memory/heap.h"
#include "../networking/timer.h"
#include "../networking/tls_probe_transport.h"
#include "../bin/basic.h"
#include "tls_probe.h"
#include "tls_clock.h"
#include "trust_anchors.h"
#include "https_preview.h"

#define TLS_PROBE_TIMEOUT_MS 35000u
#define TLS_RESPONSE_TIMEOUT_MS 20000u

static struct {
    int active, tcp_connected, saw_handshake, sent_hello;
    int verified, request_queued, status_reported;
    int browser_mode, result;
    const char *error;
    char request[512];
    size_t request_len, request_pos;
    https_preview response;
    br_ssl_client_context *client;
    br_x509_minimal_context *x509;
    unsigned char *records;
    uint64_t deadline;
    char host[128];
} p;

static void print_uint(unsigned value)
{
    char digits[16];
    unsigned n = 0;
    do { digits[n++] = (char)('0' + value % 10u); value /= 10u; }
    while (value && n < sizeof digits);
    while (n) console_putc(digits[--n]);
}

/* Diagnostic only: identify the final certificate issuer by its DER DN
 * SHA-256, without retaining certificates or weakening verification.
 * This is the issuer of the last certificate BearSSL processed, not
 * necessarily the root. Matching a DN is not proof of a valid signature. */
static void print_hex(const unsigned char *src, unsigned len)
{
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < len; i++) {
        console_putc(hex[src[i] >> 4]);
        console_putc(hex[src[i] & 15u]);
    }
}

static void tls_probe_log_untrusted_issuer(void)
{
    if (!p.x509) return;
    const br_x509_minimal_context *x = p.x509;
    console_print("TLS probe: certificates processed: ");
    print_uint(x->num_certs);
    console_print("\n");
    console_print("TLS probe: final issuer DN SHA-256:\n  ");
    print_hex(x->saved_dn_hash, 16);
    console_print("\n  ");
    print_hex(x->saved_dn_hash + 16, 16);
    console_print("\n");

    unsigned matches = 0;
    for (size_t i = 0; i < tiny64_trust_anchor_count; i++) {
        const br_x509_trust_anchor *ta = &tiny64_trust_anchors[i];
        br_sha256_context sha;
        unsigned char digest[32];
        br_sha256_init(&sha);
        br_sha256_update(&sha, ta->dn.data, ta->dn.len);
        br_sha256_out(&sha, digest);
        int equal = 1;
        for (unsigned j = 0; j < sizeof digest; j++) {
            if (digest[j] != x->saved_dn_hash[j]) equal = 0;
        }
        if (equal) {
            matches++;
            console_print("TLS probe: matching root index: ");
            print_uint((unsigned)i);
            console_print("\n");
        }
    }
    console_print("TLS probe: matching root names: ");
    print_uint(matches);
    console_print("\n");
}

/* CPUID-gated RDRAND. No weak fallback; VM must expose a real entropy source. */
static int hardware_entropy(unsigned char seed[48])
{
    unsigned eax, ebx, ecx, edx;
    __asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    if (!(ecx & (1u << 30))) return 0;
    uint64_t previous = 0;
    for (unsigned i = 0; i < 6; i++) {
        uint64_t value = 0;
        unsigned char ok = 0;
        unsigned tries;
        for (tries = 0; tries < 16; tries++) {
            __asm volatile("rdrand %0; setc %1" : "=r"(value), "=qm"(ok));
            if (ok) break;
        }
        if (!ok || (i && value == previous)) return 0;
        previous = value;
        for (unsigned j = 0; j < 8; j++) seed[i * 8 + j] = (unsigned char)(value >> (8 * j));
    }
    return 1;
}

int tls_probe_active(void) { return p.active && !p.browser_mode; }
int tls_https_active(void) { return p.active && p.browser_mode; }
int tls_https_poll(void) { return p.browser_mode ? (p.active ? 0 : p.result) : -1; }
size_t tls_https_length(void) { return p.response.body_used; }
int tls_https_status(void) { return (int)p.response.status; }
const char *tls_https_location(void) { return p.response.location; }
const char *tls_https_error(void) { return p.error ? p.error : ""; }
void tls_https_cancel(void) { tls_probe_cancel(); p.result = 0; }

void tls_probe_cancel(void)
{
    tls_transport_close();
    if (p.records) { kfree(p.records); p.records = NULL; }
    if (p.x509) { kfree(p.x509); p.x509 = NULL; }
    if (p.client) { kfree(p.client); p.client = NULL; }
    p.active = p.tcp_connected = p.saw_handshake = p.sent_hello = 0;
    p.verified = p.request_queued = p.status_reported = 0;
    p.request_len = p.request_pos = 0;
}

static void tls_probe_fail(const char *message)
{
    int browser = p.browser_mode;
    if (!browser) {
        console_print("TLS probe: ");
        console_print(message);
        console_print("\n");
    }
    tls_probe_cancel();
    p.error = message;
    p.result = browser ? -1 : 0;
}

/* Construct a bounded HTTP/1.0 request; reject header and request-line
 * injection. The URL parser supplies an origin-form path without fragments. */
static int prepare_request(const char *path, uint16_t port)
{
    static const char get[] = "GET ";
    static const char middle[] = " HTTP/1.0\r\nHost: ";
    static const char suffix[] =
        "\r\nConnection: close\r\nAccept-Encoding: identity\r\n"
        "User-Agent: Tiny64/10.5\r\n\r\n";
    size_t pos = 0;
#define APPEND_CHAR(ch) do { if (pos + 1 >= sizeof p.request) return 0; p.request[pos++] = (char)(ch); } while (0)
    for (size_t i = 0; get[i]; i++) APPEND_CHAR(get[i]);
    if (!path || path[0] != '/') return 0;
    for (size_t i = 0; path[i]; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c <= 32 || c >= 127 || c == '#') return 0;
        APPEND_CHAR(c);
    }
    for (size_t i = 0; middle[i]; i++) APPEND_CHAR(middle[i]);
    for (size_t i = 0; p.host[i]; i++) {
        char ch = p.host[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '-')) return 0;
        APPEND_CHAR(ch);
    }
    if (port != 443) {
        char digits[6]; unsigned n = 0;
        APPEND_CHAR(':');
        do { digits[n++] = (char)('0' + port % 10u); port /= 10u; } while (port);
        while (n) APPEND_CHAR(digits[--n]);
    }
    for (size_t i = 0; suffix[i]; i++) APPEND_CHAR(suffix[i]);
#undef APPEND_CHAR
    p.request[pos] = 0;
    p.request_len = pos;
    p.request_pos = 0;
    return 1;
}

static void report_http_status(void)
{
    if (p.browser_mode || p.status_reported || !p.response.header_done) return;
    p.status_reported = 1;
    console_print("TLS probe: HTTPS status: ");
    console_print(p.response.status_line);
    console_print("\n");
    if (p.response.location[0]) {
        console_print("TLS probe: Location: ");
        console_print(p.response.location);
        console_print("\n");
    }
    if (p.response.chunked)
        console_print("TLS probe: chunked response (raw body preview)\n");
}

static void report_http_success(void)
{
    if (!p.response.header_done) { tls_probe_fail("no HTTP response headers"); return; }
    if (p.browser_mode) {
        tls_probe_cancel();
        p.result = 1;
        return;
    }
    report_http_status();
    console_print("TLS probe: decrypted body bytes: ");
    print_uint(p.response.body_bytes);
    console_print("\n");
    console_print("TLS probe: decrypted body preview: ");
    console_print(p.response.preview_used ? p.response.preview : "(empty)");
    if (p.response.body_bytes > HTTPS_PREVIEW_CAP) console_print(" ...");
    console_print("\nTLS probe: HTTPS GET complete (verified TLS)\n");
    tls_probe_cancel();
}

static int tls_start(const char *host, uint16_t port, const char *path,
                     char *out, size_t capacity, int browser)
{
    if (p.active) return 0;
    p.browser_mode = browser;
    p.result = 0;
    p.error = "";
    if (!host || !*host || !port) { p.error = "invalid HTTPS host/port"; return 0; }
    unsigned n = 0;
    while (host[n] && n < sizeof p.host) n++;
    if (!n || n >= sizeof p.host) { p.error = "hostname too long"; return 0; }
    for (unsigned i = 0; i <= n; i++) p.host[i] = host[i];
    if (!prepare_request(path, port)) {
        p.error = "invalid HTTPS path or hostname";
        return 0;
    }
    https_preview_init(&p.response);
    if (browser) https_preview_set_body(&p.response, (unsigned char *)out, capacity);
    p.verified = p.request_queued = p.status_reported = 0;

    uint32_t utc_days, utc_seconds;
    if (!tls_clock_now(&utc_days, &utc_seconds)) {
        p.error = "UTC not approved; run tlsclock first";
        if (!browser) console_print("TLS probe: UTC not approved; run tlsclock first\n");
        return 0;
    }
    if (!tiny64_trust_anchor_count) {
        p.error = "no trusted CA roots";
        if (!browser) console_print("TLS probe: no trusted CA roots; refusing handshake\n");
        return 0;
    }
    if (!network_ensure_ready()) {
        p.error = "network unavailable";
        if (!browser) console_print("TLS probe: network unavailable\n");
        return 0;
    }
    p.client = kmalloc(sizeof *p.client);
    p.x509 = kmalloc(sizeof *p.x509);
    p.records = kmalloc(BR_SSL_BUFSIZE_MONO);
    if (!p.client || !p.x509 || !p.records) {
        tls_probe_fail("out of memory");
        return 0;
    }

    unsigned char seed[48] = {0};
    if (!hardware_entropy(seed)) {
        tls_probe_fail("RDRAND unavailable/failed; refusing insecure handshake");
        return 0;
    }
    br_ssl_client_init_full(p.client, p.x509,
                            tiny64_trust_anchors, tiny64_trust_anchor_count);
    br_x509_minimal_set_time(p.x509, utc_days, utc_seconds);
    br_ssl_engine_set_versions(&p.client->eng, BR_TLS12, BR_TLS12);
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
    };
    br_ssl_engine_set_suites(&p.client->eng, suites, sizeof suites / sizeof suites[0]);
    br_ssl_engine_set_buffer(&p.client->eng, p.records, BR_SSL_BUFSIZE_MONO, 0);
    br_ssl_engine_inject_entropy(&p.client->eng, seed, sizeof seed);
    /* Erase the local copy of the entropy as soon as it has been injected. */
    volatile unsigned char *wipe = seed;
    for (unsigned i = 0; i < sizeof seed; i++) wipe[i] = 0;
    if (!br_ssl_client_reset(p.client, p.host, 0)) {
        tls_probe_fail("BearSSL reset failed");
        return 0;
    }
    if (!tls_transport_start(p.host, port)) {
        tls_probe_fail("network transport start failed (HTTP busy or no route)");
        return 0;
    }
    p.active = 1;
    p.tcp_connected = p.saw_handshake = p.sent_hello = 0;
    p.deadline = net_millis() + TLS_PROBE_TIMEOUT_MS;
    if (!browser) {
        console_print("TLS probe: DNS/TCP connecting; TLS 1.2 ClientHello queued\n");
        console_print("TLS probe: certificate chain, hostname and UTC validation ENABLED\n");
    }
    return 1;
}

int tls_probe_start(const char *host, uint16_t port)
{
    return tls_start(host, port, "/", NULL, 0, 0);
}

int tls_https_start(const char *host, uint16_t port, const char *path,
                    char *out, size_t capacity)
{
    if (!out || capacity < 2) return 0;
    return tls_start(host, port, path, out, capacity, 1);
}

void tls_probe_poll(void)
{
    if (!p.active) return;
    if (net_millis() >= p.deadline) {
        tls_probe_fail(p.request_queued ? "HTTPS response timeout" : "handshake timeout");
        return;
    }
    int transport = tls_transport_poll();
    if (transport < 0) {
        tls_probe_fail(tls_transport_error());
        return;
    }
    if (transport == 0) return;
    if (!p.tcp_connected) {
        p.tcp_connected = 1;
        if (!p.browser_mode) console_print("TLS probe: TCP connected\n");
    }

    br_ssl_engine_context *eng = &p.client->eng;
    unsigned state = br_ssl_engine_current_state(eng);
    if (state & BR_SSL_CLOSED) {
        int error = br_ssl_engine_last_error(eng);
        if (error == BR_ERR_OK && p.request_queued &&
            https_preview_close(&p.response)) {
            report_http_success();
            return;
        }
        if (p.browser_mode) {
            tls_probe_fail(error == BR_ERR_X509_NOT_TRUSTED ? "untrusted certificate issuer" :
                error == BR_ERR_X509_TIME_UNKNOWN ? "TLS clock unavailable" :
                error == BR_ERR_X509_EXPIRED ? "expired certificate" :
                error == BR_ERR_X509_BAD_SERVER_NAME ? "certificate hostname mismatch" :
                error == BR_ERR_X509_BAD_SIGNATURE ? "invalid certificate signature" :
                error == BR_ERR_OK ? "TLS closed before complete HTTP response" :
                "TLS handshake or record failure");
            return;
        }
        console_print("TLS probe: BearSSL closed, error "); print_uint((unsigned)error);
        console_print("\n");
        if (error == BR_ERR_X509_NOT_TRUSTED) {
            console_print("TLS probe: certificate rejected (untrusted issuer)\n");
            tls_probe_log_untrusted_issuer();
        }
        else if (error == BR_ERR_X509_TIME_UNKNOWN)
            console_print("TLS probe: certificate rejected (UTC unavailable)\n");
        else if (error == BR_ERR_X509_EXPIRED)
            console_print("TLS probe: certificate rejected (date not valid)\n");
        else if (error == BR_ERR_X509_BAD_SERVER_NAME)
            console_print("TLS probe: certificate rejected (hostname mismatch)\n");
        else if (error == BR_ERR_X509_BAD_SIGNATURE)
            console_print("TLS probe: certificate rejected (bad signature)\n");
        else if (error == BR_ERR_OK)
            console_print("TLS probe: TLS closed before complete HTTP response\n");
        tls_probe_cancel();
        return;
    }

    /* Read one decrypted application fragment per desktop iteration. */
    if (state & BR_SSL_RECVAPP) {
        size_t len = 0;
        unsigned char *src = br_ssl_engine_recvapp_buf(eng, &len);
        if (src && len) {
            if (!p.request_queued || !https_preview_feed(&p.response, src, len)) {
                tls_probe_fail("malformed or oversized HTTPS response");
                return;
            }
            br_ssl_engine_recvapp_ack(eng, len);
            p.deadline = net_millis() + TLS_RESPONSE_TIMEOUT_MS;
            report_http_status();
            if (p.response.complete) {
                report_http_success();
                return;
            }
            state = br_ssl_engine_current_state(eng);
        }
    }

    /* SENDAPP is reached only after BearSSL completes and authenticates
     * the TLS handshake. No plaintext request is sent before verification. */
    if (state & BR_SSL_SENDAPP && !p.request_queued) {
        if (!p.verified) {
            p.verified = 1;
            if (!p.browser_mode)
                console_print("TLS probe: VERIFIED TLS 1.2 handshake completed!\n");
        }
        size_t len = 0;
        unsigned char *dst = br_ssl_engine_sendapp_buf(eng, &len);
        if (dst && len) {
            size_t n = p.request_len - p.request_pos;
            if (n > len) n = len;
            for (size_t i = 0; i < n; i++) dst[i] = (unsigned char)p.request[p.request_pos + i];
            br_ssl_engine_sendapp_ack(eng, n);
            p.request_pos += n;
            if (p.request_pos == p.request_len) {
                p.request_queued = 1;
                br_ssl_engine_flush(eng, 0);
                p.deadline = net_millis() + TLS_RESPONSE_TIMEOUT_MS;
                if (!p.browser_mode)
                    console_print("TLS probe: encrypted HTTPS GET / queued\n");
            }
            state = br_ssl_engine_current_state(eng);
        }
    }

    /* Feed at most one portion of one TCP segment per GUI iteration. */
    if (state & BR_SSL_RECVREC) {
        size_t len = 0;
        unsigned char *dst = br_ssl_engine_recvrec_buf(eng, &len);
        if (dst && len) {
            size_t n = tls_transport_read(dst, len);
            if (n) {
                if (!p.saw_handshake && dst[0] == 22) {
                    p.saw_handshake = 1;
                    if (!p.browser_mode)
                        console_print("TLS probe: received TLS handshake record\n");
                }
                br_ssl_engine_recvrec_ack(eng, n);
                state = br_ssl_engine_current_state(eng);
            }
        }
    }

    /* Move at most one TLS record fragment to TCP per GUI iteration. */
    if (state & BR_SSL_SENDREC) {
        size_t len = 0;
        unsigned char *src = br_ssl_engine_sendrec_buf(eng, &len);
        if (src && len) {
            int sent = tls_transport_send(src, len);
            if (sent < 0) { tls_probe_fail("TCP record send failed"); return; }
            if (sent > 0) {
                if (!p.sent_hello) {
                    p.sent_hello = 1;
                    console_print("TLS probe: ClientHello sent (SNI enabled)\n");
                }
                br_ssl_engine_sendrec_ack(eng, (size_t)sent);
            }
        }
    }

    /* TCP FIN alone is not an authenticated TLS close_notify. */
    if (transport == 2 && !(br_ssl_engine_current_state(eng) & BR_SSL_CLOSED))
        tls_probe_fail("TCP EOF without TLS close_notify (truncated response)");
}
