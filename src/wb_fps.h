#pragma once
#include <stdint.h>
typedef struct { uint64_t start; unsigned frames,tenths; int started,valid; } wb_fps;
void wb_fps_sample(wb_fps *,uint64_t);
void wb_fps_draw(const wb_fps *,uint32_t *,unsigned,unsigned,unsigned);
void wb_fps_install(void);
void wb_fps_enable(void);
void wb_fps_toggle(void);
void wb_present_stats(uint32_t *,uint32_t *,uint32_t *,uint32_t *,uint32_t *);
int wb_fps_bind_localize(const char *(*)(uint64_t));
const char *wb_fps_localize(uint64_t);
