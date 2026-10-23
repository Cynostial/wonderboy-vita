#ifndef WB_TEXTURE_REUSE_H
#define WB_TEXTURE_REUSE_H
#include <stdint.h>
typedef struct {
    void *emulator;
    void *(*create)(const void *,unsigned);
    void (*destroy)(void *);
    void (*versions)(uint64_t [32]);
    void (*update)(unsigned,const void *,uint32_t);
} wb_texture_reuse_api;
void wb_texture_reuse_bind(const wb_texture_reuse_api *);
void wb_texture_reuse_begin(void);
void wb_texture_reuse_end(void);
void *wb_texture_reuse_create(const void *,unsigned);
void wb_texture_reuse_destroy(void *);
#endif
