#include <stdint.h>
#include "net.h"
#include "net_route.h"
#include "timer.h"
#include "e1000.h"

/*
 * Tiny64 minimal HTTP/1.0 client.
 *
 * Required networking functions:
 *
 * net_resolve(ip, mac)
 * eth_send(frame, len)
 * eth_recv(frame, cap)
 * net_millis()
 * net_parse_ipv4(text, &ip)
 *
 * Also requires access to Tiny64's configured
 * own_ip and own_mac.
 */

#define ETH_HDR 14
#define IP_HDR  20
#define TCP_HDR 20

#define ETH_IP  0x0800

#define HTTP_RX_SIZE 1600
#define HTTP_TIMEOUT 10000

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

extern void console_print(const char *);
extern void console_putc(char);

static uint8_t rxbuf[HTTP_RX_SIZE];

static void put16(uint8_t *p, uint16_t v) {
    p[0] = v >> 8;
    p[1] = v;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static uint16_t get16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           p[3];
}

static uint32_t sum_bytes(uint32_t sum,
                          const uint8_t *p,
                          unsigned len) {
    while (len >= 2) {
        sum += get16(p);
        p += 2;
        len -= 2;
    }

    if (len)
        sum += (uint16_t)p[0] << 8;

    return sum;
}

static uint16_t finish_sum(uint32_t sum) {
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return (uint16_t)~sum;
}

static uint16_t tcp_checksum(uint32_t src,
                             uint32_t dst,
                             const uint8_t *tcp,
                             unsigned len) {
    uint32_t sum = 0;

    sum += (src >> 16) & 0xffff;
    sum += src & 0xffff;

    sum += (dst >> 16) & 0xffff;
    sum += dst & 0xffff;

    sum += 6;
    sum += len;

    sum = sum_bytes(sum, tcp, len);

    return finish_sum(sum);
}

static unsigned text_length(const char *s) {
    unsigned n = 0;
    while (s[n])
        ++n;
    return n;
}

static unsigned append(uint8_t *dst,
                       unsigned pos,
                       unsigned cap,
                       const char *s) {
    while (*s && pos < cap)
        dst[pos++] = (uint8_t)*s++;

    return pos;
}

typedef struct {
    uint32_t remote_ip;
    uint8_t remote_mac[6];

    uint16_t local_port;
    uint16_t remote_port;

    uint32_t seq;
    uint32_t ack;
} tcp_connection;

static int tcp_send(tcp_connection *c,
                    uint8_t flags,
                    const uint8_t *data,
                    unsigned data_len) {
    uint8_t frame[1514] = {0};
    uint32_t own_ip = net_local_ip();
    uint8_t own_mac[6];

    net_local_mac(own_mac);

    if (data_len > 1460)
        return -1;

    uint8_t *ip = frame + ETH_HDR;
    uint8_t *tcp = ip + IP_HDR;

    for (unsigned i = 0; i < 6; ++i) {
        frame[i] = c->remote_mac[i];
        frame[i + 6] = own_mac[i];
    }

    put16(frame + 12, ETH_IP);

    unsigned tcp_len = TCP_HDR + data_len;
    unsigned ip_len = IP_HDR + tcp_len;

    ip[0] = 0x45;
    put16(ip + 2, (uint16_t)ip_len);
    ip[8] = 64;
    ip[9] = 6;

    put32(ip + 12, own_ip);
    put32(ip + 16, c->remote_ip);

    put16(ip + 10,
          finish_sum(sum_bytes(0, ip, IP_HDR)));

    put16(tcp + 0, c->local_port);
    put16(tcp + 2, c->remote_port);

    put32(tcp + 4, c->seq);
    put32(tcp + 8, c->ack);

    tcp[12] = 0x50;
    tcp[13] = flags;

    put16(tcp + 14, 4096);

    for (unsigned i = 0; i < data_len; ++i)
        tcp[TCP_HDR + i] = data[i];

    put16(tcp + 16,
          tcp_checksum(own_ip,
                       c->remote_ip,
                       tcp,
                       tcp_len));

    unsigned frame_len = ETH_HDR + ip_len;

    if (frame_len < 60)
        frame_len = 60;

    if (eth_send(frame, frame_len) != 1)
        return -1;

    c->seq += data_len;

    if (flags & TCP_SYN)
        ++c->seq;

    if (flags & TCP_FIN)
        ++c->seq;

    return 1;
}

/*
 * Returns:
 *  1 = valid TCP packet for this connection
 *  0 = no matching packet
 * -1 = receive error
 */
static int tcp_receive(tcp_connection *c,
                       uint8_t *flags,
                       const uint8_t **payload,
                       unsigned *payload_len,
                       uint32_t *remote_seq,
                       uint32_t *remote_ack) {
    int len = eth_recv(rxbuf, sizeof rxbuf);

    if (len < 0)
        return -1;

    if (len >= 14 && get16(rxbuf + 12) == 0x0806) {
        net_handle_arp_frame(rxbuf, (unsigned)len);
        return 0;
    }

    if (len < ETH_HDR + IP_HDR + TCP_HDR)
        return 0;

    uint32_t own_ip = net_local_ip();
    uint8_t own_mac[6];

    net_local_mac(own_mac);

    const uint8_t *ip = rxbuf + ETH_HDR;

    if (get16(rxbuf + 12) != ETH_IP)
        return 0;

    if ((ip[0] >> 4) != 4 || (ip[0] & 15) != 5)
        return 0;

    if (ip[9] != 6)
        return 0;

    if (get32(ip + 12) != c->remote_ip ||
        get32(ip + 16) != own_ip)
        return 0;

    unsigned ip_len = get16(ip + 2);

    if (ip_len < IP_HDR + TCP_HDR ||
        ip_len > (unsigned)len - ETH_HDR)
        return 0;

    const uint8_t *tcp = ip + IP_HDR;

    if (get16(tcp + 0) != c->remote_port ||
        get16(tcp + 2) != c->local_port)
        return 0;

    unsigned header_len = (tcp[12] >> 4) * 4;

    if (header_len < TCP_HDR ||
        header_len > ip_len - IP_HDR)
        return 0;

    unsigned segment_len = ip_len - IP_HDR;

    if (tcp_checksum(c->remote_ip,
                     own_ip,
                     tcp,
                     segment_len) != 0)
        return 0;

    *flags = tcp[13];
    *remote_seq = get32(tcp + 4);
    *remote_ack = get32(tcp + 8);

    *payload = tcp + header_len;
    *payload_len = segment_len - header_len;

    return 1;
}

static int tcp_wait(tcp_connection *c,
                    uint8_t *flags,
                    const uint8_t **payload,
                    unsigned *payload_len,
                    uint32_t *remote_seq,
                    uint32_t *remote_ack) {
    uint64_t start = net_millis();

    while (net_millis() - start < HTTP_TIMEOUT) {
        int r = tcp_receive(c, flags, payload,
                            payload_len, remote_seq,
                            remote_ack);

        if (r != 0)
            return r;

        __asm volatile ("pause");
    }

    return 0;
}

int http_get(uint32_t server_ip,
             uint16_t port,
             const char *path) {
    tcp_connection c = {0};

    c.remote_ip = server_ip;
    c.remote_port = port;
    c.local_port = 49152;

    c.seq = (uint32_t)net_millis() ^ 0x12345678u;

    if (net_resolve(server_ip, c.remote_mac) != 1) {
        console_print("HTTP: ARP failed\n");
        return 0;
    }

    console_print("HTTP: connecting...\n");

    if (tcp_send(&c, TCP_SYN, 0, 0) != 1)
        return 0;

    uint8_t flags;
    const uint8_t *payload;
    unsigned payload_len;
    uint32_t remote_seq;
    uint32_t remote_ack;

    int r = tcp_wait(&c, &flags, &payload,
                     &payload_len, &remote_seq,
                     &remote_ack);

    if (r != 1 ||
        (flags & (TCP_SYN | TCP_ACK)) !=
        (TCP_SYN | TCP_ACK) ||
        remote_ack != c.seq) {
        console_print("HTTP: TCP handshake failed\n");
        return 0;
    }

    c.ack = remote_seq + 1;

    if (tcp_send(&c, TCP_ACK, 0, 0) != 1)
        return 0;

    console_print("HTTP: connected\n");

    uint8_t request[512];
    unsigned n = 0;

    n = append(request, n, sizeof request, "GET ");
    n = append(request, n, sizeof request, path);
    n = append(request, n, sizeof request,
               " HTTP/1.0\r\nHost: ");
    /*
     * Host is set to a fixed value for this
     * minimal example. For name-based virtual
     * hosting, generate the actual hostname.
     */
    n = append(request, n, sizeof request,
               "localhost\r\nConnection: close\r\n\r\n");

    if (tcp_send(&c, TCP_ACK | TCP_PSH,
                 request, n) != 1)
        return 0;

    console_print("HTTP: response\n");

    uint64_t start = net_millis();

    while (net_millis() - start < 5000) {
        r = tcp_receive(&c, &flags, &payload,
                        &payload_len, &remote_seq,
                        &remote_ack);

        if (r < 0)
            return 0;

        if (r == 0) {
            __asm volatile ("pause");
            continue;
        }

        if (flags & TCP_RST)
            return 0;

        if (remote_seq == c.ack && payload_len) {
            for (unsigned i = 0; i < payload_len; ++i)
                console_putc((char)payload[i]);

            c.ack += payload_len;

            if (tcp_send(&c, TCP_ACK, 0, 0) != 1)
                return 0;

            start = net_millis();
        }

        if (flags & TCP_FIN) {
            if (remote_seq + payload_len == c.ack)
                ++c.ack;

            tcp_send(&c, TCP_ACK, 0, 0);
            tcp_send(&c, TCP_FIN | TCP_ACK, 0, 0);

            return 1;
        }
    }

    console_print("\nHTTP: response timed out\n");
    return 0;
}