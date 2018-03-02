#include "vector.h"

#include "string.h"
#include "util.h"

#include <assert.h>
#include <string.h>
#include <stdint.h>

// vector初始化的长度
int vector_init(vector_t* vec, int width, int size) {  //字符宽度
    assert(width != 0);
    if (size != 0) {
	    vec->data = malloc(width * size); //初始化数据.
        if (vec->data == NULL)		    
            return ERROR;
    } else {
        vec->data = NULL;
    }

    vec->size = size;	//对于vector的初始化
    vec->capacity = size;
    vec->width = width;
    return OK;
}

int vector_reserve(vector_t* vec, int c) {
    if (c != 0) {
        assert(vec->data == NULL);
        vec->data = malloc(vec->width * c);
        if (vec->data == NULL)
            return ERROR;
    }
    return OK;
}
	    
int vector_resize(vector_t* vec, int new_size) {
    if (new_size > vec->capacity) {
        int new_capacity = max(new_size, vec->capacity * 2 + 1); //添加一倍的长度
        vec->data = realloc(vec->data, vec->width * new_capacity); //向量大小
        if (vec->data == NULL)
            return ERROR;
        vec->capacity = new_capacity;
    }
    
    vec->size = new_size;
    return OK;
}

void vector_clear(vector_t* vec) {
    vec->size = 0;
    vec->capacity = 0;
    free(vec->data);
    vec->data = NULL;
}
