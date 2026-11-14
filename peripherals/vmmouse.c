#include "vmmouse.h"
#include <stdint.h>

enum {
    VM_GET_VERSION = 10,
    VM_MOUSE_DATA = 39,
    VM_MOUSE_STATUS = 40,
    VM_MOUSE_COMMAND = 41
};

#define VM_MAGIC       UINT32_C(0x564D5868)
#define VM_MOUSE_ID    UINT32_C(0x3442554A)
#define VM_MOUSE_ENABLE   UINT32_C(0x45414552)
#define VM_MOUSE_DISABLE  UINT32_C(0x000000F5)
#define VM_MOUSE_ABSOLUTE UINT32_C(0x53424152)
#define VM_RELATIVE_SAMPLE UINT32_C(0x00010000)
#define VM_LEFT_BUTTON     UINT32_C(0x00000020)
#define VM_QUEUE_MAX_WORDS 1024u

struct vm_reply {
    uint32_t ax, bx, cx, dx;
};

#ifdef VMMOUSE_TEST
extern int vm_test_virtualized(void);
extern struct vm_reply vm_test_call(uint32_t command, uint32_t argument);
#define virtualized vm_test_virtualized
#define vm_call vm_test_call
#else
static int virtualized(void) {
    uint32_t a = 1, b, c = 0, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    return (c >> 31) != 0;
}

static struct vm_reply vm_call(uint32_t command, uint32_t argument) {
    struct vm_reply r = { VM_MAGIC, argument, command, 0x5658u };
    __asm__ volatile ("inl %%dx, %%eax"
                      : "+a"(r.ax), "+b"(r.bx), "+c"(r.cx), "+d"(r.dx)
                      :
                      : "rsi", "rdi", "memory");
    return r;
}
#endif

static void disable(void) {
    (void)vm_call(VM_MOUSE_COMMAND, VM_MOUSE_DISABLE);
}

int vmmouse_init(void) {
    /* Don't probe the vendor-specific I/O port on a bare PC. */
    if (!virtualized())
        return 0;

    struct vm_reply version = vm_call(VM_GET_VERSION, ~VM_MAGIC);
    if (version.bx != VM_MAGIC || version.ax == UINT32_MAX)
        return 0;

    (void)vm_call(VM_MOUSE_COMMAND, VM_MOUSE_ENABLE);
    uint32_t status = vm_call(VM_MOUSE_STATUS, 0).ax;

    if (status >> 16 || !(status & 0xFFFFu) ||
        vm_call(VM_MOUSE_DATA, 1).ax != VM_MOUSE_ID) {
        disable();
        return 0;
    }

    (void)vm_call(VM_MOUSE_COMMAND, VM_MOUSE_ABSOLUTE);
    return 1;
}

int vmmouse_poll(void (*on_event)(unsigned x, unsigned y, int left_down)) {
    uint32_t status = vm_call(VM_MOUSE_STATUS, 0).ax;
    unsigned words = status & 0xFFFFu;

    if ((status >> 16) || words % 4u || words > VM_QUEUE_MAX_WORDS) {
        disable();
        return -1;
    }

    int delivered = 0;
    /* Work from one queue snapshot, so input can't starve the keyboard. */
    for (; words != 0; words -= 4) {
        struct vm_reply r = vm_call(VM_MOUSE_DATA, 4);
        /* An event may have been queued just before switching modes. */
        if (r.ax & VM_RELATIVE_SAMPLE)
            continue;
        if (r.bx > 0xFFFFu || r.cx > 0xFFFFu)
            continue;
        on_event(r.bx, r.cx, (r.ax & VM_LEFT_BUTTON) != 0);
        ++delivered;
    }
    return delivered;
}

unsigned vmmouse_cell(unsigned value, unsigned cells) {
    if (value > 0xFFFFu)
        value = 0xFFFFu;
    return (unsigned)(((uint64_t)value * cells) >> 16);
}

