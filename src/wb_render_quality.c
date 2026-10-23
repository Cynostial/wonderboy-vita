#include <stdint.h>
#include <string.h>
/* Hash-pinned libwb.so. Preserve the original screen-fit calculation and
 * timing/presentation code; loading stalls must not halve the fitted image. */
int wb_apply_render_quality(void *image) {
    unsigned char *p=image;
    const unsigned char load_scale[]={0x96,0xed,0x0a,0x0a};
    const unsigned char half[]={0xb6,0xee,0x00,0x2a};
    const unsigned char one_s0[]={0xb7,0xee,0x00,0x0a};
    const unsigned char one_s4[]={0xb7,0xee,0x00,0x2a};
    const unsigned char one_r1[]={0x4f,0xf0,0x7e,0x51};
    const unsigned char one_r2[]={0x4f,0xf0,0x7e,0x52};
    const unsigned char s0_r1[]={0x10,0xee,0x10,0x1a};
    const unsigned char s2_r2[]={0x11,0xee,0x10,0x2a};
    const unsigned char reflection_load[]={0x96,0xed,0x0d,0x0a};
    const unsigned char scene_load[]={0x96,0xed,0x0b,0x0a};
    if(memcmp(p+0x10458a,load_scale,4) || memcmp(p+0x16468c,half,4) ||
       memcmp(p+0x1045b8,one_r1,4) || memcmp(p+0x1045ce,half,4) ||
       memcmp(p+0x10466a,one_r2,4) || memcmp(p+0x10b3ae,reflection_load,4)) return -1;
    /* SetupRenderResolutionScaleAuto: use 1.0 for the adaptive multiplier.
     * The separate screen-fit factor is still applied (0.5 at 960-wide). */
    memcpy(p+0x10458a,one_s0,4);
    /* Auto hardcodes the reflection target at 1920x1080 even though the
     * scene fits 960x540. Use that same fit for reflections, through the
     * engine's scale parameter (which also updates its render coordinates).
     * Match the equality check to the stored scale so repeated setup does
     * not destroy/recreate every render target. Main scene scale is unchanged. */
    memcpy(p+0x1045b8,s0_r1,4);
    memcpy(p+0x1045ce,one_s4,4);
    /* Explicit resolution mode has the same hardcoded store. Respect the
     * reflection scale it already computes/compares, including its default. */
    memcpy(p+0x10466a,s2_r2,4);
    /* Initial settings can bypass both setup functions. The 00.17 device log
     * still created a 1920x1080 reflection surface. At its creation call,
     * load the scene's fitted scale directly, including this startup path. */
    memcpy(p+0x10b3ae,scene_load,4);
    /* afterPaint: clamp the adaptive multiplier at 1.0, avoiding repeated
     * framebuffer destruction/recreation after each slow loading interval. */
    memcpy(p+0x16468c,one_s4,4);
    return 0;
}
