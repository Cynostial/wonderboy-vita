#ifndef WB_SHA256_H
#define WB_SHA256_H
#include <stddef.h>
#include <stdint.h>
void wb_sha256(const void *,size_t,uint8_t out[32]);
typedef struct { uint32_t h[8]; uint64_t bytes; uint8_t tail[64]; size_t used; } wb_sha256_state;
void wb_sha256_init(wb_sha256_state *);
void wb_sha256_update(wb_sha256_state *,const void *,size_t);
void wb_sha256_final(wb_sha256_state *,uint8_t [32]);
#endif
