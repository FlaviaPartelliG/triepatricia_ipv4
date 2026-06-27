/* test_multibit.c - multibit trie correctness vs. the oracle, across strides. */
#include "multibit.h"
#include "lpm_oracle.h"
#include "test_util.h"

#include <stdlib.h>

static nexthop_t nh_iface(int32_t i) { nexthop_t nh = { 0u, i }; return nh; }

static uint32_t rng_state = 0xC0FFEEu;
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

static void test_basic(int stride) {
    multibit_t *m = mb_create(stride);
    CHECK(m != NULL);

    prefix_t p;
    prefix_parse("0.0.0.0/0", &p);   CHECK(mb_insert(m, p, nh_iface(1)));
    prefix_parse("10.0.0.0/8", &p);  CHECK(mb_insert(m, p, nh_iface(2)));
    prefix_parse("10.1.0.0/16", &p); CHECK(mb_insert(m, p, nh_iface(3)));
    prefix_parse("10.1.2.0/24", &p); CHECK(mb_insert(m, p, nh_iface(4)));

    uint32_t a;
    ipv4_parse("10.1.2.5", &a); CHECK(mb_lookup(m, a).iface == 4);
    ipv4_parse("10.1.3.5", &a); CHECK(mb_lookup(m, a).iface == 3);
    ipv4_parse("10.2.0.1", &a); CHECK(mb_lookup(m, a).iface == 2);
    ipv4_parse("8.8.8.8",  &a); CHECK(mb_lookup(m, a).iface == 1);

    prefix_parse("10.1.2.0/24", &p); CHECK(mb_remove(m, p));
    ipv4_parse("10.1.2.5", &a); CHECK(mb_lookup(m, a).iface == 3);

    mb_destroy(m);
}

static void test_random(int stride) {
    multibit_t *m = mb_create(stride);
    oracle_t  *o = oracle_create();

    for (int i = 0; i < 6000; i++) {
        int op = rng() % 3;
        if (op < 2) {
            prefix_t p = rand_prefix();
            nexthop_t nh = nh_iface((int32_t)(rng() % 64));
            CHECK(mb_insert(m, p, nh) == oracle_insert(o, p, nh));
        } else {
            prefix_t p = rand_prefix();
            CHECK(mb_remove(m, p) == oracle_remove(o, p));
        }
        if (i % 11 == 0) {
            for (int k = 0; k < 6; k++) {
                uint32_t a = rng();
                CHECK_MSG(mb_lookup(m, a).iface == oracle_lookup(o, a).iface,
                          "stride=%d addr=%08x", stride, a);
            }
        }
    }
    CHECK_MSG(mb_count(m) == oracle_count(o), "count stride=%d", stride);

    for (int k = 0; k < 3000; k++) {
        uint32_t a = rng();
        CHECK(mb_lookup(m, a).iface == oracle_lookup(o, a).iface);
    }

    mb_destroy(m);
    oracle_destroy(o);
}

int main(void) {
    TEST_BEGIN("multibit");
    int strides[] = { 1, 2, 4, 8 };
    for (size_t i = 0; i < sizeof(strides) / sizeof(strides[0]); i++) {
        test_basic(strides[i]);
        test_random(strides[i]);
    }
    TEST_REPORT();
}
