/* bench.c - throughput benchmark: RCU vs rwlock, scaling reader threads.
 *
 * For each backend and each reader-thread count, runs the readers for a fixed
 * interval (optionally alongside a writer churning routes) and reports the
 * aggregate lookups/second. Emits CSV so scripts/plot.py can draw the report
 * graphs: throughput vs threads (the RCU-vs-mutex scalability comparison).
 *
 * Usage:
 *   bench [--table PATH] [--threads "1,2,4,8,16"] [--seconds S]
 *         [--writer on|off] [--out CSV] [--hit R]
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L   /* clock_gettime, nanosleep */
#endif
#include "router.h"
#include "prefixgen.h"
#include "pktgen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct {
    router_t            *r;
    const route_entry_t *routes;
    size_t               n;
    unsigned             seed;
    double               hit;
    _Atomic int         *done;
    unsigned long        ops;
} rd_arg_t;

static void *rd_fn(void *arg) {
    rd_arg_t *a = arg;
    int id = router_reader_register(a->r);
    pktgen_t g;
    pktgen_init(&g, a->routes, a->n, a->seed, a->hit);
    unsigned long ops = 0;
    volatile unsigned long sink = 0;
    while (!atomic_load_explicit(a->done, memory_order_acquire)) {
        for (int i = 0; i < 512; i++) {
            uint32_t dst = pktgen_next(&g);
            sink += (unsigned long)(router_lookup(a->r, id, dst).iface + 1);
            ops++;
        }
    }
    (void)sink;
    router_reader_unregister(a->r, id);
    a->ops = ops;
    return NULL;
}

typedef struct {
    router_t            *r;
    const route_entry_t *pool;
    size_t               n;
    _Atomic int         *done;
} wr_arg_t;

static void *wr_fn(void *arg) {
    wr_arg_t *a = arg;
    bool *present = calloc(a->n, sizeof(bool));
    for (size_t i = 0; i < a->n; i++) present[i] = true;
    uint32_t s = 0x1234u;
    unsigned long k = 0;
    while (!atomic_load_explicit(a->done, memory_order_acquire)) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        size_t i = s % a->n;
        if (present[i]) { router_remove(a->r, a->pool[i].p); present[i] = false; }
        else            { router_insert(a->r, a->pool[i].p, a->pool[i].nh); present[i] = true; }
        if ((++k & 511) == 0) router_writer_sync(a->r);
    }
    router_writer_sync(a->r);
    free(present);
    return NULL;
}

static double run_one(router_mode_t mode, const route_entry_t *routes, size_t n,
                      int threads, double seconds, int with_writer, double hit) {
    router_t *r = router_create(mode);
    for (size_t i = 0; i < n; i++) router_insert(r, routes[i].p, routes[i].nh);

    _Atomic int done = 0;
    pthread_t th[64];
    rd_arg_t  ra[64];
    for (int i = 0; i < threads; i++) {
        ra[i] = (rd_arg_t){ r, routes, n, (unsigned)(i * 2654435761u + 1),
                            hit, &done, 0 };
        pthread_create(&th[i], NULL, rd_fn, &ra[i]);
    }
    pthread_t wt;
    wr_arg_t wa = { r, routes, n, &done };
    if (with_writer) pthread_create(&wt, NULL, wr_fn, &wa);

    double t0 = now_s();
    while (now_s() - t0 < seconds) {
        struct timespec nap = { 0, 10 * 1000 * 1000 };
        nanosleep(&nap, NULL);
    }
    double elapsed = now_s() - t0;
    atomic_store_explicit(&done, 1, memory_order_release);

    unsigned long total = 0;
    for (int i = 0; i < threads; i++) { pthread_join(th[i], NULL); total += ra[i].ops; }
    if (with_writer) pthread_join(wt, NULL);
    router_writer_sync(r);
    router_destroy(r);

    return total / elapsed;   /* lookups per second */
}

int main(int argc, char **argv) {
    const char *table = "data/prefixes.txt";
    const char *thread_list = "1,2,4,8,16";
    const char *out = NULL;
    double seconds = 2.0, hit = 0.8;
    int with_writer = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--table") && i + 1 < argc) table = argv[++i];
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) thread_list = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--writer") && i + 1 < argc) with_writer = strcmp(argv[++i], "off") != 0;
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--hit") && i + 1 < argc) hit = atof(argv[++i]);
        else { fprintf(stderr, "unknown arg: %s\n", argv[i]); return 1; }
    }

    route_entry_t *routes = NULL;
    size_t n = prefixgen_load_file(table, &routes);
    if (n == 0) {
        fprintf(stderr, "could not load table '%s' (run `make gen` first)\n", table);
        return 1;
    }

    int counts[32]; int nc = 0;
    { char tmp[128]; strncpy(tmp, thread_list, sizeof(tmp) - 1); tmp[sizeof(tmp)-1] = 0;
      for (char *tok = strtok(tmp, ","); tok && nc < 32; tok = strtok(NULL, ","))
          counts[nc++] = atoi(tok); }

    FILE *f = out ? fopen(out, "w") : NULL;
    fprintf(stderr, "Benchmark: %lu routes, %.1fs/point, writer=%s, hit=%.2f\n",
            (unsigned long)n, seconds, with_writer ? "on" : "off", hit);
    printf("backend,threads,writer,lookups_per_sec,mlookups_per_sec\n");
    if (f) fprintf(f, "backend,threads,writer,lookups_per_sec,mlookups_per_sec\n");

    router_mode_t modes[] = { ROUTER_RCU, ROUTER_RWLOCK };
    const char *names[] = { "rcu", "rwlock" };
    for (int m = 0; m < 2; m++) {
        for (int c = 0; c < nc; c++) {
            int t = counts[c];
            if (t < 1) continue;
            if (t > 64) t = 64;
            double lps = run_one(modes[m], routes, n, t, seconds, with_writer, hit);
            printf("%s,%d,%d,%.0f,%.3f\n", names[m], t, with_writer, lps, lps / 1e6);
            if (f) fprintf(f, "%s,%d,%d,%.0f,%.3f\n", names[m], t, with_writer, lps, lps / 1e6);
            fflush(stdout);
        }
    }

    if (f) fclose(f);
    free(routes);
    return 0;
}
