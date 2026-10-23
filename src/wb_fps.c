/* Small CPU overlay, used only after the display queue's GPU completion.
 * No textures, shaders, GPU calls, allocation, or game GL-state changes. */
#include "wb_fps.h"
#include <stddef.h>
void wb_fps_sample(wb_fps *s,uint64_t now) {
    if(!s->started || now<s->start) { s->start=now;s->frames=0;s->started=1;s->valid=0;return; }
    ++s->frames;
    uint64_t elapsed=now-s->start;
    if(elapsed<500000) return;
    uint64_t rate=((uint64_t)s->frames*10000000+elapsed/2)/elapsed;
    s->tenths=rate>9999 ? 9999 : (unsigned)rate;s->valid=1;
    s->start=now;s->frames=0;
}
/* Seven rows of five pixels, MSB at the left. */
static const unsigned char glyphs[][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,2,12},{31,16,16,30,16,16,16}, /* F */
 {30,17,17,30,16,16,16}, /* P */ {15,16,16,14,1,1,30}, /* S */
 {0,0,0,0,0,12,12}, /* dot */ {0,0,0,31,0,0,0} /* dash */
};
void wb_fps_draw(const wb_fps *s,uint32_t *pixels,unsigned width,unsigned height,unsigned stride) {
    if(!pixels || stride<width || width<124 || height<32) return;
    /* Nine fixed character positions: "FPS  60.0". A solid dark panel stays
     * readable over both bright and dark scenes without blending old frames. */
    unsigned top=height-30; /* Eight pixels from the left and bottom edges. */
    for(unsigned y=top;y<height-8;++y)
        for(unsigned x=8;x<124;++x) pixels[(size_t)y*stride+x]=0xff201c18u;
    int chars[]={10,11,12,-1,-1,14,14,13,14};
    if(s->valid) {
        chars[4]=s->tenths>=1000 ? (int)(s->tenths/1000) : -1;
        chars[5]=s->tenths>=100 ? (int)(s->tenths/100%10) : -1;
        chars[6]=(int)(s->tenths/10%10);chars[8]=(int)(s->tenths%10);
    }
    for(unsigned i=0;i<9;++i) if(chars[i]>=0)
        for(unsigned y=0;y<7;++y) for(unsigned x=0;x<5;++x)
            if(glyphs[chars[i]][y] & (16u>>x))
                for(unsigned dy=0;dy<2;++dy) for(unsigned dx=0;dx<2;++dx)
                    pixels[(size_t)(top+4+y*2+dy)*stride+12+i*12+x*2+dx]=0xfff4f4f4u;
}
