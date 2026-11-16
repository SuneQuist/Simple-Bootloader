#ifndef BASIC_H
#define BASIC_H

void help(void);
void ls(unsigned argc, const char *argv[]);
void cat(unsigned argc, const char *argv[]);
void echo(unsigned argc, const char *argv[]);
void cd(unsigned argc, const char *argv[]);
void netinfo(void);
void netcards(void);
void ping(const char *argv[]);
void clear(void);

#endif