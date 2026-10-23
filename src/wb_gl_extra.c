#include <vitaGL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
static unsigned fences[64];
void wb_buffer_finish(void);
typedef struct { uint64_t timestamp; unsigned used,ready; } query;
static query queries[4096];
static void bad(const char *s) { fprintf(stderr,"Unsupported GL operation: %s\n",s);abort(); }
/* Retain real completion semantics without stalling at fence creation.
 * Only a wait needs completion; one finish completes every earlier fence. */
static void *fence_sync(GLenum condition,unsigned flags) {
    if(condition!=0x9117 || flags) bad("fence condition/flags");
    for(unsigned i=0;i<64;++i) if(!fences[i]) { fences[i]=1;return &fences[i]; }
    bad("fence capacity");return NULL;
}
static unsigned *fence(void *sync) {
    uintptr_t p=(uintptr_t)sync,b=(uintptr_t)fences;
    if(p<b || p-b>=sizeof(fences) || (p-b)%sizeof(*fences) || !*(unsigned *)p)
        bad("invalid fence");
    return (unsigned *)p;
}
static void delete_sync(void *sync) { if(sync) *fence(sync)=0; }
static void wait_sync(void *sync,unsigned flags,uint64_t timeout) {
    (void)fence(sync);
    if(flags || timeout!=UINT64_MAX) bad("wait-sync flags/timeout");
    if(*fence(sync)==1) {
        wb_buffer_finish();
        for(unsigned i=0;i<64;++i) if(fences[i]) fences[i]=2;
    }
}
static void draw_buffers(int n,const GLenum *buffers) {
    /* VitaGL renders to attachment zero or the back buffer. The engine's
     * single-output shaders use exactly this selection. Reject MRT. */
    GLint fbo=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);
    if(n!=1 || !buffers || buffers[0]!=(fbo ? GL_COLOR_ATTACHMENT0 : GL_BACK))
        bad("multiple or disabled draw buffers");
}
/* The engine's profiler requests timestamp queries even during initialization.
 * VitaGL has no timestamp-query implementation. Use completion wall-clock
 * timestamps, synchronizing the GPU first; this is deliberately conservative
 * and may stall when profiling is enabled. It is not a GPU cycle counter. */
static uint64_t completion_time(void) { wb_buffer_finish();return sceKernelGetProcessTimeWide()*1000ull; }
static query *query_object(GLuint id) {
    if(!id || id>4096 || !queries[id-1].used) bad("invalid timestamp query");
    return &queries[id-1];
}
static void gen_queries(GLsizei n,GLuint *ids) {
    if(n<0 || (n && !ids)) bad("query allocation arguments");
    for(int i=0;i<n;++i) {
        unsigned slot=0;while(slot<4096 && queries[slot].used) ++slot;
        if(slot==4096) bad("query capacity");
        queries[slot]=(query){.used=1};ids[i]=slot+1;
    }
}
static void delete_queries(GLsizei n,const GLuint *ids) {
    if(n<0 || (n && !ids)) bad("query deletion arguments");
    for(int i=0;i<n;++i)
        if(ids[i] && ids[i]<=4096) queries[ids[i]-1]=(query){0};
}
static void query_counter(GLuint id,GLenum target) {
    if(target!=0x8e28) bad("query target");
    query *q=query_object(id);q->timestamp=completion_time();q->ready=1;
}
static void query_u64(GLuint id,GLenum pname,uint64_t *out) {
    query *q=query_object(id);
    if(!out) bad("query output");
    if(pname==0x8867) *out=q->ready; /* GL_QUERY_RESULT_AVAILABLE */
    else if(pname==0x8866 && q->ready) *out=q->timestamp;
    else bad("timestamp query result before issue or unsupported parameter");
}
static void query_u32(GLuint id,GLenum pname,GLuint *out) {
    uint64_t value;query_u64(id,pname,&value);*out=(GLuint)value;
}
static void integer64(GLenum pname,int64_t *out) {
    if(!out) bad("integer64 output");
    if(pname==0x8e28) *out=(int64_t)completion_time();
    else { GLint value=0;glGetIntegerv(pname,&value);*out=value; }
}
static void program_integer(GLuint program,GLenum pname,GLint *out) {
    /* Prepared ES 1.00 shaders use ordinary uniforms only. The game asks
     * these ES3 reflection counts unconditionally. This VitaGL build rejects
     * the enums without writing out; a stale positive stack value then makes
     * buildSlotMap call the unavailable uniform-block reflection functions. */
    if(pname==0x8a36 || pname==0x8a35) { /* active blocks / max block-name length */
        if(out) *out=0;
        return;
    }
    glGetProgramiv(program,pname,out);
}
void *wb_gl_extra_proc(const char *name) {
#define MAP(n,f) if(!strcmp(name,n)) return (void *)(uintptr_t)f
    MAP("glFenceSync",fence_sync);
    MAP("glDeleteSync",delete_sync);
    MAP("glWaitSync",wait_sync);
    MAP("glDrawBuffers",draw_buffers);
    MAP("glGenQueries",gen_queries);
    MAP("glDeleteQueries",delete_queries);
    MAP("glQueryCounter",query_counter);
    MAP("glGetQueryObjectuiv",query_u32);
    MAP("glGetQueryObjectui64v",query_u64);
    MAP("glGetInteger64v",integer64);
    MAP("glGetProgramiv",program_integer);
#undef MAP
    return NULL;
}
