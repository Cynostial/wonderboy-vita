#include "wb_splash_art.h"
/* Only three dots beneath the official title, bouncing in sequence. */
void wb_splash_art(wb_rect r,unsigned phase) {
    static const int bounce[]={0,3,6,3};
    for(unsigned i=0;i<3;++i) {
        unsigned step=(phase+12-i*4)%12;
        int x=214+(int)i*18,y=248-(step<4 ? bounce[step] : 0);
        uint32_t color=step<4 ? 0x403327 : 0xc9bda8;
        r(x+1,y,4,6,color);r(x,y+1,6,4,color);
    }
}
