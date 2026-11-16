#include <stdint.h>
#include "basic.h"

#include "../filesystem/fs.h"
#include "../console/console.h"
#include "../networking/netcard.h"
#include "../networking/net.h"

static void error(const char *msg) {
    console_print(msg);
    console_putc('\n');
    return;
}

void help() {
    console_print("help  ls  cat FILE  echo WORDS  sh FILE  clear\n");
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
    for (unsigned bus = 0; bus < 256; ++bus) {
        for (unsigned slot = 0; slot < 32; ++slot) {
            uint32_t id = pci_read(bus, slot, 0, 0x00);
            if ((id & 0xffffu) == 0xffffu)
                continue;

            /* Header type bit 7 says whether to check functions 1–7. */
            uint32_t header = pci_read(bus, slot, 0, 0x0c);
            unsigned functions = (header & (1u << 23)) ? 8 : 1;

            for (unsigned fn = 0; fn < functions; ++fn) {
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

static void setup_network(void) {
    uint8_t mac[6];
    uint32_t local_ip;

    if (!net_parse_ipv4("172.24.48.50", &local_ip)) {
        console_print("Invalid local IP\n");
        return;
    }

    if (!tulip_read_mac(0, 0x0a, 0, mac)) {
        console_print("Tulip MAC read failed\n");
        return;
    }

    if (tulip_eth_init(0, 0x0a, 0, mac) != 1) {
        console_print("Tulip init failed\n");
        return;
    }

    net_configure(mac, local_ip);
    console_print("Tulip ready\n");
}

void ping(void) {
    const char *target = "172.24.48.1";
    uint32_t target_ip;

    if (!net_parse_ipv4(target, &target_ip)) {
        console_print("Invalid IP address\n");
        return;
    }

    setup_network();

    uint8_t mac_out[8];
    int result = net_ping(target_ip);

    if (result == 1) {
        console_print("Reply from ");
        console_print(target);
        console_putc('\n');
    } else if (result == 0) {
        console_print("No reply (ARP or ping timed out)\n");
    } else {
        console_print("Ping failed (configuration or driver error)\n");
    }
}

void clear() {
    console_clear();
}