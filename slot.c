#include "proxy_internal.h"

#include <string.h>

struct slot *slot_alloc(struct ctx *c, u32 *idx, u8 desired_id)
{
	struct slot *s;
	u32 start_sock = c->ring_head % (u32)c->srv_cnt;

	if (!c->secret) {
		/* TRANSPARENT MODE: we MUST use desired_id.
		 * Search for a socket pool that doesn't have this ID used.
		 */
		for (u32 i = 0; i < (u32)c->srv_cnt; i++) {
			u32 s_idx = ((start_sock + i) % (u32)c->srv_cnt) * 256 + desired_id;
			s = &c->ring[s_idx];
			if (!s->used) {
				*idx = s_idx;
				memset(s, 0, sizeof(*s));
				c->ring_head++;
				return s;
			}
		}
		/* Fallback: overwrite the first socket we tried for this ID */
		*idx = (start_sock % (u32)c->srv_cnt) * 256 + desired_id;
	} else {
		/* REMAPPING MODE: we can use ANY free slot.
		 * Try next available slots starting from head.
		 */
		for (u32 i = 0; i < (u32)c->ring_sz; i++) {
			u32 s_idx = (c->ring_head + i) % (u32)c->ring_sz;
			s = &c->ring[s_idx];
			if (!s->used) {
				*idx = s_idx;
				memset(s, 0, sizeof(*s));
				c->ring_head += (i + 1);
				return s;
			}
		}
		/* Fallback: take next one even if used (overwrite) */
		*idx = c->ring_head % (u32)c->ring_sz;
	}

	s = &c->ring[*idx];
	memset(s, 0, sizeof(*s));
	c->ring_head++;
	return s;
}

struct slot *slot_find(struct ctx *c, int sock_idx, u8 id)
{
	int i = sock_idx * 256 + id;

	if (i >= c->ring_sz)
		return NULL;

	struct slot *s = &c->ring[i];

	if (!s->used)
		return NULL;

	return s;
}
