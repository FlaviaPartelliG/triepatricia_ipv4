/* prefixgen.c - BGP-like prefix table generator. */
#include "prefixgen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Prefix-length histogram approximating a real IPv4 BGP table. Weights are
 * relative; only their ratios matter. /24 dominates, as in the wild. */
static const struct { uint8_t len; unsigned w; } LEN_HIST[] = {
    {  8,   3 }, {  9,   1 }, { 10,   2 }, { 11,   3 }, { 12,   6 },
    { 13,   7 }, { 14,  12 }, { 15,  14 }, { 16,  60 }, { 17,  18 },
    { 18,  22 }, { 19,  45 }, { 20,  55 }, { 21,  60 }, { 22,  90 },
    { 23,  75 }, { 24, 560 }, { 25,   4 }, { 26,   4 }, { 27,   3 },
    { 28,   3 }, { 29,   2 }, { 30,   1 }, { 32,   1 },
};
#define LEN_HIST_N (sizeof(LEN_HIST) / sizeof(LEN_HIST[0]))

typedef struct { uint32_t s; } rng_t;
static uint32_t rng_next(rng_t *r) {
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return r->s = x ? x : 0x1234567u;
}

static uint8_t pick_len(rng_t *r, unsigned total) {
    unsigned k = rng_next(r) % total;
    unsigned acc = 0;
    for (size_t i = 0; i < LEN_HIST_N; i++) {
        acc += LEN_HIST[i].w;
        if (k < acc) return LEN_HIST[i].len;
    }
    return 24;
}

/* ---- small open-addressing set of (addr,len) for dedup ------------------ */

typedef struct { uint64_t *slot; size_t cap; } keyset_t;

static uint64_t key_of(prefix_t p) {
    return ((uint64_t)p.addr << 8) | (uint64_t)p.len;
}

static bool keyset_init(keyset_t *s, size_t want) {
    size_t cap = 16;
    while (cap < want * 2) cap <<= 1;
    s->slot = calloc(cap, sizeof(uint64_t));   /* 0 == empty (key has len>=8) */
    if (!s->slot) return false;
    s->cap = cap;
    return true;
}

/* returns true if newly inserted, false if already present */
static bool keyset_add(keyset_t *s, uint64_t key) {
    size_t mask = s->cap - 1;
    size_t i = (size_t)(key * 0x9E3779B97F4A7C15ull >> 32) & mask;
    while (s->slot[i]) {
        if (s->slot[i] == key) return false;
        i = (i + 1) & mask;
    }
    s->slot[i] = key;
    return true;
}

static void keyset_free(keyset_t *s) { free(s->slot); }

/* ------------------------------------------------------------------------ */

size_t prefixgen_generate(route_entry_t *out, size_t n, unsigned seed, int n_ifaces) {
    if (n == 0 || n_ifaces < 1) return 0;

    unsigned total = 0;
    for (size_t i = 0; i < LEN_HIST_N; i++) total += LEN_HIST[i].w;

    keyset_t seen;
    if (!keyset_init(&seen, n)) return 0;

    rng_t r = { seed ? seed : 0xDEADBEEFu };
    size_t produced = 0;
    size_t guard = 0, guard_max = n * 64 + 1024;

    while (produced < n && guard++ < guard_max) {
        prefix_t p;
        p.len = pick_len(&r, total);
        uint32_t raw = (rng_next(&r) << 1) ^ rng_next(&r);
        p.addr = ipv4_network(raw, p.len);
        if (p.addr == 0 && p.len < 8) continue;          /* skip 0.0.0.0/x noise */
        if (!keyset_add(&seen, key_of(p))) continue;     /* duplicate */

        out[produced].p = p;
        out[produced].nh.iface = (int32_t)(1 + (int)(rng_next(&r) % (unsigned)n_ifaces));
        out[produced].nh.gateway = rng_next(&r);
        produced++;
    }

    keyset_free(&seen);
    return produced;
}

bool prefixgen_write_file(const char *path, size_t n, unsigned seed, int n_ifaces) {
    route_entry_t *v = malloc(n * sizeof(*v));
    if (!v) return false;
    size_t got = prefixgen_generate(v, n, seed, n_ifaces);
    if (got != n) { free(v); return false; }

    FILE *f = fopen(path, "w");
    if (!f) { free(v); return false; }

    fprintf(f, "# Tema 6 synthetic BGP-like routing table\n");
    fprintf(f, "# format: <prefix/len> <iface>\n");
    char buf[32];
    for (size_t i = 0; i < got; i++) {
        prefix_format(v[i].p, buf, sizeof(buf));
        fprintf(f, "%s %ld\n", buf, (long)v[i].nh.iface);
    }
    fclose(f);
    free(v);
    return true;
}

size_t prefixgen_load_file(const char *path, route_entry_t **out) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    size_t cap = 1024, n = 0;
    route_entry_t *v = malloc(cap * sizeof(*v));
    if (!v) { fclose(f); return 0; }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') continue;
        char pfx[64];
        long iface;
        if (sscanf(line, "%63s %ld", pfx, &iface) != 2) continue;

        prefix_t p;
        if (!prefix_parse(pfx, &p)) continue;

        if (n == cap) {
            cap *= 2;
            route_entry_t *nv = realloc(v, cap * sizeof(*v));
            if (!nv) { free(v); fclose(f); return 0; }
            v = nv;
        }
        v[n].p = p;
        v[n].nh.iface = (int32_t)iface;
        v[n].nh.gateway = 0;
        n++;
    }
    fclose(f);
    *out = v;
    return n;
}
