#include "shell.h"
#include "../console/console.h"
#include "../filesystem/fs.h"
#include "../peripherals/keyboard.h"
#include "../bin/basic.h"

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

static int execute(const char *line, unsigned depth, unsigned script_argc, const char *const *script_argv);

static int run_script(const char *path, unsigned depth, 
                        unsigned script_argc, const char *const *script_argv, 
                        int require_exec) {
    unsigned len;
    const char *text = require_exec ? fs_read_exec(path, &len)
                                    : fs_read(path, &len);
    if (!text)
        return error("sh: file not found or inaccessible");
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
        if (execute(line, depth + 1, script_argc, script_argv))
            return 1;
    }

    return 0;
}

/* Split on spaces outside double quotes. Quotes are removed. */
static int execute(const char *line, unsigned depth, 
                    unsigned script_argc, const char *const *script_argv) {
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

        /* Only expand these forms when they are a whole word in a script. */
        if (script_argv && line[0] == '"' && line[1] == '$' &&
            (line[2] == '@' || line[2] == '1') && line[3] == '"' &&
            (line[4] == '\0' || space(line[4]))) {

            if (line[2] == '@') {
                if (script_argc > MAX_ARGS - argc)
                    return error("shell: too many arguments");

                for (unsigned i = 0; i < script_argc; ++i)
                    argv[argc++] = script_argv[i];
            } else {
                if (argc == MAX_ARGS)
                    return error("shell: too many arguments");

                argv[argc++] = script_argc ? script_argv[0] : "";
            }

            line += 4;  /* Skip the four characters in "$@" or "$1". */
            while (space(*line))
                ++line;
            continue;
        }

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
    
    if (equal(argv[0], "help")) {               help(); } 
    else if (equal(argv[0], "ls")) {            ls(argc, argv); }
    else if (equal(argv[0], "cat")) {           cat(argc, argv); }
    else if (equal(argv[0], "echo")) {          echo(argc, argv); }
    else if (equal(argv[0], "cd")) {            cd(argc, argv); }
    else if (equal(argv[0], "netinfo")) {       netinfo(); }
    else if (equal(argv[0], "netcards")) {      netcards(); }
    else if (equal(argv[0], "ping")) {          ping(); }
    else if (equal(argv[0], "clear")) {         clear(); }
    // Other commands that aren't in an include file like basich 
    else if (equal(argv[0], "run")) {
        if (argc < 2)
            return error("usage: run FILE [ARG ...]");
        return run_script(argv[1], depth, argc - 2, argv + 2, 0);
    } else {
        char path[sizeof "/bin/" + 29] = "/bin/";
        unsigned end = sizeof "/bin/" - 1;

        for (const char *p = argv[0]; *p; ++p) {
            if (*p == '/' || end == sizeof path - 1)
                return error("shell: invalid command name");

            path[end++] = *p;
        }

        path[end] = '\0';
        return run_script(path, depth, argc - 1, argv + 1, 1);
    }
    
    return 0;
}

int shell_execute(const char *line) {
    return execute(line, 0, 0, 0);
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
