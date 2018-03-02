#ifndef _JULIA_POOL_H_
#define _JULIA_POOL_H_

#include "util.h"
#include "vector.h"

typedef union {
    void* next;
} chunk_slot_t;

typedef struct {
    void* data;
} chunk_t;

typedef struct {
    int width;		    //宽度
    int chunk_size;	    //块大小
    int nallocated;	    //已经分配的大小
    void* cur;		    //当前的指针
    vector_t chunks;
} pool_t;

int pool_init(pool_t* pool, int width, int chunk_size, int nchunks);
void* pool_alloc(pool_t* pool);
void pool_clear(pool_t* pool);

static inline void pool_free(pool_t* pool, void* x) {
    if (x == NULL)
        return;
    --pool->nallocated;
    ((chunk_slot_t*)x)->next = pool->cur;
    pool->cur = x;
}

#endif
