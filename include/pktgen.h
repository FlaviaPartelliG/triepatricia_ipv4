/* pktgen.h - synthetic destination-IP stream for the data plane.
 *
 * Produces a stream of destination addresses to forward. A tunable fraction is
 * drawn from inside existing prefixes (guaranteed hits, exercising deep trie
 * walks) and the rest is uniform random (a mix of hits and default-route
 * misses). Each thread should own its own generator (distinct seed); pktgen_next
 * is not thread-safe by design, so the hot path stays lock-free.
 */
#ifndef PKTGEN_H
#define PKTGEN_H

#include "prefixgen.h"

typedef struct {
    const route_entry_t *routes;  /* borrowed */
    size_t   nroutes;
    uint32_t state;
    uint32_t hit_threshold;       /* rng < threshold => derive from a prefix */
} pktgen_t;

/* hit_ratio in [0,1]: fraction of packets aimed inside a known prefix. */
void     pktgen_init(pktgen_t *g, const route_entry_t *routes, size_t nroutes,
                     unsigned seed, double hit_ratio);
uint32_t pktgen_next(pktgen_t *g);

#endif /* PKTGEN_H */
