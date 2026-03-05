#include "../proxy_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_slot_allocation(void) {
    struct ctx c;
    memset(&c, 0, sizeof(c));
    c.ring_sz = 1024;
    c.srv_cnt = 4;
    c.ring = calloc((size_t)c.ring_sz, sizeof(struct slot));
    c.ring_head = 0;

    u32 idx;
    /* Transparent mode search: finds first socket pool (0) with ID 10 */
    struct slot *s1 = slot_alloc(&c, &idx, 10);
    assert(s1 != NULL);
    assert(idx == 10);

    /* ID 10 is now used in pool 0. Request for ID 10 again should go to pool 1 */
    s1->used = 1;
    struct slot *s2 = slot_alloc(&c, &idx, 10);
    assert(s2 != NULL);
    assert(idx == 256 + 10);

    free(c.ring);
    printf("test_slot_allocation passed\n");
}

static void test_slot_wraparound(void) {
    struct ctx c;
    memset(&c, 0, sizeof(c));
    c.ring_sz = 1024;
    c.srv_cnt = 2;
    c.ring = calloc((size_t)c.ring_sz, sizeof(struct slot));
    c.ring_head = 0;

    u32 idx;
    slot_alloc(&c, &idx, 0);
    slot_alloc(&c, &idx, 1);
    
    /* Simulate all pools busy for ID 0 */
    c.ring[0].used = 1;
    c.ring[256].used = 1;
    
    /* Should force overwrite pool-relative ID 0 if no free pools found */
    struct slot *s3 = slot_alloc(&c, &idx, 0);
    assert(s3 != NULL);
    assert((idx % 256) == 0);

    free(c.ring);
    printf("test_slot_wraparound passed\n");
}

static void test_slot_lookup(void) {
    struct ctx c;
    memset(&c, 0, sizeof(c));
    c.ring_sz = 1024;
    c.srv_cnt = 4;
    c.ring = calloc((size_t)c.ring_sz, sizeof(struct slot));
    c.ring_head = 0;

    u8 auth1[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    u32 idx1;
    struct slot *s1 = slot_alloc(&c, &idx1, 42);
    s1->id = 42;
    memcpy(s1->auth, auth1, 16);
    s1->used = 1;

    struct slot *found = slot_find(&c, (int)(idx1 / 256), 42);
    assert(found == s1);

    found = slot_find(&c, 0, 255);
    assert(found == NULL);

    free(c.ring);
    printf("test_slot_lookup passed\n");
}

static void test_slot_overwrite(void) {
    struct ctx c;
    memset(&c, 0, sizeof(c));
    c.ring_sz = 512;
    c.srv_cnt = 2;
    c.ring = calloc((size_t)c.ring_sz, sizeof(struct slot));
    c.ring_head = 0;

    u32 idx1, idx2, idx3;
    struct slot *s1 = slot_alloc(&c, &idx1, 10);
    s1->id = 10;
    s1->used = 1;

    struct slot *s2 = slot_alloc(&c, &idx2, 10);
    s2->id = 10;
    s2->used = 1;

    /* Both pools used for ID 10. Third allocation should force overwrite. */
    struct slot *s3 = slot_alloc(&c, &idx3, 10);
    assert(s3 != NULL);
    assert(s3 == s1 || s3 == s2);

    free(c.ring);
    printf("test_slot_overwrite passed\n");
}

int main(void) {
    test_slot_allocation();
    test_slot_wraparound();
    test_slot_lookup();
    test_slot_overwrite();
    printf("All unit tests passed!\n");
    return 0;
}
