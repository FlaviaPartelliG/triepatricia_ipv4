/* rcu.c - QSBR-style RCU implementation.
 *
 * A monotonically increasing global epoch counter marks grace periods. Each
 * registered reader publishes the last epoch it observed at a quiescent point.
 * To wait for a grace period, the writer bumps the global epoch and then spins
 * until every online reader has published an epoch at least as new -- meaning
 * each reader has been quiescent (held no references) at least once since the
 * bump, so any previously unlinked node is unreachable and safe to free.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L   /* sched_yield */
#endif
#include "rcu.h"

#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>

typedef struct defer_node {
    void *p;
    void (*dtor)(void *);
    struct defer_node *next;
} defer_node_t;

typedef struct {
    _Atomic unsigned long epoch;   /* last epoch observed at a quiescent state */
    _Atomic int           online;  /* participating in grace periods           */
} rcu_thread_t;

struct rcu {
    _Atomic unsigned long global;          /* grace-period counter            */
    _Atomic int           nslots;          /* high-water of allocated slots   */
    rcu_thread_t          t[RCU_MAX_THREADS];

    pthread_mutex_t       defer_lock;      /* protects the defer list         */
    defer_node_t         *defer_head;
    _Atomic size_t        pending;
};

rcu_t *rcu_create(void) {
    rcu_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    atomic_init(&r->global, 1);
    atomic_init(&r->nslots, 0);
    for (int i = 0; i < RCU_MAX_THREADS; i++) {
        atomic_init(&r->t[i].epoch, 0);
        atomic_init(&r->t[i].online, 0);
    }
    atomic_init(&r->pending, 0);
    pthread_mutex_init(&r->defer_lock, NULL);
    return r;
}

void rcu_destroy(rcu_t *r) {
    if (!r) return;
    defer_node_t *n = r->defer_head;
    while (n) {
        defer_node_t *next = n->next;
        if (n->dtor) n->dtor(n->p);
        free(n);
        n = next;
    }
    pthread_mutex_destroy(&r->defer_lock);
    free(r);
}

int rcu_register(rcu_t *r) {
    int id = atomic_fetch_add_explicit(&r->nslots, 1, memory_order_relaxed);
    if (id >= RCU_MAX_THREADS) {
        atomic_fetch_sub_explicit(&r->nslots, 1, memory_order_relaxed);
        return -1;
    }
    unsigned long g = atomic_load_explicit(&r->global, memory_order_acquire);
    atomic_store_explicit(&r->t[id].epoch, g, memory_order_release);
    atomic_store_explicit(&r->t[id].online, 1, memory_order_release);
    return id;
}

void rcu_unregister(rcu_t *r, int id) {
    if (id < 0 || id >= RCU_MAX_THREADS) return;
    atomic_store_explicit(&r->t[id].online, 0, memory_order_release);
}

void rcu_quiescent_state(rcu_t *r, int id) {
    if (id < 0 || id >= RCU_MAX_THREADS) return;
    unsigned long g = atomic_load_explicit(&r->global, memory_order_acquire);
    atomic_store_explicit(&r->t[id].epoch, g, memory_order_release);
}

void rcu_synchronize(rcu_t *r) {
    /* Advance the grace period, then wait for every online reader to catch up. */
    unsigned long target =
        atomic_fetch_add_explicit(&r->global, 1, memory_order_acq_rel) + 1;

    int slots = atomic_load_explicit(&r->nslots, memory_order_acquire);
    for (int i = 0; i < slots; i++) {
        while (atomic_load_explicit(&r->t[i].online, memory_order_acquire)) {
            unsigned long e =
                atomic_load_explicit(&r->t[i].epoch, memory_order_acquire);
            if (e >= target) break;
            sched_yield();
        }
    }
}

void rcu_defer(rcu_t *r, void *p, void (*dtor)(void *)) {
    defer_node_t *n = malloc(sizeof(*n));
    if (!n) {                 /* last resort: synchronize and free now */
        rcu_synchronize(r);
        if (dtor) dtor(p);
        return;
    }
    n->p = p;
    n->dtor = dtor;
    pthread_mutex_lock(&r->defer_lock);
    n->next = r->defer_head;
    r->defer_head = n;
    pthread_mutex_unlock(&r->defer_lock);
    atomic_fetch_add_explicit(&r->pending, 1, memory_order_relaxed);
}

void rcu_reclaim(rcu_t *r) {
    pthread_mutex_lock(&r->defer_lock);
    defer_node_t *list = r->defer_head;
    r->defer_head = NULL;
    pthread_mutex_unlock(&r->defer_lock);

    if (!list) return;

    rcu_synchronize(r);       /* one grace period covers everything unlinked so far */

    size_t freed = 0;
    while (list) {
        defer_node_t *next = list->next;
        if (list->dtor) list->dtor(list->p);
        free(list);
        list = next;
        freed++;
    }
    atomic_fetch_sub_explicit(&r->pending, freed, memory_order_relaxed);
}

size_t rcu_pending(rcu_t *r) {
    return atomic_load_explicit(&r->pending, memory_order_relaxed);
}
