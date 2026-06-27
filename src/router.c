/* router.c - facade with RCU (lock-free read) and rwlock-baseline backends. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L   /* pthread_rwlock_* */
#endif
#include "router.h"
#include "patricia.h"
#include "rcu.h"

#include <stdlib.h>
#include <pthread.h>

struct router {
    router_mode_t   mode;
    patricia_t     *trie;

    /* RCU backend */
    rcu_t          *rcu;
    pthread_mutex_t wlock;     /* serializes writers (readers never take it) */

    /* rwlock backend */
    pthread_rwlock_t rwlock;
};

/* deferred-free hook handed to the trie in RCU mode */
static void router_defer(void *ctx, void *p) {
    rcu_t *rcu = (rcu_t *)ctx;
    rcu_defer(rcu, p, free);
}

router_t *router_create(router_mode_t mode) {
    router_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->mode = mode;
    r->trie = patricia_create();
    if (!r->trie) { free(r); return NULL; }

    if (mode == ROUTER_RCU) {
        r->rcu = rcu_create();
        if (!r->rcu) { patricia_destroy(r->trie); free(r); return NULL; }
        pthread_mutex_init(&r->wlock, NULL);
        patricia_set_reclaimer(r->trie, router_defer, r->rcu);
    } else {
        pthread_rwlock_init(&r->rwlock, NULL);
    }
    return r;
}

void router_destroy(router_t *r) {
    if (!r) return;
    if (r->mode == ROUTER_RCU) {
        rcu_reclaim(r->rcu);       /* flush any deferred frees */
        patricia_destroy(r->trie); /* frees everything still in the trie */
        rcu_destroy(r->rcu);
        pthread_mutex_destroy(&r->wlock);
    } else {
        patricia_destroy(r->trie);
        pthread_rwlock_destroy(&r->rwlock);
    }
    free(r);
}

int router_reader_register(router_t *r) {
    if (r->mode == ROUTER_RCU) return rcu_register(r->rcu);
    return -1;   /* rwlock backend needs no per-reader state */
}

void router_reader_unregister(router_t *r, int id) {
    if (r->mode == ROUTER_RCU) rcu_unregister(r->rcu, id);
}

nexthop_t router_lookup(router_t *r, int reader_id, uint32_t addr) {
    if (r->mode == ROUTER_RCU) {
        nexthop_t nh = patricia_lookup(r->trie, addr);
        rcu_quiescent_state(r->rcu, reader_id);
        return nh;
    }
    pthread_rwlock_rdlock(&r->rwlock);
    nexthop_t nh = patricia_lookup(r->trie, addr);
    pthread_rwlock_unlock(&r->rwlock);
    return nh;
}

bool router_insert(router_t *r, prefix_t p, nexthop_t nh) {
    bool ok;
    if (r->mode == ROUTER_RCU) {
        pthread_mutex_lock(&r->wlock);
        ok = patricia_insert(r->trie, p, nh);
        pthread_mutex_unlock(&r->wlock);
    } else {
        pthread_rwlock_wrlock(&r->rwlock);
        ok = patricia_insert(r->trie, p, nh);
        pthread_rwlock_unlock(&r->rwlock);
    }
    return ok;
}

bool router_remove(router_t *r, prefix_t p) {
    bool ok;
    if (r->mode == ROUTER_RCU) {
        pthread_mutex_lock(&r->wlock);
        ok = patricia_remove(r->trie, p);
        pthread_mutex_unlock(&r->wlock);
    } else {
        pthread_rwlock_wrlock(&r->rwlock);
        ok = patricia_remove(r->trie, p);
        pthread_rwlock_unlock(&r->rwlock);
    }
    return ok;
}

void router_writer_sync(router_t *r) {
    if (r->mode == ROUTER_RCU) rcu_reclaim(r->rcu);
}

router_mode_t router_mode(const router_t *r) { return r->mode; }
size_t router_count(const router_t *r) { return patricia_count(r->trie); }
size_t router_nodes(const router_t *r) { return patricia_nodes(r->trie); }
size_t router_mem(const router_t *r)   { return patricia_mem_bytes(r->trie); }

size_t router_pending(const router_t *r) {
    return r->mode == ROUTER_RCU ? rcu_pending(r->rcu) : 0;
}

const char *router_mode_name(const router_t *r) {
    return r->mode == ROUTER_RCU ? "rcu" : "rwlock";
}
