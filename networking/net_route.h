#ifndef NET_ROUTE_H
#define NET_ROUTE_H

#include <stdint.h>

#define NET_MAC_LEN 6

void net_set_route(uint32_t ip, uint32_t mask, uint32_t gateway);
int net_resolve(uint32_t target, uint8_t mac[NET_MAC_LEN]);

/* Call this whenever a received frame is available in rx. */
void net_process_arp(unsigned len);

#endif