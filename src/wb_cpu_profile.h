#ifndef WB_CPU_PROFILE_H
#define WB_CPU_PROFILE_H
#include <stdint.h>
#define WB_CPU_MARKERS 128
typedef struct {
    unsigned capacity,first,end;
    uint32_t frame_start,frame_ticks;
    const char *const *names;
    const unsigned char *depths;
    const uint32_t *starts,*ends;
} wb_cpu_view;
typedef struct {
    char name[48];
    uint64_t inclusive,self;
    uint32_t calls;
} wb_cpu_cost;
typedef struct {
    wb_cpu_cost costs[WB_CPU_MARKERS];
    uint64_t frame_ticks;
    unsigned samples,used,skipped;
} wb_cpu_totals;
/* Reads completed existing guest markers; no extra per-marker clocks. */
int wb_cpu_collect(wb_cpu_totals *,const wb_cpu_view *);
#endif
