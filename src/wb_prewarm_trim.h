#pragma once
#include <stdint.h>
typedef struct {
    void *manager,*cache;
    uint32_t *bytes;
    void *deletes;
    void (*erase)(void *,const uint64_t *);
    void (*unload)(void *,void *);
    void (*drain)(void *);
    unsigned (*size)(const void *);
    int (*admit)(unsigned);
    int (*finish)(void);
} wb_prewarm_trim_api;
int wb_prewarm_trim_bind(const wb_prewarm_trim_api *);
void wb_prewarm_trim_begin(void);
int wb_prewarm_trim(unsigned,void *const *,unsigned);
int wb_prewarm_gpu_finish(void);
