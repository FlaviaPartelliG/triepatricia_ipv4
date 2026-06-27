/* pktgen.c - synthetic destination-IP stream. */
#include "pktgen.h"

static inline uint32_t xs32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x ? x : 0x9abcdef1u;
}

void pktgen_init(pktgen_t *g, const route_entry_t *routes, size_t nroutes,
                 unsigned seed, double hit_ratio) {
    g->routes = routes;
    g->nroutes = nroutes;
    g->state = seed ? seed : 0x1u;
    if (hit_ratio < 0.0) hit_ratio = 0.0;
    if (hit_ratio > 1.0) hit_ratio = 1.0;
    g->hit_threshold = (uint32_t)(hit_ratio * 4294967295.0);
}

uint32_t pktgen_next(pktgen_t *g) {
    uint32_t roll = xs32(&g->state);
    if (g->nroutes && roll < g->hit_threshold) {
        /* pick a prefix and randomize its host bits -> guaranteed match */
        const route_entry_t *e = &g->routes[xs32(&g->state) % g->nroutes];
        uint32_t host = xs32(&g->state) & ~ipv4_mask(e->p.len);
        return e->p.addr | host;
    }
    return xs32(&g->state);   /* uniform random destination */
}
