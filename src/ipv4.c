/* ipv4.c - implementation of IPv4 primitives. */
#include "ipv4.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

uint8_t ipv4_common_prefix(uint32_t a, uint32_t b) {
    uint32_t x = a ^ b;
    if (x == 0) return IPV4_BITS;
    /* count leading zeros, MSB first */
    uint8_t n = 0;
    for (uint32_t mask = 0x80000000u; mask; mask >>= 1) {
        if (x & mask) break;
        n++;
    }
    return n;
}

bool ipv4_parse(const char *s, uint32_t *out) {
    if (!s || !out) return false;
    unsigned a, b, c, d;
    char extra;
    /* %c at the end catches trailing garbage like "1.2.3.4x". */
    int n = sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra);
    if (n != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    *out = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
           ((uint32_t)c << 8) | (uint32_t)d;
    return true;
}

bool prefix_parse(const char *s, prefix_t *out) {
    if (!s || !out) return false;

    char tmp[64];
    size_t len = strlen(s);
    if (len == 0 || len >= sizeof(tmp)) return false;
    memcpy(tmp, s, len + 1);

    char *slash = strchr(tmp, '/');
    unsigned plen = IPV4_BITS;
    if (slash) {
        *slash = '\0';
        char *endp = NULL;
        long v = strtol(slash + 1, &endp, 10);
        if (endp == slash + 1 || *endp != '\0') return false;
        if (v < 0 || v > IPV4_BITS) return false;
        plen = (unsigned)v;
    }

    uint32_t addr;
    if (!ipv4_parse(tmp, &addr)) return false;

    out->len = (uint8_t)plen;
    out->addr = ipv4_network(addr, out->len);
    return true;
}

char *ipv4_format(uint32_t addr, char *buf, size_t buflen) {
    snprintf(buf, buflen, "%u.%u.%u.%u",
             (addr >> 24) & 0xFFu, (addr >> 16) & 0xFFu,
             (addr >> 8) & 0xFFu, addr & 0xFFu);
    return buf;
}

char *prefix_format(prefix_t p, char *buf, size_t buflen) {
    char ip[16];
    ipv4_format(p.addr, ip, sizeof(ip));
    snprintf(buf, buflen, "%s/%u", ip, (unsigned)p.len);
    return buf;
}
