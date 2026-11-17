#ifndef HISTORY_H
#define HISTORY_H

void history_add(const char *command);

unsigned history_count(void);

/* Index 0 is the oldest retained command. */
int history_get(unsigned index, char *out, unsigned capacity);

#endif