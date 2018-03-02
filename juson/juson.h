#ifndef _JUSON_H_
#define _JUSON_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ERR_HINT    0

#define JUSON_OK     (0)
#define JUSON_ERR    (-1)
#define JUSON_CHUNK_SIZE    (128)


typedef struct juson_value  juson_value_t;
typedef long                juson_int_t;
typedef double              juson_float_t;
typedef unsigned char       juson_bool_t;
//json类型
typedef enum juson_type {
    JUSON_OBJECT,
    JUSON_INTEGER,
    JUSON_FLOAT,
    JUSON_ARRAY,
    JUSON_BOOL,
    JUSON_STRING,
    JUSON_NULL,
    JUSON_PAIR,
    JUSON_LIST,
} juson_type_t;

//json值
struct juson_value { 
    juson_type_t t;   //任意的类型
    union {
        juson_int_t ival;
        juson_float_t fval;
        juson_bool_t bval;
        
        // String
        struct {
            const char* sval; //指针
            int need_free;   
            int len;	    //长度
        };
        
        // Array
        struct {
            int size;		//大小
            int capacity;	//容量
            juson_value_t** adata;  //指向当前结构体的二位数组
        };
        
        // Object
        struct {
            juson_value_t* tail; // pair 表示一个头尾的指针
            juson_value_t* head; // pair 头指针
        };
        
        struct {
            union {
                // Pair   键值对
                struct {
			juson_value_t* key; // string
			juson_value_t* val;
                };
                // List	  链表
                juson_value_t* data;
            };
            juson_value_t* next;
        };
    };
};

typedef union  juson_slot   juson_slot_t;
typedef struct juson_chunk  juson_chunk_t;
typedef struct juson_pool   juson_pool_t;

union juson_slot {
    juson_slot_t* next;  //指向下一个slot
    juson_value_t val;	//当前的值
};

struct juson_chunk {  //指向下一个块
    juson_chunk_t* next; //下一个chunk
    juson_slot_t slots[JUSON_CHUNK_SIZE]; //128个slot组成块
};

struct juson_pool {
    int allocated_n;	//已经分配了多少
    juson_slot_t* cur;   //当前的slot
    juson_chunk_t head;  //chunk的头
};

typedef struct {
    const char* p;    //字符串
    int line;	     //行数
    juson_value_t* val; //存放的值指针
    juson_pool_t pool;  //池子
} juson_doc_t;

char* juson_load(const char* file_name);
juson_value_t* juson_parse(juson_doc_t* doc, const char* json);
void juson_destroy(juson_doc_t* doc);
juson_value_t* juson_object_get(juson_value_t* obj, char* name);
juson_value_t* juson_array_get(juson_value_t* arr, size_t idx);

#ifdef __cplusplus
}
#endif

#endif
