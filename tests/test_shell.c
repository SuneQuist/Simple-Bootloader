#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "fs.h"
#include "keyboard.h"
#include "shell.h"

static char output[4096];
static unsigned length;
void console_clear(void) { length = 0; output[0] = 0; }
void console_putc(char c) {
    assert(length < sizeof(output) - 1);
    output[length++] = c;
    output[length] = 0;
}
void console_write(const char *s, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        console_putc(s[i]);
}
void console_print(const char *s) { console_write(s, strlen(s)); }
void console_line_end(void) { }
char keyboard_getc(void) { assert(0 && "unexpected keyboard read"); return 0; }

static void check(const char *line, int status, const char *expected) {
    console_clear();
    assert(shell_execute(line) == status);
    assert(strcmp(output, expected) == 0);
}

int main(void) {
    fs_init();
    assert(fs_put("notes.txt", "first\nlast", 10) == 2);
    assert(fs_put("two words", "quoted", 6) == 3);
    static const char script[] = "# comment\r\nls\ncat notes.txt";
    assert(fs_put("demo.sh", script, sizeof(script) - 1) == 4);
    check("  ", 0, "");
    check("echo \"hello world\" ok", 0, "hello world ok\n");
    check("echo \"\" ok", 0, " ok\n");
    check("ls", 0, " 2 notes.txt\n 3 two words\n 4 demo.sh\n");
    check("cat notes.txt", 0, "first\nlast");
    check("cat \"two words\"", 0, "quoted");
    check("sh demo.sh", 0, " 2 notes.txt\n 3 two words\n 4 demo.sh\nfirst\nlast");
    check("cat missing", 1, "cat: file not found\n");
    check("cat", 1, "usage: cat FILE\n");
    check("echo \"oops", 1, "shell: missing closing quote\n");
    check("echo a b c d e f g h", 1, "shell: too many arguments\n");
    check("unknown", 1, "shell: unknown command\n");
    assert(fs_put("loop.sh", "sh loop.sh", 10) == 5);
    check("sh loop.sh", 1, "sh: scripts nested too deeply\n");
    assert(fs_put("notes.txt", "new", 3) == 2);
    check("cat notes.txt", 0, "new");
    char overlong[130];
    memset(overlong, 'a', sizeof overlong - 1);
    overlong[sizeof overlong - 1] = 0;
    check(overlong, 1, "shell: line too long\n");
    assert(fs_put("long.sh", overlong, sizeof overlong - 1));
    check("sh long.sh", 1, "sh: invalid or overlong script line\n");
    puts("Shell and RAM filesystem tests passed.");
}

