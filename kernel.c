#include <stdint.h>
#include "filesystem/fs.h"
#include "console/console.h"
#include "shell/terminal.h"
#include "shell/shell.h"
#include "shell/login.h"
#include "account/account.h"
#include "peripherals/keyboard.h"
#include "peripherals/mouse.h"
#include "peripherals/interrupts.h"
#include "peripherals/vmmouse.h"
#include "networking/timer.h"
#include "graphics/framebuffer.h"
#include "graphics/desktop.h"
#include "graphics/cursor.h"
#include "graphics/font.h"
#include "memory/heap.h"
#include "memory/lexbor_memory.h"
#include "graphics/window.h"
#include "graphics/frame_pacing.h"
#include "web/renderer.h"
#include "web/navigation.h"
#include "tls/tls_probe.h"

extern volatile uint32_t irq12_count;
extern volatile uint32_t mouse_byte_count;

// Framebuffer .globals found in start.s
extern uint8_t fb_active;

extern uint32_t fb_virtual;
extern uint32_t fb_width;
extern uint32_t fb_height;
extern uint32_t fb_pitch;
extern uint32_t fb_bpp;

extern uint8_t vbe_status; // diagnostic status
extern uint8_t vbe_attributes;
extern uint8_t vbe_bpp_detected;
extern uint32_t vbe_width_detected;
extern uint32_t vbe_height_detected;

static void activate_framebuffer(void) {
    if (!fb_active) {
        volatile uint16_t *vga =
            (volatile uint16_t *)0xB8000;

        const char hex[] = "0123456789ABCDEF";

        vga[0] = 0x4F56; /* V */
        vga[1] = 0x4F30 + vbe_status;
        vga[2] = 0x4F20; /* space */

        vga[3] = 0x4F00 | hex[vbe_attributes >> 4];
        vga[4] = 0x4F00 | hex[vbe_attributes & 0x0F];

        vga[6] = 0x4F00 | hex[vbe_bpp_detected >> 4];
        vga[7] = 0x4F00 | hex[vbe_bpp_detected & 0x0F];

        for (;;) {
            __asm volatile ("hlt");
        }
    }

    if (framebuffer_init(
            (uintptr_t)fb_virtual,
            fb_width,
            fb_height,
            fb_pitch,
            fb_bpp)) {

        desktop_draw();

        return;
    }

    //fb_clear(0x00202020);

    /* Blue bar: framebuffer width */
    //fb_rect(0, 20, (int)fb_width, 20, 0x000000FF);

    /* Green bar: framebuffer height */
    //fb_rect(0, 60, (int)fb_height, 20, 0x0000FF00);

    /* Yellow background, black I */
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    vga[0] = 0xE049;

    for (;;) {
        __asm volatile ("hlt");
    }
}

static int cpu_interrupts_enabled(void)
{
    uint64_t flags;

    __asm volatile ("pushfq\n" "popq %0" : "=r"(flags));

    return (flags & (1ULL << 9)) != 0;
}

static inline uint8_t ps2_status(void)
{
    uint8_t status;

    __asm volatile ("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x64));

    return status;
}

void kernel_main(void)
{
    activate_framebuffer();

    heap_init();
    lexbor_init();

    keyboard_init();
    fs_init();

    /*
     * Install the IDT and configure the PIC.
     * CPU interrupts remain disabled.
     */
    interrupts_init();

    int mouse_ok = keyboard_enable_mouse();
    
    if (fb_active) {
        window_init();
        terminal_init();

        int terminal_window_id = window_add(
            terminal_window_draw,
            NULL,
            172, 222, 560, 300,
            10
        );

        if (terminal_window_id < 0) {
            console_print("Failed to register terminal window\n");
        } else {
            /*
            * Give the terminal its own window-manager title bar.
            *
            * title_above = 1 keeps the existing terminal
            * drawing area intact.
            *
            * closable = 0 prevents the terminal from being
            * permanently hidden before taskbar restoration
            * is implemented.
            */
            window_set_title(
                terminal_window_id,
                "Tiny64 Terminal"
            );

            window_set_decorations(
                terminal_window_id,
                1,  /* decorated */
                1,  /* title bar above content */
                0   /* not closable yet */
            );

            /*
            * Give the terminal initial keyboard focus.
            */
            window_focus(terminal_window_id);
        }

        desktop_redraw();

        mouse_graphics_enable();
        /* Show the newly initialized cursor without a full repaint. */
        desktop_cursor_refresh();

        if (!timer_init()) {
            console_print("Network timer initialization failed\n");
        }

        if (mouse_ok)
            interrupts_enable();

        for (;;) {
            /*
            * Refresh the graphical mouse.
            *
            * This processes pending mouse movement outside
            * interrupt context.
            */
            mouse_graphics_refresh();

            /*
            * Read the latest mouse position and button state.
            */
            int mx, my, left;
            mouse_get_gui_state(&mx, &my, &left);

            /*
            * Process mouse clicks and window dragging.
            */
            /* Dispatch content clicks after window-manager focus handling. */
            static int previous_browser_left = 0;
            window_mouse_event(mx, my, left);
            if (left && !previous_browser_left)
                browser_mouse_click(mx, my);
            previous_browser_left = left;

            /* Process queued navigation outside the mouse click callback. */
            browser_navigation_poll();
            tls_probe_poll();

            /*
            * Synchronize the terminal renderer with its
            * window-manager position.
            *
            * The window manager owns the position.
            * The terminal renderer simply follows it.
            */
            if (terminal_window_id >= 0) {
                int terminal_x;
                int terminal_y;

                window_get_position(
                    terminal_window_id,
                    &terminal_x,
                    &terminal_y
                );

                terminal_set_position(terminal_x, terminal_y);
            }

            /*
            * Keyboard input routing.
            *
            * When the terminal is focused, let it process
            * keyboard input normally.
            *
            * When another window is focused, consume pending
            * keyboard events without sending them to the terminal.
            *
            * This prevents keystrokes from accumulating while
            * the browser is active.
            */
            if (window_is_focused(terminal_window_id)) {
                terminal_update();
            } else {
                unsigned mods;

                /*
                * Drain a bounded number of events per iteration.
                *
                * This prevents keyboard input from filling the
                * queue while keeping the main loop responsive.
                *
                * Using the normal keyboard decoder also keeps
                * Shift, Ctrl, and other modifier states updated.
                */
                for (int i = 0; i < 64; i++) {
                    int key = keyboard_try_getc_mod(&mods);
                    if (key < 0) break;
                    browser_keyboard_event(key);
                }
            }

            /*
            * Handle mouse-wheel scrolling.
            */
            int wheel = mouse_take_wheel();

            if (wheel != 0 && browser_mouse_wheel(mx, my, wheel)) {
                /* Browser requested a coalesced repaint. */
            }
            else if (wheel != 0 && terminal_active()) {
                terminal_scroll(-wheel * 3);
                desktop_redraw();
            }
            
            /*
            * Focus changes require a full repaint because
            * overlapping windows may change their stacking order.
            *
            * Dragging continues to use the existing redraw path.
            */
            int focus_changed = window_take_focus_redraw();
            int window_changed = window_take_redraw();

            if (focus_changed) {
                /*
                * A full redraw supersedes pending drag damage.
                */
                int dx, dy, dw, dh;
                window_take_damage(&dx, &dy, &dw, &dh);

                desktop_redraw();
            }
            else if (window_changed) {
                /*
                * Keep the working dragging redraw mechanism.
                *
                * If your current kernel uses a specialized
                * dirty-region redraw here, retain it rather
                * than replacing it with a full repaint.
                */
                desktop_redraw();
            }

            /*
            * Present the latest cursor position.
            *
            * This is important with our newer back-buffer
            * rendering system because mouse movement alone
            * may not trigger a full desktop redraw.
            */
            desktop_cursor_refresh();

            /*
            * Pause briefly between iterations.
            */
            __asm volatile ("pause");
        }
    }
}