#include "console/console.h"
#include "filesystem/fs.h"
#include "peripherals/keyboard.h"
#include "shell/shell.h"
#include "shell/login.h"
#include "account/account.h"
#include "networking/timer.h"

void kernel_main(void) {
    if (timer_init())
        console_print("TIMER: initialized\n");
    else
        console_print("TIMER: initialization failed\n");

    static const char notes[] = "Hello from notes.txt!\nThis file lives in RAM.\n";
    static const char script[] = "echo \"Running demo.sh\"\nls\ncat notes.txt";

    console_clear();
    
    account_init();

    unsigned uid = create_account("guest", "test-only");
    if (uid == ACCOUNT_NONE) {
        console_print("Account creation failed\n");
    } else {
        login("guest", "test-only");
        console_print(account_uid() == uid ? "Login OK\n" : "Login failed\n");
        account_logout();
    }
        
    keyboard_init();
    fs_init();
    login_prompt();

    //fs_put("fold", 2, "", 0);
    //fs_goto("/fold");
    //fs_put("test.txt", 1, notes, sizeof(notes) - 1);
    keyboard_enable_mouse();
    shell_run();
}

