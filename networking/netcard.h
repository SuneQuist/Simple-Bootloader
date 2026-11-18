#ifndef NETCARD_H
#define NETCARD_H

#include <stdint.h>

uint32_t pci_read(unsigned bus, unsigned slot, unsigned fn, unsigned offset);
int tulip_read_mac(unsigned bus, unsigned slot, unsigned fn, uint8_t mac[6]);
int tulip_eth_init(unsigned bus, unsigned slot, unsigned fn, const uint8_t mac[6]);
int tulip_send(const uint8_t *frame, unsigned len);
int tulip_recv(uint8_t *frame, unsigned cap);
void print_hex(unsigned value, unsigned digits);
void print_hex_byte(unsigned n);
int map_e1000_bar(unsigned bar);

#endif