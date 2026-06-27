/* multibit.h - fixed-stride multibit trie for IPv4 LPM (comparison structure).
 *
 * A multibit trie consumes `stride` bits per level using controlled prefix
 * expansion, trading memory for fewer memory accesses per lookup. It is the
 * comparative optimization required by Tema 6 and is benchmarked against the
 * Patricia trie. This structure is single-threaded (used for the static-table
 * lookup-speed/memory comparison), so it uses no atomics.
 *
 * Supported strides: 1, 2, 4, 8 (must divide 32). Stride 8 -> 4 levels.
 */
#ifndef MULTIBIT_H
#define MULTIBIT_H

#include "ipv4.h"

typedef struct multibit multibit_t;

multibit_t *mb_create(int stride);
void        mb_destroy(multibit_t *m);

bool      mb_insert(multibit_t *m, prefix_t p, nexthop_t nh);
bool      mb_remove(multibit_t *m, prefix_t p);
nexthop_t mb_lookup(const multibit_t *m, uint32_t addr);

int    mb_stride(const multibit_t *m);
size_t mb_count(const multibit_t *m);       /* number of prefixes */
size_t mb_nodes(const multibit_t *m);       /* number of trie nodes */
size_t mb_mem_bytes(const multibit_t *m);   /* approx resident bytes */

#endif /* MULTIBIT_H */
