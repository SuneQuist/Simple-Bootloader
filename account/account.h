#ifndef ACCOUNT_H
#define ACCOUNT_H

#define ACCOUNT_NONE (~0u)

enum {
    UID_ROOT  = 0,
    UID_GUEST = 1
};

void account_init(void);
void account_logout(void);

unsigned create_account(const char *name, const char *password);
void login(const char *name, const char *password);
unsigned account_uid();

#endif
