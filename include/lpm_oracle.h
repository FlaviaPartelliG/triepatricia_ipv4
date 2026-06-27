/* lpm_oracle.h - brute-force longest-prefix-match reference.
 *
 * A deliberately simple O(N) implementation used only by the test suite to
 * validate the Patricia and multibit tries. Correctness is obvious here, so it
 * serves as the ground truth oracle.
 */
#ifndef LPM_ORACLE_H
#define LPM_ORACLE_H

#include "ipv4.h"

typedef struct oracle oracle_t;

oracle_t *oracle_create(void);
void      oracle_destroy(oracle_t *o);

/* Insert or update a route. Returns true on success. */
bool oracle_insert(oracle_t *o, prefix_t p, nexthop_t nh);

/* Remove an exact prefix. Returns true if it existed. */
bool oracle_remove(oracle_t *o, prefix_t p);

/* Longest prefix match for `addr`. Returns nexthop_none() if no match. */
nexthop_t oracle_lookup(const oracle_t *o, uint32_t addr);

size_t oracle_count(const oracle_t *o);

#endif /* LPM_ORACLE_H */
