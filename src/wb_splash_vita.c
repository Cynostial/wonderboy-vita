/* GXM lifecycle adapted from vitaGL splashscreen.c, Copyright 2017-2020
 * Rinnegatamante; this file is licensed LGPL-3.0-or-later. */
/* Dedicated startup GXM context, following VitaGL's semaphore/display-queue
 * lifecycle. The library remains unchanged; wrap only invoke/clear. */
#include "wb_splash_art.h"
#include "wb_paths.h"
#include <vitaGL.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
extern SceUID splash_mutex[2];
extern GLboolean is_splashscreen_active;
extern SceGxmRenderTarget *gxm_render_target;
extern SceGxmSyncObject *gxm_sync_objects[];
extern SceGxmColorSurface gxm_color_surfaces[];
extern SceGxmDepthStencilSurface gxm_depth_stencil_surface;
extern void *gxm_color_surfaces_addr[];
extern unsigned gxm_front_buffer_index,gxm_back_buffer_index;
extern uint8_t gxm_display_buffer_count;
extern SceGxmVertexProgram *clear_vertex_program_patched;
extern SceGxmFragmentProgram *clear_fragment_program_patched;
extern SceGxmVertexProgram *blit_vertex_program_patched;
extern SceGxmFragmentProgram *blit_fragment_program_patched;
extern const SceGxmProgramParameter *clear_position,*clear_depth,*clear_color;
extern uint16_t *depth_clear_indices;
extern void *vdm_ring_buffer_addr[],*vertex_ring_buffer_addr[],*fragment_ring_buffer_addr[];
extern void *fragment_usse_ring_buffer_addr[],*context_host_mem[];
extern void init_gxm_context(SceGxmContext **,uint8_t);
extern void gpu_fragment_usse_free_mapped(void *);
extern void vgl_free(void *);
static SceGxmContext *context;
static void *pixels;
static float *vertices;
static SceGxmTexture texture;
extern void *vgl_memalign(size_t,size_t,vglMemType);
extern void sceGxmSetViewport_sfp(SceGxmContext *,float,float,float,float,float,float);
static atomic_int programs_ready;
void wb_splash_programs_ready(void) { atomic_store_explicit(&programs_ready,1,memory_order_release); }
static void rect(int x,int y,int w,int h,uint32_t rgb) {
    float pos[]={2.0f*x/960-1,2.0f*(x+w)/960-1,1-2.0f*y/544,1-2.0f*(y+h)/544};
    float rgba[]={((rgb>>16)&255)/255.0f,((rgb>>8)&255)/255.0f,(rgb&255)/255.0f,1};
    const float depth=1;
    void *v,*f;
    sceGxmReserveVertexDefaultUniformBuffer(context,&v);
    sceGxmSetUniformDataF(v,clear_position,0,4,pos);
    sceGxmSetUniformDataF(v,clear_depth,0,1,&depth);
    sceGxmReserveFragmentDefaultUniformBuffer(context,&f);
    sceGxmSetUniformDataF(f,clear_color,0,4,rgba);
    sceGxmDraw(context,SCE_GXM_PRIMITIVE_TRIANGLE_FAN,SCE_GXM_INDEX_FORMAT_U16,depth_clear_indices,4);
}
static int run(unsigned args,void *arg) {
    (void)args;(void)arg;
    /* invoke_splashscreen precedes VitaGL's blit-program registration.
     * Wait for the actual initializer to return, not an assumed time delay. */
    while(!atomic_load_explicit(&programs_ready,memory_order_acquire)) {
        unsigned timeout=10000;
        if(!sceKernelWaitSema(splash_mutex[0],1,&timeout)) goto acknowledge;
    }
    init_gxm_context(&context,1);
    sceGxmSetCullMode(context,SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthWriteEnable(context,SCE_GXM_DEPTH_WRITE_ENABLED);
    sceGxmSetFrontDepthFunc(context,SCE_GXM_DEPTH_FUNC_ALWAYS);
    /* Private immutable allocations remain alive until this context finishes. */
    pixels=vgl_memalign(16,960*544*4,VGL_MEM_RAM);
    vertices=vgl_memalign(16,16*sizeof(float),VGL_MEM_RAM);
    FILE *f=fopen(WB_ROOT "/overlays/loading.rgba","rb");
    if(!f)f=fopen("app0:art/loading.rgba","rb");
    int ready=pixels && vertices && f && fread(pixels,1,960*544*4,f)==960*544*4;
    if(f) fclose(f);
    if(ready) {
        const float quad[]={-1,1,-1,-1,1,-1,1,1, 0,0,0,1,1,1,1,0};
        memcpy(vertices,quad,sizeof(quad));
        ready=sceGxmTextureInitLinear(&texture,pixels,SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,960,544,1)>=0;
        sceGxmTextureSetMinFilter(&texture,SCE_GXM_TEXTURE_FILTER_POINT);
        sceGxmTextureSetMagFilter(&texture,SCE_GXM_TEXTURE_FILTER_POINT);
        sceGxmTextureSetUAddrMode(&texture,SCE_GXM_TEXTURE_ADDR_CLAMP);
        sceGxmTextureSetVAddrMode(&texture,SCE_GXM_TEXTURE_ADDR_CLAMP);
    }
    if(!ready) fprintf(stderr,"Official loading artwork unavailable\n");
    unsigned phase=0;
    for(;;) {
        unsigned timeout=100000; /* 10 fps leaves CPU time for game startup. */
        if(!sceKernelWaitSema(splash_mutex[0],1,&timeout)) break;
        sceGxmBeginScene(context,0,gxm_render_target,NULL,NULL,gxm_sync_objects[gxm_back_buffer_index],
                         &gxm_color_surfaces[gxm_back_buffer_index],&gxm_depth_stencil_surface);
        sceGxmSetViewport_sfp(context,480,480,272,-272,0.5f,0.5f);
        if(ready) {
            sceGxmSetVertexProgram(context,blit_vertex_program_patched);
            sceGxmSetFragmentProgram(context,blit_fragment_program_patched);
            sceGxmSetFragmentTexture(context,0,&texture);
            sceGxmSetVertexStream(context,0,vertices);
            sceGxmDraw(context,SCE_GXM_PRIMITIVE_TRIANGLE_FAN,SCE_GXM_INDEX_FORMAT_U16,depth_clear_indices,4);
        }
        sceGxmSetVertexProgram(context,clear_vertex_program_patched);
        sceGxmSetFragmentProgram(context,clear_fragment_program_patched);
        if(!ready) rect(0,0,960,544,0xffffff);
        wb_splash_art(rect,phase++);
        sceGxmEndScene(context,NULL,NULL);
        struct { void *addr; } cb={gxm_color_surfaces_addr[gxm_back_buffer_index]};
        sceGxmDisplayQueueAddEntry(gxm_sync_objects[gxm_front_buffer_index],gxm_sync_objects[gxm_back_buffer_index],&cb);
        gxm_front_buffer_index=gxm_back_buffer_index;
        gxm_back_buffer_index=(gxm_back_buffer_index+1)%gxm_display_buffer_count;
    }
    sceGxmFinish(context);
    if(pixels) vglFree(pixels);
    if(vertices) vglFree(vertices);
    sceGxmDestroyContext(context);
    vgl_free(vdm_ring_buffer_addr[1]);vgl_free(vertex_ring_buffer_addr[1]);
    vgl_free(fragment_ring_buffer_addr[1]);vgl_free(context_host_mem[1]);
    gpu_fragment_usse_free_mapped(fragment_usse_ring_buffer_addr[1]);
    /* Acknowledge only after this context can no longer access shared state. */
acknowledge:
    sceKernelSignalSema(splash_mutex[1],1);
    return sceKernelExitDeleteThread(0);
}
void __wrap_invoke_splashscreen(void) {
    atomic_store_explicit(&programs_ready,0,memory_order_relaxed);
    splash_mutex[0]=sceKernelCreateSema("Wonder loading stop",0,0,1,NULL);
    splash_mutex[1]=sceKernelCreateSema("Wonder loading done",0,0,1,NULL);
    if(splash_mutex[0]<0 || splash_mutex[1]<0) goto fail;
    SceUID thread=sceKernelCreateThread("Wonder loading",run,64,0x10000,0,SCE_KERNEL_CPU_MASK_USER_ALL,NULL);
    if(thread<0) goto fail;
    if(sceKernelStartThread(thread,0,NULL)<0) { sceKernelDeleteThread(thread);goto fail; }
    is_splashscreen_active=GL_TRUE;
    fprintf(stderr,"Official Wonder Boy artwork active (10 fps, seven draws, no extra start delay)\n");
    return;
fail:
    if(splash_mutex[0]>=0) sceKernelDeleteSema(splash_mutex[0]);
    if(splash_mutex[1]>=0) sceKernelDeleteSema(splash_mutex[1]);
    fprintf(stderr,"Loading animation unavailable; continuing game startup\n");
}
void __wrap_clear_splashscreen(void) { /* No extra shader registrations. */ }
