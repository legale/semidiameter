/*
 * proxy_internal.h — internal functions exposed for unit testing
 */
#ifndef PROXY_INTERNAL_H
#define PROXY_INTERNAL_H

#include "proxy.h"

struct slot *slot_alloc(struct ctx *c, u32 *idx, u8 desired_id);
struct slot *slot_find(struct ctx *c, int sock_idx, u8 id);

#endif /* PROXY_INTERNAL_H */
