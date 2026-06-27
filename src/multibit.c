/* multibit.c - fixed-stride multibit trie with controlled prefix expansion.
 *
 * Each level consumes `stride` bits. A node is an array of 2^stride entries;
 * each entry holds the best (longest) route that terminates at this level for
 * that index, plus an optional child for longer prefixes. Lookups walk down,
 * letting deeper (longer) entries override shallower ones.
 *
 * Inserts expand a prefix across the entries it covers, keeping the longest on
 * overlap. Removes are rare and correctness-critical with expansion, so they
 * rebuild the trie from the authoritative prefix list - simple and always
 * correct, which matters since the trie is validated against the oracle.
 */
#include "multibit.h"

#include <stdlib.h>
#include <string.h>

typedef struct mb_node mb_node_t;

typedef struct {
    nexthop_t  nh;
    uint8_t    len;        /* length of the prefix owning this entry */
    bool       has_route;
    mb_node_t *child;
} mb_entry_t;

struct mb_node {
    mb_entry_t *entry;     /* 2^stride entries */
};

typedef struct {
    prefix_t  p;
    nexthop_t nh;
} pfx_t;

struct multibit {
    int        stride;
    int        levels;     /* 32 / stride */
    int        fanout;     /* 1 << stride */
    mb_node_t *root;
    nexthop_t  dflt;       /* default route (/0) */
    bool       has_dflt;
    pfx_t     *list;       /* authoritative prefix set (for rebuild) */
    size_t     n;
    size_t     cap;
    size_t     nnodes;
};

static mb_node_t *node_new(multibit_t *m) {
    mb_node_t *n = malloc(sizeof(*n));
    if (!n) return NULL;
    n->entry = calloc((size_t)m->fanout, sizeof(mb_entry_t));
    if (!n->entry) { free(n); return NULL; }
    m->nnodes++;
    return n;
}

static void node_free(multibit_t *m, mb_node_t *n) {
    if (!n) return;
    for (int i = 0; i < m->fanout; i++)
        node_free(m, n->entry[i].child);
    free(n->entry);
    free(n);
    m->nnodes--;
}

multibit_t *mb_create(int stride) {
    if (stride != 1 && stride != 2 && stride != 4 && stride != 8) return NULL;
    multibit_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->stride = stride;
    m->levels = IPV4_BITS / stride;
    m->fanout = 1 << stride;
    m->root = node_new(m);
    if (!m->root) { free(m); return NULL; }
    return m;
}

void mb_destroy(multibit_t *m) {
    if (!m) return;
    node_free(m, m->root);
    free(m->list);
    free(m);
}

/* the `level`-th stride-bit chunk of addr, as an integer index */
static inline unsigned chunk(const multibit_t *m, uint32_t addr, int level) {
    int shift = IPV4_BITS - (level + 1) * m->stride;
    return (addr >> shift) & (unsigned)(m->fanout - 1);
}

/* install one prefix into the trie structure (no list bookkeeping) */
static bool trie_install(multibit_t *m, prefix_t p, nexthop_t nh) {
    if (p.len == 0) { m->dflt = nh; m->has_dflt = true; return true; }

    int d = (p.len - 1) / m->stride;        /* level where the prefix ends */
    mb_node_t *node = m->root;
    for (int j = 0; j < d; j++) {
        unsigned idx = chunk(m, p.addr, j);
        if (!node->entry[idx].child) {
            mb_node_t *c = node_new(m);
            if (!c) return false;
            node->entry[idx].child = c;
        }
        node = node->entry[idx].child;
    }

    int r = p.len - d * m->stride;          /* significant bits at level d, 1..stride */
    unsigned top = chunk(m, p.addr, d) >> (m->stride - r);
    unsigned base = top << (m->stride - r);
    unsigned span = 1u << (m->stride - r);
    for (unsigned f = 0; f < span; f++) {
        mb_entry_t *e = &node->entry[base + f];
        if (!e->has_route || p.len >= e->len) {
            e->has_route = true;
            e->len = p.len;
            e->nh = nh;
        }
    }
    return true;
}

static void trie_clear(multibit_t *m) {
    node_free(m, m->root);
    m->root = node_new(m);
    m->has_dflt = false;
}

static bool rebuild(multibit_t *m) {
    trie_clear(m);
    if (!m->root) return false;
    for (size_t i = 0; i < m->n; i++)
        if (!trie_install(m, m->list[i].p, m->list[i].nh)) return false;
    return true;
}

static pfx_t *list_find(multibit_t *m, prefix_t p) {
    for (size_t i = 0; i < m->n; i++)
        if (m->list[i].p.addr == p.addr && m->list[i].p.len == p.len)
            return &m->list[i];
    return NULL;
}

bool mb_insert(multibit_t *m, prefix_t p, nexthop_t nh) {
    pfx_t *e = list_find(m, p);
    if (e) {                       /* update existing */
        e->nh = nh;
        return rebuild(m);
    }
    if (m->n == m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 64;
        pfx_t *nl = realloc(m->list, nc * sizeof(*nl));
        if (!nl) return false;
        m->list = nl;
        m->cap = nc;
    }
    m->list[m->n].p = p;
    m->list[m->n].nh = nh;
    m->n++;
    return trie_install(m, p, nh);   /* incremental: expansion handles longest-wins */
}

bool mb_remove(multibit_t *m, prefix_t p) {
    for (size_t i = 0; i < m->n; i++) {
        if (m->list[i].p.addr == p.addr && m->list[i].p.len == p.len) {
            m->list[i] = m->list[m->n - 1];
            m->n--;
            return rebuild(m);     /* expansion makes in-place removal hard */
        }
    }
    return false;
}

nexthop_t mb_lookup(const multibit_t *m, uint32_t addr) {
    nexthop_t best = m->has_dflt ? m->dflt : nexthop_none();
    mb_node_t *node = m->root;
    for (int level = 0; level < m->levels && node; level++) {
        unsigned idx = chunk(m, addr, level);
        mb_entry_t *e = &node->entry[idx];
        if (e->has_route) best = e->nh;   /* deeper overrides */
        node = e->child;
    }
    return best;
}

int    mb_stride(const multibit_t *m) { return m->stride; }
size_t mb_count(const multibit_t *m)  { return m->n; }   /* default route is in the list */
size_t mb_nodes(const multibit_t *m)  { return m->nnodes; }

size_t mb_mem_bytes(const multibit_t *m) {
    return m->nnodes * (sizeof(mb_node_t) + (size_t)m->fanout * sizeof(mb_entry_t));
}
