/* Pinned Android libc++ lru_cache<uint64_t, CTexRaw*>. The original put()
 * allocates a new list node, erases both old nodes, then allocates a hash node
 * even when merely touching an existing texture. Reuse the existing list
 * node, keeping the exact MRU order/value/count and the map's iterator valid.
 * Misses and capacity-reduction cases retain the original eviction path.
 * No entry survives a call outside the cache itself; no extra memory is used.
 */
#include "wb_stream_lru.h"
#include <stddef.h>
#include <string.h>
static wb_stream_lru_put_fn original_put;
static wb_stream_lru_find_fn original_find;
static uint32_t read_word(const void *p,unsigned offset) {
    uint32_t n;memcpy(&n,(const char *)p+offset,4);return n;
}
static void write_word(void *p,unsigned offset,uint32_t n) {
    memcpy((char *)p+offset,&n,4);
}
static void *pointer(const void *p,unsigned offset) {
    return (void *)(uintptr_t)read_word(p,offset);
}
int wb_stream_lru_bind(wb_stream_lru_put_fn put,wb_stream_lru_find_fn find) {
    if(!put || !find) return -1;
    original_put=put;original_find=find;return 0;
}
void wb_stream_lru_put(void *cache,const uint64_t *key,void *const *value) {
    /* map.size <= max entries means a hit cannot trigger capacity eviction.
     * Copy the incoming value before modifying any links: callers may pass
     * the value field of the node which is being moved. */
    if(read_word(cache,0x18)<=read_word(cache,0x20)) {
        void *map_node=original_find((char *)cache+0x0c,key);
        if(map_node) {
            void *node=pointer(map_node,0x10);
            uint32_t new_value=read_word(value,0);
            void *first=pointer(cache,4);
            if(node!=first) {
                void *prev=pointer(node,0),*next=pointer(node,4);
                write_word(prev,4,(uint32_t)(uintptr_t)next);
                write_word(next,0,(uint32_t)(uintptr_t)prev);
                write_word(node,0,(uint32_t)(uintptr_t)cache);
                write_word(node,4,(uint32_t)(uintptr_t)first);
                write_word(first,0,(uint32_t)(uintptr_t)node);
                write_word(cache,4,(uint32_t)(uintptr_t)node);
            }
            write_word(node,0x10,new_value);
            return;
        }
    }
    original_put(cache,key,value);
}
