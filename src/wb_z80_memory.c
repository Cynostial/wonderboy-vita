/* Direct SMS memory access for the hash-pinned Android interpreter. The ROM
 * pointer is re-read on every call: RunFrame switches the patched ROM banks.
 * Only ordinary RAM writes bypass the original method. Mapper writes and
 * debugger watchpoints retain its side effects, pause behavior and ordering.
 * No pointers into ROM or RAM are cached across calls. */
#include "wb_z80_memory.h"
#include <string.h>
/* Used by the leaf reader in wb_z80_memory_arm.S. Only bind writes it. */
const unsigned char **wb_z80_memory_rom;
#define rom wb_z80_memory_rom
static void (*original_write)(void *,uint16_t,uint8_t);
static inline uint32_t word(const unsigned char *p,unsigned offset) {
    uint32_t value;memcpy(&value,p+offset,sizeof value);return value;
}
static inline const unsigned char *read_pointer(const unsigned char *p,unsigned a) {
    if(a<0x8000u) return *rom+a;
    if(a<0xc000u) return *rom+((unsigned)p[0xa420c]<<14)+(a&0x3fffu);
    return (const unsigned char *)(uintptr_t)word(p,0x180d0)+(a&0x1fffu);
}
int wb_z80_memory_bind(const unsigned char **r,
        void (*write_fn)(void *,uint16_t,uint8_t)) {
    if(!r || !write_fn) return -1;
    rom=r;original_write=write_fn;return 0;
}
uint16_t wb_z80_memory_read16(void *cpu,uint16_t address) {
    unsigned a=address;
    /* All native areas have identical mappings until the next 8 KiB edge.
     * Boundaries deliberately take independent reads, including 0xffff wrap. */
    if((a&0x1fffu)!=0x1fffu) {
        const unsigned char *p=read_pointer(cpu,a);
        return (uint16_t)(p[0]|((unsigned)p[1]<<8));
    }
    unsigned b=(uint16_t)(address+1);
    return (uint16_t)(*read_pointer(cpu,a)|((unsigned)*read_pointer(cpu,b)<<8));
}
void wb_z80_memory_write(void *cpu,uint16_t address,uint8_t value) {
    unsigned char *p=cpu;
    if(address<0xc000u) return;
    if(address>=0xfffcu || word(p,0x78)==address || word(p,0x7c)==address) {
        original_write(cpu,address,value);return;
    }
    unsigned char *ram=(unsigned char *)(uintptr_t)word(p,0x180d0);
    ram[address&0x1fffu]=value;
}
