#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

int http_get(uint32_t server_ip, uint16_t port, const char *host, const char *path);

#endif