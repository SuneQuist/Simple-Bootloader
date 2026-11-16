/*
 * Tiny64 local-network ARP and IPv4 ping (RFC 826, 791, 792).
 *
 * Public API (declare these in your net.h):
 *   void net_configure(const uint8_t mac[6], uint32_t ip);
 *   int net_parse_ipv4(const char *text, uint32_t *ip);
 *   int net_arp(uint32_t target, uint8_t mac_out[6]);
 *   int net_ping(uint32_t target);
 *
 * IPv4 addresses are numbers with the first octet in the high byte.
 * Example: 192.168.50.2 is 0xc0a83202. net_parse_ipv4 does this for you.
 * net_arp/net_ping return 1 for a reply, 0 for no reply, -1 for an error.
 * This is single-threaded, same-link only, and uses no malloc or libc.
 */
#include <stdint.h>

/* Provide these two hooks in your NIC driver. All frames start at the
 * Ethernet destination MAC. No VLAN tag, preamble or FCS is supplied here.
 * eth_send must copy the bytes or finish with them before returning.
 * Return 1 for send success and a negative value for failure.
 * eth_recv must not block. Return byte count, 0 for no frame, or <0 for error.
 * Do not write more than cap bytes. Strip the FCS if the hardware exposes it.
 */
extern int eth_send(const uint8_t *frame, unsigned len);
extern int eth_recv(uint8_t *frame, unsigned cap);

#ifndef NET_POLL_LIMIT
#define NET_POLL_LIMIT 250000u
#endif

enum {
    ETH_HDR = 14, ETH_MIN = 60, MAC_LEN = 6,
    ETH_ARP = 0x0806, ETH_IP = 0x0800,
    ARP_LEN = 28, ARP_REQUEST = 1, ARP_REPLY = 2,
    IP_HDR = 20, ICMP_HDR = 8, ICMP_DATA = 4,
    ECHO_ID = 0x5401
};

static uint8_t own_mac[MAC_LEN];
static uint32_t own_ip;
static uint16_t sequence;
static uint8_t rx[1536];
static const uint8_t broadcast[MAC_LEN] = {255, 255, 255, 255, 255, 255};
static const uint8_t zero_mac[MAC_LEN];
static const uint8_t echo_data[ICMP_DATA] = {'T', '6', '4', '!'};

static void copy(uint8_t *to, const uint8_t *from, unsigned len) {
    for (unsigned i = 0; i < len; ++i)
        to[i] = from[i];
}

static int same(const uint8_t *a, const uint8_t *b, unsigned len) {
    for (unsigned i = 0; i < len; ++i)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}

static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)get16(p) << 16) | get16(p + 2);
}

static void put16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)(n >> 8);
    p[1] = (uint8_t)n;
}

static void put32(uint8_t *p, uint32_t n) {
    put16(p, (uint16_t)(n >> 16));
    put16(p + 2, (uint16_t)n);
}

/* RFC Internet checksum. For a valid received packet this returns zero. */
static uint16_t checksum(const uint8_t *p, unsigned len) {
    uint32_t sum = 0;

    while (len >= 2) {
        sum += get16(p);
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (unsigned)p[0] << 8;

    while (sum >> 16)
        sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)~sum;
}

void net_configure(const uint8_t mac[6], uint32_t ip) {
    copy(own_mac, mac, MAC_LEN);
    own_ip = ip;
}

int net_parse_ipv4(const char *text, uint32_t *ip) {
    uint32_t address = 0;

    for (unsigned i = 0; i < 4; ++i) {
        unsigned octet = 0, digits = 0;
        while (*text >= '0' && *text <= '9') {
            if (++digits > 3)
                return 0;
            octet = octet * 10 + (unsigned)(*text++ - '0');
        }
        if (!digits || octet > 255)
            return 0;
        address = (address << 8) | octet;
        if (i != 3 && *text++ != '.')
            return 0;
    }
    if (*text != '\0')
        return 0;
    *ip = address;
    return 1;
}

static int valid_mac(const uint8_t *mac) {
    return !(mac[0] & 1u) && !same(mac, zero_mac, MAC_LEN);
}

static int can_probe(uint32_t target) {
    return own_ip && valid_mac(own_mac) && target && target != own_ip
        && target < 0xe0000000u;
}

static void ethernet(uint8_t *f, const uint8_t *dst, uint16_t type) {
    copy(f, dst, MAC_LEN);
    copy(f + MAC_LEN, own_mac, MAC_LEN);
    put16(f + 12, type);
}

static int arp_send(uint16_t op, const uint8_t *dst, uint32_t target) {
    uint8_t f[ETH_MIN] = {0};
    uint8_t *a = f + ETH_HDR;

    ethernet(f, dst, ETH_ARP);
    put16(a, 1);                    /* Ethernet hardware type. */
    put16(a + 2, ETH_IP);
    a[4] = MAC_LEN;
    a[5] = 4;                      /* IPv4 address length. */
    put16(a + 6, op);
    copy(a + 8, own_mac, MAC_LEN);
    put32(a + 14, own_ip);
    if (op == ARP_REPLY)
        copy(a + 18, dst, MAC_LEN);
    put32(a + 24, target);
    return eth_send(f, sizeof f);
}

static int is_arp(unsigned len) {
    const uint8_t *a = rx + ETH_HDR;
    return len >= ETH_HDR + ARP_LEN && get16(rx + 12) == ETH_ARP
        && get16(a) == 1 && get16(a + 2) == ETH_IP
        && a[4] == MAC_LEN && a[5] == 4
        && valid_mac(a + 8) && same(a + 8, rx + 6, MAC_LEN);
}

/* The peer might need to resolve OUR MAC before it can return a ping. */
static void arp_answer(void) {
    const uint8_t *a = rx + ETH_HDR;
    if (get16(a + 6) == ARP_REQUEST && get32(a + 24) == own_ip
        && (same(rx, broadcast, MAC_LEN) || same(rx, own_mac, MAC_LEN)))
        (void)arp_send(ARP_REPLY, a + 8, get32(a + 14));
}

int net_arp(uint32_t target, uint8_t mac_out[6]) {
    if (!can_probe(target) || arp_send(ARP_REQUEST, broadcast, target) != 1)
        return -1;

    for (unsigned i = 0; i < NET_POLL_LIMIT; ++i) {
        int len = eth_recv(rx, sizeof rx);
        if (len < 0 || (unsigned)len > sizeof rx)
            return -1;
        if (!is_arp((unsigned)len))
            continue;

        const uint8_t *a = rx + ETH_HDR;
        if (get16(a + 6) == ARP_REPLY && get32(a + 14) == target
            && get32(a + 24) == own_ip && same(rx, own_mac, MAC_LEN)
            && same(a + 18, own_mac, MAC_LEN)) {
            copy(mac_out, a + 8, MAC_LEN);
            return 1;
        }
        arp_answer();
    }
    return 0;
}

static int is_echo_reply(unsigned len, uint32_t target, uint16_t seq) {
    if (len < ETH_HDR + IP_HDR || get16(rx + 12) != ETH_IP
        || !same(rx, own_mac, MAC_LEN))
        return 0;

    const uint8_t *ip = rx + ETH_HDR;
    unsigned hdr_len = (ip[0] & 15u) * 4;
    unsigned total = get16(ip + 2);
    if (ip[0] >> 4 != 4 || hdr_len < IP_HDR || total > len - ETH_HDR
        || total < hdr_len + ICMP_HDR + ICMP_DATA || ip[9] != 1
        || (get16(ip + 6) & 0x3fffu)    /* More fragments or offset. */
        || get32(ip + 12) != target || get32(ip + 16) != own_ip
        || checksum(ip, hdr_len) != 0)
        return 0;

    const uint8_t *icmp = ip + hdr_len;
    return icmp[0] == 0 && icmp[1] == 0 && get16(icmp + 4) == ECHO_ID
        && get16(icmp + 6) == seq && same(icmp + ICMP_HDR, echo_data, ICMP_DATA)
        && checksum(icmp, total - hdr_len) == 0;
}

int net_ping(uint32_t target) {
    uint8_t peer_mac[MAC_LEN];
    int result = net_arp(target, peer_mac);
    if (result != 1)
        return result;

    uint8_t f[ETH_MIN] = {0};
    uint8_t *ip = f + ETH_HDR;
    uint8_t *icmp = ip + IP_HDR;
    uint16_t seq = ++sequence;

    ethernet(f, peer_mac, ETH_IP);
    ip[0] = 0x45;                  /* IPv4, five 32-bit header words. */
    put16(ip + 2, IP_HDR + ICMP_HDR + ICMP_DATA);
    put16(ip + 4, seq);
    ip[8] = 64;                    /* TTL */
    ip[9] = 1;                     /* ICMP */
    put32(ip + 12, own_ip);
    put32(ip + 16, target);
    put16(ip + 10, checksum(ip, IP_HDR));

    icmp[0] = 8;                   /* Echo Request */
    put16(icmp + 4, ECHO_ID);
    put16(icmp + 6, seq);
    copy(icmp + ICMP_HDR, echo_data, ICMP_DATA);
    put16(icmp + 2, checksum(icmp, ICMP_HDR + ICMP_DATA));

    if (eth_send(f, sizeof f) != 1)
        return -1;

    for (unsigned i = 0; i < NET_POLL_LIMIT; ++i) {
        int len = eth_recv(rx, sizeof rx);
        if (len < 0 || (unsigned)len > sizeof rx)
            return -1;
        if (is_echo_reply((unsigned)len, target, seq))
            return 1;
        if (is_arp((unsigned)len))
            arp_answer();
    }
    return 0;
}
