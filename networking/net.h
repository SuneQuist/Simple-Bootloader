#ifndef NET_H
#define NET_H

#include <stdint.h>

void net_configure(const uint8_t mac[6], uint32_t ip);
int net_parse_ipv4(const char *text, uint32_t *ip);
int net_arp(uint32_t target, uint8_t mac_out[6]);
int net_ping(uint32_t target);

uint32_t net_local_ip(void);
void net_local_mac(uint8_t mac[6]);
void arp_answer(void);
void net_handle_arp_frame(const uint8_t *frame, unsigned len);

#endif