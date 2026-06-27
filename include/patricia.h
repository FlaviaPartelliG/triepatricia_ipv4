/* patricia.h - path-compressed binary radix trie of IPv4 prefixes (LPM).
 *
 * This is the ED core of Tema 6. The trie stores CIDR prefixes and answers
 * longest-prefix-match queries. It is designed to be read concurrently without
 * locks: child and route pointers are C11 atomics, readers use acquire loads,
 * and the (serialized) writer publishes nodes with release stores.
 *
 * Reclamation is pluggable. By default an unlinked node/route is freed
 * immediately (correct for single-threaded use and the mutex backend). The RCU
 * backend installs a deferred reclaimer via patricia_set_reclaimer() so that
 * objects still visible to lock-free readers are only freed after a grace
 * period.
 *
 * Concurrency contract:
 *   - patricia_lookup() is safe to call from many threads with no lock.
 *   - patricia_insert()/patricia_remove() are writer-side and must be
 *     serialized against each other (single writer, or a writer lock).
 */
#ifndef PATRICIA_H
#define PATRICIA_H

#include "ipv4.h"

typedef struct patricia patricia_t;

/* Deferred-reclamation callback: hand ownership of `p` back for freeing. */
typedef void (*patricia_retire_fn)(void *ctx, void *p);

patricia_t *patricia_create(void);
void        patricia_destroy(patricia_t *t);

/* Install a deferred reclaimer (RCU). If never called, frees are immediate. */
void patricia_set_reclaimer(patricia_t *t, patricia_retire_fn fn, void *ctx);

/* Writer ops. insert adds or updates a route; returns true on success. */
bool patricia_insert(patricia_t *t, prefix_t p, nexthop_t nh);
/* remove deletes an exact prefix; returns true if it existed. */
bool patricia_remove(patricia_t *t, prefix_t p);

/* Reader op: longest prefix match. Lock-free; returns nexthop_none() if none. */
nexthop_t patricia_lookup(const patricia_t *t, uint32_t addr);

/* Stats (writer-side; not safe to call concurrently with writes). */
size_t patricia_count(const patricia_t *t);      /* number of routes */
size_t patricia_nodes(const patricia_t *t);      /* number of trie nodes */
size_t patricia_mem_bytes(const patricia_t *t);  /* approx resident bytes */

#endif /* PATRICIA_H */
