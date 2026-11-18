/* Single-flight incremental ARP, DNS, TCP and HTTP client.
 * Exactly one RX frame and at most one TX frame per poll. No wait loops.
 * The existing synchronous shell HTTP client remains unchanged.
 */
#define ASYNC_TIMEOUT 10000u
#define DNS_TIMEOUT 3000u
#define ARP_TIMEOUT 2500u
#define DNS_SERVER 0x0a000203u /* VirtualBox NAT DNS proxy */
#define A_IDLE 0
#define A_ARP_DNS 1
#define A_DNS 2
#define A_ARP_HTTP 3
#define A_SYN 4
#define A_RESPONSE 5
#define A_DONE 6
#define A_ERROR 7

extern int eth_send(const uint8_t *, unsigned);
extern int eth_recv(uint8_t *, unsigned);

static struct {
    int state;
    uint32_t server_ip, next_hop, dns_hop;
    uint8_t mac[6], dns_mac[6];
    uint64_t deadline, retry_at;
    uint8_t dns_retries;
    uint16_t dns_id, port;
    char hostname[128], path[256];
    char *out;
    size_t cap, used, header_used;
    char headers[4096];
    int header_done, status, chunked;
    char location[256];
    tcp_connection tcp;
    const char *error;
} a;

static int a_fail(const char *message) {
    a.error = message;
    a.state = A_ERROR;
    return -1;
}
static void a_copy(char *dst, const char *src, unsigned cap) {
    unsigned i=0;
    while (src[i] && i+1<cap) { dst[i]=src[i]; i++; }
    dst[i]=0;
}
static int a_arp_send(uint32_t target) {
    uint8_t f[60]={0};
    uint8_t mac[6]; net_local_mac(mac);
    for (unsigned i=0;i<6;i++) { f[i]=255; f[i+6]=mac[i]; f[22+i]=mac[i]; }
    put16(f+12,0x0806); put16(f+14,1); put16(f+16,0x0800);
    f[18]=6; f[19]=4; put16(f+20,1);
    put32(f+28,net_local_ip()); put32(f+38,target);
    return eth_send(f,sizeof f);
}
static int a_arp_reply(const uint8_t *f, int len, uint32_t target, uint8_t mac[6]) {
    if (len<42 || get16(f+12)!=0x0806 || get16(f+20)!=2 ||
        get32(f+28)!=target || get32(f+38)!=net_local_ip()) return 0;
    uint8_t local[6]; net_local_mac(local);
    for (unsigned i=0;i<6;i++) if(f[32+i]!=local[i] || f[i]!=local[i]) return 0;
    for (unsigned i=0;i<6;i++) mac[i]=f[22+i];
    net_arp_remember(target,mac);
    return 1;
}
static int a_udp_dns_send(void) {
    uint8_t f[512]={0};
    uint8_t own[6]; net_local_mac(own);
    for(unsigned i=0;i<6;i++) { f[i]=a.dns_mac[i]; f[i+6]=own[i]; }
    put16(f+12,0x0800);
    uint8_t *ip=f+14, *udp=ip+20, *d=udp+8;
    unsigned n=12;
    const char *p=a.hostname;
    while (*p) {
        unsigned start=n++, label=0;
        while (*p && *p!='.') {
            if (label>=63 || n>=sizeof f-60) return 0;
            d[n++]=(uint8_t)*p++; label++;
        }
        if (!label) return 0;
        d[start]=(uint8_t)label;
        if (*p=='.') p++;
    }
    d[n++]=0; put16(d+n,1); n+=2; put16(d+n,1); n+=2;
    put16(d,a.dns_id); put16(d+2,0x0100); put16(d+4,1);
    put16(udp,53064); put16(udp+2,53); put16(udp+4,(uint16_t)(8+n));
    ip[0]=0x45; put16(ip+2,(uint16_t)(20+8+n)); ip[8]=64; ip[9]=17;
    put32(ip+12,net_local_ip()); put32(ip+16,DNS_SERVER);
    put16(ip+10,finish_sum(sum_bytes(0,ip,20)));
    unsigned total=14+20+8+n;
    if(total<60) total=60;
    return eth_send(f,total)==1;
}
static int a_dns_parse(const uint8_t *f, int len, uint32_t *address) {
    if (len<14+20+8+12 || get16(f+12)!=0x0800) return 0;
    const uint8_t *ip=f+14;
    unsigned ihl=(ip[0]&15)*4;
    if ((ip[0]>>4)!=4 || ihl<20 || ip[9]!=17 ||
        get32(ip+12)!=DNS_SERVER || get32(ip+16)!=net_local_ip() ||
        (unsigned)len<14+ihl+8+12) return 0;
    unsigned iplen=get16(ip+2);
    if (iplen<ihl+8+12 || iplen>(unsigned)len-14) return 0;
    const uint8_t *udp=ip+ihl;
    if(get16(udp)!=53 || get16(udp+2)!=53064) return 0;
    unsigned udplen=get16(udp+4);
    if(udplen<20 || udplen>iplen-ihl) return 0;
    const uint8_t *d=udp+8;
    unsigned sz=udplen-8;
    if(get16(d)!=a.dns_id || !(get16(d+2)&0x8000) ||
       (get16(d+2)&0x000f) || get16(d+4)!=1) return 0;
    unsigned pos=12;
    /* Skip the echoed question, disallow compression in question. */
    while(pos<sz && d[pos]) {
        unsigned label=d[pos++];
        if(label>63 || pos+label>=sz) return 0;
        pos+=label;
    }
    if(pos+5>sz) return 0;
    pos+=5;
    unsigned answers=get16(d+6);
    for(unsigned i=0;i<answers && i<32;i++) {
        /* Answer names may use compression pointers. */
        unsigned jumps=0;
        while(pos<sz && d[pos] && !(d[pos]&0xc0)) {
            unsigned label=d[pos++];
            if(label>63 || pos+label>sz) return 0;
            pos+=label;
            if(++jumps>32) return 0;
        }
        if(pos>=sz) return 0;
        if((d[pos]&0xc0)==0xc0) pos+=2;
        else pos++;
        if(pos+10>sz) return 0;
        unsigned type=get16(d+pos), cls=get16(d+pos+2);
        unsigned rdlen=get16(d+pos+8); pos+=10;
        if(pos+rdlen>sz) return 0;
        if(type==1 && cls==1 && rdlen==4) {
            *address=get32(d+pos); return *address!=0;
        }
        pos+=rdlen;
    }
    return 0;
}
static int a_send_syn(void) {
    a.tcp.remote_ip=a.server_ip;
    a.tcp.remote_port=a.port;
    a.tcp.local_port=http_alloc_port();
    a.tcp.seq=(uint32_t)net_millis()^0x12345678u;
    for(unsigned i=0;i<6;i++) a.tcp.remote_mac[i]=a.mac[i];
    if(tcp_send(&a.tcp,TCP_SYN,0,0)!=1) return a_fail("TCP SYN send failed");
    a.deadline=net_millis()+ASYNC_TIMEOUT;
    a.state=A_SYN;
    return 0;
}
static int a_begin_http_arp(void) {
    a.next_hop=net_next_hop(a.server_ip);
    if(!a.next_hop) return a_fail("No route to host");
    if(net_arp_cached(a.next_hop,a.mac)) return a_send_syn();
    if(a_arp_send(a.next_hop)!=1) return a_fail("ARP request failed");
    a.deadline=net_millis()+ARP_TIMEOUT;
    a.state=A_ARP_HTTP;
    return 0;
}
int http_async_start(const char *hostname,uint16_t port,const char *path,
                     char *out,size_t capacity) {
    if (tls_transport_active()) return 0; /* The NIC RX queue is single-consumer. */
    if(!hostname || !*hostname || !path || path[0]!='/' || !out || capacity<2 || !port)
        return 0;
    unsigned hn=0,pn=0;
    while(hostname[hn] && hn<sizeof a.hostname) hn++;
    while(path[pn] && pn<sizeof a.path) pn++;
    if(hn==0 || hn>=sizeof a.hostname || pn>=sizeof a.path ||
       hn+pn+100>=512) return 0;
    a.state=A_IDLE;
    a_copy(a.hostname,hostname,sizeof a.hostname);
    a_copy(a.path,path,sizeof a.path);
    a.port=port; a.out=out; a.cap=capacity; a.used=0;
    a.header_used=0; a.header_done=0; a.status=0; a.chunked=0; a.location[0]=0;
    a.error=""; out[0]=0;
    a.dns_retries = 0;
    a.retry_at = 0;
    if(net_parse_ipv4(hostname,&a.server_ip)) {
        return a_begin_http_arp()>=0;
    }
    a.dns_hop=net_next_hop(DNS_SERVER);
    if(!a.dns_hop) return a_fail("No DNS route"),0;
    a.dns_id=(uint16_t)(net_millis()^0x6419u);
    if (net_arp_cached(a.dns_hop, a.dns_mac)) {
        if (!a_udp_dns_send())
            return a_fail("DNS send failed"), 0;

        a.state = A_DNS;
        a.deadline = net_millis() + DNS_TIMEOUT;
        a.retry_at = net_millis() + 1000u;
    } else {
        if(a_arp_send(a.dns_hop)!=1) return a_fail("DNS ARP failed"),0;
        a.state=A_ARP_DNS; a.deadline=net_millis()+ARP_TIMEOUT;
    }
    return 1;
}
static int a_request_send(void) {
    uint8_t req[512]; unsigned n=0;
    n=append(req,n,sizeof req,"GET ");
    n=append(req,n,sizeof req,a.path);
    n=append(req,n,sizeof req," HTTP/1.0\r\nHost: ");
    n=append(req,n,sizeof req,a.hostname);
    n=append(req,n,sizeof req,"\r\nConnection: close\r\nAccept-Encoding: identity\r\n\r\n");
    if(n>=sizeof req || tcp_send(&a.tcp,TCP_ACK|TCP_PSH,req,n)!=1)
        return a_fail("HTTP request send failed");
    a.state=A_RESPONSE; a.deadline=net_millis()+ASYNC_TIMEOUT;
    return 0;
}
static int a_http_byte(char ch) {
    if(!a.header_done) {
        if(a.header_used+1>=sizeof a.headers) return a_fail("Headers too large");
        a.headers[a.header_used++]=ch;
        if(a.header_used>=4 &&
           a.headers[a.header_used-4]=='\r' && a.headers[a.header_used-3]=='\n' &&
           a.headers[a.header_used-2]=='\r' && a.headers[a.header_used-1]=='\n') {
            a.headers[a.header_used]=0;
            a.header_done=1;
            if(a.header_used<12 || a.headers[0]!='H' || a.headers[1]!='T' ||
               a.headers[2]!='T' || a.headers[3]!='P' || a.headers[8]!=' ')
                return a_fail("Malformed HTTP status line");
            if(a.headers[9]<'0' || a.headers[9]>'9' ||
               a.headers[10]<'0' || a.headers[10]>'9' ||
               a.headers[11]<'0' || a.headers[11]>'9')
                return a_fail("Malformed HTTP status code");
            a.status=(a.headers[9]-'0')*100+(a.headers[10]-'0')*10+(a.headers[11]-'0');
            /* Extract Location from a complete response header block. */
            for (unsigned j=0;j+9<a.header_used;j++) {
                if (j && a.headers[j-1]!='\n') continue;
                const char *key="location:";
                unsigned k=0;
                while(k<9) {
                    char c=a.headers[j+k];
                    if(c>='A' && c<='Z') c=(char)(c+32);
                    if(c!=key[k]) break;
                    k++;
                }
                if(k!=9) continue;
                unsigned at=j+9, n=0;
                while(at<a.header_used && (a.headers[at]==' ' || a.headers[at]=='\t')) at++;
                while(at<a.header_used && a.headers[at]!='\r' && a.headers[at]!='\n' && n+1<sizeof a.location)
                    a.location[n++]=a.headers[at++];
                a.location[n]=0;
                break;
            }
            for(unsigned j=0;j+7<a.header_used;j++) {
                const char *s="chunked";
                unsigned k=0;
                while(k<7 && (a.headers[j+k]==s[k] || a.headers[j+k]==s[k]-32)) k++;
                if(k==7) a.chunked=1;
            }
        }
    } else {
        if(a.used+1>=a.cap) return a_fail("Page too large");
        a.out[a.used++]=ch;
    }
    return 0;
}
int http_async_poll(void) {
    if(a.state==A_DONE) return 1;
    if(a.state==A_ERROR) return -1;
    if(a.state==A_IDLE) return 0;
    /* Retry unanswered DNS queries without blocking the GUI. */
    if (a.state == A_DNS &&
        a.dns_retries < 2 &&
        net_millis() >= a.retry_at &&
        net_millis() < a.deadline) {

        if (!a_udp_dns_send())
            return a_fail("DNS retry send failed");

        a.dns_retries++;
        a.retry_at = net_millis() + 1000u;

        console_print("Browser: retrying DNS\n");
    }
    if (net_millis() >= a.deadline) {
        if (a.state == A_ARP_DNS)
            return a_fail("ARP timeout (DNS gateway)");
        if (a.state == A_DNS)
            return a_fail("DNS timeout");
        if (a.state == A_ARP_HTTP)
            return a_fail("ARP timeout (HTTP gateway)");
        if (a.state == A_SYN)
            return a_fail("TCP connection timeout");
        if (a.state == A_RESPONSE)
            return a_fail("HTTP response timeout");

        return a_fail("Network timeout");
    }
    /* Consume at most one Ethernet frame per GUI iteration. */
    int len=eth_recv(rxbuf,sizeof rxbuf);
    if(len<0) return a_fail("NIC receive failed");
    if(!len) return 0;
    if(len>=14 && get16(rxbuf+12)==0x0806) {
        if (a.state == A_ARP_DNS &&
            a_arp_reply(rxbuf, len, a.dns_hop, a.dns_mac)) {

            if (!a_udp_dns_send())
                return a_fail("DNS send failed");

            a.state = A_DNS;
            a.deadline = net_millis() + DNS_TIMEOUT;
            a.retry_at = net_millis() + 1000u;
        } else if(a.state==A_ARP_HTTP && a_arp_reply(rxbuf,len,a.next_hop,a.mac)) {
            return a_send_syn();
        } else net_handle_arp_frame(rxbuf,(unsigned)len);
        return 0;
    }
    if(a.state==A_DNS) {
        uint32_t ip;
        if(a_dns_parse(rxbuf,len,&ip)) {
            a.server_ip=ip;
            return a_begin_http_arp();
        }
        return 0;
    }
    if(a.state!=A_SYN && a.state!=A_RESPONSE) return 0;
    /* tcp_receive() owns the RX buffer, so it must not fetch a second frame.
     * Parse the already-fetched frame through the shared packet validator. */
    uint8_t flags; const uint8_t *payload; unsigned payload_len;
    uint32_t remote_seq,remote_ack;
    int r=tcp_parse_frame(&a.tcp,rxbuf,len,&flags,&payload,&payload_len,&remote_seq,&remote_ack);
    if(r<=0) return 0;
    if(flags&TCP_RST) return a_fail("TCP reset");
    if(a.state==A_SYN) {
        if((flags&(TCP_SYN|TCP_ACK))!=(TCP_SYN|TCP_ACK) || remote_ack!=a.tcp.seq)
            return 0;
        a.tcp.ack=remote_seq+1;
        if(tcp_send(&a.tcp,TCP_ACK,0,0)!=1) return a_fail("TCP ACK failed");
        return a_request_send();
    }
    if(remote_seq==a.tcp.ack && payload_len) {
        for(unsigned i=0;i<payload_len;i++) if(a_http_byte((char)payload[i])<0) return -1;
        a.tcp.ack+=payload_len;
        if(tcp_send(&a.tcp,TCP_ACK,0,0)!=1) return a_fail("TCP ACK failed");
        a.deadline=net_millis()+ASYNC_TIMEOUT;
    } else if(remote_seq<a.tcp.ack) {
        if(tcp_send(&a.tcp,TCP_ACK,0,0)!=1) return a_fail("TCP duplicate ACK failed");
    }
    if(flags&TCP_FIN) {
        if(remote_seq+payload_len==a.tcp.ack) a.tcp.ack++;
        (void)tcp_send(&a.tcp,TCP_ACK,0,0);
        (void)tcp_send(&a.tcp,TCP_FIN|TCP_ACK,0,0);
        if(!a.header_done) return a_fail("Incomplete HTTP headers");
        if(a.chunked) return a_fail("Chunked response unsupported");
        if(a.status!=200 && a.status!=301 && a.status!=302 && a.status!=303 && a.status!=307 && a.status!=308) return a_fail("HTTP status unsupported");
        a.out[a.used]=0;
        a.state=A_DONE;
        return 1;
    }
    return 0;
}
void http_async_cancel(void) { a.state=A_IDLE; a.out=0; }
size_t http_async_length(void) { return a.used; }
const char *http_async_error(void) { return a.error ? a.error : ""; }

int http_async_status(void) { return a.status; }
const char *http_async_location(void) { return a.location; }
