#ifndef BASIC_H
#define BASIC_H

void help(void);
void ls(unsigned argc, const char *argv[]);
void cat(unsigned argc, const char *argv[]);
void echo(unsigned argc, const char *argv[]);
void cd(unsigned argc, const char *argv[]);
void netinfo(void);
void netcards(void);
int setup_network(const char *set_ip);
int network_ensure_ready(void);
const char *network_shared_ip(void);
void ip_command(const char *argv[]);
void ping(const char *argv[]);
void http(const char *argv[]);
void clear(void);

#endif