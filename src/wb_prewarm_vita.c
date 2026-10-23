#include "wb_prewarm.h"
#include "wb_paths.h"
#include <vitaGL.h>
#include <psp2/gxm.h>
#include <stdio.h>
#include <string.h>
size_t wb_gl_texture_bytes(void);
/* Prepared assets are immutable for the lifetime of the game process. Keep
 * only successful header estimates, with owned path bytes: repeated level
 * transitions must not reopen even resident images just to budget them.
 * Only the render-thread transition callback calls this helper. Collisions
 * and long names simply use the original bounded header read. */
enum { ESTIMATES=128, ESTIMATE_NAME=256 };
static struct { char name[ESTIMATE_NAME]; unsigned bytes; } estimates[ESTIMATES];
static unsigned estimate_file(const char *name) {
    if(!name) return 0;
    char path[WB_PATH_MAX];
    if(wb_path_resolve(name,path,sizeof(path))) return 0;
    FILE *file=fopen(path,"rb");if(!file) return 0;
    unsigned char header[64];size_t got=fread(header,1,sizeof(header),file);fclose(file);
    static const unsigned char magic[]={0xab,'K','T','X',' ', '1','1',0xbb,13,10,26,10};
    if(got!=sizeof(header) || memcmp(header,magic,12)) return 0;
    uint32_t h[13];memcpy(h,header+12,sizeof(h));
    if(h[0]!=0x04030201 || h[1] || h[3] || h[8] || h[9] || h[10]!=1 ||
       !h[6] || !h[7] || h[6]>16384 || h[7]>16384 || !h[11] || h[11]>13) return 0;
    unsigned block=h[4]==0x83f1 || h[4]==0x83f0 ? 8 : h[4]==0x83f2 || h[4]==0x83f3 ? 16 : 0;
    if(!block) return 0;
    unsigned w=h[6],height=h[7],levels=h[11];
    while(levels>1 && (w>1024 || height>1024)) {
        w=w>1 ? w/2 : 1;height=height>1 ? height/2 : 1;--levels;
    }
    unsigned pw=1,ph=1,bytes=0;
    while(pw<w) pw*=2;
    while(ph<height) ph*=2;
    for(unsigned level=0;level<levels;++level) {
        bytes+=((pw+3)/4)*((ph+3)/4)*block;
        pw=pw>1 ? pw/2 : 1;ph=ph>1 ? ph/2 : 1;
    }
    return bytes;
}
unsigned wb_prewarm_estimate(const char *name) {
    if(!name) return 0;
    unsigned hash=2166136261u,n=0;
    for(;n<ESTIMATE_NAME && name[n];++n) hash=(hash^(unsigned char)name[n])*16777619u;
    if(n==ESTIMATE_NAME) return estimate_file(name);
    unsigned slot=hash&(ESTIMATES-1);
    if(estimates[slot].bytes && !strcmp(estimates[slot].name,name)) return estimates[slot].bytes;
    unsigned bytes=estimate_file(name);
    if(bytes) {
        memcpy(estimates[slot].name,name,n+1);
        estimates[slot].bytes=bytes;
    }
    return bytes;
}
int wb_prewarm_admit(unsigned bytes) {
    /* Leave headroom for transient upload/storage padding and dynamic draws.
     * LoadFinalize retains the normal 48 MiB LRU and upload ownership. */
    size_t used=wb_gl_texture_bytes(),available=vglMemFree(VGL_MEM_VRAM);
    return bytes<=4u*1024*1024 && used<=76u*1024*1024 &&
        bytes<=76u*1024*1024-used && available>bytes+2u*1024*1024;
}
int wb_prewarm_gpu_finish(void) {
    glFinish();return sceGxmTransferFinish()>=0;
}
