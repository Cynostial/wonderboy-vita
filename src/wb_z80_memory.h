#ifndef WB_Z80_MEMORY_H
#define WB_Z80_MEMORY_H
#include <stdint.h>
int wb_z80_memory_bind(const unsigned char **rom,
    void (*original_write)(void *,uint16_t,uint8_t));
uint8_t wb_z80_memory_read(void *,uint16_t);
uint16_t wb_z80_memory_read16(void *,uint16_t);
void wb_z80_memory_write(void *,uint16_t,uint8_t);
#endif
