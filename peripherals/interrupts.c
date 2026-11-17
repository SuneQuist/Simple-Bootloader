#include <stdint.h>
#include "interrupts.h"
#include "mouse.h"
#include "keyboard.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define IDT_PRESENT  0x8E
#define MOUSE_VECTOR 44

#pragma pack(push, 1)

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
};

struct idt_pointer {
    uint16_t limit;
    uint64_t base;
};

#pragma pack(pop)

_Static_assert(sizeof(struct idt_entry) == 16,
               "IDT entry must be 16 bytes");

_Static_assert(sizeof(struct idt_pointer) == 10,
               "IDT pointer must be 10 bytes");

static struct idt_entry idt[256];

extern void irq1_stub(void);
extern void irq12_stub(void);

static inline void outb(uint16_t port, uint8_t value)
{
    __asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));

    return value;
}

static void io_wait(void)
{
    outb(0x80, 0);
}

static void idt_set_gate(
    uint8_t vector,
    void (*handler)(void)
) {
    uint64_t address = (uint64_t)handler;
    idt[vector].offset_low  = address & 0xFFFF;

    uint16_t cs;
    __asm volatile ("mov %%cs, %0" : "=r"(cs));

    idt[vector].selector = cs;
    idt[vector].ist         = 0;
    idt[vector].type_attr   = IDT_PRESENT;
    idt[vector].offset_mid  = (address >> 16) & 0xFFFF;
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved    = 0;
}

static void pic_remap(void)
{
    /* Mask all interrupts during PIC initialization. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    /* Start PIC initialization. */
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    /* Master: vectors 32-39; slave: vectors 40-47. */
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();

    /* Slave connected to master's IRQ2. */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* 8086-compatible PIC mode. */
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /* Master: enable IRQ1 (keyboard) and IRQ2 (slave cascade). */
    outb(PIC1_DATA, 0xF9);

    /* Slave: enable IRQ12 (mouse). */
    outb(PIC2_DATA, 0xEF);
}

void interrupts_init(void)
{
    __asm volatile ("cli" ::: "memory");

    /* Clear all IDT entries. */
    for (unsigned i = 0; i < 256; ++i) {
        idt[i] = (struct idt_entry){0};
    }

    idt_set_gate(33, irq1_stub);
    idt_set_gate(44, irq12_stub);

    struct idt_pointer pointer = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)idt
    };

    __asm volatile ("lidt %0" : : "m"(pointer) : "memory");

    pic_remap();
}

void interrupts_enable(void)
{
    __asm volatile ("sti" ::: "memory");
}

volatile uint32_t irq1_count = 0;

void irq1_handler(void)
{
    uint8_t status = inb(0x64);

    if (status & 0x01) {
        uint8_t data = inb(0x60);

        if (!(status & 0x20)) {
            irq1_count++;

            keyboard_irq_scancode(data);
        }
    }

    outb(PIC1_COMMAND, 0x20);
}

/*
 * Called from the assembly IRQ12 stub.
 * The PS/2 controller puts mouse bytes at port 0x60.
 */
volatile uint32_t irq12_count = 0;
volatile uint32_t mouse_byte_count = 0;

void irq12_handler(void)
{
    /* Counts every entry into the IRQ12 handler. */
    irq12_count++;

    uint8_t status = inb(0x64);

    if (status & 0x01) {
        uint8_t data = inb(0x60);

        if (status & 0x20) {
            mouse_byte_count++;
            handle_mouse(data);
        }
    }

    outb(PIC2_COMMAND, 0x20);
    outb(PIC1_COMMAND, 0x20);
}