#pragma once
#include <stdint.h>
typedef struct {
    void (*load_location)(void *,int,unsigned);
    void *manager;
    void *(*get_texture)(void *,const char *,uint64_t,unsigned);
    int (*is_render_thread)(void);
    uint64_t (*clock_us)(void);
    void (*prewarm_location)(void *);
} wb_effects_api;
int wb_effects_bind(const wb_effects_api *);
void wb_effects_load_location(void *,int,unsigned);
