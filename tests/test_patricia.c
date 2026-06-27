/* test_patricia.c - correctness of the Patricia trie vs. the brute-force oracle. */
#include "patricia.h"
#include "lpm_oracle.h"
#include "test_util.h"

#include <stdlib.h>
#include <string.h>

static nexthop_t nh_iface(int32_t i) {
    nexthop_t nh = { 0u, i };
    return nh;
}

static uint32_t rng_state = 0x12345678u;
static uint32_t rng(void) {
    /* xorshift32 - deterministic across platforms */
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static prefix_t rand_prefix(void) {
    prefix_t p;
    p.len = (uint8_t)(rng() % 33);          /* 0..32 */
    p.addr = ipv4_network(rng(), p.len);
    return p;
}

static void test_basic(void) {
    patricia_t *t = patricia_create();

    prefix_t p;
    prefix_parse("0.0.0.0/0", &p);
    CHECK(patricia_insert(t, p, nh_iface(1)));      /* default route */
    prefix_parse("10.0.0.0/8", &p);
    CHECK(patricia_insert(t, p, nh_iface(2)));
    prefix_parse("10.1.0.0/16", &p);
    CHECK(patricia_insert(t, p, nh_iface(3)));
    prefix_parse("10.1.2.0/24", &p);
    CHECK(patricia_insert(t, p, nh_iface(4)));

    uint32_t a;
    ipv4_parse("10.1.2.5", &a);
    CHECK(patricia_lookup(t, a).iface == 4);        /* /24 wins */
    ipv4_parse("10.1.3.5", &a);
    CHECK(patricia_lookup(t, a).iface == 3);        /* /16 wins */
    ipv4_parse("10.2.0.1", &a);
    CHECK(patricia_lookup(t, a).iface == 2);        /* /8 wins */
    ipv4_parse("8.8.8.8", &a);
    CHECK(patricia_lookup(t, a).iface == 1);        /* default */

    /* update an existing prefix */
    prefix_parse("10.1.2.0/24", &p);
    CHECK(patricia_insert(t, p, nh_iface(9)));
    ipv4_parse("10.1.2.5", &a);
    CHECK(patricia_lookup(t, a).iface == 9);
    CHECK(patricia_count(t) == 4);                  /* update, not a new route */

    /* remove the /24 -> falls back to /16 */
    CHECK(patricia_remove(t, p));
    ipv4_parse("10.1.2.5", &a);
    CHECK(patricia_lookup(t, a).iface == 3);
    CHECK(!patricia_remove(t, p));                  /* already gone */

    /* remove default -> 8.8.8.8 now unrouted */
    prefix_parse("0.0.0.0/0", &p);
    CHECK(patricia_remove(t, p));
    ipv4_parse("8.8.8.8", &a);
    CHECK(!nexthop_valid(patricia_lookup(t, a)));

    patricia_destroy(t);
}

/* randomized differential test against the oracle */
static void test_random(void) {
    patricia_t *t = patricia_create();
    oracle_t  *o = oracle_create();

    const int OPS = 20000;
    for (int i = 0; i < OPS; i++) {
        int op = rng() % 3;
        if (op < 2) {                  /* insert (2/3 of the time) */
            prefix_t p = rand_prefix();
            nexthop_t nh = nh_iface((int32_t)(rng() % 64));
            bool a = patricia_insert(t, p, nh);
            bool b = oracle_insert(o, p, nh);
            CHECK(a == b);
        } else {                       /* remove */
            prefix_t p = rand_prefix();
            bool a = patricia_remove(t, p);
            bool b = oracle_remove(o, p);
            CHECK_MSG(a == b, "remove mismatch len=%u", p.len);
        }

        if (i % 97 == 0) {
            CHECK_MSG(patricia_count(t) == oracle_count(o),
                      "count mismatch %lu vs %lu",
                      (unsigned long)patricia_count(t),
                      (unsigned long)oracle_count(o));
        }

        if (i % 7 == 0) {              /* spot-check lookups */
            for (int k = 0; k < 8; k++) {
                uint32_t a = rng();
                nexthop_t na = patricia_lookup(t, a);
                nexthop_t nb = oracle_lookup(o, a);
                CHECK_MSG(na.iface == nb.iface,
                          "lookup mismatch addr=%08x trie=%d oracle=%d",
                          a, na.iface, nb.iface);
            }
        }
    }

    /* exhaustive-ish final sweep */
    for (int k = 0; k < 5000; k++) {
        uint32_t a = rng();
        CHECK(patricia_lookup(t, a).iface == oracle_lookup(o, a).iface);
    }

    patricia_destroy(t);
    oracle_destroy(o);
}

int main(void) {
    TEST_BEGIN("patricia");
    test_basic();
    test_random();
    TEST_REPORT();
}
