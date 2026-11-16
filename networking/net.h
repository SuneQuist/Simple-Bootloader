#ifndef NET_H
#define NET_H

#include <stdint.h>

void net_configure(const uint8_t mac[6], uint32_t ip);
int net_parse_ipv4(const char *text, uint32_t *ip);
int net_arp(uint32_t target, uint8_t mac_out[6]);
int net_ping(uint32_t target);

#endif