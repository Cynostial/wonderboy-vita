#pragma once
#include <stdint.h>
typedef struct {
    void *manager;
    void *(*get_texture)(void *,const char *,uint64_t,unsigned);
    void (*preload_location)(void *,int,const void *);
    void (*preload_animation)(void *,int,int);
    int (*finalize)(void *,void *);
    int (*is_render_thread)(void);
    unsigned (*estimate)(const char *);
    int (*admit)(unsigned);
    uint64_t (*clock_us)(void);
    void (*trim_begin)(void);
    int (*trim)(unsigned,void *const *,unsigned);
} wb_prewarm_api;
int wb_prewarm_bind(const wb_prewarm_api *);
void *wb_prewarm_texture(void *,const char *,uint64_t,unsigned);
void wb_prewarm_animation(void *,int,int);
void wb_prewarm_location(void *);
unsigned wb_prewarm_estimate(const char *);
int wb_prewarm_admit(unsigned);
