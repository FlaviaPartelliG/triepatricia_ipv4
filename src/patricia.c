
#include "patricia.h"

#include <stdlib.h>
#include <stdatomic.h>

typedef struct { nexthop_t nh; } route_t;

typedef struct pnode {
    uint32_t key;                       
    uint8_t  bitlen;                  
    _Atomic(struct pnode *) child[2];   
    _Atomic(route_t *)      route;      
} pnode_t;

struct patricia {
    _Atomic(pnode_t *) root;
    patricia_retire_fn retire;
    void              *retire_ctx;
    size_t             nprefixes;
    size_t             nnodes;
};


static void retire_now(void *ctx, void *p) {
    (void)ctx;
    free(p);
}

static inline void retire(patricia_t *t, void *p) {
    t->retire(t->retire_ctx, p);
}

void patricia_set_reclaimer(patricia_t *t, patricia_retire_fn fn, void *ctx) {
    t->retire = fn;
    t->retire_ctx = ctx;
}


static inline bool bits_equal(uint32_t a, uint32_t b, uint8_t n) {
    if (n == 0) return true;
    return ipv4_common_prefix(a, b) >= n;
}

static inline pnode_t *child_get(const pnode_t *n, int b) {
    return atomic_load_explicit(&n->child[b], memory_order_acquire);
}

static pnode_t *node_new(patricia_t *t, uint32_t key, uint8_t bitlen) {
    pnode_t *n = malloc(sizeof(*n));
    if (!n) return NULL;
    n->key = key;
    n->bitlen = bitlen;
    atomic_init(&n->child[0], NULL);
    atomic_init(&n->child[1], NULL);
    atomic_init(&n->route, NULL);
    t->nnodes++;
    return n;
}

static route_t *route_new(nexthop_t nh) {
    route_t *r = malloc(sizeof(*r));
    if (!r) return NULL;
    r->nh = nh;
    return r;
}

static int child_count(const pnode_t *n) {
    int c = 0;
    if (child_get(n, 0)) c++;
    if (child_get(n, 1)) c++;
    return c;
}

patricia_t *patricia_create(void) {
    patricia_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    atomic_init(&t->root, NULL);
    t->retire = retire_now;
    t->retire_ctx = NULL;
    return t;
}

static void free_subtree(pnode_t *n) {
    if (!n) return;
    free_subtree(atomic_load_explicit(&n->child[0], memory_order_relaxed));
    free_subtree(atomic_load_explicit(&n->child[1], memory_order_relaxed));
    route_t *r = atomic_load_explicit(&n->route, memory_order_relaxed);
    free(r);
    free(n);
}

void patricia_destroy(patricia_t *t) {
    if (!t) return;
    free_subtree(atomic_load_explicit(&t->root, memory_order_relaxed));
    free(t);
}


static bool set_route(patricia_t *t, pnode_t *n, nexthop_t nh) {
    route_t *r = route_new(nh);
    if (!r) return false;
    route_t *old = atomic_exchange_explicit(&n->route, r, memory_order_release);
    if (old) retire(t, old);
    else     t->nprefixes++;
    return true;
}

bool patricia_insert(patricia_t *t, prefix_t p, nexthop_t nh) {
    _Atomic(pnode_t *) *link = &t->root;
    pnode_t *cur = atomic_load_explicit(link, memory_order_acquire);

    for (;;) {
        if (cur == NULL) {
            pnode_t *leaf = node_new(t, p.addr, p.len);
            if (!leaf) return false;
            if (!set_route(t, leaf, nh)) { free(leaf); t->nnodes--; return false; }
            atomic_store_explicit(link, leaf, memory_order_release);
            return true;
        }

        uint8_t match = (p.len < cur->bitlen) ? p.len : cur->bitlen;
        if (bits_equal(p.addr, cur->key, match)) {
            if (p.len == cur->bitlen) {
                return set_route(t, cur, nh);
            }
            if (p.len < cur->bitlen) {
                pnode_t *m = node_new(t, p.addr, p.len);
                if (!m) return false;
                if (!set_route(t, m, nh)) { free(m); t->nnodes--; return false; }
                int b = ipv4_bit_at(cur->key, p.len);
                atomic_store_explicit(&m->child[b], cur, memory_order_relaxed);
                atomic_store_explicit(link, m, memory_order_release);
                return true;
            }
            int b = ipv4_bit_at(p.addr, cur->bitlen);
            link = &cur->child[b];
            cur = atomic_load_explicit(link, memory_order_acquire);
            continue;
        }

        uint8_t d = ipv4_common_prefix(p.addr, cur->key);
        pnode_t *g = node_new(t, ipv4_network(p.addr, d), d);
        if (!g) return false;
        pnode_t *leaf = node_new(t, p.addr, p.len);
        if (!leaf) { free(g); t->nnodes--; return false; }
        if (!set_route(t, leaf, nh)) { free(g); free(leaf); t->nnodes -= 2; return false; }
        int bp = ipv4_bit_at(p.addr, d);
        atomic_store_explicit(&g->child[bp], leaf, memory_order_relaxed);
        atomic_store_explicit(&g->child[1 - bp], cur, memory_order_relaxed);
        atomic_store_explicit(link, g, memory_order_release);
        return true;
    }
}


bool patricia_remove(patricia_t *t, prefix_t p) {
    _Atomic(pnode_t *) *parent_link = NULL;
    _Atomic(pnode_t *) *link = &t->root;
    pnode_t *cur = atomic_load_explicit(link, memory_order_acquire);

    while (cur) {
        if (cur->bitlen == p.len && cur->key == p.addr) break;     
        if (cur->bitlen >= p.len) return false;                    
        if (!bits_equal(p.addr, cur->key, cur->bitlen)) return false;
        int b = ipv4_bit_at(p.addr, cur->bitlen);
        parent_link = link;
        link = &cur->child[b];
        cur = atomic_load_explicit(link, memory_order_acquire);
    }
    if (!cur) return false;

    route_t *r = atomic_load_explicit(&cur->route, memory_order_relaxed);
    if (!r) return false;   

    atomic_store_explicit(&cur->route, NULL, memory_order_release);
    retire(t, r);
    t->nprefixes--;

    int nc = child_count(cur);
    if (nc == 2) {
        return true;
    }
    if (nc == 1) {
        pnode_t *only = child_get(cur, 0);
        if (!only) only = child_get(cur, 1);
        atomic_store_explicit(link, only, memory_order_release);
        retire(t, cur);
        t->nnodes--;
        return true;
    }
    atomic_store_explicit(link, NULL, memory_order_release);
    retire(t, cur);
    t->nnodes--;

    if (parent_link) {
        pnode_t *pp = atomic_load_explicit(parent_link, memory_order_relaxed);
        if (pp && atomic_load_explicit(&pp->route, memory_order_relaxed) == NULL
            && child_count(pp) == 1) {
            pnode_t *only = child_get(pp, 0);
            if (!only) only = child_get(pp, 1);
            atomic_store_explicit(parent_link, only, memory_order_release);
            retire(t, pp);
            t->nnodes--;
        }
    }
    return true;
}


nexthop_t patricia_lookup(const patricia_t *t, uint32_t addr) {
    nexthop_t best = nexthop_none();
    pnode_t *cur = atomic_load_explicit(&t->root, memory_order_acquire);

    while (cur) {
        if (!bits_equal(addr, cur->key, cur->bitlen)) break;  
        route_t *r = atomic_load_explicit(&cur->route, memory_order_acquire);
        if (r) best = r->nh;                                  
        if (cur->bitlen == IPV4_BITS) break;
        int b = ipv4_bit_at(addr, cur->bitlen);
        cur = atomic_load_explicit(&cur->child[b], memory_order_acquire);
    }
    return best;
}


size_t patricia_count(const patricia_t *t) { return t->nprefixes; }
size_t patricia_nodes(const patricia_t *t) { return t->nnodes; }

size_t patricia_mem_bytes(const patricia_t *t) {
    return t->nnodes * sizeof(pnode_t) + t->nprefixes * sizeof(route_t);
}
