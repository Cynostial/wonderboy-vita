#include "wb_phase.h"
int wb_phase_enter(wb_phase_totals *t,unsigned id,uint64_t now) {
    if(id>=WB_PHASE_COUNT || t->depth==WB_PHASE_DEPTH) { ++t->dropped;return 0; }
    unsigned d=t->depth++;
    t->stack[d].start=now;t->stack[d].children=0;t->stack[d].id=id;
    return 1;
}
void wb_phase_leave(wb_phase_totals *t,uint64_t now) {
    if(!t->depth) { ++t->dropped;return; }
    unsigned d=--t->depth,id=t->stack[d].id;
    uint64_t time=now-t->stack[d].start,children=t->stack[d].children;
    t->cost[id].total+=time;t->cost[id].self+=time>=children ? time-children : 0;
    ++t->cost[id].calls;
    if(d) t->stack[d-1].children+=time;
}
