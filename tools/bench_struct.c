/* bench_struct.c - single-thread comparison of the ED variants.
 *
 * Builds the same table in the Patricia trie and in multibit tries of several
 * strides, then measures lookup throughput and memory footprint. Emits CSV for
 * the Patricia-vs-multibit space/speed trade-off graph in the report.
 *
 * Usage: bench_struct [--table PATH] [--packets N] [--out CSV]
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L   /* clock_gettime */
#endif
#include "patricia.h"
#include "multibit.h"
#include "prefixgen.h"
#include "pktgen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static double bench_patricia(const route_entry_t *r, size_t n, unsigned long pk,
                             size_t *nodes, size_t *mem) {
    patricia_t *t = patricia_create();
    for (size_t i = 0; i < n; i++) patricia_insert(t, r[i].p, r[i].nh);
    *nodes = patricia_nodes(t);
    *mem = patricia_mem_bytes(t);

    pktgen_t g; pktgen_init(&g, r, n, 7, 0.8);
    volatile unsigned long sink = 0;
    double t0 = now_s();
    for (unsigned long i = 0; i < pk; i++)
        sink += (unsigned long)(patricia_lookup(t, pktgen_next(&g)).iface + 1);
    double dt = now_s() - t0;
    (void)sink;
    patricia_destroy(t);
    return pk / dt;
}

static double bench_multibit(int stride, const route_entry_t *r, size_t n,
                             unsigned long pk, size_t *nodes, size_t *mem) {
    multibit_t *m = mb_create(stride);
    for (size_t i = 0; i < n; i++) mb_insert(m, r[i].p, r[i].nh);
    *nodes = mb_nodes(m);
    *mem = mb_mem_bytes(m);

    pktgen_t g; pktgen_init(&g, r, n, 7, 0.8);
    volatile unsigned long sink = 0;
    double t0 = now_s();
    for (unsigned long i = 0; i < pk; i++)
        sink += (unsigned long)(mb_lookup(m, pktgen_next(&g)).iface + 1);
    double dt = now_s() - t0;
    (void)sink;
    mb_destroy(m);
    return pk / dt;
}

int main(int argc, char **argv) {
    const char *table = "data/prefixes.txt";
    const char *out = NULL;
    unsigned long packets = 5000000;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--table") && i + 1 < argc) table = argv[++i];
        else if (!strcmp(argv[i], "--packets") && i + 1 < argc) packets = strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else { fprintf(stderr, "unknown arg: %s\n", argv[i]); return 1; }
    }

    route_entry_t *routes = NULL;
    size_t n = prefixgen_load_file(table, &routes);
    if (n == 0) {
        fprintf(stderr, "could not load table '%s' (run `make gen` first)\n", table);
        return 1;
    }

    FILE *f = out ? fopen(out, "w") : NULL;
    const char *hdr = "structure,stride,prefixes,nodes,mem_kb,mlookups_per_sec";
    printf("%s\n", hdr);
    if (f) fprintf(f, "%s\n", hdr);

    size_t nodes, mem;
    double lps;

    lps = bench_patricia(routes, n, packets, &nodes, &mem);
    printf("patricia,1,%lu,%lu,%.1f,%.3f\n",
           (unsigned long)n, (unsigned long)nodes, mem / 1024.0, lps / 1e6);
    if (f) fprintf(f, "patricia,1,%lu,%lu,%.1f,%.3f\n",
                   (unsigned long)n, (unsigned long)nodes, mem / 1024.0, lps / 1e6);

    int strides[] = { 4, 8 };
    for (size_t s = 0; s < sizeof(strides) / sizeof(strides[0]); s++) {
        lps = bench_multibit(strides[s], routes, n, packets, &nodes, &mem);
        printf("multibit,%d,%lu,%lu,%.1f,%.3f\n",
               strides[s], (unsigned long)n, (unsigned long)nodes, mem / 1024.0, lps / 1e6);
        if (f) fprintf(f, "multibit,%d,%lu,%lu,%.1f,%.3f\n",
                       strides[s], (unsigned long)n, (unsigned long)nodes, mem / 1024.0, lps / 1e6);
    }

    if (f) fclose(f);
    free(routes);
    return 0;
}
