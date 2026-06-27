/* router_sim.c - routing table simulator / forwarder.
 *
 * Loads a routing table (the programmatically generated data/prefixes.txt by
 * default), forwards a stream of synthetic packets through the LPM trie, and
 * reports the egress interface decisions. This is the user-facing demo of the
 * Tema 6 data plane.
 *
 * Usage:
 *   router_sim [--table PATH] [--packets N] [--backend rcu|rwlock]
 *              [--threads T] [--hit R] [--show K]
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L   /* clock_gettime */
#endif
#include "router.h"
#include "prefixgen.h"
#include "pktgen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

typedef struct {
    router_t            *r;
    const route_entry_t *routes;
    size_t               nroutes;
    unsigned             seed;
    double               hit;
    unsigned long        packets;
    unsigned long        hits;       /* matched a route */
    unsigned long        misses;     /* no route */
    unsigned long        iface_sum;
} fwd_arg_t;

static void *fwd_fn(void *arg) {
    fwd_arg_t *a = arg;
    int id = router_reader_register(a->r);
    pktgen_t g;
    pktgen_init(&g, a->routes, a->nroutes, a->seed, a->hit);
    unsigned long hits = 0, miss = 0, sum = 0;
    for (unsigned long i = 0; i < a->packets; i++) {
        uint32_t dst = pktgen_next(&g);
        nexthop_t nh = router_lookup(a->r, id, dst);
        if (nh.iface >= 0) { hits++; sum += (unsigned long)nh.iface; }
        else miss++;
    }
    router_reader_unregister(a->r, id);
    a->hits = hits; a->misses = miss; a->iface_sum = sum;
    return NULL;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    const char *table = "data/prefixes.txt";
    unsigned long packets = 2000000;
    router_mode_t mode = ROUTER_RCU;
    int threads = 1;
    double hit = 0.8;
    long show = 10;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--table") && i + 1 < argc) table = argv[++i];
        else if (!strcmp(argv[i], "--packets") && i + 1 < argc) packets = strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--backend") && i + 1 < argc)
            mode = strcmp(argv[++i], "rwlock") ? ROUTER_RCU : ROUTER_RWLOCK;
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hit") && i + 1 < argc) hit = atof(argv[++i]);
        else if (!strcmp(argv[i], "--show") && i + 1 < argc) show = atol(argv[++i]);
        else { fprintf(stderr, "unknown arg: %s\n", argv[i]); return 1; }
    }
    if (threads < 1) threads = 1;
    if (threads > 64) threads = 64;

    route_entry_t *routes = NULL;
    size_t n = prefixgen_load_file(table, &routes);
    if (n == 0) {
        fprintf(stderr, "could not load table '%s' (run `make gen` first)\n", table);
        return 1;
    }

    router_t *r = router_create(mode);
    for (size_t i = 0; i < n; i++) router_insert(r, routes[i].p, routes[i].nh);

    printf("Loaded %lu routes from %s (backend=%s, nodes=%lu, mem=%.1f KB)\n",
           (unsigned long)n, table, router_mode_name(r),
           (unsigned long)router_nodes(r), router_mem(r) / 1024.0);

    /* show a few forwarding decisions */
    if (show > 0) {
        pktgen_t g;
        pktgen_init(&g, routes, n, 12345, hit);
        char ab[16];
        printf("Sample forwarding decisions:\n");
        for (long k = 0; k < show; k++) {
            uint32_t dst = pktgen_next(&g);
            nexthop_t nh = router_lookup(r, -1, dst);
            ipv4_format(dst, ab, sizeof(ab));
            if (nh.iface >= 0) printf("  %-15s -> iface %ld\n", ab, (long)nh.iface);
            else               printf("  %-15s -> DROP (no route)\n", ab);
        }
    }

    /* forward the full stream, possibly across threads */
    pthread_t th[64];
    fwd_arg_t fa[64];
    unsigned long per = packets / (unsigned long)threads;
    double t0 = now_s();
    for (int i = 0; i < threads; i++) {
        fa[i] = (fwd_arg_t){ r, routes, n, (unsigned)(i * 2246822519u + 1),
                             hit, per, 0, 0, 0 };
        pthread_create(&th[i], NULL, fwd_fn, &fa[i]);
    }
    unsigned long hits = 0, miss = 0;
    for (int i = 0; i < threads; i++) {
        pthread_join(th[i], NULL);
        hits += fa[i].hits; miss += fa[i].misses;
    }
    double dt = now_s() - t0;
    unsigned long total = hits + miss;

    printf("Forwarded %lu packets in %.3fs with %d thread(s)\n", total, dt, threads);
    printf("  hits=%lu misses=%lu  throughput=%.2f Mpps\n",
           hits, miss, total / dt / 1e6);

    router_destroy(r);
    free(routes);
    return 0;
}
