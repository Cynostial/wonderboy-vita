/* Five-second summaries from the actual game. CPU clocks around draw calls
 * do not wait for GPU completion or re-enable the guest GPU profiler. */
#include <SDL2/SDL.h>
#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <stdio.h>
#include "wb_fps.h"
#include "wb_pacing.h"
#include "wb_log_vita.h"
void wb_buffer_wait_stats(uint32_t *,uint64_t *);
void wb_buffer_rename_stats(uint32_t *,uint32_t *,uint32_t *);
size_t wb_gl_texture_bytes(void);
uint32_t wb_gl_draw_calls(void);
void wb_gl_draw_times(uint64_t *,uint64_t *);
void wb_gl_upload_stats(uint64_t *,uint64_t *,uint32_t *,uint32_t *,uint64_t *,uint64_t *);
void wb_fps_enable(void);
void wb_gl_scene_stats(uint64_t *,uint64_t *,uint32_t *,uint32_t *);
void wb_buffer_scan_stats(uint32_t *,uint32_t *);
int wb_set_swap_interval(int interval) {
    /* The pinned Vita EGL accepts signed -1 but stores it in an unsigned
     * VBlank count. It does not implement EXT_swap_control_tear. Report that
     * honestly so the game's existing fallback selects regular interval 1. */
    if(interval<0) return SDL_SetError("Adaptive VSync is unavailable on Vita");
    int result=SDL_GL_SetSwapInterval(interval);
    if(!result) __atomic_store_n(&wb_pacing_vblank,interval>0,__ATOMIC_RELEASE);
    return result;
}
void wb_swap_window(SDL_Window *window) {
    static uint64_t begin,last,swap_us,last_wait,last_setup,last_submit;
    static uint64_t last_image,last_subimage,last_image_bytes,last_subimage_bytes;
    static uint32_t last_images,last_subimages;
    static uint64_t last_clear,last_attachment;
    static uint32_t last_clears,last_attachments,last_hits,last_scanned;
    static unsigned frames,slow,last_draws,last_waits,last_renames,last_copies,last_fallbacks;
    uint64_t before=sceKernelGetProcessTimeWide();
    wb_fps_enable();
    SDL_GL_SwapWindow(window);
    uint64_t now=sceKernelGetProcessTimeWide();
    if(!begin) {
        begin=last=now;
        wb_buffer_wait_stats(&last_waits,&last_wait);
        wb_buffer_rename_stats(&last_renames,&last_copies,&last_fallbacks);
        last_draws=wb_gl_draw_calls();
        wb_gl_draw_times(&last_setup,&last_submit);
        wb_gl_upload_stats(&last_image,&last_subimage,&last_images,&last_subimages,
                           &last_image_bytes,&last_subimage_bytes);
        wb_gl_scene_stats(&last_clear,&last_attachment,&last_clears,&last_attachments);
        wb_buffer_scan_stats(&last_hits,&last_scanned);
        return;
    }
    swap_us+=now-before;++frames;
    if(now-last>20000) ++slow;
    last=now;
    if(now-begin<5000000) return;
    uint32_t waits,renames,copies,fallbacks;uint64_t wait,setup,submit;
    wb_buffer_wait_stats(&waits,&wait);
    wb_buffer_rename_stats(&renames,&copies,&fallbacks);
    uint32_t draws=wb_gl_draw_calls();
    wb_gl_draw_times(&setup,&submit);
    uint64_t image,subimage,image_bytes,subimage_bytes;uint32_t images,subimages;
    wb_gl_upload_stats(&image,&subimage,&images,&subimages,&image_bytes,&subimage_bytes);
    uint64_t clear,attachment;uint32_t clears,attachments,hits,scanned;
    wb_gl_scene_stats(&clear,&attachment,&clears,&attachments);
    wb_buffer_scan_stats(&hits,&scanned);
    fprintf(stderr,"Frame stats: fps=%.2f over20ms=%u/%u swap=%.2fms buffer_wait=%.2fms waits=%u renames=%u copied=%uKiB fallbacks=%u draws/frame=%.1f draw_setup=%.2fms draw_submit=%.2fms image=%.2fms subimage=%.2fms images/frame=%.1f subimages/frame=%.1f image32/frame=%.0fKiB subimage32/frame=%.0fKiB textures=%uKiB free_gpu=%uKiB free_ram=%uKiB\n",
        frames*1000000.0/(double)(now-begin),slow,frames,swap_us/(1000.0*frames),
        (wait-last_wait)/(1000.0*frames),waits-last_waits,renames-last_renames,
        (copies-last_copies)/1024,fallbacks-last_fallbacks,(draws-last_draws)/(double)frames,
        (setup-last_setup)/(1000.0*frames),(submit-last_submit)/(1000.0*frames),
        (image-last_image)/(1000.0*frames),(subimage-last_subimage)/(1000.0*frames),
        (images-last_images)/(double)frames,(subimages-last_subimages)/(double)frames,
        (image_bytes-last_image_bytes)/(1024.0*frames),(subimage_bytes-last_subimage_bytes)/(1024.0*frames),
        (unsigned)(wb_gl_texture_bytes()/1024),(unsigned)(vglMemFree(VGL_MEM_VRAM)/1024),
        (unsigned)(vglMemFree(VGL_MEM_RAM)/1024));
    fprintf(stderr,"Scene stats: clear=%.2fms clears/frame=%.1f attachment=%.2fms attachments/frame=%.1f index_hits/frame=%.1f scanned/frame=%.0f\n",
        (clear-last_clear)/(1000.0*frames),(clears-last_clears)/(double)frames,
        (attachment-last_attachment)/(1000.0*frames),(attachments-last_attachments)/(double)frames,
        (hits-last_hits)/(double)frames,(scanned-last_scanned)/(double)frames);
    uint32_t intervals,over20,over33,over100,worst;
    wb_present_stats(&intervals,&over20,&over33,&over100,&worst);
    fprintf(stderr,"Present stats: intervals=%u over20ms=%u over33ms=%u over100ms=%u worst=%.2fms\n",
        intervals,over20,over33,over100,worst/1000.0);
    unsigned dropped,errors;wb_log_stats(&dropped,&errors);
    fprintf(stderr,"Log stats: dropped=%u errors=%u\n",dropped,errors);
    begin=now;frames=slow=0;swap_us=0;last_wait=wait;last_waits=waits;
    last_renames=renames;last_copies=copies;last_fallbacks=fallbacks;last_draws=draws;
    last_setup=setup;last_submit=submit;
    last_image=image;last_subimage=subimage;last_images=images;last_subimages=subimages;
    last_image_bytes=image_bytes;last_subimage_bytes=subimage_bytes;
    last_clear=clear;last_attachment=attachment;last_clears=clears;last_attachments=attachments;
    last_hits=hits;last_scanned=scanned;
}
