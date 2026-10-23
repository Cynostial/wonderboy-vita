/* Exact water-displacement reuse for the SHA-pinned ARMv7 DrawDeformationGrid.
 * Keep its 193 x 25 grid, exclusions, vertices, UVs and shader unchanged.
 * The horizontal displacement is identical across each row. Vertical wave
 * arguments often repeat on diagonals, but floating addition order matters:
 * compare their exact bits and call the same sinf on every cache miss.
 */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "so_util.h"

uintptr_t wb_water_resume;
void wb_water_entry(void);

void wb_water_fill(float *grid, float time, float scaled_time, const void *game) {
    unsigned first=0,last=192;
    if(game) {
        float left,right;
        memcpy(&left,(const char *)game+0xd8,4);
        memcpy(&right,(const char *)game+0xe0,4);
        /* Match the original renderer's integer conversion and negative
         * floor adjustment. Include the right edge shared by adjacent quads.
         * At the final world boundary the original may read across a row;
         * retain the full grid in that case rather than change its geometry. */
        if(isfinite(left) && isfinite(right) && left>=-48.0f &&
           right>=left && right<9168.0f) {
            int lo=(int)(left/48.0f);
            if(left<0.0f && fmodf(left,48.0f)<0.0f) --lo;
            int hi=(int)(right/48.0f+1.0f);
            if(hi>=0 && lo<=192) {
                first=lo<0 ? 0 : (unsigned)lo;
                last=(unsigned)hi;
            }
        }
    }
    struct { uint32_t bits; float value; } diagonal[217][4];
    unsigned char counts[217];
    float columns[193];
    for (unsigned col=first; col<=last; ++col)
        columns[col]=scaled_time+((float)col*48.0f)/100.0f;
    memset(counts,0,sizeof(counts));
    for (unsigned row=0; row<25; ++row) {
        float row_distance=(float)row*48.0f;
        float dx=sinf(time+row_distance/50.0f)*5.0f;
        float row_phase=row_distance/100.0f;
        for (unsigned col=first; col<=last; ++col) {
            float phase=columns[col]+row_phase;
            uint32_t bits;memcpy(&bits,&phase,4);
            unsigned index=row+col;
            unsigned found=0,used=counts[index];
            while (found<used && diagonal[index][found].bits!=bits) ++found;
            if (found==used) {
                /* Four exact rounding variants plus column phases use less
                 * than 8 KiB of stack. Unusual inputs replace and recompute. */
                if (used==4) found=0;
                else ++counts[index];
                diagonal[index][found].bits=bits;
                diagonal[index][found].value=sinf(phase)*3.0f;
            }
            grid[(row*193+col)*2]=dx;
            grid[(row*193+col)*2+1]=diagonal[index][found].value;
        }
    }
}

int wb_water_install(uintptr_t base) {
    static const unsigned char before[]={
        0x00,0xee,0x10,0x4a,0x00,0x26,0x45,0x46,
        0xb8,0xee,0xc0,0x0a,0x20,0xee,0x08,0x0a
    };
    /* No other entry targets this loop. Resume at its original location
     * lookup, so all exclusion masks and geometry generation still execute. */
    if(memcmp((const void *)(base+0x137dcc),before,sizeof(before))) return -1;
    wb_water_resume=base+0x137e3f;
    hook_addr(base+0x137dcd,(uintptr_t)wb_water_entry);
    return 0;
}
