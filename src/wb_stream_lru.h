#ifndef WB_STREAM_LRU_H
#define WB_STREAM_LRU_H
#include <stdint.h>
typedef void (*wb_stream_lru_put_fn)(void *,const uint64_t *,void *const *);
typedef void *(*wb_stream_lru_find_fn)(void *,const uint64_t *);
int wb_stream_lru_bind(wb_stream_lru_put_fn,wb_stream_lru_find_fn);
void wb_stream_lru_put(void *,const uint64_t *,void *const *);
#endif
