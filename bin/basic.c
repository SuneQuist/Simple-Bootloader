#include <stdint.h>
#include "basic.h"

#include "../filesystem/fs.h"
#include "../console/console.h"
#include "../networking/netcard.h"
#include "../networking/timer.h"
#include "../networking/e1000.h"
#include "../networking/net.h"
#include "../networking/net_route.h"
#include "../networking/http.h"

static void error(const char *msg) {
    console_print(msg);
    console_putc('\n');
    return;
}

void help() {
    console_print("help ls cat echo sh clear ip ping http htmltest\n");
    console_print("ip [IPv4]  - show or set shared guest IP\n");
    console_print("ping <target> or ping <local_ip> <target>\n");
    console_print("http <server_ip> <port> <host> [path]\n");
    console_print("http <local_ip> <server_ip> <port> <host> [path]\n");
    console_print("tlsclock [accept | set YYYY-MM-DD HH:MM:SS]\n");
    console_print("tlstest <hostname> [port]  - verified TLS 1.2 diagnostic\n");
}

void ls(unsigned argc, const char *argv[]) {
    if (argc != 2)
            return error("usage: ls DIRECTORY");

    for (unsigned slot = 0;; ++slot) {
        unsigned dir_ino = fs_absolute(argv[1]);
        unsigned ino;
        const char *name = fs_entry(dir_ino, slot, &ino);
        if (!name)
            break;
        /* Inode numbers fit in two digits in this filesystem. */
         /* ino < 10 then its number is 0-9, else we divide by 1, 
         * so e.g. 16/10 = 1;
         * Modulo gives us the remainder, so 16/10 = 1, and 16 % 10 = 6,
         * combined it gives 16;
         * The first leading space ' ' mean is we get 2, then it won't write 02, 
         * but just 2 instead. The '0' on the second entry turns a number into
         * into its display charcter, meaning it goes from '0' + 6 to '6';
        */
        console_putc(ino < 10 ? ' ' : '0' + ino / 10);
        console_putc('0' + ino % 10);
        console_putc(' ');
        console_print(name);
        console_putc('\n');
    }
}

void cat(unsigned argc, const char *argv[]) {
    if (argc != 2)
        return error("usage: cat FILE");

    unsigned len;
    const char *text = fs_read(argv[1], &len);
    if (!text)
        return error("cat: file not found");
    console_write(text, len);
}

void echo(unsigned argc, const char *argv[]) {
    for (unsigned i = 1; i < argc; ++i) {
        if (i > 1)
            console_putc(' ');
        console_print(argv[i]);
    }
    console_putc('\n');
}

void cd(unsigned argc, const char *argv[]) {
    if (argc != 2)
        return error("usage: cd DIR");
    fs_goto(argv[1]);
}

void netinfo() {
    uint8_t mac[6];
    uint32_t local_ip;

    if (!tulip_read_mac(0, 0x0a, 0, mac)) {
        console_print("Tulip MAC read failed\n");
    } else if (tulip_eth_init(0, 0x0a, 0, mac) != 1) {
        console_print("Tulip init failed\n");
    } else {
        if (net_parse_ipv4("172.28.64.50", &local_ip))
            net_configure(mac, local_ip);
        console_print("Tulip ready\n");
    }

    if (tulip_read_mac(0, 0x0a, 0, mac)) {
        console_print("Tulip MAC ");
        for (unsigned i = 0; i < 6; ++i) {
            if (i != 0)
                console_putc(':');
            print_hex(mac[i], 2);
        }
        console_putc('\n');
        return;
    }

    for (unsigned slot = 0; slot < 32; ++slot) {
        for (unsigned fn = 0; fn < 8; ++fn) {
            /* Intel vendor 8086, QEMU E1000 device 100e. */
            if (pci_read(0, slot, fn, 0) != 0x100E8086u)
                continue;

            uint32_t bar0 = pci_read(0, slot, fn, 0x10);
            uint32_t base = bar0 & ~0xFu;

            /* For this demo, the BIOS must already enable PCI MMIO. */
            if (!(pci_read(0, slot, fn, 4) & 2u)) {
                console_print("netinfo: PCI memory access is disabled\n");
                return;
            }

            if ((bar0 & 7u) || !map_e1000_bar(base)) {
                console_print("netinfo: unsupported E1000 BAR0\n");
                return;
            }

            /* RAL0 contains four MAC bytes, RAH0 contains the last two. */
            uint32_t lo = *(volatile uint32_t *)(uintptr_t)(base + 0x5400u);
            uint32_t hi = *(volatile uint32_t *)(uintptr_t)(base + 0x5404u);

            if (hi == UINT32_MAX || !(hi & 0x80000000u)) {
                console_print("netinfo: MAC register is not valid\n");
                return;
            }

            console_print("E1000 MAC ");
            for (unsigned i = 0; i < 6; ++i) {
                if (i)
                    console_putc(':');
                print_hex_byte(i < 4 ? lo >> (8 * i) : hi >> (8 * (i - 4)));
            }
            console_putc('\n');
            return;
        }
    }

    console_print("netinfo: E1000 not found on PCI bus 0\n");
    return;
}

void netcards(void) {
    for (unsigned bus = 0; bus < 256; ++bus) { // 8 bits total so 256 possible combinations 16-23
        for (unsigned slot = 0; slot < 32; ++slot) { // 4 bts total so 32 possible combinations 11-15
            uint32_t id = pci_read(bus, slot, 0, 0x00); 
            if ((id & 0xffffu) == 0xffffu)
                continue;

            /* Header type bit 7 says whether to check functions 1–7. */
            uint32_t header = pci_read(bus, slot, 0, 0x0c);
            unsigned functions = (header & (1u << 23)) ? 8 : 1;

            for (unsigned fn = 0; fn < functions; ++fn)  {
                if (fn != 0)
                    id = pci_read(bus, slot, fn, 0x00);

                unsigned vendor = id & 0xffffu;
                unsigned device = id >> 16;

                if (vendor == 0xffffu)
                    continue;

                print_hex(bus, 2);
                console_putc(':');
                print_hex(slot, 2);
                console_putc('.');
                print_hex(fn, 1);
                console_print("  ");
                print_hex(vendor, 4);
                console_putc(':');
                print_hex(device, 4);
                console_putc('\n');
            }
        }
    }
}

/* Shared guest address for the shell and browser. */
static char shared_ip[16] = "10.0.2.15";

const char *network_shared_ip(void) { return shared_ip; }

int network_ensure_ready(void) { return setup_network(shared_ip); }

void ip_command(const char *argv[]) {
    if (argv[1] && argv[2]) { console_print("Usage: ip [IPv4]\n"); return; }
    if (argv[1]) {
        uint32_t parsed;
        if (!net_parse_ipv4(argv[1], &parsed)) { console_print("Invalid IPv4 address\n"); return; }
        unsigned i = 0;
        while (argv[1][i] && i < sizeof shared_ip - 1) { shared_ip[i] = argv[1][i]; ++i; }
        shared_ip[i] = 0;
        if (!setup_network(shared_ip)) return;
    }
    console_print("Guest IP: "); console_print(shared_ip); console_print("\n");
    console_print("Subnet: 255.255.255.0  Gateway: 10.0.2.2\n");
}

int setup_network(const char *set_ip) {
    if (!timer_init()) {
        console_print("Network timer initialization failed\n");
        return 0;
    }

    static uint8_t mac[6];
    static int nic_ready = 0;
    static uint32_t configured_ip = 0;

    uint32_t local_ip;
    uint32_t mask;
    uint32_t gateway;

    if (!set_ip || !net_parse_ipv4(set_ip, &local_ip)) {
        console_print("Invalid local IP\n");
        return 0;
    }

    /* VirtualBox NAT configuration */
    if (!net_parse_ipv4("255.255.255.0", &mask) ||
        !net_parse_ipv4("10.0.2.2", &gateway)) {
        console_print("Invalid network configuration\n");
        return 0;
    }

    /* Initialize the NIC only once. */
    if (!nic_ready) {
        if (pci_read(0, 0x03, 0, 0x00) == 0x100e8086u) {
            volatile void *bar0 = e1000_map_bar0(0, 0x03, 0);

            if (!bar0 || !net_use_e1000(0, 0x03, 0, bar0, mac)) {
                console_print("E1000 init failed\n");
                return 0;
            }

        } else if (pci_read(0, 0x0a, 0, 0x00) == 0x00091011u) {
            if (!net_use_tulip(0, 0x0a, 0, mac)) {
                console_print("Tulip init failed\n");
                return 0;
            }

        } else {
            console_print("No supported NIC found\n");
            return 0;
        }

        nic_ready = 1;
    }

    /*
     * Only reconfigure when the IP changes.
     * This preserves the ARP cache between ping commands.
     */
    if (configured_ip != local_ip) {
        net_configure(mac, local_ip);
        net_set_route(local_ip, mask, gateway);

        configured_ip = local_ip;
    }

    return 1;
}


#ifndef PING_ATTEMPTS
#define PING_ATTEMPTS 3
#endif

void ping(const char *argv[])
{
    if (!argv[1]) {
        console_print("Usage: ping [local_ip] <target_ip>\n");
        return;
    }

    const char *target = argv[2] ? argv[2] : argv[1];
    uint32_t target_ip;

    if (!net_parse_ipv4(target, &target_ip)) {
        console_print("Invalid target IP\n");
        return;
    }

    if (!setup_network(argv[2] ? argv[1] : network_shared_ip())) {
        console_print("Network initialization failed\n");
        return;
    }

    console_print("Pinging ");
    console_print(target);
    console_print("...\n");

    for (unsigned attempt = 0; attempt < PING_ATTEMPTS; ++attempt) {
        int result = net_ping(target_ip);

        if (result == 1) {
            console_print("Reply from ");
            console_print(target);
            console_putc('\n');
            return;
        }

        if (result < 0) {
            console_print("Ping failed (network or driver error)\n");
            return;
        }

        if (attempt + 1 < PING_ATTEMPTS)
            console_print("Request timed out, retrying...\n");
    }

    console_print("No reply after ");
    console_print("3");
    console_print(" attempts\n");
}


void http(const char *argv[])
{
    if (!argv[1] || !argv[2] || !argv[3]) {
        console_print("Usage: http [local_ip] <server_ip> <port> <host> [path]\n");
        return;
    }

    /* Four or five arguments mean the original explicit-local-IP syntax. */
    uint32_t possible_ip;
    int old_form = net_parse_ipv4(argv[2], &possible_ip);
    const char *local = old_form ? argv[1] : network_shared_ip();
    const char *server = old_form ? argv[2] : argv[1];
    const char *port_text = old_form ? argv[3] : argv[2];
    uint32_t server_ip;

    if (!net_parse_ipv4(server, &server_ip)) {
        console_print("Invalid server IP\n");
        return;
    }

    unsigned port = 0;

    for (const char *p = port_text; *p; ++p) {
        if (*p < '0' || *p > '9' || port > (65535u - (unsigned)(*p - '0')) / 10u) {
            console_print("Invalid port\n");
            return;
        }

        port = port * 10u + (unsigned)(*p - '0');
    }

    if (port == 0) {
        console_print("Invalid port\n");
        return;
    }

    const char *host = old_form ? argv[4] : argv[3];
    const char *path = old_form ? (argv[5] ? argv[5] : "/") : (argv[4] ? argv[4] : "/");

    if (path[0] != '/') {
        console_print("Path must start with /\n");
        return;
    }

    if (!setup_network(local))
        return;

    if (old_form) {
        unsigned i = 0;
        while (local[i] && i < sizeof shared_ip - 1) { shared_ip[i] = local[i]; ++i; }
        shared_ip[i] = 0;
    }
    http_get(server_ip, (uint16_t)port, host, path);
}

void clear(void) {
    console_clear();
}