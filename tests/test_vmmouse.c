#include <assert.h>
#include <stdio.h>
#define VMMOUSE_TEST
#include "../peripherals/vmmouse.c"

static int hypervisor = 1;
static struct exchange {
    uint32_t cmd, arg;
    struct vm_reply reply;
} exchanges[16];
static unsigned length, next;
static unsigned seen;
static unsigned last_x, last_y;
static int last_left;

static void setup(void) {
    length = next = seen = 0;
}

static void expect(uint32_t cmd, uint32_t arg,
                   uint32_t a, uint32_t b, uint32_t c) {
    assert(length < sizeof exchanges / sizeof exchanges[0]);
    exchanges[length++] = (struct exchange){cmd, arg, {a, b, c, 0}};
}

int vm_test_virtualized(void) { return hypervisor; }

struct vm_reply vm_test_call(uint32_t cmd, uint32_t arg) {
    assert(next < length);
    struct exchange e = exchanges[next++];
    assert(e.cmd == cmd && e.arg == arg);
    return e.reply;
}

static void sample(unsigned x, unsigned y, int left_down) {
    if (seen == 0)
        assert(x == 65535 && y == 0 && left_down);
    last_x = x;
    last_y = y;
    last_left = left_down;
    ++seen;
}

int main(void) {
    hypervisor = 0;
    assert(!vmmouse_init() && next == 0);
    hypervisor = 1;

    setup();
    expect(VM_GET_VERSION, ~VM_MAGIC, UINT32_MAX, ~VM_MAGIC, 0);
    assert(!vmmouse_init() && next == length);

    setup();
    expect(VM_GET_VERSION, ~VM_MAGIC, 6, VM_MAGIC, 0);
    expect(VM_MOUSE_COMMAND, VM_MOUSE_ENABLE, 0, 0, 0);
    expect(VM_MOUSE_STATUS, 0, 0xFFFF0000u, 0, 0);
    expect(VM_MOUSE_COMMAND, VM_MOUSE_DISABLE, 0, 0, 0);
    assert(!vmmouse_init() && next == length);

    setup();
    expect(VM_GET_VERSION, ~VM_MAGIC, 6, VM_MAGIC, 0);
    expect(VM_MOUSE_COMMAND, VM_MOUSE_ENABLE, 0, 0, 0);
    expect(VM_MOUSE_STATUS, 0, 1, 0, 0);
    expect(VM_MOUSE_DATA, 1, VM_MOUSE_ID, 0, 0);
    expect(VM_MOUSE_COMMAND, VM_MOUSE_ABSOLUTE, 0, 0, 0);
    assert(vmmouse_init() && next == length);

    setup();
    expect(VM_MOUSE_STATUS, 0, 12, 0, 0);
    expect(VM_MOUSE_DATA, 4, VM_LEFT_BUTTON, 65535, 0);
    expect(VM_MOUSE_DATA, 4, VM_RELATIVE_SAMPLE, 3, 10);
    expect(VM_MOUSE_DATA, 4, 0, 32768, 65535);
    assert(vmmouse_poll(sample) == 2 && next == length);
    assert(seen == 2 && last_x == 32768 && last_y == 65535 && !last_left);

    setup();
    expect(VM_MOUSE_STATUS, 0, 0, 0, 0);
    assert(vmmouse_poll(sample) == 0 && next == length);

    setup();
    expect(VM_MOUSE_STATUS, 0, 3, 0, 0);
    expect(VM_MOUSE_COMMAND, VM_MOUSE_DISABLE, 0, 0, 0);
    assert(vmmouse_poll(sample) == -1 && next == length);

    assert(vmmouse_cell(0, 80) == 0);
    assert(vmmouse_cell(32768, 80) == 40);
    assert(vmmouse_cell(65535, 80) == 79);
    assert(vmmouse_cell(65535, 25) == 24);

    puts("VMMouse handshake, queue, and coordinate tests passed.");
}

