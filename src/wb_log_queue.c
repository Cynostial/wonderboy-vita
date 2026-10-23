#include "wb_log_queue.h"
#include <string.h>
void wb_log_push(wb_log_queue *q,const void *data,size_t n) {
    if(!n) return;
    if(n>WB_LOG_CAPACITY || __atomic_exchange_n(&q->lock,1,__ATOMIC_ACQUIRE)) {
        __atomic_fetch_add(&q->dropped,1,__ATOMIC_RELAXED);return;
    }
    unsigned used=__atomic_load_n(&q->used,__ATOMIC_RELAXED);
    if(n>WB_LOG_CAPACITY-used) {
        __atomic_fetch_add(&q->dropped,1,__ATOMIC_RELAXED);
    } else {
        unsigned at=(q->read+used)%WB_LOG_CAPACITY;
        size_t first=WB_LOG_CAPACITY-at;if(first>n) first=n;
        memcpy(q->bytes+at,data,first);
        memcpy(q->bytes,(const unsigned char *)data+first,n-first);
        __atomic_store_n(&q->used,used+(unsigned)n,__ATOMIC_RELEASE);
    }
    __atomic_store_n(&q->lock,0,__ATOMIC_RELEASE);
}
size_t wb_log_pop(wb_log_queue *q,void *data,size_t n) {
    if(__atomic_exchange_n(&q->lock,1,__ATOMIC_ACQUIRE)) return 0;
    unsigned used=__atomic_load_n(&q->used,__ATOMIC_RELAXED);
    if(n>used) n=used;
    size_t first=WB_LOG_CAPACITY-q->read;if(first>n) first=n;
    memcpy(data,q->bytes+q->read,first);
    memcpy((unsigned char *)data+first,q->bytes,n-first);
    q->read=(q->read+(unsigned)n)%WB_LOG_CAPACITY;
    __atomic_store_n(&q->used,used-(unsigned)n,__ATOMIC_RELEASE);
    __atomic_store_n(&q->lock,0,__ATOMIC_RELEASE);
    return n;
}
unsigned wb_log_pending(const wb_log_queue *q) {
    return __atomic_load_n(&q->used,__ATOMIC_ACQUIRE);
}
