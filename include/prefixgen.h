/* prefixgen.h - programmatic generator of a BGP-like IPv4 routing table.
 *
 * Tema 6's test of fire calls for "10^5 real prefixes" from public BGP
 * datasets. Per the project requirement we instead synthesize the table
 * programmatically beforehand, drawing prefix lengths from a histogram that
 * mirrors a real global BGP table (heavy /24, /23, /22, /16, with a realistic
 * tail). Generation is deterministic given a seed, so runs are reproducible and
 * the table "is already made when the project runs".
 */
#ifndef PREFIXGEN_H
#define PREFIXGEN_H

#include "ipv4.h"

typedef struct {
    prefix_t  p;
    nexthop_t nh;
} route_entry_t;

/* Fill `out[0..n)` with n unique, valid prefixes (host bits zeroed) and synthetic
 * next hops over interfaces 1..n_ifaces. Returns the number actually produced
 * (== n on success). */
size_t prefixgen_generate(route_entry_t *out, size_t n, unsigned seed, int n_ifaces);

/* Generate and write `n` prefixes to `path` as "A.B.C.D/len  iface" lines. */
bool prefixgen_write_file(const char *path, size_t n, unsigned seed, int n_ifaces);

/* Load a prefix file written by prefixgen_write_file (or compatible). On success
 * *out is malloc'd (caller frees) and the count is returned; 0 on error. */
size_t prefixgen_load_file(const char *path, route_entry_t **out);

#endif /* PREFIXGEN_H */
