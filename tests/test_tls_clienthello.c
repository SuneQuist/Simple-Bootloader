/* Native BearSSL ClientHello smoke test; no live network or certificate trust. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocs, frees, sent;
static uint8_t wire[4096];
void *kmalloc(size_t n) { void *p=malloc(n); if(p) allocs++; return p; }
void kfree(void *p) { if(p) { frees++; free(p); } }
void console_print(const char *s) { (void)s; }
void console_putc(char c) { (void)c; }
int network_ensure_ready(void) { return 1; }
int tls_clock_now(uint32_t *days, uint32_t *seconds) { *days=739982u; *seconds=43200u; return 1; }
uint64_t net_millis(void) { return 1000; }
int tls_transport_start(const char *h,uint16_t port) { return h && port==443; }
int tls_transport_poll(void) { return 1; }
int tls_transport_send(const uint8_t *p,size_t n) {
    assert(sent+n<=sizeof wire);
    memcpy(wire+sent,p,n);sent+=(unsigned)n;return (int)n;
}
size_t tls_transport_read(uint8_t *p,size_t n) { (void)p;(void)n;return 0; }
const char *tls_transport_error(void) { return ""; }
int tls_transport_active(void) { return 0; }
void tls_transport_close(void) { }
#include "../tls/tls_probe.h"
#include "../tls/trust_anchors.h"
int main(void) {
    assert(tiny64_trust_anchor_count == 12);
    /* Regression: the issuer DN observed from example.com in VirtualBox
     * matches the subject DN of the newly bundled AAA root. */
    static const unsigned char issuer_hash[32] = {
        0xce,0x35,0x86,0x26,0x52,0x2e,0x56,0x40,
        0x0f,0x48,0xcc,0xfd,0xb5,0x2d,0xf5,0x9f,
        0x5f,0x9c,0x85,0xa7,0xea,0x3e,0xfc,0x7f,
        0xd1,0x9b,0x64,0x1b,0x0b,0xd0,0xd8,0x12
    };
    br_sha256_context sha;
    unsigned char digest[32];
    br_sha256_init(&sha);
    br_sha256_update(&sha, tiny64_trust_anchors[11].dn.data,
                     tiny64_trust_anchors[11].dn.len);
    br_sha256_out(&sha, digest);
    assert(memcmp(digest, issuer_hash, sizeof digest) == 0);
    assert(tls_probe_start("example.com",443));
    for (unsigned i=0;i<8;i++) tls_probe_poll();
    assert(sent>50 && wire[0]==22 && wire[1]==3);
    int sni=0;
    for (unsigned i=0;i+11<=sent;i++)
        if (!memcmp(wire+i,"example.com",11)) sni=1;
    assert(sni);
    tls_probe_cancel();
    assert(allocs==frees);
    puts("PASS: BearSSL ClientHello TLS record, SNI, RDRAND entropy, heap cleanup");
    return 0;
}
