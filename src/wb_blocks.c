/* Reuse the original renderer's world-space block lists, never its dynamic
 * vertex/index buffers. Layout and dependencies are pinned to libwb.so
 * 562a0eee...b4842, DrawBlocksBuild at ELF 0x136954.
 *
 * Mouse-Man's proximity deformation and moving-block animation deliberately
 * use the original builder. Ordinary lists depend on the tile maps, block
 * definitions, location width and the layer's block mask, not camera position.
 * Compare complete input bytes rather than a collision-prone hash. */
#include "wb_blocks.h"
#include <stddef.h>
#include <string.h>
static unsigned char *game,*emu,*lists;
static void (*build_original)(void *,int,const void *);
static int (*is_special)(const void *);
enum { KEY_MAX=16384 };
static unsigned char previous[KEY_MAX],scratch[KEY_MAX],headers[36];
static size_t previous_size;
static uint32_t calls,hits,bypasses;
static uint32_t get(const void *p,unsigned at) {
    uint32_t v;memcpy(&v,(const unsigned char *)p+at,4);return v;
}
static unsigned char *ptr(const void *p,unsigned at) { return (unsigned char *)(uintptr_t)get(p,at); }
static int append(size_t *n,const void *p,size_t bytes) {
    if(bytes>KEY_MAX-*n)return 0;
    memcpy(scratch+*n,p,bytes);*n+=bytes;return 1;
}
void wb_blocks_invalidate(void) { previous_size=0; }
int wb_blocks_bind(void *g,void *e,void *l,void (*b)(void *,int,const void *),int (*s)(const void *)) {
    if(!g||!e||!l||!b||!s)return -1;
    game=g;emu=e;lists=l;build_original=b;is_special=s;wb_blocks_invalidate();return 0;
}
void wb_blocks_stats(uint32_t *a,uint32_t *h,uint32_t *b) { *a=calls;*h=hits;*b=bypasses; }
static size_t key(int layer) {
    if(layer<0||layer>=60||!game||is_special(game))return 0;
    unsigned char *ram=ptr(emu,0x180d0),*old=ptr(emu,0x200d8),*loc=ptr(game,0x128);
    uint32_t width=get(game,0x140);
    if(!ram||!old||!loc||width<1||width>6)return 0;
    if(ram[0x24f]==2) {
        unsigned a=ram[0x260]&127,b=old[0x260]&127;
        if(a==6||a==8||a==9||b==6||b==8||b==9)return 0;
    }
    /* A special moving block can hide a tile according to its live animation
     * player. Avoid retaining that dependency graph at all. */
    if(ram[0x28e]==1&&ram[0x28f]==1&&ram[0x28c]==0x6a&&ram[0x28d]==0x9f)return 0;
    unsigned char *layers=ptr(loc,0x20);
    if(!layers)return 0;
    size_t n=0;
    if(!append(&n,game+0x128,4)||!append(&n,&width,4)||!append(&n,&layer,4)||
       !append(&n,layers+layer*0x80+0x6f,1)||
       !append(&n,loc+0x30,0x4e2)||!append(&n,ram+0xb00,0x480)||
       !append(&n,game+0x144,256*4))return 0;
    /* The builder only reads each definition's texture-choice count/pointer
     * and tests the chosen texture pointer for NULL. Include all choices so
     * asynchronous completion and location-specific definitions invalidate. */
    for(unsigned i=0;i<256;++i) {
        unsigned char *def=ptr(game,0x144+4*i);
        if(!def)continue;
        unsigned count=get(def,0x128);unsigned char *choices=ptr(def,0x130);
        if(!count||count>256||!choices||!append(&n,def+0x128,12)||
           !append(&n,choices,count*4))return 0;
    }
    return n;
}
void wb_blocks_build(void *draw,int layer,const void *clip) {
    ++calls;
    size_t n=key(layer);
    if(n&&n==previous_size&&!memcmp(previous,scratch,n)&&!memcmp(headers,lists,36)) {
        ++hits;return;
    }
    build_original(draw,layer,clip);
    if(n) {
        memcpy(previous,scratch,n);memcpy(headers,lists,36);previous_size=n;
    } else { ++bypasses;wb_blocks_invalidate(); }
}
