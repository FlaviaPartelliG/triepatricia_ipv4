
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L   /* clock_gettime, nanosleep */
#endif
#include "router.h"
#include "prefixgen.h"
#include "pktgen.h"
#include "lpm_oracle.h"
#include "test_util.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

typedef struct {
    router_t           *r;
    const route_entry_t *routes;
    size_t              nroutes;
    unsigned            seed;
    int                 max_iface;
    _Atomic int        *done;
    unsigned long       ops;
    unsigned long       checksum;
    int                 errors;
} reader_arg_t;

static void *reader_fn(void *arg) {
    reader_arg_t *a = arg;
    int id = router_reader_register(a->r);
    pktgen_t g;
    pktgen_init(&g, a->routes, a->nroutes, a->seed, 0.7);

    unsigned long ops = 0, sum = 0;
    int errs = 0;
    while (!atomic_load_explicit(a->done, memory_order_acquire)) {
        for (int i = 0; i < 256; i++) {
            uint32_t addr = pktgen_next(&g);
            nexthop_t nh = router_lookup(a->r, id, addr);
            if (nh.iface < -1 || nh.iface > a->max_iface) errs++;
            sum += (unsigned long)(nh.iface + 1);
            ops++;
        }
    }
    router_reader_unregister(a->r, id);
    a->ops = ops;
    a->checksum = sum;
    a->errors = errs;
    return NULL;
}

/* ---- Phase A: correctness under concurrency ---------------------------- */

typedef struct {
    router_t            *r;
    oracle_t            *oracle;     /* writer-owned mirror */
    const route_entry_t *vol;        /* volatile prefix pool */
    size_t               nvol;
    unsigned long        churn;
} writerA_arg_t;

static void *writerA_fn(void *arg) {
    writerA_arg_t *a = arg;
    bool *present = calloc(a->nvol, sizeof(bool));
    uint32_t s = 0x5151u;
    for (unsigned long k = 0; k < a->churn; k++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        size_t i = s % a->nvol;
        if (!present[i]) {
            router_insert(a->r, a->vol[i].p, a->vol[i].nh);
            oracle_insert(a->oracle, a->vol[i].p, a->vol[i].nh);
            present[i] = true;
        } else {
            router_remove(a->r, a->vol[i].p);
            oracle_remove(a->oracle, a->vol[i].p);
            present[i] = false;
        }
        if ((k & 255) == 0) router_writer_sync(a->r);
    }
    router_writer_sync(a->r);
    free(present);
    return NULL;
}

static void phase_A(size_t n0, int readers, unsigned long churn) {
    printf("Phase A: %lu base routes, %d readers, %lu churn ops\n",
           (unsigned long)n0, readers, churn);

    size_t nvol = n0;   /* volatile pool same size as base */
    route_entry_t *base = malloc(n0 * sizeof(*base));
    route_entry_t *vol  = malloc(nvol * sizeof(*vol));
    CHECK(base && vol);
    CHECK(prefixgen_generate(base, n0, 11, 200) == n0);
    CHECK(prefixgen_generate(vol, nvol, 999, 200) == nvol);

    router_t *r = router_create(ROUTER_RCU);
    oracle_t *o = oracle_create();
    for (size_t i = 0; i < n0; i++) {
        router_insert(r, base[i].p, base[i].nh);
        oracle_insert(o, base[i].p, base[i].nh);
    }

    _Atomic int done = 0;
    pthread_t rt[64];
    reader_arg_t ra[64];
    if (readers > 64) readers = 64;
    for (int i = 0; i < readers; i++) {
        ra[i] = (reader_arg_t){ r, base, n0, (unsigned)(i * 2654435761u + 1),
                                200, &done, 0, 0, 0 };
        pthread_create(&rt[i], NULL, reader_fn, &ra[i]);
    }

    pthread_t wt;
    writerA_arg_t wa = { r, o, vol, nvol, churn };
    pthread_create(&wt, NULL, writerA_fn, &wa);
    pthread_join(wt, NULL);

    atomic_store_explicit(&done, 1, memory_order_release);
    unsigned long total_ops = 0;
    int total_errs = 0;
    for (int i = 0; i < readers; i++) {
        pthread_join(rt[i], NULL);
        total_ops += ra[i].ops;
        total_errs += ra[i].errors;
    }
    router_writer_sync(r);

    CHECK_MSG(total_errs == 0, "readers saw %d out-of-range results", total_errs);

    /* final exact comparison against the oracle */
    int mism = 0;
    uint32_t s = 0xC1A0u;
    for (int k = 0; k < 8000; k++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        uint32_t addr = s;
        if (router_lookup(r, -1, addr).iface != oracle_lookup(o, addr).iface) mism++;
    }
    CHECK_MSG(mism == 0, "final trie diverged from oracle in %d/8000 lookups", mism);
    CHECK(router_count(r) == oracle_count(o));

    printf("  readers performed %lu lookups; final state matches oracle\n", total_ops);

    router_destroy(r);
    oracle_destroy(o);
    free(base);
    free(vol);
}

/* ---- Phase B: test of fire (scale) ------------------------------------- */

typedef struct {
    router_t            *r;
    const route_entry_t *pool;
    size_t               npool;
    _Atomic int         *done;
    unsigned long        ops;
} writerB_arg_t;

static void *writerB_fn(void *arg) {
    writerB_arg_t *a = arg;
    bool *present = calloc(a->npool, sizeof(bool));
    for (size_t i = 0; i < a->npool; i++) present[i] = true;  /* all loaded */
    uint32_t s = 0x77f1u;
    unsigned long ops = 0;
    while (!atomic_load_explicit(a->done, memory_order_acquire)) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        size_t i = s % a->npool;
        if (present[i]) { router_remove(a->r, a->pool[i].p); present[i] = false; }
        else            { router_insert(a->r, a->pool[i].p, a->pool[i].nh); present[i] = true; }
        if ((++ops & 511) == 0) router_writer_sync(a->r);
    }
    router_writer_sync(a->r);
    free(present);
    a->ops = ops;
    return NULL;
}

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void phase_B(size_t n0, int readers, long run_ms) {
    printf("Phase B (test of fire): %lu prefixes, %d readers + 1 writer, %ld ms\n",
           (unsigned long)n0, readers, run_ms);

    route_entry_t *pool = malloc(n0 * sizeof(*pool));
    CHECK(pool != NULL);
    CHECK(prefixgen_generate(pool, n0, 1, 256) == n0);

    router_t *r = router_create(ROUTER_RCU);
    for (size_t i = 0; i < n0; i++) router_insert(r, pool[i].p, pool[i].nh);
    CHECK(router_count(r) == n0);

    _Atomic int done = 0;
    pthread_t rt[64];
    reader_arg_t ra[64];
    if (readers > 64) readers = 64;
    for (int i = 0; i < readers; i++) {
        ra[i] = (reader_arg_t){ r, pool, n0, (unsigned)(i * 40503u + 7),
                                256, &done, 0, 0, 0 };
        pthread_create(&rt[i], NULL, reader_fn, &ra[i]);
    }
    pthread_t wt;
    writerB_arg_t wb = { r, pool, n0, &done, 0 };
    pthread_create(&wt, NULL, writerB_fn, &wb);

    long t0 = now_ms();
    while (now_ms() - t0 < run_ms) {
        struct timespec nap = { 0, 20 * 1000 * 1000 };  /* 20 ms */
        nanosleep(&nap, NULL);
    }
    atomic_store_explicit(&done, 1, memory_order_release);

    unsigned long total = 0;
    int errs = 0;
    for (int i = 0; i < readers; i++) {
        pthread_join(rt[i], NULL);
        total += ra[i].ops;
        errs += ra[i].errors;
    }
    pthread_join(wt, NULL);
    router_writer_sync(r);

    CHECK_MSG(errs == 0, "readers saw %d out-of-range results", errs);
    double secs = (double)run_ms / 1000.0;
    printf("  %lu lookups in %.2fs => %.2f Mlookups/s aggregate; writer did %lu ops\n",
           total, secs, total / secs / 1e6, wb.ops);
    printf("  pending deferred at end: %lu\n", (unsigned long)router_pending(r));

    router_destroy(r);
    free(pool);
}

int main(int argc, char **argv) {
    TEST_BEGIN("concurrent");

    size_t n0       = (argc > 1) ? strtoul(argv[1], NULL, 10) : 2000;
    int    readers  = (argc > 2) ? atoi(argv[2]) : 8;
    unsigned long churn = (argc > 3) ? strtoul(argv[3], NULL, 10) : 40000;
    size_t scale_n0 = (argc > 4) ? strtoul(argv[4], NULL, 10) : 100000;
    long   scale_ms = (argc > 5) ? atol(argv[5]) : 800;

    phase_A(n0, readers, churn);
    phase_B(scale_n0, readers, scale_ms);

    TEST_REPORT();
}
