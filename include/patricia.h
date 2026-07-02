
#ifndef PATRICIA_H
#define PATRICIA_H

#include "ipv4.h"

typedef struct patricia patricia_t;

typedef void (*patricia_retire_fn)(void *ctx, void *p);

patricia_t *patricia_create(void);
void        patricia_destroy(patricia_t *t);

void patricia_set_reclaimer(patricia_t *t, patricia_retire_fn fn, void *ctx);

bool patricia_insert(patricia_t *t, prefix_t p, nexthop_t nh);
bool patricia_remove(patricia_t *t, prefix_t p);

nexthop_t patricia_lookup(const patricia_t *t, uint32_t addr);

size_t patricia_count(const patricia_t *t);      
size_t patricia_nodes(const patricia_t *t);      
size_t patricia_mem_bytes(const patricia_t *t);  

#endif 
