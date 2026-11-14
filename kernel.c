#include "console/console.h"
#include "filesystem/fs.h"
#include "keyboard/keyboard.h"
#include "shell/shell.h"

void kernel_main(void) {
    static const char notes[] = "Hello from notes.txt!\nThis file lives in RAM.\n";
    static const char script[] = "echo \"Running demo.sh\"\nls\ncat notes.txt";

    console_clear();
    keyboard_init();
    fs_init();
    fs_put("notes.txt", 1, notes, sizeof(notes) - 1);
    fs_put("demo.sh", 1, script, sizeof(script) - 1);
    fs_put("fold", 2, "", 0);
    fs_goto("/fold");
    fs_put("test.txt", 1, notes, sizeof(notes) - 1);
    console_print("Tiny64 shell. Type help for commands.\n");
    console_print(keyboard_enable_mouse() ? "PS/2 mouse ready\n": "Ps/2 mouse did not acknowledge\n");
    shell_run();
}

