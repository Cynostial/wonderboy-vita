#include "wb_fps.h"
#include <vitaGL.h>
#include <stdatomic.h>
#include <psp2/kernel/processmgr.h>
extern unsigned DISPLAY_WIDTH,DISPLAY_HEIGHT,DISPLAY_STRIDE;
static atomic_int started;
static atomic_int visible;
static wb_fps counter;
static uint64_t last_present;
/* Display-thread producer; game-thread summaries use atomic exchanges. A
 * sample crossing a summary boundary may land in adjacent windows. No locks,
 * allocation, GPU calls or file writes run in the display callback. */
static atomic_uint present_intervals,present_over20,present_over33,present_over100,present_worst;
static const char *(*localize_message)(uint64_t);
static void wb_fps_present(void *framebuffer) {
    if(!atomic_load_explicit(&started,memory_order_acquire)) return;
    /* Keep the sample current while hidden. Only this display thread touches
     * counter, so toggling cannot race a reset or count the hidden time as a stall. */
    uint64_t now=sceKernelGetProcessTimeWide();
    if(last_present && now>=last_present) {
        uint64_t elapsed=now-last_present;
        uint32_t gap=elapsed>UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
        atomic_fetch_add_explicit(&present_intervals,1,memory_order_relaxed);
        if(gap>20000) atomic_fetch_add_explicit(&present_over20,1,memory_order_relaxed);
        if(gap>33334) atomic_fetch_add_explicit(&present_over33,1,memory_order_relaxed);
        if(gap>100000) atomic_fetch_add_explicit(&present_over100,1,memory_order_relaxed);
        unsigned worst=atomic_load_explicit(&present_worst,memory_order_relaxed);
        while(gap>worst && !atomic_compare_exchange_weak_explicit(&present_worst,&worst,gap,
            memory_order_relaxed,memory_order_relaxed)) {}
    }
    last_present=now;
    wb_fps_sample(&counter,now);
    if(!atomic_load_explicit(&visible,memory_order_relaxed)) return;
    wb_fps_draw(&counter,framebuffer,DISPLAY_WIDTH,DISPLAY_HEIGHT,DISPLAY_STRIDE);
}
void wb_fps_install(void) {
    /* The pinned VitaGL calls this after GPU completion, immediately before
     * sceDisplaySetFrameBuf. Count presented game frames, not the 10 FPS splash. */
    vglSetDisplayCallback(wb_fps_present);
}
/* Called every game swap; visibility is a separate user choice. */
void wb_fps_enable(void) { atomic_store_explicit(&started,1,memory_order_release); }
void wb_fps_toggle(void) { atomic_fetch_xor_explicit(&visible,1,memory_order_relaxed); }
void wb_present_stats(uint32_t *intervals,uint32_t *over20,uint32_t *over33,
                      uint32_t *over100,uint32_t *worst) {
    *intervals=atomic_exchange_explicit(&present_intervals,0,memory_order_relaxed);
    *over20=atomic_exchange_explicit(&present_over20,0,memory_order_relaxed);
    *over33=atomic_exchange_explicit(&present_over33,0,memory_order_relaxed);
    *over100=atomic_exchange_explicit(&present_over100,0,memory_order_relaxed);
    *worst=atomic_exchange_explicit(&present_worst,0,memory_order_relaxed);
}
int wb_fps_bind_localize(const char *(*original)(uint64_t)) {
    if(!original) return -1;
    localize_message=original;return 0;
}
const char *wb_fps_localize(uint64_t key) {
    /* Rate Game's exact localization key in the hash-pinned Android build.
     * Both title Settings and the pause menu use this label and action 0x12. */
    if(key==UINT64_C(0x3ba3b5787d079963)) return "Toggle FPS counter";
    return localize_message(key);
}
