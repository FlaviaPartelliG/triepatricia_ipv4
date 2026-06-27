/* router.h - routing table facade over the Patricia trie.
 *
 * Presents one API with two interchangeable concurrency backends so the
 * benchmark can compare them on identical workloads:
 *
 *   ROUTER_RCU   - lock-free readers + QSBR deferred reclamation (the delivered
 *                  concurrent data plane of Tema 6).
 *   ROUTER_RWLOCK- readers/writer serialized by a single rwlock (the lock-based
 *                  baseline the report measures against).
 *
 * Reader threads must obtain an id with router_reader_register() and pass it to
 * router_lookup(); the RCU backend uses it to announce quiescent states. The
 * rwlock backend ignores the id. Writers must be serialized by the caller in
 * RCU mode (single control thread, as the theme specifies); the rwlock backend
 * serializes them itself.
 */
#ifndef ROUTER_H
#define ROUTER_H

#include "ipv4.h"

typedef enum { ROUTER_RCU, ROUTER_RWLOCK } router_mode_t;

typedef struct router router_t;

router_t *router_create(router_mode_t mode);
void      router_destroy(router_t *r);

/* Reader side. */
int       router_reader_register(router_t *r);
void      router_reader_unregister(router_t *r, int id);
nexthop_t router_lookup(router_t *r, int reader_id, uint32_t addr);

/* Writer side. */
bool      router_insert(router_t *r, prefix_t p, nexthop_t nh);
bool      router_remove(router_t *r, prefix_t p);
void      router_writer_sync(router_t *r);  /* RCU: reclaim deferred; rwlock: no-op */

/* Stats (writer-side; call when not racing with writes). */
router_mode_t router_mode(const router_t *r);
size_t        router_count(const router_t *r);
size_t        router_nodes(const router_t *r);
size_t        router_mem(const router_t *r);
size_t        router_pending(const router_t *r);
const char   *router_mode_name(const router_t *r);

#endif /* ROUTER_H */
