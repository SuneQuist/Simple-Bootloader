#include "../account/account.h"
#include "../console/console.h"
#include "../console/tty.h"

#include "login.h"

unsigned login_prompt(void) {
    char name[32];        /* Up to 31 characters plus '\0'. */
    char password[129];   /* Up to 128 characters plus '\0'. */

    for (;;) {
        console_print("login: ");
        if (!tty_readline(name, sizeof name, 1)) {
            console_print("Login incorrect\n");
            continue;
        }

        console_print("Password: ");
        int complete = tty_readline(password, sizeof password, 0);

        if (complete)
            login(name, password);

        /* Clear the input buffer even if the line was too long. */
        volatile char *p = password;
        for (unsigned i = 0; i < sizeof password; ++i)
            p[i] = 0;

        if (complete && account_uid() != ACCOUNT_NONE)
            return account_uid();

        console_print("Login incorrect\n");
    }
}
