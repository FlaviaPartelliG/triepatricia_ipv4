/* rcu.h - simplified userspace RCU (QSBR-style) for the concurrent data plane.
 *
 * This is the SO core of Tema 6: it lets many reader threads traverse the
 * routing trie with no locks while a control thread mutates it, deferring the
 * freeing of unlinked nodes until a grace period has elapsed (every reader has
 * passed through a quiescent state). This is Quiescent-State-Based Reclamation.
 *
 * Model
 * -----
 *   - Each reader thread registers once (rcu_register) and announces a
 *     quiescent state between read operations (rcu_quiescent_state). A reader
 *     must hold no RCU-protected pointer across a quiescent state; our lookups
 *     are self-contained, so calling it after each lookup is correct.
 *   - A writer unlinks nodes (atomic publish), hands them to rcu_defer, and
 *     later calls rcu_reclaim: this waits one grace period (rcu_synchronize)
 *     and then frees the batch.
 *
 * Memory ordering: the quiescent store is release and rcu_synchronize's read is
 * acquire, so a reader's last access to a node happens-before the writer's free.
 */
#ifndef RCU_H
#define RCU_H

#include <stddef.h>

#define RCU_MAX_THREADS 128

typedef struct rcu rcu_t;

rcu_t *rcu_create(void);
void   rcu_destroy(rcu_t *r);   /* frees pending deferrals; assumes no readers */

/* Reader side. */
int  rcu_register(rcu_t *r);              /* returns a slot id for this thread  */
void rcu_unregister(rcu_t *r, int id);    /* stop participating in grace periods */
void rcu_quiescent_state(rcu_t *r, int id); /* "I hold no RCU references now"    */

/* Writer side. */
void   rcu_defer(rcu_t *r, void *p, void (*dtor)(void *)); /* free p after a GP */
void   rcu_synchronize(rcu_t *r);         /* block until one grace period passes */
void   rcu_reclaim(rcu_t *r);             /* synchronize + free all deferred     */
size_t rcu_pending(rcu_t *r);             /* number of objects awaiting free     */

#endif /* RCU_H */
