#include "wb_cpu_profile.h"
#include <string.h>
int wb_cpu_collect(wb_cpu_totals *out,const wb_cpu_view *v) {
    if(!out || !v || !v->capacity || v->capacity>8192 ||
       v->first>=v->capacity || v->end>=v->capacity || !v->frame_ticks ||
       v->frame_ticks>0x7fffffff || !v->names || !v->depths || !v->starts || !v->ends)
        return -1;
    struct { int slot; uint32_t start,end; } parents[32];
    for(unsigned i=0;i<32;++i) parents[i].slot=-1;
    for(unsigned i=v->first;i!=v->end;i=(i+1)%v->capacity) {
        unsigned depth=v->depths[i];
        uint32_t start=v->starts[i]-v->frame_start,end=v->ends[i]-v->frame_start;
        if(depth>=32 || start>end || end>v->frame_ticks || !v->names[i]) {
            ++out->skipped;
            /* An incomplete scope must not inherit a previous sibling. */
            if(depth<32) for(unsigned d=depth;d<32;++d) parents[d].slot=-1;
            continue;
        }
        char name[48]={0};
        for(unsigned j=0;j<sizeof(name)-1 && v->names[i][j];++j) {
            unsigned char c=(unsigned char)v->names[i][j];
            name[j]=c<32 || c==127 ? '?' : (char)c;
        }
        unsigned slot=0;
        while(slot<out->used && strcmp(out->costs[slot].name,name)) ++slot;
        if(slot==WB_CPU_MARKERS) { ++out->skipped;continue; }
        if(slot==out->used) { strcpy(out->costs[slot].name,name);++out->used; }
        uint32_t ticks=end-start;
        wb_cpu_cost *cost=&out->costs[slot];
        cost->inclusive+=ticks;cost->self+=ticks;++cost->calls;
        if(depth && parents[depth-1].slot>=0 &&
           start>=parents[depth-1].start && end<=parents[depth-1].end) {
            wb_cpu_cost *parent=&out->costs[parents[depth-1].slot];
            if(parent->self>=ticks) parent->self-=ticks;
        }
        parents[depth].slot=(int)slot;parents[depth].start=start;parents[depth].end=end;
        for(unsigned d=depth+1;d<32;++d) parents[d].slot=-1;
    }
    out->frame_ticks+=v->frame_ticks;++out->samples;
    return 0;
}
