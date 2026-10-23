/* Cycle-equivalent batching for the hash-pinned ROM's three RAM polling
 * loops. Events are delivered between execute_run calls (scanlines), so a
 * complete LD A,(flag); OR A; JR Z,loop repetition cannot observe new input
 * while the flag stays zero. Preserve each possible final instruction,
 * budget overshoot, R refresh, trace/coverage, memory-read latch, WZ and
 * previous PC. Equivalence is checked against the original ARM interpreter. */
#include <stdint.h>
#include <string.h>
static const unsigned char **rom;
static void (*original_run)(void *);
static uint32_t calls,batched,loops_skipped;
static uint32_t word(const unsigned char *p,unsigned offset) {
    uint32_t v;memcpy(&v,p+offset,4);return v;
}
static void setword(unsigned char *p,unsigned offset,uint32_t v) { memcpy(p+offset,&v,4); }
static void sethalf(unsigned char *p,unsigned offset,uint16_t v) { memcpy(p+offset,&v,2); }
void wb_z80_wait_bind(const unsigned char **r,void (*run)(void *)) { rom=r;original_run=run; }
void wb_z80_wait_stats(uint32_t *all,uint32_t *fast,uint32_t *skipped) {
    *all=calls;*fast=batched;*skipped=loops_skipped;
}
void wb_z80_wait_run(void *cpu) {
    ++calls;
    unsigned char *p=cpu;uint32_t pc=word(p,8);int32_t budget=(int32_t)word(p,0x5c);
    unsigned loop=pc,prefix=0;
    if(pc==0xff1 || pc==0x1000 || pc==0x100b) { loop=pc-3;prefix=16; }
    if(pc==0xff2 || pc==0x1001 || pc==0x100c) { loop=pc-4;prefix=12; }
    if((loop!=0xfee && loop!=0xffd && loop!=0x1008) || budget<(int32_t)(58+prefix) ||
       p[0x44] || p[0x45] || p[0x40] || p[0x50] || p[0x51] || !rom || !*rom ||
       word(p,0)!=(uint32_t)(uintptr_t)p) goto ordinary;
    /* A scanline can end at OR A or JR Z. Account for its partial repetition,
     * including refresh increments, before batching whole repetitions. */
    if(prefix && (p[0x11] || (prefix==12 && !(p[0x10]&0x40)))) goto ordinary;
    const unsigned char *code=*rom+loop;
    unsigned address=loop==0x1008 ? 0xcf84 : 0xcf81;
    if(code[0]!=0x3a || code[1]!=(address&255) || code[2]!=(address>>8) ||
       code[3]!=0xb7 || code[4]!=0x28 || code[5]!=0xfa) goto ordinary;
    const unsigned char *ram=(const unsigned char *)(uintptr_t)word(p,0x180d0);
    if(!ram || ram[address&0x1fff]) goto ordinary;
    const unsigned char *cycles=(const unsigned char *)(uintptr_t)word(p,0x60);
    const unsigned char *extra=(const unsigned char *)(uintptr_t)word(p,0x74);
    if(!cycles || !extra || cycles[0x3a]!=13 || cycles[0xb7]!=4 ||
       cycles[0x28]!=7 || extra[0x28]!=5) goto ordinary;
    unsigned char *trace=(unsigned char *)(uintptr_t)word(p,0xcc);
    if(!trace) goto ordinary;
    /* Hooks and either breakpoint can alter the loop's observable behavior. */
    for(unsigned i=0;i<3;++i) {
        unsigned at=loop+(i==0 ? 0 : i==1 ? 3 : 4);
        if(p[0xd0+at] || word(p,0x78)==at || word(p,0x7c)==at) goto ordinary;
    }
    uint32_t remaining=(uint32_t)budget-prefix,whole=remaining/29u;
    unsigned refresh=whole*3+(prefix==16 ? 2 : prefix==12 ? 1 : 0);
    int32_t rest=(int32_t)(remaining%29u);
    unsigned next=loop,previous=loop+4,wz=loop;
    if(rest>0 && rest<=13) {
        ++refresh;rest-=13;next=loop+3;previous=loop;wz=address+1;
    } else if(rest>13 && rest<=17) {
        refresh+=2;rest-=17;next=loop+4;previous=loop+3;wz=address+1;
    } else if(rest>17) { refresh+=3;rest-=29; }
    /* At least two complete repetitions execute under the budget guard, so
     * all three opcodes have been traced and OR A has established F=0x44. */
    for(unsigned i=0;i<3;++i) {
        unsigned at=loop+(i==0 ? 0 : i==1 ? 3 : 4);
        trace[at]|=0x80;p[0xc0d0+at]=1;
    }
    setword(p,4,previous);sethalf(p,8,(uint16_t)next);
    p[0x10]=0x44;p[0x11]=0;sethalf(p,0x28,(uint16_t)wz);
    p[0x3c]=(unsigned char)(p[0x3c]+refresh);sethalf(p,0x50,0);
    setword(p,0x54,address);setword(p,0x5c,(uint32_t)rest);
    ++batched;loops_skipped+=whole;
    return;
ordinary:
    original_run(cpu);
}
