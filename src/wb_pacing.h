#ifndef WB_PACING_H
#define WB_PACING_H
#include <stdint.h>
extern unsigned wb_pacing_vblank;
extern uintptr_t wb_pacing_resume,wb_pacing_skip;
int wb_pacing_prepare(uintptr_t);
void wb_pacing_entry(void);
#endif
