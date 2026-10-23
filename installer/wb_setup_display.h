#pragma once
#include <stdint.h>
/* Two distinct, display-compatible 960x544 buffers. The presenter owns
 * their reuse; callers must never draw into either buffer themselves. */
void wb_setup_display_init(uint32_t *,uint32_t *);
int wb_setup_present(const char *,unsigned,unsigned,int);
