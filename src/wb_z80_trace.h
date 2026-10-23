#ifndef WB_Z80_TRACE_H
#define WB_Z80_TRACE_H
#include <stdint.h>
/* Validates the pinned ApplyHacksMain loop and binds its continuation.
 * After success, hook base+0x17c1b1 to wb_z80_trace_entry and flush caches. */
int wb_z80_trace_prepare(uintptr_t base);
void wb_z80_trace_entry(void);
#endif
