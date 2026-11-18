#include <stdint.h>
#include "net_route.h"
#include "../console/console.h"
#include "net.h"
#include "timer.h"

#define ARP_CACHE_SIZE 4
#define ARP_CACHE_TTL  60000ULL

extern int net_arp(uint32_t ip, uint8_t mac[6]);
extern uint32_t own_ip;

static uint32_t local_ip;
static uint32_t subnet_mask;
static uint32_t default_gateway;

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint64_t expires;
    int valid;
} arp_entry_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

static unsigned replacement_index;

static void mac_copy(uint8_t dst[6], const uint8_t src[6]) {
    for (unsigned i = 0; i < 6; i++)
        dst[i] = src[i];
}

void net_set_route(uint32_t ip, uint32_t mask, uint32_t gateway) {
    local_ip = ip;
    subnet_mask = mask;
    default_gateway = gateway;

    for (unsigned i = 0; i < ARP_CACHE_SIZE; i++)
        arp_cache[i].valid = 0;
}

static int arp_cache_lookup(uint32_t ip, uint8_t mac[6]) {
    uint64_t now = net_millis();

    for (unsigned i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_entry_t *entry = &arp_cache[i];

        if (!entry->valid || entry->ip != ip)
            continue;

        if (now >= entry->expires) {
            entry->valid = 0;
            continue;
        }

        mac_copy(mac, entry->mac);
        return 1;
    }

    return 0;
}

static void arp_cache_insert(uint32_t ip, const uint8_t mac[6]) {
    unsigned index = replacement_index++ % ARP_CACHE_SIZE;
    arp_entry_t *entry = &arp_cache[index];

    entry->ip = ip;
    mac_copy(entry->mac, mac);
    entry->expires = net_millis() + ARP_CACHE_TTL;
    entry->valid = 1;
}

int net_resolve(uint32_t target, uint8_t mac[6]) {
    uint32_t next_hop;

    if ((target & subnet_mask) == (local_ip & subnet_mask)) {
        next_hop = target;
    } else {
        if (!default_gateway)
            return -1;

        next_hop = default_gateway;
    }

    if (arp_cache_lookup(next_hop, mac))
        return 1;

    int result = net_arp(next_hop, mac);

    if (result == 1)
        arp_cache_insert(next_hop, mac);

    return result;
}

/*
 * ARP handling remains in your existing networking code.
 * net_process_arp() can be implemented there if needed.
 */
uint32_t net_next_hop(uint32_t target) {
    if ((target & subnet_mask) == (local_ip & subnet_mask)) return target;
    return default_gateway;
}
int net_arp_cached(uint32_t next_hop, uint8_t mac[6]) {
    return arp_cache_lookup(next_hop, mac);
}
void net_arp_remember(uint32_t next_hop, const uint8_t mac[6]) {
    arp_cache_insert(next_hop, mac);
}
