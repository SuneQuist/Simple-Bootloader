#ifndef E1000_H
#define E1000_H

int net_use_e1000(unsigned bus, unsigned slot, unsigned fn, volatile void *mapped_bar0, uint8_t mac[6]);
int net_use_tulip(unsigned bus, unsigned slot, unsigned fn, uint8_t mac[6]);
volatile void *e1000_map_bar0(unsigned bus, unsigned slot, unsigned fn);
int eth_send(const uint8_t *frame, unsigned len);
int eth_recv(uint8_t *frame, unsigned cap);

#endif