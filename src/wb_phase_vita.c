/* Direct measurements of the actual release build, independent of whether
 * its optional Profiler singleton/marker producer exists. Original methods
 * and return values are preserved. Only the main game thread records costs. */
#include "wb_phase.h"
#include "wb_texture_reuse.h"
#include "wb_blocks.h"
#include "so_util.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
static wb_phase_totals totals;
static _Atomic int owner;
static uint64_t period;
static uint64_t worst_frame;
static unsigned frames_over_33ms,frames_over_100ms;
enum { FRAME,SDL,CORE,STATE,PAINT,END,AFTER,EMU,GAME,ANIM,DRAW,CONTEXT,PREDRAW,GUI,VRAM,
       RESET,LOADSTATE,STREAM,METRICS,UI,SCENE,BINDINGS,AUDIO,BLOCKS,COUNT };
static const char *names[COUNT]={"frame","sdl","core","state","paint","end","after",
    "emu","game","anim","draw","context","predraw","gui","vram",
    "reset","loadstate","stream","metrics","ui","scene","bindings","audio","blocks"};
void wb_retro_begin(void *);
void wb_retro_end(void);
void wb_retro_invalidate(void);
void wb_retro_update(void *);
void wb_retro_stats(uint32_t *,uint32_t *);
void wb_z80_wait_stats(uint32_t *,uint32_t *,uint32_t *);
static int enter(unsigned id) {
    int tid=atomic_load_explicit(&owner,memory_order_relaxed);
    return tid && tid==sceKernelGetThreadId() &&
        wb_phase_enter(&totals,id,sceKernelGetProcessTimeWide());
}
static void leave(int active) { if(active) wb_phase_leave(&totals,sceKernelGetProcessTimeWide()); }
#define THIS_WRAPPER(name,id) \
    static void (*original_##name)(void *); \
    static void name(void *p) { int active=enter(id);original_##name(p);leave(active); }
#define STATIC_WRAPPER(name,id) \
    static void (*original_##name)(void); \
    static void name(void) { int active=enter(id);original_##name();leave(active); }
THIS_WRAPPER(sdl,SDL)
THIS_WRAPPER(core,CORE)
THIS_WRAPPER(paint,PAINT)
THIS_WRAPPER(end,END)
THIS_WRAPPER(after,AFTER)
THIS_WRAPPER(emu,EMU)
static void (*original_game)(void *);
static void game(void *p) {
    int active=enter(GAME);wb_texture_reuse_begin();original_game(p);wb_texture_reuse_end();leave(active);
}
THIS_WRAPPER(anim,ANIM)
THIS_WRAPPER(draw,DRAW)
THIS_WRAPPER(predraw,PREDRAW)
THIS_WRAPPER(stream,STREAM)
THIS_WRAPPER(metrics,METRICS)
THIS_WRAPPER(ui,UI)
THIS_WRAPPER(scene,SCENE)
STATIC_WRAPPER(bindings,BINDINGS)
static void (*original_audio)(void *,float);
static void audio(void *p,float dt) {
    int active=enter(AUDIO);original_audio(p,dt);leave(active);
}
static void (*original_blocks)(void *,int,const void *);
static void blocks(void *p,int layer,const void *clip) {
    int active=enter(BLOCKS);wb_blocks_build(p,layer,clip);leave(active);
}
static void (*original_vram)(void *);
static void vram(void *p) {
    int active=enter(VRAM);wb_retro_update(p);leave(active);
}
static void (*original_reset)(void *);
static void reset(void *p) {
    int active=enter(RESET);wb_blocks_invalidate();wb_retro_invalidate();original_reset(p);wb_retro_invalidate();leave(active);
}
static void (*original_loadstate)(void *,void *);
static void loadstate(void *p,void *file) {
    int active=enter(LOADSTATE);wb_blocks_invalidate();wb_retro_invalidate();original_loadstate(p,file);
    wb_retro_invalidate();leave(active);
}
STATIC_WRAPPER(context,CONTEXT)
STATIC_WRAPPER(gui,GUI)
static int (*original_state)(void *,int);
static int state(void *p,int delta) {
    int active=enter(STATE);int result=original_state(p,delta);leave(active);return result;
}
static void (*original_frame)(void *);
static void frame(void *p) {
    atomic_store_explicit(&owner,sceKernelGetThreadId(),memory_order_relaxed);
    uint64_t now=sceKernelGetProcessTimeWide();
    uint64_t frame_begin=now;
    if(!period) period=now;
    int active=wb_phase_enter(&totals,FRAME,now);
    original_frame(p);
    leave(active);
    atomic_store_explicit(&owner,0,memory_order_relaxed);
    now=sceKernelGetProcessTimeWide();
    uint64_t elapsed=now-frame_begin;
    if(elapsed>worst_frame) worst_frame=elapsed;
    frames_over_33ms+=elapsed>33333;
    frames_over_100ms+=elapsed>100000;
    if(now-period<5000000 || !totals.cost[FRAME].calls) return;
    /* At most one bounded line per five seconds, after the completed frame. */
    char line[2304];
    unsigned frames=totals.cost[FRAME].calls;
    int n=snprintf(line,sizeof(line),"Game phases: frames=%u dropped=%u",frames,totals.dropped);
    for(unsigned i=0;i<COUNT;++i) if(n>=0 && (unsigned)n<sizeof(line))
        n+=snprintf(line+n,sizeof(line)-(unsigned)n," | %s=%.2f/%.2fms(%.1f)",names[i],
            totals.cost[i].total/(1000.0*frames),totals.cost[i].self/(1000.0*frames),
            totals.cost[i].calls/(double)frames);
    fprintf(stderr,"%s\n",line);
    fprintf(stderr,"Frame deadlines: worst=%.2fms over33ms=%u over100ms=%u frames=%u\n",
        worst_frame/1000.0,frames_over_33ms,frames_over_100ms,frames);
    static uint32_t prior_decoded,prior_unchanged;
    uint32_t decoded,unchanged;wb_retro_stats(&decoded,&unchanged);
    fprintf(stderr,"Retro tiles/frame: decoded=%.1f unchanged=%.1f\n",
            (decoded-prior_decoded)/(double)frames,(unchanged-prior_unchanged)/(double)frames);
    prior_decoded=decoded;prior_unchanged=unchanged;
    static uint32_t prior_calls,prior_fast,prior_loops;
    uint32_t calls,fast,loops;wb_z80_wait_stats(&calls,&fast,&loops);
    fprintf(stderr,"Z80 wait batching/frame: calls=%.1f fast=%.1f skipped_loops=%.1f\n",
            (calls-prior_calls)/(double)frames,(fast-prior_fast)/(double)frames,
            (loops-prior_loops)/(double)frames);
    prior_calls=calls;prior_fast=fast;prior_loops=loops;
    static uint32_t prior_blocks,prior_hits,prior_bypasses;
    uint32_t block_calls,block_hits,block_bypasses;
    wb_blocks_stats(&block_calls,&block_hits,&block_bypasses);
    fprintf(stderr,"Block lists/frame: builds=%.2f reused=%.2f dynamic=%.2f\n",
        (block_calls-prior_blocks-block_hits+prior_hits)/(double)frames,
        (block_hits-prior_hits)/(double)frames,(block_bypasses-prior_bypasses)/(double)frames);
    prior_blocks=block_calls;prior_hits=block_hits;prior_bypasses=block_bypasses;
    memset(&totals,0,sizeof(totals));period=now;
    worst_frame=0;frames_over_33ms=frames_over_100ms=0;
}
typedef struct {
    const char *symbol,*vtable;
    unsigned slot;
    uintptr_t replacement;
    void *original;
} binding;
#define BIND(name,symbol,table,offset) {symbol,table,offset,(uintptr_t)name,&original_##name}
static const binding hooks[]={
    BIND(frame,"_ZN9WbGameApp7executeEv","_ZTV9WbGameApp",0x50),
    BIND(paint,"_ZN5Pasta11Application8appPaintEv","_ZTV9WbGameApp",0x34),
    BIND(end,"_ZN9WbGameApp8endFrameEv","_ZTV9WbGameApp",0x2c),
    BIND(after,"_ZN9WbGameApp10afterPaintEv","_ZTV9WbGameApp",0x38),
    BIND(state,"_ZN11WbGameState16executeGameStateEl","_ZTV11WbGameState",0xa4),
    BIND(sdl,"_ZN5Pasta14SDLApplication7executeEv",NULL,0),
    BIND(core,"_ZN5Pasta11Application7executeEv",NULL,0),
    BIND(emu,"_ZN8Emulator8RunFrameEv",NULL,0),
    BIND(game,"_ZN5CGame6UpdateEv",NULL,0),
    BIND(anim,"_ZN5CGame17UpdateAnimPlayersEv",NULL,0),
    BIND(draw,"_ZN9CRenderer22RenderPerformRenderingEv",NULL,0),
    BIND(context,"_ZN5Pasta17OGLGraphicContext10StartFrameEv",NULL,0),
    BIND(predraw,"_ZN5Pasta17OGLGraphicContext7preDrawEv",NULL,0),
    BIND(gui,"_ZN5Pasta6ImGuiH7executeEv",NULL,0),
    BIND(vram,"_ZN8Emulator13UpdateVramTexEv",NULL,0),
    BIND(reset,"_ZN8Emulator5ResetEv",NULL,0),
    BIND(loadstate,"_ZN8Emulator13LoadSavestateEP7__sFILE",NULL,0),
    BIND(stream,"_ZN5CGame15UpdateStreamingEv",NULL,0),
    BIND(metrics,"_ZN5CGame13UpdateMetricsEv",NULL,0),
    BIND(ui,"_ZN3CUi6UpdateEv",NULL,0),
    BIND(scene,"_Z16DrawLocationMainP10ImDrawList",NULL,0),
    BIND(bindings,"_Z21AnimUpdateAllBindingsv",NULL,0),
    BIND(audio,"_ZN12CAudioSystem6UpdateEf",NULL,0),
    BIND(blocks,"_Z15DrawBlocksBuildP10ImDrawListiPK6ImRect",NULL,0),
};
int wb_phase_bind(so_module *mod) {
    uintptr_t original[COUNT],target[COUNT];
    _Static_assert(sizeof(hooks)/sizeof(*hooks)==COUNT,"phase hook count");
    for(unsigned i=0;i<COUNT;++i) {
        original[i]=so_symbol(mod,hooks[i].symbol);
        uintptr_t vt=hooks[i].vtable ? so_symbol(mod,hooks[i].vtable) : 0;
        target[i]=hooks[i].vtable ? (vt ? vt+8+hooks[i].slot : 0) : so_trampoline_symbol(mod,hooks[i].symbol);
        if(!original[i] || !target[i] || (vt && *(uintptr_t *)target[i]!=original[i])) return -1;
    }
    for(unsigned i=0;i<COUNT;++i) {
        memcpy(hooks[i].original,&original[i],sizeof(uintptr_t));
        if(hooks[i].vtable) *(uintptr_t *)target[i]=hooks[i].replacement;
        else hook_addr(target[i],hooks[i].replacement);
    }
    return 0;
}
