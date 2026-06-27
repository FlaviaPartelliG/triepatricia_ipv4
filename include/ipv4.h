/* ipv4.h - IPv4 address and CIDR prefix primitives.
 *
 * Tema 6 - Roteador IP por Longest Prefix Match.
 * Addresses are kept as host-order uint32_t with the most significant bit
 * (bit index 0) corresponding to the leftmost bit of A in A.B.C.D.
 */
#ifndef IPV4_H
#define IPV4_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define IPV4_BITS 32

/* A next hop / egress decision for a route. */
typedef struct {
    uint32_t gateway;   /* next-hop IP (0 = directly connected)        */
    int32_t  iface;     /* egress interface id; -1 means "no route"    */
} nexthop_t;

/* A CIDR prefix: network address (host bits zeroed) and length in bits. */
typedef struct {
    uint32_t addr;      /* network address, host bits MUST be zero     */
    uint8_t  len;       /* prefix length, 0..32                        */
} prefix_t;

/* The sentinel returned when no route matches. */
static inline nexthop_t nexthop_none(void) {
    nexthop_t nh = { 0u, -1 };
    return nh;
}

static inline bool nexthop_valid(nexthop_t nh) {
    return nh.iface >= 0;
}

/* Build a /len mask. mask(0) == 0, mask(32) == 0xFFFFFFFF. */
static inline uint32_t ipv4_mask(uint8_t len) {
    if (len == 0) return 0u;
    if (len >= IPV4_BITS) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (IPV4_BITS - len);
}

/* Return bit `idx` (0 = MSB) of `addr` as 0 or 1. */
static inline int ipv4_bit_at(uint32_t addr, uint8_t idx) {
    return (int)((addr >> (IPV4_BITS - 1 - idx)) & 1u);
}

/* Zero the host bits of `addr` for prefix length `len`. */
static inline uint32_t ipv4_network(uint32_t addr, uint8_t len) {
    return addr & ipv4_mask(len);
}

/* True if `addr` is contained in the network described by `p`. */
static inline bool prefix_matches(prefix_t p, uint32_t addr) {
    uint32_t m = ipv4_mask(p.len);
    return (addr & m) == (p.addr & m);
}

/* Number of identical leading bits of a and b, capped at 32. */
uint8_t ipv4_common_prefix(uint32_t a, uint32_t b);

/* Parse "A.B.C.D/len" or "A.B.C.D" (len defaults to 32). Host bits are
 * normalized to zero. Returns true on success. */
bool prefix_parse(const char *s, prefix_t *out);

/* Parse a bare "A.B.C.D" into *out. Returns true on success. */
bool ipv4_parse(const char *s, uint32_t *out);

/* Format `addr` as "A.B.C.D" into buf (needs >= 16 bytes). Returns buf. */
char *ipv4_format(uint32_t addr, char *buf, size_t buflen);

/* Format prefix as "A.B.C.D/len" into buf (needs >= 19 bytes). Returns buf. */
char *prefix_format(prefix_t p, char *buf, size_t buflen);

#endif /* IPV4_H */
