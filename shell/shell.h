#ifndef SHELL_H
#define SHELL_H

/* Execute one NUL-terminated command line, or keep reading from the keyboard. */
int shell_execute(const char *line);
void shell_run(void);

#endif

