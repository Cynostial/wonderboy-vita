#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include "wb_gpu_completion.h"
#define MIB (1024*1024)
void wb_buffer_bind_rename(int (*)(GLuint,size_t,unsigned char **),void (*)(void));
int wb_buffer_rename_native(GLuint,size_t,unsigned char **);
int wb_buffer_retire_native(GLuint,size_t);
void wb_buffer_bind_retire(int (*)(GLuint,size_t));
void wb_buffer_release_native(void);
void wb_buffer_bind_clock(uint64_t (*)(void));
void wb_gl_bind_clock(uint64_t (*)(void));
void wb_splash_programs_ready(void);
void wb_fps_install(void);
/* Pinned VitaGL increments this fragment-completion notification in every
 * scene_end, including framebuffer switches and the display swap path. */
extern SceGxmNotification query_fence;
static int wait_latest_scene(void) { return sceGxmNotificationWait(&query_fence); }
extern int __real_sceGxmEndScene(SceGxmContext *,const SceGxmNotification *,const SceGxmNotification *);
int __wrap_sceGxmEndScene(SceGxmContext *context,const SceGxmNotification *vertex,const SceGxmNotification *fragment) {
    int result=__real_sceGxmEndScene(context,vertex,fragment);
    /* VitaGL advances its counter before EndScene. A failed submission has
     * no future notification: never wait for, or trust, that counter. */
    if(result<0 && fragment==&query_fence) wb_gpu_completion_disable();
    return result;
}
extern SceUID __real_sceKernelAllocMemBlock(const char *,SceKernelMemBlockType,SceSize,SceKernelAllocMemBlockOpt *);
static _Noreturn void pool_error(void) {
    fprintf(stderr,"VitaGL memory pool initialization failed\n");
    fflush(stderr);sceKernelExitProcess(1);abort();
}
/* VitaGL's allocator retains a nonzero requested size when allocation fails.
 * Catch the real kernel error before it can fall back silently or query a
 * NULL mspace. Other kernel allocations preserve their normal result. */
SceUID __wrap_sceKernelAllocMemBlock(const char *name,SceKernelMemBlockType type,SceSize size,SceKernelAllocMemBlockOpt *options) {
    SceUID result=__real_sceKernelAllocMemBlock(name,type,size,options);
    if(name && (!strcmp(name,"cdram_mempool") || !strcmp(name,"ram_mempool"))) {
        fprintf(stderr,"VitaGL pool %s: %u bytes, kernel result 0x%08x\n",name,(unsigned)size,(unsigned)result);
        if(result<0) pool_error();
    }
    return result;
}
/* SDL's Vita backend owns context initialization. Replace its default
 * "consume all free RAM" sizing at link time, without initializing twice. */
GLboolean __wrap_vglInitExtended(int legacy,int width,int height,int threshold,SceGxmMultisampleMode msaa) {
    (void)legacy;(void)width;(void)height;(void)threshold;(void)msaa;
    vglUseExtraMem(GL_FALSE);
    vglUseTripleBuffering(GL_TRUE);
    wb_buffer_bind_rename(wb_buffer_rename_native,wb_buffer_release_native);
    wb_buffer_bind_retire(wb_buffer_retire_native);
    wb_buffer_bind_clock(sceKernelGetProcessTimeWide);
    wb_gl_bind_clock(sceKernelGetProcessTimeWide);
    vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
    /* VitaGL returns whether it clamped the resolution, NOT a success flag.
     * A normal 960x544 initialization returns GL_FALSE. SDL ignores this
     * value; preserve it for any other callers of the wrapped function. */
    /* GXM consumes a separate 16 MiB CDRAM parameter buffer before pools:
     * 112 MiB app CDRAM = 16 MiB GXM + 96 MiB VitaGL. */
    GLboolean resolution_fallback=vglInitWithCustomSizes(0,960,544,64*MIB,96*MIB,0,0,SCE_GXM_MULTISAMPLE_NONE);
    wb_gpu_completion_bind(&query_fence.value,query_fence.address,glFlush,wait_latest_scene);
    wb_splash_programs_ready();
    wb_fps_install();
    size_t ram=vglMemTotal(VGL_MEM_RAM),vram=vglMemTotal(VGL_MEM_VRAM);
    size_t ram_free=vglMemFree(VGL_MEM_RAM),vram_free=vglMemFree(VGL_MEM_VRAM);
    fprintf(stderr,"VitaGL initialized: RAM %u (free %u), CDRAM %u (free %u) bytes; resolution fallback %u\n",
            (unsigned)ram,(unsigned)ram_free,(unsigned)vram,(unsigned)vram_free,
            (unsigned)resolution_fallback);
    if(ram!=64*MIB || vram!=96*MIB || !ram_free || !vram_free) {
        fprintf(stderr,"VitaGL pools are unavailable or exhausted after initialization\n");
        pool_error();
    }
    return resolution_fallback;
}
