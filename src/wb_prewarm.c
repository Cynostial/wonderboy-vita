/* Discover the actual destination's texture dependencies with its original
 * prefetch routine, then complete bounded image loads before play resumes.
 * Speculative neighboring locations and per-frame streaming remain metadata
 * only. No resources are pinned and no guest pointers survive this call. */
#include "wb_prewarm.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
enum { CAPACITY=256, BUDGET=32*1024*1024 };
static wb_prewarm_api api;
static _Atomic unsigned collecting;
static struct { void *raw; unsigned priority; } requests[CAPACITY];
static unsigned count,overflow;
static uint32_t word(const void *p,unsigned offset) {
    uint32_t v;memcpy(&v,(const char *)p+offset,4);return v;
}
static void *pointer(const void *p,unsigned offset) {
    return (void *)(uintptr_t)word(p,offset);
}
int wb_prewarm_bind(const wb_prewarm_api *p) {
    if(!p || !p->manager || !p->get_texture || !p->preload_location ||
       !p->preload_animation || !p->finalize || !p->is_render_thread ||
       !p->estimate || !p->admit) return -1;
    api=*p;return 0;
}
static int active(void) {
    return atomic_load_explicit(&collecting,memory_order_relaxed) && api.is_render_thread();
}
static void collect_raw(void *raw) {
    if(!raw) return;
    for(unsigned i=0;i<count;++i) if(requests[i].raw==raw) return;
    if(count==CAPACITY) { ++overflow;return; }
    const char *path=pointer(raw,4);
    unsigned priority=2;
    if(path && (!strncmp(path,"atlas/Monsters_",15) ||
                !strncmp(path,"atlas/Boss",10) || !strncmp(path,"atlas/Hero",10))) priority=0;
    else if(path && (!strncmp(path,"atlas/Objects",13) ||
                     !strncmp(path,"atlas/GameOver",14) || !strncmp(path,"spine/Fx/",9))) priority=1;
    requests[count].raw=raw;requests[count++].priority=priority;
}
void *wb_prewarm_texture(void *manager,const char *name,uint64_t hash,unsigned flags) {
    void *texture=api.get_texture(manager,name,hash,flags);
    if(flags==2 && texture && active()) collect_raw(pointer(texture,0x98));
    return texture;
}
void wb_prewarm_animation(void *set,int colors,int first) {
    if(!active()) { api.preload_animation(set,colors,first);return; }
    /* PreloadTextures has a transient per-set visited flag. Collect a set
     * even if UpdateStreaming visited it earlier in this frame, and restore
     * that flag exactly. The ordinary frame path is left untouched. */
    unsigned char *visited=(unsigned char *)set+0x135;
    unsigned char saved=*visited;*visited=0;
    api.preload_animation(set,colors,first);
    *visited=saved;
}
void wb_prewarm_location(void *game) {
    if(!api.manager || !game || (word(game,0x20)!=2 && word(game,0x20)!=3) ||
       word(game,0x114)>=250 || !api.is_render_thread() ||
       atomic_load_explicit(&collecting,memory_order_relaxed)) return;
    unsigned location=word(game,0x114),bytes=0,loaded=0,resident=0,skipped=0;
    uint64_t start=api.clock_us ? api.clock_us() : 0;
    count=overflow=0;
    atomic_store_explicit(&collecting,1,memory_order_relaxed);
    /* A null position excludes the original neighboring-door recursion. */
    api.preload_location(game,(int)location,NULL);
    /* The Spine effects page is not enumerated by PreloadLoc's animation
     * sets. Its first hit/landing use otherwise performs a synchronous image
     * load during play. Fx.atlas names Fx.png; GetTexture's raw fallback
     * resolves that same page as Spine's loader. Collect metadata only here,
     * then use the same total budget and GPU admission as every other page. */
    wb_prewarm_texture(api.manager,"spine/Fx/Fx.png",0,2);
    atomic_store_explicit(&collecting,0,memory_order_relaxed);
    /* Original PreloadLoc skips already resident static decoration images.
     * Protect those too before reclaiming any old scene pages. The current
     * resolved location descriptor is the same one DrawLocationMain uses.
     * Missing/oversized graphs conservatively disable reclamation. */
    void *loc=pointer(game,0x128);
    unsigned decorations=loc ? word(loc,0x24) : 0;
    void *objects=loc ? pointer(loc,0x2c) : 0;
    int complete=loc && decorations<=4096 && (!decorations || objects);
    if(complete) for(unsigned i=0;i<decorations;++i) {
        void *obj=pointer(objects,i*4);
        if(obj && word(obj,4)!=2 && word(obj,4)!=4) {
            void *texture=pointer(obj,0xc8);
            if(texture) collect_raw(pointer(texture,0x98));
        }
    }
    void *keep[CAPACITY];for(unsigned i=0;i<count;++i) keep[i]=requests[i].raw;
    if(api.trim_begin) api.trim_begin();
    for(unsigned priority=0;priority<3;++priority) for(unsigned i=0;i<count;++i) {
        if(requests[i].priority!=priority) continue;
        void *raw=requests[i].raw;
        unsigned cost=api.estimate(pointer(raw,4));
        if(!cost || cost>BUDGET-bytes) { ++skipped;continue; }
        bytes+=cost;
        if(pointer(raw,0xa8)) { ++resident;continue; }
        int admitted=api.trim && complete && !overflow ? api.trim(cost,keep,count) : api.admit(cost);
        if(!admitted) { ++skipped;continue; }
        api.finalize(api.manager,raw);
        if(pointer(raw,0xa8)) ++loaded;else ++skipped;
    }
    fprintf(stderr,"Level texture prewarm: location=%u unique=%u loaded=%u resident=%u skipped=%u overflow=%u budgeted=%uKiB time=%llu us\n",
            location,count,loaded,resident,skipped,overflow,bytes/1024,
            (unsigned long long)(api.clock_us ? api.clock_us()-start : 0));
    memset(requests,0,sizeof(requests));count=0;
}
