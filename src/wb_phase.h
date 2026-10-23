#ifndef WB_PHASE_H
#define WB_PHASE_H
#include <stdint.h>
#define WB_PHASE_COUNT 24
#define WB_PHASE_DEPTH 32
typedef struct { uint64_t total,self; uint32_t calls; } wb_phase_cost;
typedef struct {
    wb_phase_cost cost[WB_PHASE_COUNT];
    struct { uint64_t start,children; unsigned id; } stack[WB_PHASE_DEPTH];
    unsigned depth,dropped;
} wb_phase_totals;
int wb_phase_enter(wb_phase_totals *,unsigned,uint64_t);
void wb_phase_leave(wb_phase_totals *,uint64_t);
#endif
