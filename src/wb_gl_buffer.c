/* The pinned VitaGL MapBufferRange returns the backing allocation directly,
 * ignoring synchronization flags. The game's transient buffers use 0x22
 * (WRITE|UNSYNCHRONIZED). Keep non-overlapping ring appends fast, but complete
 * queued draws before a range is overwritten. No extra full-buffer copies. */
#include <vitaGL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wb_gl_state.h"
#include "wb_gpu_completion.h"
#define RECORDS 1024
typedef struct { GLuint id; uintptr_t begin,end,size; unsigned char *cpu; uint32_t epoch,version,scene; int mapped; } buffer_range;
static buffer_range records[RECORDS];
/* At most one index buffer plus sixteen vertex attribute buffers. These
 * pointers are consumed synchronously after this draw's native submission. */
static buffer_range *draw_ranges[17];
static unsigned draw_range_count;
/* Exact-ID lookaside only: collisions fall through to the authoritative
 * records. Clearing a deleted record also invalidates any cached pointer. */
static buffer_range *recent_records[64];
/* Static quad index buffers are reused by many draws and across frames.
 * Cache only bounds, never GPU pointers. Every CPU write invalidates the
 * buffer's generation; protection of queued reads is unchanged. */
typedef struct {
    GLuint id; uint32_t version; uintptr_t offset,low,high;
    GLsizei count; GLenum type;
} index_bounds;
static index_bounds bounds[128];
static uint32_t write_version,index_hits,index_scanned;
void wb_index_bounds(const unsigned char *,unsigned,unsigned,uintptr_t *,uintptr_t *);
void wb_buffer_scan_stats(uint32_t *hits,uint32_t *scanned) {
    *hits=index_hits;*scanned=index_scanned;
}
static void written(buffer_range *r) {
    if(!r) return;
    if(++write_version==0) { memset(bounds,0,sizeof(bounds));++write_version; }
    r->version=write_version;
}
static uint32_t epoch=1;
static int (*rename_buffer)(GLuint,size_t,unsigned char **);
static void (*release_retired)(void);
static int (*retire_buffer)(GLuint,size_t);
void wb_buffer_bind_retire(int (*retire)(GLuint,size_t)) { retire_buffer=retire; }
static uint64_t (*clock_us)(void);
static uint64_t wait_us;
static uint32_t waits;
void wb_buffer_bind_clock(uint64_t (*clock)(void)) { clock_us=clock; }
void wb_buffer_wait_stats(uint32_t *count,uint64_t *duration) { *count=waits;*duration=wait_us; }
void wb_buffer_bind_rename(int (*rename)(GLuint,size_t,unsigned char **),void (*release)(void)) {
    rename_buffer=rename;release_retired=release;
}
static void completed(void) {
    if(++epoch==0) {
        for(unsigned i=0;i<RECORDS;++i) records[i].epoch=0;
        epoch=1;
    }
}
void wb_buffer_finish(void) {
    uint64_t start=clock_us ? clock_us() : 0;
    glFinish();
    if(clock_us) wait_us+=clock_us()-start;
    ++waits;
    if(release_retired) release_retired();
    completed();
}
void wb_buffer_sync_scene(uint32_t scene) {
    if(wb_gpu_complete(scene)) return;
    uint64_t start=clock_us ? clock_us() : 0;
    if(scene && wb_gpu_wait(scene)) {
        if(clock_us) wait_us+=clock_us()-start;
        ++waits;
    } else wb_buffer_finish();
}
#define finish wb_buffer_finish
static int pending(buffer_range *r) {
    if(!r || r->epoch!=epoch) return 0;
    if(wb_gpu_complete(r->scene)) { r->epoch=0;return 0; }
    return 1;
}
static buffer_range *record(GLuint id,int create) {
    buffer_range *empty=NULL;
    if(!id) return NULL;
    buffer_range **recent=&recent_records[(id^(id>>6))%64];
    if(*recent && (*recent)->id==id) return *recent;
    for(unsigned i=0;i<RECORDS;++i) {
        if(records[i].id==id) { *recent=&records[i];return *recent; }
        if(!records[i].id && !empty) empty=&records[i];
    }
    if(!create) return NULL;
    if(!empty) {
        fprintf(stderr,"GPU buffer metadata capacity exceeded\n");abort();
    }
    empty->id=id;*recent=empty;return empty;
}
static GLuint binding(GLenum target) {
    GLint id=0;
    if(target==GL_ARRAY_BUFFER) glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&id);
    else if(target==GL_ELEMENT_ARRAY_BUFFER) glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&id);
    return (GLuint)id;
}
static buffer_range *before_write(GLenum target,GLintptr offset,GLsizeiptr size) {
    if(offset<0 || size<=0 || (uintptr_t)size>UINTPTR_MAX-(uintptr_t)offset) return NULL;
    buffer_range *r=record(binding(target),1);
    if(!r) return NULL;
    uintptr_t begin=(uintptr_t)offset,end=begin+(uintptr_t)size;
    if(pending(r) && begin<r->end && end>r->begin) {
        /* Rename only when the bounded backend can retain the old storage.
         * Submitted draws keep their old address; future draws use the copy. */
        if(rename_buffer && r->size && end<=r->size && rename_buffer(r->id,r->size,&r->cpu)) {
            r->epoch=0;
            return r;
        }
        static unsigned reports;
        if(reports++<4) fprintf(stderr,"Synchronizing reused GPU buffer range: %u..%u\n",
                               (unsigned)begin,(unsigned)end);
        finish();
    }
    return r;
}
static void read_range(buffer_range *r,uintptr_t begin,uintptr_t end) {
    if(!r) return;
    pending(r);
    if(r->epoch!=epoch) { r->begin=begin;r->end=end;r->epoch=epoch; }
    else { if(begin<r->begin) r->begin=begin;if(end>r->end) r->end=end; }
    r->scene=0;
    if(draw_range_count<17) draw_ranges[draw_range_count++]=r;
}
void wb_buffer_draw_submitted(uint32_t scene) {
    for(unsigned i=0;i<draw_range_count;++i) draw_ranges[i]->scene=scene;
    draw_range_count=0;
}
static void *map_range(GLenum target,GLintptr offset,GLsizeiptr size,GLbitfield access) {
    buffer_range *r=NULL;
    if(access & GL_MAP_READ_BIT) finish();
    if(access & GL_MAP_WRITE_BIT) r=before_write(target,offset,size);
    if(access & GL_MAP_WRITE_BIT) written(r);
    void *p=glMapBufferRange(target,offset,size,access);
    /* This backend's mapping stays at the backing allocation until Data or
     * SubData replaces it. Retain the address to inspect index bounds only. */
    if(p) {
        if(!r) r=record(binding(target),1);
        if(r) { r->cpu=(unsigned char *)p-offset;r->mapped=1; }
    }
    return p;
}
static GLboolean unmap_buffer(GLenum target) {
    GLboolean result=glUnmapBuffer(target);
    buffer_range *r=record(binding(target),0);
    if(r) {
        r->mapped=0;
        if(!result) { r->cpu=NULL;written(r); }
    }
    return result;
}
static void buffer_data(GLenum target,GLsizeiptr size,const void *data,GLenum usage) {
    /* Native UnmapBuffer clears last_frame for the entire allocation. An
     * append map can therefore conceal older queued draws from Data/Delete. */
    buffer_range *r=record(binding(target),1);
    if(pending(r) &&
       (!retire_buffer || !retire_buffer(r->id,r->size))) finish();
    glBufferData(target,size,data,usage);
    written(r);
    if(r) { r->epoch=0;r->cpu=NULL;r->mapped=0;r->size=size>0 ? (uintptr_t)size : 0; }
}
static void sub_data(GLenum target,GLintptr offset,GLsizeiptr size,const void *data) {
    buffer_range *r=before_write(target,offset,size);
    written(r);
    glBufferSubData(target,offset,size,data);
    /* SubData may replace its native allocation, but may also modify it in
     * place after UnmapBuffer reset last_frame. Preserve pending read bounds. */
    if(r) r->cpu=NULL;
}
static unsigned type_bytes(GLenum t) {
    switch(t) {
    case GL_BYTE:case GL_UNSIGNED_BYTE:return 1;
    case GL_SHORT:case GL_UNSIGNED_SHORT:case GL_HALF_FLOAT:return 2;
    case GL_FLOAT:case GL_INT:case GL_UNSIGNED_INT:return 4;
    default:return 0;
    }
}
static void read_vertices(uintptr_t low,uintptr_t high,int known) {
    const wb_gl_attribute *attributes=wb_gl_state_attributes();
    for(GLuint i=0;i<16;++i) {
        const wb_gl_attribute *a=&attributes[i];
        if(!a->enabled) continue;
        buffer_range *r=record((GLuint)a->buffer,1);
        if(!r) continue; /* client arrays are copied by VitaGL */
        if(!known) { read_range(r,0,r->size ? r->size : UINTPTR_MAX);continue; }
        GLint stride=a->stride;
        unsigned bytes=type_bytes((GLenum)a->type)*(unsigned)a->size;
        if(stride==0) stride=(GLint)bytes;
        uintptr_t offset=(uintptr_t)a->pointer;
        if(!bytes || stride<=0 || offset>UINTPTR_MAX-bytes || high>(UINTPTR_MAX-offset-bytes)/(unsigned)stride)
            read_range(r,0,r->size ? r->size : UINTPTR_MAX);
        else read_range(r,offset+low*(unsigned)stride,offset+high*(unsigned)stride+bytes);
    }
}
/* Called before rebasing packed attributes, so these are original GL byte
 * ranges. Track actual index bounds; unrelated ring appends need no wait. */
void wb_buffer_draw_arrays(GLint first,GLsizei count) {
    draw_range_count=0;
    if(first<0 || count<=0) return;
    read_vertices((uintptr_t)first,(uintptr_t)first+(unsigned)count-1,1);
}
/* Instanced attributes can advance by divisor rather than vertex index.
 * Protect the whole allocation without interpreting or changing divisors. */
void wb_buffer_draw_conservative(int indexed) {
    draw_range_count=0;
    if(indexed) {
        buffer_range *r=record(binding(GL_ELEMENT_ARRAY_BUFFER),1);
        if(r) read_range(r,0,r->size ? r->size : UINTPTR_MAX);
    }
    read_vertices(0,0,0);
}
void wb_buffer_draw_elements(GLsizei count,GLenum type,const void *indices) {
    draw_range_count=0;
    if(count<=0) return;
    unsigned bytes=type_bytes(type);
    const unsigned char *p=indices;
    GLuint id=binding(GL_ELEMENT_ARRAY_BUFFER);
    buffer_range *r=record(id,1);
    uintptr_t offset=(uintptr_t)indices;
    int valid=bytes && (uintptr_t)(unsigned)count<=UINTPTR_MAX/bytes;
    uintptr_t total=valid ? (uintptr_t)(unsigned)count*bytes : UINTPTR_MAX;
    int known=valid && (type==GL_UNSIGNED_BYTE || type==GL_UNSIGNED_SHORT || type==GL_UNSIGNED_INT);
    if(id) {
        known=known && r && r->cpu && offset<=r->size && total<=r->size-offset;
        p=known ? r->cpu+offset : NULL;
        read_range(r,offset,offset<=UINTPTR_MAX-total ? offset+total : UINTPTR_MAX);
    } else known=known && p;
    uintptr_t low=UINTPTR_MAX,high=0;
    index_bounds *cached=NULL;
    if(known && r && !r->mapped) {
        unsigned hash=(unsigned)(id^(offset>>1)^((uintptr_t)(unsigned)count<<3)^type);
        cached=&bounds[hash%128];
        if(cached->id==id && cached->version==r->version && cached->offset==offset &&
           cached->count==count && cached->type==type) {
            ++index_hits;
            read_vertices(cached->low,cached->high,1);
            return;
        }
    }
    if(known) wb_index_bounds(p,(unsigned)count,bytes,&low,&high);
    if(known) {
        index_scanned+=(uint32_t)count;
        if(cached) *cached=(index_bounds){id,r->version,offset,low,high,count,type};
    }
    read_vertices(low,high,known);
}
static void delete_buffers(GLsizei n,const GLuint *ids) {
    for(GLsizei i=0;i<n;++i) {
        buffer_range *r=record(ids[i],0);
        if(pending(r) &&
           (!retire_buffer || !retire_buffer(r->id,r->size))) { finish();break; }
    }
    glDeleteBuffers(n,ids);
    for(GLsizei i=0;i<n;++i) { buffer_range *r=record(ids[i],0);if(r) memset(r,0,sizeof(*r)); }
}
void *wb_gl_buffer_proc(const char *name) {
#define MAP(n,f) if(!strcmp(name,n)) return (void *)(uintptr_t)f
    MAP("glMapBufferRange",map_range);
    MAP("glUnmapBuffer",unmap_buffer);
    MAP("glBufferData",buffer_data);
    MAP("glBufferSubData",sub_data);
    MAP("glDeleteBuffers",delete_buffers);
#undef MAP
    return NULL;
}
