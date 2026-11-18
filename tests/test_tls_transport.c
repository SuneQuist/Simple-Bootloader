/* Native deterministic test for Phase 10.2's nonblocking wire transport.
 * Uses synthetic Ethernet frames; does NOT establish a secure TLS session. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
static uint64_t ticks;
static uint8_t frames[16][1600];
static unsigned lengths[16], head, tail, sends;
static uint32_t local_ip = 0x0a00020f;
uint64_t net_millis(void) { return ticks; }
uint32_t net_local_ip(void) { return local_ip; }
void net_local_mac(uint8_t mac[6]) { uint8_t v[6]={2,1,2,3,4,5}; memcpy(mac,v,6); }
int net_parse_ipv4(const char *s, uint32_t *out) {
    unsigned a,b,c,d; char tailch;
    if (sscanf(s,"%u.%u.%u.%u%c",&a,&b,&c,&d,&tailch)!=4 ||
        a>255 || b>255 || c>255 || d>255) return 0;
    *out=(a<<24)|(b<<16)|(c<<8)|d; return 1;
}
uint32_t net_next_hop(uint32_t target) {
    return (target&0xffffff00)==(local_ip&0xffffff00) ? target : 0x0a000202;
}
int net_arp_cached(uint32_t target, uint8_t mac[6]) { (void)target;(void)mac;return 0; }
void net_arp_remember(uint32_t target, const uint8_t mac[6]) { (void)target;(void)mac; }
int net_resolve(uint32_t target, uint8_t mac[6]) { (void)target;(void)mac;return 0; }
void net_handle_arp_frame(const uint8_t *f,unsigned n) { (void)f;(void)n; }
int eth_send(const uint8_t *f,unsigned n) { (void)f;(void)n;sends++;return 1; }
int eth_recv(uint8_t *f,unsigned cap) {
    if(head==tail) return 0;
    assert(lengths[head]<=cap);
    memcpy(f,frames[head],lengths[head]);return (int)lengths[head++];
}
void console_print(const char *s) { (void)s; }
void console_putc(char c) { (void)c; }
#include "../networking/http.c"
static void push_arp(uint32_t target) {
    uint8_t *f=frames[tail]; memset(f,0,60);
    uint8_t own[6]; net_local_mac(own);
    memcpy(f,own,6);memcpy(f+32,own,6);
    for(int i=0;i<6;i++){f[6+i]=0x11+i;f[22+i]=0x11+i;}
    put16(f+12,0x0806);put16(f+20,2);
    put32(f+28,target);put32(f+38,local_ip);
    lengths[tail++]=60;
}
static void push_dns(void) {
    uint8_t *f=frames[tail];memset(f,0,1600);
    uint8_t *ip=f+14,*udp=ip+20,*d=udp+8;
    put16(f+12,0x0800);ip[0]=0x45;ip[9]=17;
    put32(ip+12,0x0a000203);put32(ip+16,local_ip);
    put16(udp,53);put16(udp+2,53065);
    put16(d,t.dns_id);put16(d+2,0x8180);put16(d+4,1);put16(d+6,1);
    unsigned n=12;
    d[n++]=7;memcpy(d+n,"example",7);n+=7;
    d[n++]=3;memcpy(d+n,"com",3);n+=3;d[n++]=0;
    put16(d+n,1);n+=2;put16(d+n,1);n+=2;
    d[n++]=0xc0;d[n++]=0x0c;
    put16(d+n,1);n+=2;put16(d+n,1);n+=2;
    put32(d+n,60);n+=4;put16(d+n,4);n+=2;
    put32(d+n,0x6814179a);n+=4;
    put16(udp+4,8+n);put16(ip+2,20+8+n);
    lengths[tail++]=14+20+8+n;
}
static void push_tcp(uint8_t flags,uint32_t seq,uint32_t ack,
                     const uint8_t *body,unsigned n) {
    uint8_t *f=frames[tail];memset(f,0,1600);
    uint8_t *ip=f+14,*tcp=ip+20;
    put16(f+12,0x0800);ip[0]=0x45;ip[9]=6;
    put32(ip+12,t.remote_ip);put32(ip+16,local_ip);
    put16(ip+2,20+20+n);
    put16(tcp,t.port);put16(tcp+2,t.tcp.local_port);
    put32(tcp+4,seq);put32(tcp+8,ack);
    tcp[12]=0x50;tcp[13]=flags;
    if(n)memcpy(tcp+20,body,n);
    put16(tcp+16,tcp_checksum(t.remote_ip,local_ip,tcp,20+n));
    lengths[tail++]=14+20+20+n;
}
int main(void) {
    assert(tls_transport_start("example.com",443));
    assert(t.state==T_ARP_DNS);
    push_arp(0x0a000203);
    assert(tls_transport_poll()==0 && t.state==T_DNS);
    unsigned first_sends=sends;
    ticks+=1001;
    assert(tls_transport_poll()==0 && sends==first_sends+1);
    push_dns();
    assert(tls_transport_poll()==0 && t.remote_ip==0x6814179a);
    assert(t.state==T_ARP_TCP);
    push_arp(0x0a000202);
    assert(tls_transport_poll()==0 && t.state==T_SYN);
    push_tcp(TCP_SYN|TCP_ACK,100,t.tcp.seq,NULL,0);
    assert(tls_transport_poll()==1 && t.state==T_CONNECTED);
    char httpbody[32];
    assert(!http_async_start("example.com",80,"/",httpbody,sizeof httpbody));
    uint8_t message[1200]; memset(message,0x16,sizeof message);
    assert(tls_transport_send(message,sizeof message)==1200);
    assert(tls_transport_send(message,sizeof message)==0);
    unsigned sent=sends;
    ticks+=T_TX_RETRY_MS+1;
    assert(tls_transport_poll()==1 && sends==sent+1);
    push_tcp(TCP_ACK,101,t.tcp.seq,NULL,0);
    assert(tls_transport_poll()==1 && t.tx_used==0);
    const uint8_t record[]={22,3,3,0,4,1,2,3,4};
    push_tcp(TCP_ACK|TCP_PSH,101,t.tcp.seq,record,sizeof record);
    assert(tls_transport_poll()==1 && t.rx_used==sizeof record);
    uint8_t readback[16];
    assert(tls_transport_read(readback,5)==5);
    assert(!memcmp(readback,record,5));
    assert(tls_transport_read(readback,16)==4);
    assert(!memcmp(readback,record+5,4));
    assert(t.rx_used==0);
    /* FIN with the last TLS record must not discard that record. */
    const uint8_t last_record[]={21,3,3,0,2,1,0};
    push_tcp(TCP_FIN|TCP_ACK,101+sizeof record,t.tcp.seq,last_record,sizeof last_record);
    assert(tls_transport_poll()==1 && t.state==T_EOF);
    assert(tls_transport_read(readback,sizeof readback)==sizeof last_record);
    assert(!memcmp(readback,last_record,sizeof last_record));
    assert(tls_transport_poll()==2);
    tls_transport_close();
    assert(!tls_transport_active());
    puts("PASS: TLS probe ARP, DNS retry, TCP handshake, record transport, retransmit and RX buffering");
    return 0;
}
