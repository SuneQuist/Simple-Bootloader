#include <stdint.h>
#include "../console/console.h"

#ifndef TULIP_IDENTITY_END
#define TULIP_IDENTITY_END 0x200000u
#endif
#ifndef TULIP_TX_POLLS
#define TULIP_TX_POLLS 1000000u
#endif
#ifndef TULIP_MEDIA_MODE
/* Fixed MII, full duplex for the virtual NIC. Override only if needed. */
#define TULIP_MEDIA_MODE 0x000c0200u
#endif

enum {
    CSR_BUS = 0x00, CSR_TX_POLL = 0x08, CSR_RX_POLL = 0x10,
    CSR_RX_LIST = 0x18, CSR_TX_LIST = 0x20, CSR_STATUS = 0x28,
    CSR_MODE = 0x30, CSR_INT = 0x38,
    BUS_RESET = 1u, BUS_CACHE_ALIGN_8 = 1u << 14,
    MODE_REQUIRED = 1u << 25, MODE_STORE_FORWARD = 1u << 21,
    MODE_TX = 1u << 13, MODE_RX = 1u << 1,
    STATUS_RX_UNAVAILABLE = 1u << 7, STATUS_FATAL = 1u << 13,
    PCI_IO = 1u, PCI_MASTER = 4u,
    RING_END = 1u << 25, TX_SETUP = 1u << 27, TX_FIRST_LAST = 3u << 29,
    RX_FIRST_LAST = 3u << 8, RX_ERROR = (3u << 14) | 1u,
    TX_ERROR = 1u << 15,
    RX_COUNT = 4, BUF_BYTES = 1536, SETUP_BYTES = 192, NO_PORT = 0xffff
};
#define OWN UINT32_C(0x80000000)

struct desc {
    volatile uint32_t status;
    uint32_t control;
    uint32_t buf1;
    uint32_t buf2;
};
_Static_assert(sizeof(struct desc) == 16, "Tulip DMA descriptor size");

_Alignas(32) static struct desc tx;
_Alignas(32) static struct desc rx[RX_COUNT];
_Alignas(32) static uint8_t tx_buf[BUF_BYTES];
_Alignas(32) static uint8_t rx_buf[RX_COUNT][BUF_BYTES];

static uint16_t active_base = NO_PORT;
static unsigned rx_next;

enum {
    ROM_MODE = 0x4800,
    ROM_CS   = 1u << 0,  /* Select the EEPROM. */
    ROM_CLK  = 1u << 1,  /* Clock signal. */
    ROM_DI   = 1u << 2,  /* Bit sent to the EEPROM. */
    ROM_DO   = 1u << 3   /* Bit read back from the EEPROM. */
};

static void out32(uint16_t port, uint32_t value) {
    __asm volatile ("outl %0, %w1" : : "a"(value), "Nd"(port));
}

static uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm volatile ("inl %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Read a 32-bit DWORD from PCI configuration space on bus 0. */
/* https://0x04.net/~mwk/doc/pci/PCI%20Local%20Bus%20Specification%20Revision%203.0.pdf?utm_source=chatgpt.com */
/* Section 3.2.2.3.2; */
uint32_t pci_read(unsigned bus, unsigned slot, unsigned fn, unsigned offset) {
    // equal to writing (1u << 31), each number after 0x equates to 0000, so 0x8u means 1000 cause its bit value is 8
    // out of 0000 8 4 2 1, 0xC would equate to 1100 (12);
    uint32_t addr = 0x80000000u // sets first bit to 1 (enabled)
                  // bus & 0xFFu (1111 1111) meaning only the lowest 8 bits are taken with it
                  // Bad example: 0x1CFu & 0xFF would mean you send 0x0CF, so it only sends the bottom 8 bits that
                  //    match the bad bus.
                  // Good example: 0xCFu && 0xFF would you mean send 0xCF, because & operator just checks if both are there.
                  //    meaning since we are comparing against all 1's (FF - 1111 1111) then if we simply have a 1 as well,
                  //    then we keep it. 
                  //    0xCFu = 1100 1111
                  //    0xFFu = 1111 1111 
                  //  & -----------------
                  //            1100 1111
                  // Bit number:   3  2  1  0
                  // Place value:  8  4  2  1
                  | ((bus & 0xFFu) << 16) // lower 8 bits - push to bit 16
                  | ((slot & 0x1Fu) << 11) // lower 5 bits - push to bit 11
                  | ((fn & 0x07u) << 8) // lower 3 bits - push to bit 8
                  | (offset & 0xFCu); // lower 2-7 bits (0xFCu = 1111 1100 or 7654 3210, 1111 = bits 4-7, 1100 = bits 2-3, so range is  bits 2-7);
    out32(0xCF8, addr); // I/O CONFIG_ADDRESS is 0xCF8
    return in32(0xCFC); // I/O CONFIG_DATA is 0xCFC
}



static void tulip_out32(uint16_t port, uint32_t value) {
    __asm volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t tulip_in32(uint16_t port) {
    uint32_t value;
    __asm volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static unsigned tulip_clock(uint16_t port, unsigned bit) {
    uint32_t pins = ROM_MODE | ROM_CS | ((bit & 1u) ? ROM_DI : 0);

    tulip_out32(port, pins);           /* Set the bit with clock low. */
    (void)tulip_in32(port);           /* Allow the lines to settle. */
    tulip_out32(port, pins | ROM_CLK); /* Raise the clock. */

    return (tulip_in32(port) & ROM_DO) != 0;
}

static uint32_t tulip_rom(uint16_t port, unsigned word, unsigned addr_bits) {
    uint32_t cmd = (6u << addr_bits) | word; /* READ (binary 110) + address */
    uint32_t reply = 0;

    tulip_out32(port, ROM_MODE);
    tulip_out32(port, ROM_MODE | ROM_CS);

    /* Send the command, including two leading zero bits. */
    for (int i = (int)addr_bits + 4; i >= 0; --i)
        reply = (reply << 1) | tulip_clock(port, cmd >> i);

    /* The EEPROM returns one 16-bit word, most significant bit first. */
    for (unsigned i = 0; i < 16; ++i)
        reply = (reply << 1) | tulip_clock(port, 0);

    tulip_out32(port, ROM_MODE | ROM_CS);
    tulip_out32(port, ROM_MODE);
    return reply;
}

int tulip_read_mac(unsigned bus, unsigned slot, unsigned fn, uint8_t mac[6]) {
    if (pci_read(bus, slot, fn, 0x00) != 0x00091011u)
        return 0;

    uint32_t bar = pci_read(bus, slot, fn, 0x10);
    uint32_t base = bar & ~3u;
    if (!(bar & 1u) || base == 0 || base > 0xffffu - 0x48u)
        return 0; /* No usable I/O BAR. */

    /* Enable PCI I/O decoding if the BIOS left it off.
       Write only the 16-bit Command register, not Status. */
    uint16_t command = (uint16_t)pci_read(bus, slot, fn, 0x04);
    if (!(command & 1u)) {
        uint32_t addr = 0x80000004u | (bus << 16) | (slot << 11) | (fn << 8);
        tulip_out32(0xcf8, addr);
        __asm volatile ("outw %0, %1" : : "a"((uint16_t)(command | 1u)), "Nd"((uint16_t)0xcfc));
    }

    uint16_t csr9 = (uint16_t)(base + 0x48u);
    unsigned bits = (tulip_rom(csr9, 0xff, 8) & 0x40000u) ? 8 : 6;

    /* Older ROMs repeat the first 8 bytes. The standard layout
       puts the MAC at byte 20, which is word 10. */
    unsigned first = 0;
    for (unsigned i = 0; i < 4; ++i) {
        uint16_t a = (uint16_t)tulip_rom(csr9, i, bits);
        uint16_t b = (uint16_t)tulip_rom(csr9, i + 8, bits);
        if (a != b) {
            first = 10;
            break;
        }
    }

    unsigned nonzero = 0;
    for (unsigned i = 0; i < 3; ++i) {
        uint16_t word = (uint16_t)tulip_rom(csr9, first + i, bits);
        mac[2 * i]     = (uint8_t)word;
        mac[2 * i + 1] = (uint8_t)(word >> 8);
        nonzero |= word;
    }

    return nonzero != 0 && !(mac[0] & 1u);
}

#ifndef TULIP_TEST_IO
static void out16(uint16_t port, uint16_t v) {
    __asm volatile ("outw %0, %w1" : : "a"(v), "Nd"(port) : "memory");
}
#else
/* Mock I/O for the host-side descriptor test. */
static uint32_t in32(uint16_t port);
static void out32(uint16_t port, uint32_t v);
static void out16(uint16_t port, uint16_t v);
#endif

static uint32_t csr_read(uint16_t base, unsigned reg) {
    return in32((uint16_t)(base + reg));
}

static void csr_write(uint16_t base, unsigned reg, uint32_t value) {
    out32((uint16_t)(base + reg), value);
}

/* Only the device's ownership/status word is shared asynchronously. Use
 * a DMA barrier before giving it a descriptor and after taking it back. */
static void dma_barrier(void) {
    /* A locked integer operation orders x86 memory accesses without SSE. */
    uint32_t scratch = 0;
    __asm volatile ("lock addl $0, %0" : "+m"(scratch) : : "memory", "cc");
}

static uint32_t dma_addr(const void *ptr, unsigned size) {
    uintptr_t a = (uintptr_t)ptr;
    if (a == 0 || a >= TULIP_IDENTITY_END ||
        size > TULIP_IDENTITY_END - a || a > UINT32_MAX ||
        size - 1u > UINT32_MAX - a)
        return 0;
    return (uint32_t)a;  /* Tiny64 uses the same physical and virtual address. */
}

static uint32_t tx_wait(uint16_t base) {
    for (unsigned i = 0; i < TULIP_TX_POLLS; ++i) {
        uint32_t status = tx.status;
        if (!(status & OWN)) {
            dma_barrier();
            return status;
        }
        if ((i & 1023u) == 0 && (csr_read(base, CSR_STATUS) & STATUS_FATAL))
            break;
        __asm volatile ("pause");
    }
    return OWN;  /* Still owned. Do not overwrite the DMA buffer. */
}

static void tx_start(uint16_t base, unsigned flags, unsigned len) {
    tx.control = RING_END | flags | len;
    dma_barrier();
    tx.status = OWN;
    csr_write(base, CSR_TX_POLL, 0);
}

int tulip_eth_init(unsigned bus, unsigned slot, unsigned fn,
                   const uint8_t mac[6]) {
    if (active_base != NO_PORT)
        return 1;  /* Already initialized. */

    if (bus > 255u || slot > 31u || fn > 7u || !mac || (mac[0] & 1u))
        return -1;
    unsigned nonzero = 0;
    for (unsigned i = 0; i < 6; ++i)
        nonzero |= mac[i];
    if (!nonzero)
        return -1;

    uint32_t pci = 0x80000000u | (bus << 16) | (slot << 11) | (fn << 8);
    out32(0xcf8, pci);
    if (in32(0xcfc) != 0x00091011u)
        return -1;

    out32(0xcf8, pci | 0x10u);
    uint32_t bar = in32(0xcfc);
    uint32_t base = bar & ~3u;
    if (!(bar & 1u) || base == 0 || base > 0xffffu - CSR_INT - 3u)
        return -2;
    uint16_t io = (uint16_t)base;

    uint32_t tx_phys = dma_addr(&tx, sizeof tx);
    uint32_t rx_phys = dma_addr(rx, sizeof rx);
    uint32_t tx_data = dma_addr(tx_buf, sizeof tx_buf);
    uint32_t rx_data = dma_addr(rx_buf, sizeof rx_buf);
    if (!tx_phys || !rx_phys || !tx_data || !rx_data)
        return -3;

    out32(0xcf8, pci | 0x04u);
    uint16_t command = (uint16_t)in32(0xcfc);
    command = (uint16_t)((command | PCI_IO) & ~PCI_MASTER);
    out16(0xcfc, command);
    if (((uint16_t)in32(0xcfc) & (PCI_IO | PCI_MASTER)) != PCI_IO)
        return -2;

    uint32_t mode = MODE_REQUIRED | MODE_STORE_FORWARD | TULIP_MEDIA_MODE;
    csr_write(io, CSR_INT, 0);
    csr_write(io, CSR_MODE, mode);
    csr_write(io, CSR_BUS, BUS_RESET);
    /* Keep the reset asserted for at least the specified 50 PCI clocks.
       PCI command reads provide bus turnaround without touching the NIC CSRs. */
    for (unsigned i = 0; i < 100; ++i)
        (void)in32(0xcfc);

    csr_write(io, CSR_BUS, BUS_CACHE_ALIGN_8);
    if ((csr_read(io, CSR_BUS) & BUS_RESET) != 0)
        return -4;
    csr_write(io, CSR_INT, 0);
    csr_write(io, CSR_MODE, mode);
    csr_write(io, CSR_STATUS, STATUS_FATAL | STATUS_RX_UNAVAILABLE);

    tx.status = 0;
    tx.control = RING_END;
    tx.buf1 = tx_data;
    tx.buf2 = 0;

    for (unsigned i = 0; i < RX_COUNT; ++i) {
        rx[i].control = BUF_BYTES | (i == RX_COUNT - 1 ? RING_END : 0);
        rx[i].buf1 = rx_data + i * BUF_BYTES;
        rx[i].buf2 = 0;
        dma_barrier();
        rx[i].status = OWN;
    }
    rx_next = 0;

    /* Perfect receive filter. Enter our MAC and the broadcast address.
       Each 16-bit piece occupies the low half of a dword. */
    for (unsigned i = 0; i < SETUP_BYTES; ++i)
        tx_buf[i] = 0xff;
    for (unsigned slot = 0; slot <= 15; slot += 15)
        for (unsigned i = 0; i < 3; ++i) {
            unsigned pos = 12 * slot + 4 * i;
            tx_buf[pos]     = tx_buf[pos + 2] = mac[2 * i];
            tx_buf[pos + 1] = tx_buf[pos + 3] = mac[2 * i + 1];
        }

    csr_write(io, CSR_RX_LIST, rx_phys);
    csr_write(io, CSR_TX_LIST, tx_phys);
    out16(0xcfc, (uint16_t)(command | PCI_MASTER));
    if (((uint16_t)in32(0xcfc) & (PCI_IO | PCI_MASTER)) != (PCI_IO | PCI_MASTER))
        return -2;

    /* TX must process the filter setup descriptor before RX starts.
       Setup completion sets status bits other than OWN, so don't test ES. */
    csr_write(io, CSR_MODE, mode | MODE_TX);
    tx_start(io, TX_SETUP, SETUP_BYTES);
    if ((tx_wait(io) & OWN) || (csr_read(io, CSR_STATUS) & STATUS_FATAL)) {
        csr_write(io, CSR_MODE, mode);
        out16(0xcfc, command); /* Disable bus mastering on failure. */
        return -5;
    }

    csr_write(io, CSR_MODE, mode | MODE_TX | MODE_RX);
    csr_write(io, CSR_RX_POLL, 0);
    active_base = io;
    return 1;
}

int tulip_send(const uint8_t *frame, unsigned len) {
    uint16_t io = active_base;
    if (io == NO_PORT || !frame || len < 14 || len > 1514 ||
        (tx_wait(io) & OWN) || (csr_read(io, CSR_STATUS) & STATUS_FATAL))
        return -1;

    for (unsigned i = 0; i < len; ++i)
        tx_buf[i] = frame[i];
    for (; len < 60; ++len)
        tx_buf[len] = 0;  /* Pad short Ethernet frames, let NIC append FCS. */

    tx_start(io, TX_FIRST_LAST, len);
    return (tx_wait(io) & (OWN | TX_ERROR)) ||
           (csr_read(io, CSR_STATUS) & STATUS_FATAL) ? -1 : 1;
}

int tulip_recv(uint8_t *frame, unsigned cap) {
    uint16_t io = active_base;
    if (io == NO_PORT || !frame || !cap)
        return -1;

    for (unsigned i = 0; i < RX_COUNT; ++i) {
        struct desc *d = &rx[rx_next];
        uint32_t status = d->status;
        if (status & OWN) {
            uint32_t csr = csr_read(io, CSR_STATUS);
            if (csr & STATUS_FATAL)
                return -1;
            if (csr & STATUS_RX_UNAVAILABLE) {
                csr_write(io, CSR_STATUS, STATUS_RX_UNAVAILABLE);
                csr_write(io, CSR_RX_POLL, 0);
            }
            return 0;
        }

        dma_barrier();
        unsigned n = (status >> 16) & 0x3fffu;
        unsigned len = 0;
        if ((status & (RX_FIRST_LAST | RX_ERROR)) == RX_FIRST_LAST &&
            n >= 14 + 4 && n <= 1518 && n - 4 <= cap) {
            len = n - 4; /* Hardware's frame length includes the 4-byte FCS. */
            for (unsigned j = 0; j < len; ++j)
                frame[j] = rx_buf[rx_next][j];
        }

        dma_barrier();
        d->status = OWN;
        rx_next = (rx_next + 1) % RX_COUNT;
        csr_write(io, CSR_RX_POLL, 0);
        if (len)
            return (int)len;
    }
    return 0;
}

void print_hex(unsigned value, unsigned digits) {
    static const char hex[] = "0123456789abcdef";

    while (digits != 0) {
        --digits;
        console_putc(hex[(value >> (digits * 4)) & 0xfu]);
    }
}

void print_hex_byte(unsigned n) {
    static const char hex[] = "0123456789abcdef";
    
    console_putc(hex[(n >> 4) & 15]);
    console_putc(hex[n & 15]);
}

/* Identity-map the 2 MiB region that contains the E1000 registers. */
int map_e1000_bar(uint32_t bar) {
    const uint64_t page_size = 1ull << 21;
    uint64_t base = bar & ~(page_size - 1);
    unsigned pdpt_i = bar >> 30;
    unsigned pd_i = (bar >> 21) & 511u;

    /* This model's BAR0 is 128 KiB. Don't map over low RAM. */
    if (bar < page_size || (bar & 0x1FFFFu))
        return 0;

    uint64_t *pdpt = (uint64_t *)(uintptr_t)0x2000;
    if (pdpt[pdpt_i] == 0) {
        if (pdpt_i == 0)
            return 0;

        /* PDPT slots 1, 2, and 3 get pages 0x4000, 0x5000, 0x6000. */
        uintptr_t addr = 0x3000u + pdpt_i * 0x1000u;
        uint64_t *new_pd = (uint64_t *)addr;
        for (unsigned i = 0; i < 512; ++i)
            new_pd[i] = 0;
        pdpt[pdpt_i] = addr | 3u;
    }

    if (!(pdpt[pdpt_i] & 1u) || (pdpt[pdpt_i] & 0x80u))
        return 0;

    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpt_i] & ~0xFFFull);

    /* Present, writable, large page, cache disabled with the default PAT. */
    uint64_t entry = base | 0x9Bull;

    /* Don't overwrite an unrelated mapping. Ignore accessed/dirty bits. */
    if (pd[pd_i] && (pd[pd_i] & ~0x60ull) != entry)
        return 0;

    pd[pd_i] = entry;
    __asm volatile ("invlpg (%0)" : : "r"((uintptr_t)bar) : "memory");
    return 1;
}