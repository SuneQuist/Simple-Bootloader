/* Host-only integration harness for the REAL Tiny64 TLS probe.
 * Connects to a local Python TLS server signed by an ephemeral test CA.
 * No test key or test root is compiled into the Tiny64 kernel. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <bearssl.h>
#include "generated_https_integration.h"
#include "../tls/tls_probe.h"
#include "../tls/trust_anchors.h"

const br_x509_trust_anchor tiny64_trust_anchors[] = {{
    {(unsigned char *)test_root_dn, sizeof test_root_dn}, BR_X509_TA_CA,
    {BR_KEYTYPE_RSA, {.rsa = {(unsigned char *)test_root_n, sizeof test_root_n,
                             (unsigned char *)test_root_e, sizeof test_root_e}}}
}};
const size_t tiny64_trust_anchor_count = 1;

static int fd = -1;
static unsigned char rx[4096];
static size_t rx_used, rx_pos;
static char logs[8192];
static size_t logs_used;
static unsigned allocs, frees;
void *kmalloc(size_t n) { void *p=malloc(n); if(p) allocs++; return p; }
void kfree(void *p) { if(p) { frees++; free(p); } }
void console_putc(char ch) {
    if(logs_used+1 < sizeof logs) { logs[logs_used++]=ch; logs[logs_used]=0; }
}
void console_print(const char *s) { while(*s) console_putc(*s++); }
int network_ensure_ready(void) { return 1; }
int tls_clock_now(uint32_t *days, uint32_t *seconds) {
    *days=740303u; *seconds=43200u; return 1;
}
uint64_t net_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
}
int tls_transport_start(const char *host, uint16_t port) {
    (void)host;
    fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0) return 0;
    struct sockaddr_in addr={0};
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(0x7f000001u);
    addr.sin_port=htons(port);
    if(connect(fd,(struct sockaddr *)&addr,sizeof addr)!=0) return 0;
    int flags=fcntl(fd,F_GETFL,0);
    if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0) return 0;
    return 1;
}
int tls_transport_poll(void) {
    if(fd<0) return -1;
    if(rx_pos<rx_used) return 1;
    ssize_t n=recv(fd,rx,sizeof rx,0);
    if(n>0) { rx_pos=0;rx_used=(size_t)n;return 1; }
    if(n==0) return 2;
    if(errno==EAGAIN || errno==EWOULDBLOCK) return 1;
    return -1;
}
int tls_transport_send(const uint8_t *data,size_t len) {
    ssize_t n=send(fd,data,len,MSG_NOSIGNAL);
    if(n>0) return (int)n;
    if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) return 0;
    return -1;
}
size_t tls_transport_read(uint8_t *out,size_t cap) {
    size_t n=rx_used-rx_pos;
    if(n>cap) n=cap;
    memcpy(out,rx+rx_pos,n);
    rx_pos+=n;
    if(rx_pos==rx_used) rx_pos=rx_used=0;
    return n;
}
const char *tls_transport_error(void) { return "host socket error"; }
int tls_transport_active(void) { return fd>=0; }
void tls_transport_close(void) { if(fd>=0) close(fd);fd=-1;rx_pos=rx_used=0; }

int main(int argc,char **argv) {
    assert(argc==2);
    int port=atoi(argv[1]);
    assert(port>0 && port<65536);
    assert(tls_probe_start("tiny64-test.invalid",(uint16_t)port));
    uint64_t until=net_millis()+10000u;
    while(tls_probe_active() && net_millis()<until) {
        tls_probe_poll();
        struct timespec nap={0,1000000};
        nanosleep(&nap,NULL);
    }
    puts(logs);
    fflush(stdout);
    assert(!tls_probe_active());
    assert(strstr(logs,"VERIFIED TLS 1.2 handshake completed!") != NULL);
    assert(strstr(logs,"HTTPS status: HTTP/1.1 200 OK") != NULL);
    assert(strstr(logs,"decrypted body preview: Hello Tiny64") != NULL);
    assert(strstr(logs,"HTTPS GET complete (verified TLS)") != NULL);
    char body[128];
    assert(tls_https_start("tiny64-test.invalid", (uint16_t)port,
                           "/test/path?x=1", body, sizeof body));
    until=net_millis()+10000u;
    while(tls_https_active() && net_millis()<until) {
        tls_probe_poll();
        struct timespec nap={0,1000000};
        nanosleep(&nap,NULL);
    }
    assert(!tls_https_active());
    assert(tls_https_poll()==1);
    assert(tls_https_status()==200);
    assert(tls_https_length()==12);
    assert(strcmp(body,"Hello Tiny64")==0);
    tls_https_cancel();

    assert(tls_https_start("tiny64-test.invalid", (uint16_t)port,
                           "/redirect", body, sizeof body));
    until=net_millis()+10000u;
    while(tls_https_active() && net_millis()<until) {
        tls_probe_poll();
        struct timespec nap={0,1000000};
        nanosleep(&nap,NULL);
    }
    assert(tls_https_poll()==1);
    assert(tls_https_status()==302);
    assert(strcmp(tls_https_location(),"https://tiny64-test.invalid/test/path?x=1")==0);
    tls_https_cancel();
    assert(allocs==frees);
    puts("PASS: verified TLS, probe, browser HTTPS body, chunked decode, redirects, heap cleanup");
    return 0;
}
