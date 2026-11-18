#include <stdint.h>
#include "e1000.h"
#include "netcard.h"
#include "timer.h"
#include "../console/console.h"

#define PAGE_BYTES   4096u
#define IDENTITY_END 0x00400000u
#define E1000_WINDOW 0x02000000u
#define E1000_PAGES  6u                 /* BAR0 offsets 0x0000–0x5fff */
#define PAGE_ADDR    UINT64_C(0x000ffffffffff000)

enum {
    CTRL = 0x0000, STATUS = 0x0008, ICR = 0x00c0, IMC = 0x00d8,
    RCTL = 0x0100, TCTL = 0x0400, TIPG = 0x0410,
    RDBAL = 0x2800, RDBAH = 0x2804, RDLEN = 0x2808,
    RDH = 0x2810, RDT = 0x2818,
    TDBAL = 0x3800, TDBAH = 0x3804, TDLEN = 0x3808,
    TDH = 0x3810, TDT = 0x3818,
    RAL0 = 0x5400, RAH0 = 0x5404,

    N = 8, RX_BYTES = 2048, MAX_FRAME = 1514,
    DD = 1, RX_EOP = 2,
    TX_EOP = 1, TX_IFCS = 2, TX_RS = 8
};

struct tx_desc {
    uint64_t addr;
    uint16_t len;
    uint8_t cso, cmd;
    volatile uint8_t status;
    uint8_t css;
    uint16_t special;
};

struct rx_desc {
    uint64_t addr;
    uint16_t len, checksum;
    volatile uint8_t status;
    uint8_t errors;
    uint16_t special;
};

enum {
    P_PRESENT = 1u << 0,
    P_WRITE   = 1u << 1,
    P_PWT     = 1u << 3,
    P_PCD     = 1u << 4,
    P_LARGE   = 1u << 7
};

_Alignas(4096) static uint64_t e1000_pt[512];

static volatile uint64_t *low_page_table(uint64_t entry) {
    uintptr_t phys = (uintptr_t)(entry & PAGE_ADDR);

    if (phys == 0 || phys >= IDENTITY_END)
        return 0;

    return (volatile uint64_t *)phys;
}

_Static_assert(sizeof(struct tx_desc) == 16, "E1000 TX descriptor size");
_Static_assert(sizeof(struct rx_desc) == 16, "E1000 RX descriptor size");

_Alignas(128) static struct tx_desc tx[N];
_Alignas(128) static struct rx_desc rx[N];
_Alignas(16) static uint8_t tx_buf[MAX_FRAME];
_Alignas(16) static uint8_t rx_buf[N][RX_BYTES];

static volatile uint32_t *regs;
static unsigned tx_next, rx_next, drop_fragments;
static int tx_pending;

enum { NONE = -1, TULIP, E1000 };
static int active = NONE;

static inline void out32(uint16_t port, uint32_t value) {
    __asm volatile ("outl %0, %w1" : : "a"(value), "Nd"(port) : "memory");
}

static inline uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm volatile ("inl %w1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static inline void fence(void) {
    uint32_t scratch = 0;
    __asm volatile ("lock addl $0, %0" : "+m"(scratch) : : "memory", "cc");
}

static inline void wr(unsigned reg, uint32_t value) {
    regs[reg / 4] = value;
}

static inline uint32_t rd(unsigned reg) {
    return regs[reg / 4];
}

static int dma_fits(const void *p, unsigned size) {
    uintptr_t a = (uintptr_t)p;
    /* The boot page directory identity-maps RAM below 4 MiB. */
    return a != 0 && a < IDENTITY_END && size <= IDENTITY_END - a;
}

int net_use_tulip(unsigned bus, unsigned slot, unsigned fn, uint8_t mac[6]) {
    if (active != NONE ||
        !tulip_read_mac(bus, slot, fn, mac) ||
        tulip_eth_init(bus, slot, fn, mac) != 1)
        return 0;

    active = TULIP;
    return 1;
}

int net_use_e1000(unsigned bus, unsigned slot, unsigned fn,
                  volatile void *mapped_bar0, uint8_t mac[6]) {
    if (active != NONE || !mapped_bar0 ||
        bus > 255 || slot > 31 || fn > 7)
        return 0;

    uint32_t pci = 0x80000000u | (bus << 16) | (slot << 11) | (fn << 8);
    out32(0xcf8, pci);
    if (in32(0xcfc) != 0x100e8086u)
        return 0;

    if (!dma_fits(tx, sizeof tx) || !dma_fits(rx, sizeof rx) ||
        !dma_fits(tx_buf, sizeof tx_buf) || !dma_fits(rx_buf, sizeof rx_buf))
        return 0;

    /* Memory access + bus mastering. Write Command, not PCI Status. */
    out32(0xcf8, pci | 4u);
    uint16_t cmd = (uint16_t)(in32(0xcfc) | 6u);
    __asm volatile ("outw %0, %w1" : : "a"(cmd), "Nd"((uint16_t)0xcfc) : "memory");
    if ((in32(0xcfc) & 6u) != 6u)
        return 0;

    regs = mapped_bar0;
    uint32_t lo = rd(RAL0), hi = rd(RAH0);
    if (!(hi & (1u << 31)) || (lo & 1u) ||
        !(lo | (hi & 0xffffu)))
        return 0;

    for (unsigned i = 0; i < 4; ++i)
        mac[i] = (uint8_t)(lo >> (8 * i));
    mac[4] = (uint8_t)hi;
    mac[5] = (uint8_t)(hi >> 8);

    /* Polling only. Stop RX/TX before programming the rings. */
    wr(IMC, UINT32_MAX);
    (void)rd(ICR);
    wr(RCTL, 0);
    wr(TCTL, 0);
    (void)rd(STATUS);       /* Flush posted writes. */

    for (unsigned i = 0; i < N; ++i) {
        tx[i].addr = (uintptr_t)tx_buf;
        tx[i].len = tx[i].cso = tx[i].cmd = tx[i].css = tx[i].special = 0;
        tx[i].status = DD;

        rx[i].addr = (uintptr_t)rx_buf[i];
        rx[i].len = rx[i].checksum = rx[i].errors = rx[i].special = 0;
        rx[i].status = 0;
    }
    tx_next = rx_next = drop_fragments = 0;
    tx_pending = -1;
    fence();

    wr(TDBAL, (uint32_t)(uintptr_t)tx);
    wr(TDBAH, 0);
    wr(TDLEN, sizeof tx);
    wr(TDH, 0);
    wr(TDT, 0);

    wr(RDBAL, (uint32_t)(uintptr_t)rx);
    wr(RDBAH, 0);
    wr(RDLEN, sizeof rx);
    wr(RDH, 0);
    wr(RDT, N - 1);

    wr(TIPG, 8u | (8u << 10) | (6u << 20));
    wr(CTRL, (rd(CTRL) & ~(1u << 30)) | (1u << 6)); /* SLU, no VLAN stripping */
    wr(TCTL, (1u << 1) | (1u << 3) | (15u << 4) | (64u << 12));
    wr(RCTL, (1u << 1) | (1u << 15) | (1u << 26));
    /* RX enabled, accept broadcast, strip FCS, 2048-byte buffers. */

    (void)rd(STATUS);
    active = E1000;
    return 1;
}

static int e1000_send(const uint8_t *frame, unsigned len) {
    if (!frame || len < 14 || len > MAX_FRAME)
        return -1;

    /* A timed-out send may still own the shared transmit buffer. */
    if (tx_pending >= 0 && !(tx[tx_pending].status & DD))
        return -1;

    fence();
    for (unsigned i = 0; i < len; ++i)
        tx_buf[i] = frame[i];

    unsigned i = tx_next;
    tx[i].len = (uint16_t)len;
    tx[i].cmd = TX_EOP | TX_IFCS | TX_RS;
    tx[i].status = 0;
    tx_pending = (int)i;
    tx_next = (i + 1) % N;
    fence();
    wr(TDT, tx_next);

    for (unsigned poll = 0; poll < 1000000; ++poll) {
        uint8_t status = tx[i].status;
        if (status & DD) {
            tx_pending = -1;
            return (status & 0x0e) ? -1 : 1;
        }
        __asm volatile ("pause");
    }
    return -1;
}

static int e1000_recv(uint8_t *frame, unsigned cap) {
    if (!frame || !cap)
        return -1;

    for (unsigned count = 0; count < N; ++count) {
        unsigned i = rx_next;
        uint8_t status = rx[i].status;

        if (!(status & DD))
            return 0;

        static unsigned rx_debug_count = 0;

        if (rx_debug_count < 10) {
            rx_debug_count++;
        }

        fence();
        unsigned len = rx[i].len;
        int eop = (status & RX_EOP) != 0;
        int good = eop && !drop_fragments && !rx[i].errors &&
                   len >= 14 && len <= MAX_FRAME && len <= cap;
        drop_fragments = !eop;

        if (good)
            for (unsigned j = 0; j < len; ++j)
                frame[j] = rx_buf[i][j];

        rx[i].status = 0;
        rx_next = (i + 1) % N;
        fence();
        wr(RDT, i);

        if (good)
            return (int)len;
    }
    return 0;
}

int eth_send(const uint8_t *frame, unsigned len) {
    if (active == TULIP)
        return tulip_send(frame, len);
    if (active == E1000)
        return e1000_send(frame, len);
    return -1;
}

int eth_recv(uint8_t *frame, unsigned cap) {
    if (active == TULIP)
        return tulip_recv(frame, cap);
    if (active == E1000)
        return e1000_recv(frame, cap);
    return -1;
}

volatile void *e1000_map_bar0(unsigned bus, unsigned slot, unsigned fn) {
    if (pci_read(bus, slot, fn, 0x00) != 0x100e8086u)
        return 0;

    uint32_t bar = pci_read(bus, slot, fn, 0x10);
    unsigned type = (bar >> 1) & 3u;

    if ((bar & 1u) || (type != 0 && type != 2))
        return 0;                       /* Not a 32-bit or 64-bit MMIO BAR. */

    uint64_t phys = bar & UINT32_C(0xfffffff0);
    if (type == 2)
        phys |= (uint64_t)pci_read(bus, slot, fn, 0x14) << 32;

    if (phys < IDENTITY_END || (phys & (PAGE_BYTES - 1u)) ||
        phys > PAGE_ADDR - (E1000_PAGES - 1u) * PAGE_BYTES)
        return 0;

    uint64_t cr3;
    __asm volatile ("mov %%cr3, %0" : "=r"(cr3));

    /* The 32 MiB window is under PML4[0], PDPT[0], and PD[16]. */
    volatile uint64_t *pml4 = low_page_table(cr3);
    if (!pml4 || !(pml4[0] & P_PRESENT))
        return 0;

    volatile uint64_t *pdpt = low_page_table(pml4[0]);
    if (!pdpt || !(pdpt[0] & P_PRESENT) || (pdpt[0] & P_LARGE))
        return 0;

    volatile uint64_t *pd = low_page_table(pdpt[0]);
    uintptr_t pt_phys = (uintptr_t)e1000_pt;
    if (!pd || pt_phys == 0 || pt_phys >= IDENTITY_END)
        return 0;

    const unsigned pd_index = E1000_WINDOW >> 21;
    const uint64_t flags = P_PRESENT | P_WRITE | P_PWT | P_PCD;
    uint64_t pde = pd[pd_index];

    if (pde != 0) {
        /* Allow repeat calls for this BAR, but never replace another mapping. */
        if (!(pde & P_PRESENT) || (pde & P_LARGE) ||
            (pde & PAGE_ADDR) != pt_phys ||
            (e1000_pt[0] & PAGE_ADDR) != phys)
            return 0;

        return (volatile void *)(uintptr_t)E1000_WINDOW;
    }

    for (unsigned i = 0; i < 512; ++i)
        e1000_pt[i] = 0;

    for (unsigned i = 0; i < E1000_PAGES; ++i)
        e1000_pt[i] = (phys + i * PAGE_BYTES) | flags;

    /* Publish the page table only after its entries are ready. */
    __asm volatile ("" ::: "memory");
    pd[pd_index] = pt_phys | P_PRESENT | P_WRITE;

    for (unsigned i = 0; i < E1000_PAGES; ++i) {
        uintptr_t va = E1000_WINDOW + i * PAGE_BYTES;
        __asm volatile ("invlpg (%0)" : : "r"(va) : "memory");
    }

    return (volatile void *)(uintptr_t)E1000_WINDOW;
}