/* Pinned Android libwb.so only. Keep CTexRaw/atlas metadata, but defer image
 * requests to LoadFinalize. Both GetTextureRaw paths still run their original
 * flags check: flag 2 returns metadata, flag 0 calls LoadFinalize, which queues
 * and waits if the request ID is -1. No refcounts or texture pointers are faked.
 */
#include "wb_texture_policy.h"
#include "wb_memory.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t offset; unsigned size; unsigned char before[4], after[4]; } patch;
static const patch patches[] = {
    /* A found CTexRaw already owns its resolved filename/metadata. With the
     * deferred-image policy below, flag 2 need not rebuild temporary strings
     * and stat the same .ktx every frame. At this existing-object-only site,
     * test bits 0 OR 1 rather than bit 0 alone. Missing metadata still takes
     * the full construction path; flag 0 still reaches LoadFinalize. */
    {0x1470ea,2,{0xc0,0x07},{0x80,0x07}},
    /* New CTexRaw: always continue to the flags check, not asyncUseLoad. */
    {0x1473c0,2,{0x2b,0xd1},{0x2b,0xe0}},
    /* Existing CTexRaw: fall through to the same flags check. */
    {0x147418,2,{0xd3,0xd0},{0x00,0xbf}},
    /* All three original 128 MiB LRU comparisons become 48 MiB. Encoded
     * with the Vita assembler, and exercised against the guest in Unicorn. */
    {0x1476f8,4,{0xb0,0xf1,0x00,0x6f},{0xb0,0xf1,0x40,0x7f}},
    {0x147750,4,{0xb0,0xf1,0x00,0x6f},{0xb0,0xf1,0x40,0x7f}},
    {0x147764,4,{0xb0,0xf1,0x00,0x6f},{0xb0,0xf1,0x40,0x7f}},
};
int wb_apply_texture_patches(void *image) {
    unsigned char *base=image;
    for(size_t i=0;i<sizeof(patches)/sizeof(*patches);++i)
        if(memcmp(base+patches[i].offset,patches[i].before,patches[i].size)) return -1;
    for(size_t i=0;i<sizeof(patches)/sizeof(*patches);++i)
        memcpy(base+patches[i].offset,patches[i].after,patches[i].size);
    return 0;
}
static int (*on_render_thread)(void);
static void (*load_queue)(void);
static void (*original_atlas_preload)(void *);
void wb_atlas_preload_bind(void (*original)(void *)) { original_atlas_preload=original; }
void wb_atlas_preload(void *atlas) {
    /* PreloadTexFromDisk re-formats the same name and reassigns every virtual
     * sprite on each deferred prefetch. Once the index and raw metadata are
     * bound, our metadata-only prefetch policy has no remaining work. Unload
     * clears +0x184; a missing index or raw object takes the original path.
     * Actual draws retain GetTexture/LoadFinalize and image residency logic. */
    uint32_t raw;
    memcpy(&raw,(const unsigned char *)atlas+0x184,4);
    if(((const unsigned char *)atlas)[0x16c] && raw) return;
    original_atlas_preload(atlas);
}
void wb_texture_policy_bind(int (*is_render_thread)(void), void (*upload_queue)(void)) {
    on_render_thread=is_render_thread;load_queue=upload_queue;
}
void *wb_resource_result(void *manager,uint32_t request,int32_t resource,uint32_t flags) {
    (void)flags;
    /* Exact dispatch of ResHBase::asyncGetResult at guest 0x1ca460:
     * a cached resource ID uses vtable+0x2c; otherwise request uses +0x70.
     * Android Task is three 32-bit words, passed in r1/r2/r3. */
    typedef void *(*get_result)(void *,uint32_t);
    get_result *vtable=*(get_result **)manager;
    void *result=resource!=-1 ? vtable[11](manager,(uint32_t)resource) :
                               vtable[28](manager,request);
    /* The loader copies TextureData into OGLTexture's upload queue. Drain it
     * only on the render thread, before LoadFinalize can evict that object.
     * LoadQueue performs the actual upload and frees the CPU mip copies. */
    if(result && on_render_thread && load_queue && on_render_thread()) {
        load_queue();
        static unsigned completed;
        ++completed;
        if(completed<=4 || !(completed%32)) {
            wb_memory_stats s=wb_memory_get_stats();
            fprintf(stderr,"Resource completion %u: arena live=%u peak=%u capacity=%u failures=%u\n",
                    completed,(unsigned)s.live_bytes,(unsigned)s.peak_bytes,
                    (unsigned)s.capacity,(unsigned)s.failed_requests);
        }
    }
    return result;
}
