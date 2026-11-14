#include "shell.h"
#include "../console/console.h"
#include "../filesystem/fs.h"
#include "../keyboard/keyboard.h"

enum { LINE_CAP = 128, MAX_ARGS = 8, MAX_SCRIPT_DEPTH = 4 };

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static int error(const char *msg) {
    console_print(msg);
    console_putc('\n');
    return 1;
}

static void list_files(void) {
    for (unsigned slot = 0;; ++slot) {
        unsigned ino;
        const char *name = fs_entry(slot, &ino);
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

static int execute(const char *line, unsigned depth);

static int run_script(const char *name, unsigned depth) {
    unsigned len;
    const char *text = fs_read(name, &len);
    if (!text)
        return error("sh: file not found");
    if (depth >= MAX_SCRIPT_DEPTH)
        return error("sh: scripts nested too deeply");

    unsigned pos = 0;
    while (pos < len) {
        char line[LINE_CAP];
        unsigned n = 0;
        while (pos < len && text[pos] != '\n') {
            if (n == LINE_CAP - 1 || text[pos] == '\0')
                return error("sh: invalid or overlong script line");
            line[n++] = text[pos++];
        }
        if (pos < len)
            ++pos;
        line[n] = '\0';
        if (execute(line, depth + 1))
            return 1;
    }

    return 0;
}

/* Split on spaces outside double quotes. Quotes are removed. */
static int execute(const char *line, unsigned depth) {
    unsigned argc = 0, used = 0;
    const char *argv[MAX_ARGS];
    char words[LINE_CAP];

    while (space(*line))
        ++line;
    if (*line == '#')
        return 0;

    while (*line) {
        if (argc == MAX_ARGS)
            return error("shell: too many arguments");

        // Look for quotes and null terminate string on ending (")
        argv[argc++] = &words[used];
        int quoted = 0;
        while (*line && (quoted || !space(*line))) {
            char c = *line++;
            if (c == '"') {
                quoted = !quoted;
            } else {
                if (used == LINE_CAP - 1)
                    return error("shell: line too long");
                words[used++] = c;
            }
        }
        if (quoted)
            return error("shell: missing closing quote");
        words[used++] = '\0';

        while (space(*line))
            ++line;
        if (*line && used == LINE_CAP)
            return error("shell: line too long");
    }

    // What happens if x amount of args
    if (!argc)
        return 0;
    if (equal(argv[0], "help")) {
        console_print("help  ls  cat FILE  echo WORDS  sh FILE  clear\n");
    } else if (equal(argv[0], "ls")) {
        if (argc != 1)
            return error("usage: ls");
        list_files();
    } else if (equal(argv[0], "cat")) {
        if (argc != 2)
            return error("usage: cat FILE");
        unsigned len;
        const char *text = fs_read(argv[1], &len);
        if (!text)
            return error("cat: file not found");
        console_write(text, len);
    } else if (equal(argv[0], "echo")) {
        for (unsigned i = 1; i < argc; ++i) {
            if (i > 1)
                console_putc(' ');
            console_print(argv[i]);
        }
        console_putc('\n');
    } else if (equal(argv[0], "sh")) {
        if (argc != 2)
            return error("usage: sh FILE");
        return run_script(argv[1], depth);
    } else if (equal(argv[0], "clear")) {
        console_clear();
    } else {
        return error("shell: unknown command");
    }
    return 0;
}

int shell_execute(const char *line) {
    return execute(line, 0);
}

void shell_run(void) {
    for (;;) {
        char line[LINE_CAP];
        unsigned len = 0;
        unsigned caret = 0;

        console_print("$ ");

        for (;;) {
            int key = keyboard_getc();
            unsigned old_len = len;
            int changed = 0;

            if (key == '\n')
                break;

            if (key == KEY_LEFT) {
                if (caret > 0)
                    --caret;
            } else if (key == KEY_RIGHT) {
                if (caret < len)
                    ++caret;
            } else if (key == KEY_HOME) {
                caret = 0;
            } else if (key == KEY_END) {
                caret = len;
            } else if (key == '\b' || key == KEY_DELETE) {
                if (key == '\b') {
                    if (caret == 0)
                        continue;
                    --caret;
                } else if (caret == len) {
                    continue;
                }

                for (unsigned i = caret; i + 1 < len; ++i)
                    line[i] = line[i + 1];

                --len;
                changed = 1;
            } else if (key >= ' ' && key <= '~' && len < LINE_CAP - 1) {
                for (unsigned i = len; i > caret; --i)
                    line[i] = line[i - 1];

                line[caret++] = (char)key;
                ++len;
                changed = 1;
            }

            if (changed) {
                /* pos was at the end of the old command. */
                console_seek(console_cursor_pos() - old_len);
                console_write(line, len);

                if (len < old_len) {
                    console_putc(' ');  /* Erase the leftover character. */
                    console_seek(console_cursor_pos() - 1);
                }
            }

            /* pos is at the end, so subtract the distance to the caret. */
            console_set_cursor(console_cursor_pos() - (len - caret));
        }

        console_putc('\n');
        line[len] = '\0';
        shell_execute(line);
        console_line_end();
        console_set_cursor(console_cursor_pos() - (len - caret - 2));
    }
}
