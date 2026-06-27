/* test_rcu.c - RCU reclamation mechanics + router LPM correctness (both modes).
 *
 * Concurrency stress (8 readers + writer, TSan) lives in test_concurrent.c.
 * Here everything is single-threaded so reclamation must not block: we only
 * reclaim when no reader is online.
 */
#include "rcu.h"
#include "router.h"
#include "lpm_oracle.h"
#include "test_util.h"

#include <stdlib.h>

static int g_freed;
static void counting_free(void *p) { g_freed++; free(p); }

static uint32_t rng_state = 0xABCD1234u;
static uint32_t rng(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static prefix_t rand_prefix(void) {
    prefix_t p;
    p.len = (uint8_t)(rng() % 33);
    p.addr = ipv4_network(rng(), p.len);
    return p;
}
static nexthop_t nh_iface(int32_t i) { nexthop_t nh = { 0u, i }; return nh; }

static void test_rcu_mechanics(void) {
    rcu_t *r = rcu_create();
    CHECK(r != NULL);

    g_freed = 0;
    for (int i = 0; i < 100; i++)
        rcu_defer(r, malloc(8), counting_free);
    CHECK(rcu_pending(r) == 100);

    rcu_reclaim(r);              /* no online readers -> returns immediately */
    CHECK(g_freed == 100);
    CHECK(rcu_pending(r) == 0);

    /* a reader that registers, quiesces, then leaves must not block reclaim */
    int id = rcu_register(r);
    CHECK(id == 0);
    rcu_quiescent_state(r, id);
    rcu_unregister(r, id);

    for (int i = 0; i < 10; i++)
        rcu_defer(r, malloc(8), counting_free);
    rcu_reclaim(r);
    CHECK(g_freed == 110);

    rcu_destroy(r);
}

static void test_router_correctness(router_mode_t mode) {
    router_t *r = router_create(mode);
    oracle_t *o = oracle_create();
    int id = router_reader_register(r);   /* -1 for rwlock, fine */

    for (int i = 0; i < 8000; i++) {
        int op = rng() % 3;
        if (op < 2) {
            prefix_t p = rand_prefix();
            nexthop_t nh = nh_iface((int32_t)(rng() % 64));
            CHECK(router_insert(r, p, nh) == oracle_insert(o, p, nh));
        } else {
            prefix_t p = rand_prefix();
            CHECK(router_remove(r, p) == oracle_remove(o, p));
        }
        if (i % 9 == 0) {
            for (int k = 0; k < 6; k++) {
                uint32_t a = rng();
                CHECK_MSG(router_lookup(r, id, a).iface == oracle_lookup(o, a).iface,
                          "mode=%s addr=%08x", router_mode_name(r), a);
            }
        }
    }
    CHECK(router_count(r) == oracle_count(o));

    /* reclaim only after the reader is offline (single-threaded) */
    router_reader_unregister(r, id);
    router_writer_sync(r);

    router_destroy(r);
    oracle_destroy(o);
}

int main(void) {
    TEST_BEGIN("rcu+router");
    test_rcu_mechanics();
    test_router_correctness(ROUTER_RCU);
    test_router_correctness(ROUTER_RWLOCK);
    TEST_REPORT();
}
