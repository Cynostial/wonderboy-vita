#include "wb_cpu_profile.h"
#include "so_util.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
static void **profiler;
static void *(*current_thread)(void *);
static wb_cpu_totals totals;
static uint64_t last_sample;
/* Verified against CpuThreadInfoBase::startFrame/pushMarker/popMarker and
 * ProfilerGUI::buildFrameData in the SHA-pinned ARM library. */
typedef struct {
    uint32_t vtable,depth,next,stop,capacity,frames,frame,thread;
    const uint32_t *frame_indices;
    const char *const *names;
    const uint32_t *hashes;
    const unsigned char *depths;
    unsigned char reserved[0x150-0x30];
    const uint32_t *frame_times,*starts,*ends;
} guest_cpu;
_Static_assert(offsetof(guest_cpu,starts)==0x154,"Pinned ARM CPU profiler layout");
int wb_cpu_profile_bind(so_module *mod) {
    profiler=(void **)so_symbol(mod,"_ZN5Pasta8Profiler11s_singletonE");
    current_thread=(void *(*)(void *))so_symbol(mod,"_ZN5Pasta8Profiler23getCurrentCpuThreadInfoEv");
    return !profiler || !current_thread ? -1 : 0;
}
void wb_cpu_profile_sample(uint64_t now) {
    /* Two snapshots/second of completed frames, only on the render thread.
     * The game's profiler already collected these timestamps. */
    if(now-last_sample<500000 || !profiler || !*profiler || !current_thread) return;
    last_sample=now;
    const guest_cpu *g=current_thread(*profiler);
    if(!g || g->frames<2 || g->frames>1024 || !g->frame ||
       !g->frame_indices || !g->frame_times) return;
    unsigned previous=(g->frame-1)%g->frames,current=g->frame%g->frames;
    wb_cpu_view view={
        g->capacity,g->frame_indices[previous],g->frame_indices[current],
        g->frame_times[previous],g->frame_times[current]-g->frame_times[previous],
        g->names,g->depths,g->starts,g->ends
    };
    (void)wb_cpu_collect(&totals,&view);
}
void wb_cpu_profile_report(void) {
    uint64_t frequency=SDL_GetPerformanceFrequency();
    if(!totals.samples || !frequency) return;
    double scale=1000.0/(frequency*totals.samples);
    /* One bounded write every five seconds, never per marker/per frame. */
    char line[1536];
    int n=snprintf(line,sizeof(line),"CPU samples: n=%u frame=%.2fms skipped=%u",
                   totals.samples,totals.frame_ticks*scale,totals.skipped);
    unsigned selected[8],count=totals.used<8 ? totals.used : 8;
    for(unsigned rank=0;rank<count;++rank) {
        int best=-1;
        for(unsigned i=0;i<totals.used;++i) {
            int taken=0;
            for(unsigned j=0;j<rank;++j) if(selected[j]==i) taken=1;
            if(!taken && (best<0 || totals.costs[i].self>totals.costs[best].self)) best=(int)i;
        }
        selected[rank]=(unsigned)best;
        wb_cpu_cost *c=&totals.costs[best];
        if(n>=0 && (unsigned)n<sizeof(line))
            n+=snprintf(line+n,sizeof(line)-(unsigned)n," | %s self=%.2fms total=%.2fms calls=%.1f",
                c->name,c->self*scale,c->inclusive*scale,c->calls/(double)totals.samples);
    }
    fprintf(stderr,"%s\n",line);
    memset(&totals,0,sizeof(totals));
}
