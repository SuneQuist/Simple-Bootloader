#include <stdint.h>

#include "terminal.h"
#include "shell.h"
#include "history.h"

#include "../peripherals/keyboard.h"
#include "../ux/font.h"
#include "../ux/framebuffer.h"

#define TERM_X          172
#define TERM_Y          222

#define SCROLLBACK_ROWS 128

#define FONT_SCALE      1

#define CHAR_WIDTH      (7 * FONT_SCALE)
#define CHAR_HEIGHT     (15 * FONT_SCALE)

#define TERM_PIXEL_WIDTH   560
#define TERM_PIXEL_HEIGHT  300

#define TERM_COLS (TERM_PIXEL_WIDTH / CHAR_WIDTH)
#define TERM_ROWS (TERM_PIXEL_HEIGHT / CHAR_HEIGHT)

#define TERM_BG         0x00FFFFFF
#define TERM_FG         0x0023344B
#define TERM_PROMPT     0x000078FF

#define COMMAND_CAP     128
#define PROMPT          "User> "
#define PROMPT_LEN (sizeof(PROMPT) - 1u)

typedef struct {
    char c;
    uint32_t color;
} TerminalCell;

/*
 * Committed output is kept separately from
 * the command currently being edited.
 *
 * Row numbers are logical row numbers.
 * The ring buffer maps them to physical rows.
 */
static TerminalCell cells[SCROLLBACK_ROWS][TERM_COLS];

static unsigned oldest_row;
static unsigned output_row;
static unsigned output_col;

static char command[COMMAND_CAP];
static unsigned command_len;
static unsigned caret;

static unsigned history_index;
static char saved_input[COMMAND_CAP];

static unsigned scroll_offset;

static int enabled;
static int dirty;

/* ---------- Scrollback storage ---------- */

static void clear_row(unsigned row)
{
    TerminalCell *line = cells[row % SCROLLBACK_ROWS];

    for (unsigned col = 0; col < TERM_COLS; col++) {
        line[col].c = ' ';
        line[col].color = TERM_FG;
    }
}

static void clear_storage(void)
{
    oldest_row = 0;
    output_row = 0;
    output_col = 0;
    scroll_offset = 0;

    for (unsigned row = 0; row < SCROLLBACK_ROWS; row++)
        clear_row(row);

    dirty = 1;
}

static void output_newline(void)
{
    output_row++;
    output_col = 0;

    if (output_row - oldest_row >= SCROLLBACK_ROWS)
        oldest_row = output_row - SCROLLBACK_ROWS + 1;

    clear_row(output_row);
    dirty = 1;
}

static void output_char(char c, uint32_t color)
{
    if (c == '\r') {
        output_col = 0;
        dirty = 1;
        return;
    }

    if (c == '\n') {
        output_newline();
        return;
    }

    if (c == '\t') {
        unsigned spaces = 4 - output_col % 4;

        while (spaces--)
            output_char(' ', color);

        return;
    }

    if (c < 32 || c > 126)
        return;

    TerminalCell *cell =
        &cells[output_row % SCROLLBACK_ROWS][output_col];

    cell->c = c;
    cell->color = color;

    output_col++;

    if (output_col == TERM_COLS)
        output_newline();

    dirty = 1;
}

static void output_string(const char *text, uint32_t color)
{
    while (*text)
        output_char(*text++, color);
}

/* ---------- Public console interface ---------- */

int terminal_active(void)
{
    return enabled;
}

void terminal_putc(char c)
{
    output_char(c, TERM_FG);
    scroll_offset = 0;
}

void terminal_print(const char *text)
{
    if (!text)
        return;

    while (*text)
        terminal_putc(*text++);
}

void terminal_line_end(void)
{
    if (output_col != 0)
        terminal_putc('\n');
}

void terminal_clear(void)
{
    clear_storage();
}

/* ---------- Current input layout ---------- */

typedef struct {
    unsigned row;
    unsigned col;
} Position;

static Position input_position(unsigned length)
{
    /*
     * The editable prompt starts at the end
     * of committed command output.
     */
    unsigned cells_from_start =
        output_col + PROMPT_LEN + length;

    Position pos;

    pos.row = output_row + cells_from_start / TERM_COLS;
    pos.col = cells_from_start % TERM_COLS;

    return pos;
}

static unsigned last_visible_row(void)
{
    return input_position(command_len).row;
}

static unsigned bottom_first_row(void)
{
    unsigned last = last_visible_row();

    if (last < TERM_ROWS - 1)
        return 0;

    return last - (TERM_ROWS - 1);
}

static unsigned max_scroll(void)
{
    unsigned bottom = bottom_first_row();

    if (bottom <= oldest_row)
        return 0;

    return bottom - oldest_row;
}

static unsigned first_visible_row(void)
{
    unsigned maximum = max_scroll();

    if (scroll_offset > maximum)
        scroll_offset = maximum;

    return bottom_first_row() - scroll_offset;
}

/* ---------- Rendering ---------- */

static void draw_character(unsigned logical_row,
                           unsigned col,
                           char c,
                           uint32_t color,
                           unsigned first)
{
    if (logical_row < first ||
        logical_row - first >= TERM_ROWS ||
        col >= TERM_COLS)
        return;

    if (c == ' ')
        return;

    fb_char(
        TERM_X + col * CHAR_WIDTH,
        TERM_Y + (logical_row - first) * CHAR_HEIGHT,
        c,
        color,
        FONT_SCALE
    );
}

static void draw_input(unsigned first)
{
    unsigned row = output_row;
    unsigned col = output_col;

    const char *prompt = PROMPT;

    while (*prompt) {
        draw_character(row, col, *prompt,
                       TERM_PROMPT, first);

        col++;

        if (col == TERM_COLS) {
            col = 0;
            row++;
        }

        prompt++;
    }

    for (unsigned i = 0; i < command_len; i++) {
        draw_character(row, col, command[i],
                       TERM_FG, first);

        col++;

        if (col == TERM_COLS) {
            col = 0;
            row++;
        }
    }
}

void terminal_draw(void)
{
    if (!enabled)
        return;

    unsigned first = first_visible_row();

    fb_rect(
        TERM_X,
        TERM_Y,
        TERM_COLS * CHAR_WIDTH,
        TERM_ROWS * CHAR_HEIGHT,
        TERM_BG
    );

    for (unsigned visible = 0; visible < TERM_ROWS; visible++) {
        unsigned row = first + visible;

        if (row < oldest_row || row > output_row)
            continue;

        TerminalCell *line = cells[row % SCROLLBACK_ROWS];

        for (unsigned col = 0; col < TERM_COLS; col++) {
            draw_character(
                row,
                col,
                line[col].c,
                line[col].color,
                first
            );
        }
    }

    draw_input(first);

    /*
     * Draw the caret only when we're at the
     * bottom of the terminal, not in scrollback.
     */
    if (scroll_offset == 0) {
        Position pos = input_position(caret);

        if (pos.row >= first &&
            pos.row - first < TERM_ROWS) {
            fb_rect(
                TERM_X + pos.col * CHAR_WIDTH,
                TERM_Y + (pos.row - first) * CHAR_HEIGHT
                       + CHAR_HEIGHT - 3,
                CHAR_WIDTH - 2,
                2,
                TERM_PROMPT
            );
        }
    }

    dirty = 0;
}

/* ---------- Input editing ---------- */

static void copy_string(char *dst, const char *src)
{
    unsigned i = 0;

    while (src[i] && i < COMMAND_CAP - 1) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void set_command(const char *text)
{
    copy_string(command, text);

    command_len = 0;

    while (command[command_len])
        command_len++;

    caret = command_len;
    scroll_offset = 0;
    dirty = 1;
}

static void insert_character(char c)
{
    if (command_len >= COMMAND_CAP - 1)
        return;

    for (unsigned i = command_len; i > caret; i--)
        command[i] = command[i - 1];

    command[caret++] = c;
    command_len++;
    command[command_len] = '\0';

    dirty = 1;
}

static void delete_at_caret(void)
{
    if (caret >= command_len)
        return;

    for (unsigned i = caret; i < command_len; i++)
        command[i] = command[i + 1];

    command_len--;
    dirty = 1;
}

static void backspace(void)
{
    if (caret == 0)
        return;

    caret--;
    delete_at_caret();
}

/* ---------- Flush Terminal ---------- */

void terminal_flush(void)
{
    if (!enabled)
        return;

    terminal_draw();
    dirty = 0;
}

/* ---------- Filesystem command history ---------- */

static void history_previous(void)
{
    unsigned count = history_count();

    if (count == 0)
        return;

    /*
     * history_index == count means we're
     * editing a new command.
     */
    if (history_index > count)
        history_index = count;

    if (history_index == count)
        copy_string(saved_input, command);

    if (history_index > 0)
        history_index--;

    char recalled[COMMAND_CAP];

    if (history_get(history_index, recalled, sizeof recalled))
        set_command(recalled);
}

static void history_next(void)
{
    unsigned count = history_count();

    if (history_index >= count)
        return;

    history_index++;

    if (history_index == count) {
        set_command(saved_input);
        return;
    }

    char recalled[COMMAND_CAP];

    if (history_get(history_index, recalled, sizeof recalled))
        set_command(recalled);
}

/* ---------- Command execution ---------- */


static void submit_command(void)
{
    char submitted[sizeof command];

    command[command_len] = '\0';

    /* Save the command before clearing the live input. */
    for (unsigned i = 0; i <= command_len; ++i)
        submitted[i] = command[i];

    /* Commit the command to terminal scrollback. */
    output_string(PROMPT, TERM_PROMPT);
    output_string(submitted, TERM_FG);
    output_char('\n', TERM_FG);

    history_add(submitted);

    /*
     * Clear the live input BEFORE executing.
     * This prevents the command appearing twice.
     */
    command_len = 0;
    caret = 0;
    command[0] = '\0';

    history_index = history_count();
    saved_input[0] = '\0';

    scroll_offset = 0;
    dirty = 1;

    /* Display the submitted command and an empty input line. */
    terminal_draw();

    /* Execute the saved command. */
    shell_execute(submitted);

    terminal_line_end();

    scroll_offset = 0;
    dirty = 1;
}

/* ---------- Initialization and events ---------- */

void terminal_init(void)
{
    enabled = 0;

    clear_storage();

    command_len = 0;
    caret = 0;
    command[0] = '\0';

    saved_input[0] = '\0';
    history_index = history_count();

    enabled = 1;

    //terminal_print("Welcome to Tiny64!\n");
    //terminal_print("Your graphical OS is running.\n\n");

    terminal_draw();
}

static void scroll_page_up(void)
{
    unsigned maximum = max_scroll();
    unsigned amount = TERM_ROWS / 2;

    if (maximum - scroll_offset <= amount)
        scroll_offset = maximum;
    else
        scroll_offset += amount;

    dirty = 1;
}

static void scroll_page_down(void)
{
    unsigned amount = TERM_ROWS / 2;

    if (scroll_offset <= amount)
        scroll_offset = 0;
    else
        scroll_offset -= amount;

    dirty = 1;
}

void terminal_scroll(int lines)
{
    if (!enabled || lines == 0)
        return;

    unsigned maximum = max_scroll();

    if (lines > 0) {
        /* Scroll up toward older output. */
        unsigned amount = (unsigned)lines;

        if (scroll_offset >= maximum)
            return;

        if (amount > maximum - scroll_offset)
            amount = maximum - scroll_offset;

        scroll_offset += amount;
    }
    else {
        /* Scroll down toward newer output. */
        unsigned amount = (unsigned)(-(long)lines);

        if (amount > scroll_offset)
            amount = scroll_offset;

        scroll_offset -= amount;
    }

    dirty = 1;
}

void terminal_update(void)
{
    if (!enabled)
        return;

    unsigned mods = 0;
    int key = keyboard_try_getc_mod(&mods);

    if (key >= 0) {
        if (key == KEY_UP) {
            if (mods & KEY_MOD_SHIFT)
                scroll_page_up();
            else
                history_previous();
        }
        else if (key == KEY_DOWN) {
            if (mods & KEY_MOD_SHIFT)
                scroll_page_down();
            else
                history_next();
        }
        else if (key == KEY_LEFT) {
            if (mods & KEY_MOD_CTRL) {
                caret = 0;
                dirty = 1;
            }
            else if (caret > 0) {
                caret--;
                dirty = 1;
            }
        }
        else if (key == KEY_RIGHT) {
            if (mods & KEY_MOD_CTRL) {
                caret = command_len;
                dirty = 1;
            }
            else if (caret < command_len) {
                caret++;
                dirty = 1;
            }
        }
        if (key == KEY_UP) {
            if (mods & KEY_MOD_SHIFT)
                terminal_scroll(3);
            else
                history_previous();
        }
        else if (key == KEY_DOWN) {
            if (mods & KEY_MOD_SHIFT)
                terminal_scroll(-3);
            else
                history_next();
        }
        else if (key == KEY_HOME) {
            caret = 0;
            dirty = 1;
        }
        else if (key == KEY_END) {
            caret = command_len;
            dirty = 1;
        }
        else if (key == KEY_DELETE) {
            delete_at_caret();
        }
        else if (key == '\b') {
            backspace();
        }
        else if (key == '\n') {
            submit_command();
        }
        else if (key >= 32 && key <= 126) {
            insert_character((char)key);
        }
    }

    if (dirty)
        terminal_draw();
}