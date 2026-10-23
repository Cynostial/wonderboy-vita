/* Pinned VitaGL VBO ABI, independently checked against the linked ARM code.
 * Old GPU addresses stay allocated until proven GPU completion. No frame-count
 * heuristics, unbounded orphan list, or changes to the shared VitaGL archive. */
#include <vitaGL.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "wb_gpu_completion.h"
#define CAPACITY (8u*1024u*1024u)
typedef struct {
    void *data;
    uint32_t size;
    uint8_t cpu_allocation,pad[3];
    uint32_t last_frame;
    uint8_t mapped;
} native_buffer;
_Static_assert(offsetof(native_buffer,last_frame)==12,"Pinned VBO ABI");
_Static_assert(offsetof(native_buffer,mapped)==16,"Pinned VBO ABI");
static struct { void *data;size_t size;uint32_t scene; } retired[128];
static size_t retired_bytes;
static unsigned retired_count;
static uint32_t renames,copy_bytes,fallbacks;
extern void *vgl_memalign(size_t,size_t,vglMemType);
static void reap(void) {
    for(unsigned i=0;i<retired_count;) {
        if(wb_gpu_complete(retired[i].scene)) {
            vglFree(retired[i].data);retired_bytes-=retired[i].size;
            retired[i]=retired[--retired_count];
        } else ++i;
    }
}
static void retain(void *data,size_t size) {
    retired[retired_count].data=data;retired[retired_count].size=size;
    /* The current scene may be newer than the allocation's last use. It is
     * a conservative lifetime upper bound, never a frame-age approximation. */
    retired[retired_count++].scene=wb_gpu_scene();retired_bytes+=size;
}
/* Data/Delete can detach old storage instead of waiting at the beginning
 * of a frame. The native object may be destroyed; queued GPU commands use
 * its data address, which remains alive until explicit GPU completion. */
int wb_buffer_retire_native(GLuint id,size_t size) {
    reap();
    native_buffer *b=(native_buffer *)(uintptr_t)id;
    if(!b || !b->data || b->size!=size || b->mapped ||
       size>CAPACITY-retired_bytes || retired_count==128) { ++fallbacks;return 0; }
    retain(b->data,size);
    b->data=NULL;b->last_frame=UINT32_MAX;
    return 1;
}
int wb_buffer_rename_native(GLuint id,size_t size,unsigned char **cpu) {
    reap();
    native_buffer *b=(native_buffer *)(uintptr_t)id;
    /* Limit individual copies as well as total outstanding storage. */
    if(!b || !b->data || b->size!=size || b->mapped || size>2u*1024u*1024u ||
       size>CAPACITY-retired_bytes || retired_count==128) { ++fallbacks;return 0; }
    /* Restrict copies to the existing GPU-mapped RAM pool. vglMemalign's
     * public convenience API tries the unmapped newlib heap first. */
    void *p=vgl_memalign(16,size,VGL_MEM_RAM);
    if(!p) { ++fallbacks;return 0; }
    memcpy(p,b->data,size);
    retain(b->data,size);
    b->data=p;b->last_frame=UINT32_MAX;*cpu=p;
    ++renames;copy_bytes+=(uint32_t)size;
    return 1;
}
void wb_buffer_release_native(void) {
    for(unsigned i=0;i<retired_count;++i) vglFree(retired[i].data);
    retired_count=0;retired_bytes=0;
}
void wb_buffer_rename_stats(uint32_t *count,uint32_t *bytes,uint32_t *fallback) {
    *count=renames;*bytes=copy_bytes;*fallback=fallbacks;
}
