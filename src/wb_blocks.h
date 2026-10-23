#ifndef WB_BLOCKS_H
#define WB_BLOCKS_H
#include <stdint.h>
int wb_blocks_bind(void *,void *,void *,void (*)(void *,int,const void *),int (*)(const void *));
void wb_blocks_build(void *,int,const void *);
void wb_blocks_invalidate(void);
void wb_blocks_stats(uint32_t *,uint32_t *,uint32_t *);
#endif
