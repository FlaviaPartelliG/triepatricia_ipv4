/* lpm_oracle.c - brute-force LPM reference. */
#include "lpm_oracle.h"

#include <stdlib.h>

typedef struct {
    prefix_t  p;
    nexthop_t nh;
} entry_t;

struct oracle {
    entry_t *v;
    size_t   n;
    size_t   cap;
};

oracle_t *oracle_create(void) {
    oracle_t *o = calloc(1, sizeof(*o));
    return o;
}

void oracle_destroy(oracle_t *o) {
    if (!o) return;
    free(o->v);
    free(o);
}

static entry_t *find(oracle_t *o, prefix_t p) {
    for (size_t i = 0; i < o->n; i++)
        if (o->v[i].p.addr == p.addr && o->v[i].p.len == p.len)
            return &o->v[i];
    return NULL;
}

bool oracle_insert(oracle_t *o, prefix_t p, nexthop_t nh) {
    entry_t *e = find(o, p);
    if (e) { e->nh = nh; return true; }
    if (o->n == o->cap) {
        size_t ncap = o->cap ? o->cap * 2 : 16;
        entry_t *nv = realloc(o->v, ncap * sizeof(*nv));
        if (!nv) return false;
        o->v = nv;
        o->cap = ncap;
    }
    o->v[o->n].p = p;
    o->v[o->n].nh = nh;
    o->n++;
    return true;
}

bool oracle_remove(oracle_t *o, prefix_t p) {
    for (size_t i = 0; i < o->n; i++) {
        if (o->v[i].p.addr == p.addr && o->v[i].p.len == p.len) {
            o->v[i] = o->v[o->n - 1];
            o->n--;
            return true;
        }
    }
    return false;
}

nexthop_t oracle_lookup(const oracle_t *o, uint32_t addr) {
    int best_len = -1;
    nexthop_t best = nexthop_none();
    for (size_t i = 0; i < o->n; i++) {
        if (prefix_matches(o->v[i].p, addr) && (int)o->v[i].p.len > best_len) {
            best_len = (int)o->v[i].p.len;
            best = o->v[i].nh;
        }
    }
    return best;
}

size_t oracle_count(const oracle_t *o) {
    return o->n;
}
