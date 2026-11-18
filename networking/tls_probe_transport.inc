/* Phase 10.4: TLS probe's single-flight, nonblocking DNS/ARP/TCP transport.
 * Included from http.c to reuse the proven Ethernet/TCP wire helpers.
 * This is deliberately NOT a production TCP stack: one in-order receive
 * segment, one outstanding transmit segment, bounded retransmissions.
 */
#include "tls_probe_transport.h"
#define T_IDLE 0
#define T_ARP_DNS 1
#define T_DNS 2
#define T_ARP_TCP 3
#define T_SYN 4
#define T_CONNECTED 5
#define T_FAILED 6
#define T_EOF 7 /* TCP FIN received; buffered TLS records remain readable */
#define T_DNS_SERVER 0x0a000203u
#define T_DNS_TIMEOUT 4500u
#define T_ARP_TIMEOUT 3000u
#define T_TCP_TIMEOUT 10000u
#define T_TX_RETRY_MS 1300u
#define T_TCP_MSS 1200u

static struct {
    int state;
    const char *error;
    char hostname[128];
    uint16_t port, dns_id;
    uint8_t dns_mac[6], remote_mac[6];
    uint32_t dns_hop, remote_ip, next_hop;
    uint64_t deadline, retry_at;
    unsigned dns_retries, syn_retries, tx_retries;
    tcp_connection tcp;
    uint8_t rx[T_TCP_MSS + 300];
    unsigned rx_used, rx_pos;
    uint8_t tx[T_TCP_MSS];
    unsigned tx_used;
    uint32_t tx_seq;
    uint64_t tx_retry_at;
} t;

static int t_fail(const char *message)
{
    t.error = message;
    t.state = T_FAILED;
    return -1;
}

int tls_transport_active(void)
{
    return t.state != T_IDLE && t.state != T_FAILED;
}

const char *tls_transport_error(void)
{
    return t.error ? t.error : "";
}

void tls_transport_close(void)
{
    /* Best-effort abort: do not leave an unfinished diagnostic TCP session. */
    if (t.state == T_CONNECTED)
        (void)tcp_send(&t.tcp, TCP_RST | TCP_ACK, NULL, 0);
    /* Clear pending bytes, never retain pointers to BearSSL's buffers. */
    t.rx_used = t.rx_pos = t.tx_used = 0;
    t.state = T_IDLE;
}

static int t_dns_send(void)
{
    uint8_t f[512] = {0};
    uint8_t own[6];
    net_local_mac(own);
    for (unsigned i = 0; i < 6; i++) { f[i] = t.dns_mac[i]; f[i + 6] = own[i]; }
    put16(f + 12, 0x0800);
    uint8_t *ip = f + 14, *udp = ip + 20, *d = udp + 8;
    unsigned n = 12;
    const char *p = t.hostname;
    while (*p) {
        unsigned start = n++, label = 0;
        while (*p && *p != '.') {
            if (label >= 63 || n >= sizeof f - 60) return 0;
            d[n++] = (uint8_t)*p++;
            label++;
        }
        if (!label) return 0;
        d[start] = (uint8_t)label;
        if (*p == '.') p++;
    }
    d[n++] = 0;
    put16(d + n, 1); n += 2;
    put16(d + n, 1); n += 2;
    put16(d, t.dns_id); put16(d + 2, 0x0100); put16(d + 4, 1);
    put16(udp, 53065); put16(udp + 2, 53); put16(udp + 4, (uint16_t)(8 + n));
    ip[0] = 0x45; put16(ip + 2, (uint16_t)(20 + 8 + n));
    ip[8] = 64; ip[9] = 17;
    put32(ip + 12, net_local_ip()); put32(ip + 16, T_DNS_SERVER);
    put16(ip + 10, finish_sum(sum_bytes(0, ip, 20)));
    unsigned total = 14 + 20 + 8 + n;
    if (total < 60) total = 60;
    return eth_send(f, total) == 1;
}

static int t_dns_parse(const uint8_t *f, int len, uint32_t *address)
{
    if (len < 14 + 20 + 8 + 12 || get16(f + 12) != 0x0800) return 0;
    const uint8_t *ip = f + 14;
    unsigned ihl = (ip[0] & 15) * 4;
    if ((ip[0] >> 4) != 4 || ihl < 20 || ip[9] != 17 ||
        get32(ip + 12) != T_DNS_SERVER || get32(ip + 16) != net_local_ip() ||
        (unsigned)len < 14 + ihl + 8 + 12) return 0;
    unsigned iplen = get16(ip + 2);
    if (iplen < ihl + 20 || iplen > (unsigned)len - 14) return 0;
    const uint8_t *udp = ip + ihl;
    if (get16(udp) != 53 || get16(udp + 2) != 53065) return 0;
    unsigned udplen = get16(udp + 4);
    if (udplen < 20 || udplen > iplen - ihl) return 0;
    const uint8_t *d = udp + 8;
    unsigned sz = udplen - 8;
    if (get16(d) != t.dns_id || !(get16(d + 2) & 0x8000) ||
        (get16(d + 2) & 0x000f) || get16(d + 4) != 1) return 0;
    unsigned pos = 12;
    while (pos < sz && d[pos]) {
        unsigned label = d[pos++];
        if (label > 63 || pos + label >= sz) return 0;
        pos += label;
    }
    if (pos + 5 > sz) return 0;
    pos += 5;
    unsigned answers = get16(d + 6);
    for (unsigned i = 0; i < answers && i < 32; i++) {
        unsigned jumps = 0;
        while (pos < sz && d[pos] && !(d[pos] & 0xc0)) {
            unsigned label = d[pos++];
            if (label > 63 || pos + label > sz || ++jumps > 32) return 0;
            pos += label;
        }
        if (pos >= sz) return 0;
        if ((d[pos] & 0xc0) == 0xc0) pos += 2;
        else pos++;
        if (pos + 10 > sz) return 0;
        unsigned type = get16(d + pos), cls = get16(d + pos + 2);
        unsigned rdlen = get16(d + pos + 8);
        pos += 10;
        if (pos + rdlen > sz) return 0;
        if (type == 1 && cls == 1 && rdlen == 4) {
            *address = get32(d + pos);
            return *address != 0;
        }
        pos += rdlen;
    }
    return 0;
}

static int t_send_syn(void)
{
    t.tcp.remote_ip = t.remote_ip;
    t.tcp.remote_port = t.port;
    t.tcp.local_port = http_alloc_port();
    t.tcp.seq = (uint32_t)net_millis() ^ 0x847a21f0u;
    for (unsigned i = 0; i < 6; i++) t.tcp.remote_mac[i] = t.remote_mac[i];
    if (tcp_send(&t.tcp, TCP_SYN, NULL, 0) != 1) return t_fail("TCP SYN failed");
    t.deadline = net_millis() + T_TCP_TIMEOUT;
    t.retry_at = net_millis() + 1500u;
    t.syn_retries = 0;
    t.state = T_SYN;
    console_print("TLS transport: TCP SYN sent\n");
    return 0;
}

static int t_begin_tcp_arp(void)
{
    t.next_hop = net_next_hop(t.remote_ip);
    if (!t.next_hop) return t_fail("No TCP route");
    if (net_arp_cached(t.next_hop, t.remote_mac)) return t_send_syn();
    if (a_arp_send(t.next_hop) != 1) return t_fail("TCP ARP send failed");
    t.state = T_ARP_TCP;
    t.deadline = net_millis() + T_ARP_TIMEOUT;
    return 0;
}

int tls_transport_start(const char *host, uint16_t port)
{
    if (!host || !*host || !port || tls_transport_active() ||
        (a.state != A_IDLE && a.state != A_DONE && a.state != A_ERROR)) return 0;
    unsigned n = 0;
    while (host[n] && n < sizeof t.hostname) n++;
    if (n == 0 || n >= sizeof t.hostname) return 0;
    for (unsigned i = 0; i <= n; i++) t.hostname[i] = host[i];
    t.port = port;
    t.error = "";
    t.rx_used = t.rx_pos = t.tx_used = 0;
    t.dns_retries = t.syn_retries = t.tx_retries = 0;
    if (net_parse_ipv4(host, &t.remote_ip)) return t_begin_tcp_arp() >= 0;
    t.dns_hop = net_next_hop(T_DNS_SERVER);
    if (!t.dns_hop) return t_fail("No DNS route"), 0;
    t.dns_id = (uint16_t)(net_millis() ^ 0x5213u);
    if (net_arp_cached(t.dns_hop, t.dns_mac)) {
        if (!t_dns_send()) return t_fail("DNS send failed"), 0;
        t.state = T_DNS;
        t.deadline = net_millis() + T_DNS_TIMEOUT;
        t.retry_at = net_millis() + 1000u;
    } else {
        if (a_arp_send(t.dns_hop) != 1) return t_fail("DNS ARP failed"), 0;
        t.state = T_ARP_DNS;
        t.deadline = net_millis() + T_ARP_TIMEOUT;
    }
    return 1;
}

/* TCP payload is copied before ACK, so BearSSL can consume it later. */
size_t tls_transport_read(uint8_t *out, size_t cap)
{
    if ((t.state != T_CONNECTED && t.state != T_EOF) || !out || !cap || t.rx_pos == t.rx_used) return 0;
    size_t n = t.rx_used - t.rx_pos;
    if (n > cap) n = cap;
    for (size_t i = 0; i < n; i++) out[i] = t.rx[t.rx_pos + i];
    t.rx_pos += (unsigned)n;
    if (t.rx_pos == t.rx_used) t.rx_pos = t.rx_used = 0;
    return n;
}

int tls_transport_send(const uint8_t *data, size_t len)
{
    if (t.state != T_CONNECTED) return -1;
    if (t.tx_used) return 0;
    if (!data || !len) return 0;
    if (len > T_TCP_MSS) len = T_TCP_MSS;
    for (size_t i = 0; i < len; i++) t.tx[i] = data[i];
    t.tx_seq = t.tcp.seq;
    if (tcp_send(&t.tcp, TCP_ACK | TCP_PSH, t.tx, (unsigned)len) != 1)
        return t_fail("TLS TCP send failed");
    t.tx_used = (unsigned)len;
    t.tx_retries = 0;
    t.tx_retry_at = net_millis() + T_TX_RETRY_MS;
    return (int)len;
}

int tls_transport_poll(void)
{
    if (t.state == T_IDLE || t.state == T_FAILED) return -1;
    /* Do not drop the final TLS record when TCP FIN follows it. */
    if (t.state == T_EOF) return t.rx_used ? 1 : 2;
    uint64_t now = net_millis();
    if (t.state != T_CONNECTED && now >= t.deadline) {
        if (t.state == T_DNS) return t_fail("DNS timeout");
        if (t.state == T_ARP_DNS || t.state == T_ARP_TCP) return t_fail("ARP timeout");
        return t_fail("TCP SYN timeout");
    }
    if (t.state == T_DNS && now >= t.retry_at && t.dns_retries < 3) {
        if (!t_dns_send()) return t_fail("DNS retry failed");
        t.dns_retries++;
        t.retry_at = now + 1000u;
        console_print("TLS transport: retrying DNS\n");
    }
    if (t.state == T_SYN && now >= t.retry_at && t.syn_retries < 3) {
        uint32_t seq = t.tcp.seq;
        t.tcp.seq = seq - 1;
        int ok = tcp_send(&t.tcp, TCP_SYN, NULL, 0);
        t.tcp.seq = seq;
        if (ok != 1) return t_fail("TCP SYN retry failed");
        t.syn_retries++;
        t.retry_at = now + 1500u;
    }
    if (t.state == T_CONNECTED && t.tx_used && now >= t.tx_retry_at) {
        if (t.tx_retries++ >= 4) return t_fail("TCP data unacknowledged");
        uint32_t seq = t.tcp.seq;
        t.tcp.seq = t.tx_seq;
        int ok = tcp_send(&t.tcp, TCP_ACK | TCP_PSH, t.tx, t.tx_used);
        t.tcp.seq = seq;
        if (ok != 1) return t_fail("TCP retransmit failed");
        t.tx_retry_at = now + T_TX_RETRY_MS;
    }
    /* Do not dequeue another packet until BearSSL has drained this one. */
    if (t.state == T_CONNECTED && t.rx_used) return 1;
    int len = eth_recv(rxbuf, sizeof rxbuf);
    if (len < 0) return t_fail("NIC receive failed");
    if (!len) return t.state == T_CONNECTED ? 1 : 0;
    if (len >= 14 && get16(rxbuf + 12) == 0x0806) {
        if (t.state == T_ARP_DNS && a_arp_reply(rxbuf, len, t.dns_hop, t.dns_mac)) {
            if (!t_dns_send()) return t_fail("DNS send failed");
            t.state = T_DNS;
            t.deadline = net_millis() + T_DNS_TIMEOUT;
            t.retry_at = net_millis() + 1000u;
        } else if (t.state == T_ARP_TCP &&
                   a_arp_reply(rxbuf, len, t.next_hop, t.remote_mac)) {
            return t_send_syn();
        } else net_handle_arp_frame(rxbuf, (unsigned)len);
        return 0;
    }
    if (t.state == T_DNS) {
        uint32_t address;
        if (t_dns_parse(rxbuf, len, &address)) {
            t.remote_ip = address;
            console_print("TLS transport: DNS resolved\n");
            return t_begin_tcp_arp();
        }
        return 0;
    }
    if (t.state != T_SYN && t.state != T_CONNECTED) return 0;
    uint8_t flags;
    const uint8_t *payload;
    unsigned payload_len;
    uint32_t remote_seq, remote_ack;
    int r = tcp_parse_frame(&t.tcp, rxbuf, len, &flags, &payload,
                            &payload_len, &remote_seq, &remote_ack);
    if (r <= 0) return t.state == T_CONNECTED ? 1 : 0;
    if (flags & TCP_RST) return t_fail("TCP reset");
    if (t.state == T_SYN) {
        if ((flags & (TCP_SYN | TCP_ACK)) != (TCP_SYN | TCP_ACK) ||
            remote_ack != t.tcp.seq) return 0;
        t.tcp.ack = remote_seq + 1;
        if (tcp_send(&t.tcp, TCP_ACK, NULL, 0) != 1) return t_fail("TCP ACK failed");
        t.state = T_CONNECTED;
        return 1;
    }
    if (t.tx_used && remote_ack == t.tcp.seq) t.tx_used = 0;
    if (payload_len && remote_seq == t.tcp.ack) {
        if (payload_len > sizeof t.rx) return t_fail("Oversized TCP segment");
        for (unsigned i = 0; i < payload_len; i++) t.rx[i] = payload[i];
        t.rx_used = payload_len;
        t.rx_pos = 0;
        t.tcp.ack += payload_len;
        if (tcp_send(&t.tcp, TCP_ACK, NULL, 0) != 1) return t_fail("TCP data ACK failed");
    } else if (remote_seq < t.tcp.ack) {
        if (tcp_send(&t.tcp, TCP_ACK, NULL, 0) != 1) return t_fail("TCP duplicate ACK failed");
    }
    if (flags & TCP_FIN) {
        if (remote_seq + payload_len == t.tcp.ack) {
            t.tcp.ack++;
            (void)tcp_send(&t.tcp, TCP_ACK, NULL, 0);
            t.state = T_EOF;
            return 1; /* Let BearSSL process close_notify before reporting EOF. */
        }
        (void)tcp_send(&t.tcp, TCP_ACK, NULL, 0);
    }
    return 1;
}
