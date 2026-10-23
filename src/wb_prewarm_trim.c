/* Retire old scene images before transition uploads need their space. The
 * original image finalizer evicts after upload; conservative admission can
 * therefore reject loads despite recoverable stale pages. Keep every known
 * destination dependency, remove only unprotected resident LRU pages, and
 * use the original erase/unload/delete paths. CTexRaw/virtual metadata lives
 * on. This helper is called only inside render-thread level preparation. */
#include "wb_prewarm_trim.h"
#include <string.h>
enum { LRU_BYTES=48u*1024*1024, MAX_SCAN=1024 };
static wb_prewarm_trim_api api;
static int synchronized;
static uint32_t word(const void *p,unsigned offset) {
    uint32_t value;memcpy(&value,(const char *)p+offset,4);return value;
}
static void *pointer(const void *p,unsigned offset) {
    return (void *)(uintptr_t)word(p,offset);
}
int wb_prewarm_trim_bind(const wb_prewarm_trim_api *p) {
    if(!p || !p->manager || !p->cache || !p->bytes || !p->deletes || !p->erase ||
       !p->unload || !p->drain || !p->size || !p->admit || !p->finish) return -1;
    api=*p;return 0;
}
void wb_prewarm_trim_begin(void) { synchronized=0; }
static int room(unsigned wanted) {
    return *api.bytes<=LRU_BYTES && wanted<=LRU_BYTES-*api.bytes && api.admit(wanted);
}
static int shared_page(void *raw) {
    const char *name=pointer(raw,4);
    /* Hero prefetch lives in UpdateStreaming, outside PreloadLoc. Common
     * interaction effects also remain useful in every destination. */
    return name && (!strncmp(name,"atlas/Hero",10) ||
        !strncmp(name,"atlas/Objects",13) || !strncmp(name,"spine/Fx/",9));
}
int wb_prewarm_trim(unsigned wanted,void *const *keep,unsigned count) {
    if(!api.manager || wanted>4u*1024*1024 || count>256) return 0;
    if(room(wanted)) return 1;
    void *node=pointer(api.cache,0);
    for(unsigned visited=0;node && node!=api.cache && visited<MAX_SCAN;++visited) {
        /* prev leads from the oldest node toward the newest. Save it before
         * original erase destroys this list node and its hash-table entry. */
        void *previous=pointer(node,0),*raw=pointer(node,0x10);
        unsigned i=0;for(;i<count && keep[i]!=raw;++i) {}
        void *image=raw ? pointer(raw,0xa8) : 0;
        if(i==count && image && !shared_page(raw) && image!=pointer(api.manager,0x2c) &&
           !word(image,4) && word(raw,0xac)==UINT32_MAX) {
            unsigned bytes=api.size(image);
            if(bytes && bytes<=*api.bytes) {
                if(!synchronized) {
                    if(!api.finish()) return 0;
                    synchronized=1;
                    api.drain(api.deletes);
                    if(room(wanted)) return 1;
                }
                api.erase(api.cache,(const uint64_t *)((const char *)raw+0x90));
                /* Unload does not own this counter. Match LoadFinalize's
                 * exact erase, subtract, Unload ordering without double debit. */
                *api.bytes-=bytes;
                api.unload(api.manager,raw);
                api.drain(api.deletes);
                if(room(wanted)) return 1;
            }
        }
        node=previous;
    }
    /* VitaGL can defer a recent physical free. Do not bypass its ownership
     * or treat bookkeeping reductions as actual GPU availability. */
    return room(wanted);
}
