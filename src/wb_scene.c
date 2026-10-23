/* Conservative pre-texture culling for the pinned DrawLocationDec ABI.
 * The original fetches (and may synchronously load) the image BEFORE culling.
 * Only simple, static, single decorations are eligible. Dynamic, repeated,
 * rotated, perspective, particle, audio and texture-buffer objects fall back.
 * No GPU ownership, draw reservations, scene updates or asset data is changed.
 */
#include "wb_scene.h"
#include <math.h>
#include <string.h>
static const unsigned char *game;
static wb_scene_draw_fn draw_original;
static wb_scene_pos_fn calc_pos;
static uint32_t u32(const void *p,unsigned n) { uint32_t v;memcpy(&v,(const unsigned char *)p+n,4);return v; }
static float f32(const void *p,unsigned n) { float v;memcpy(&v,(const unsigned char *)p+n,4);return v; }
int wb_scene_bind(void *g,wb_scene_draw_fn draw,wb_scene_pos_fn pos) {
    if(!g||!draw||!pos)return -1;
    game=g;draw_original=draw;calc_pos=pos;return 0;
}
void wb_scene_draw(void *draw,const wb_scene_vec *offset,float alpha,float scale,
                   void *layer,void *obj,unsigned char *dirty,const wb_scene_rect *clip) {
    /* A preceding particle may require restoring layer state even if this
     * last decoration is offscreen. Let the original perform that restore. */
    if(clip && obj && layer && offset && dirty && !*dirty && scale>0 && isfinite(scale) &&
       u32(obj,4)==1 && !u32(obj,0xc) && !u32(obj,0x10) &&
       u32(obj,0x90)==1 && u32(obj,0x94)==1 &&
       !u32(obj,0xcc) && !u32(obj,0xd0) && !u32(obj,0xd4) &&
       f32(obj,0x84)==0 && f32(obj,0x88)==0 && f32(obj,0xc0)==0 &&
       f32(obj,0xa0)<=0 && f32(layer,0x58)==0) {
        float width=f32(obj,0x6c)*fabsf(f32(obj,0x74));
        float height=f32(obj,0x70)*fabsf(f32(obj,0x78));
        /* Invalid/inverted data always uses the unmodified implementation. */
        if(width>=0 && height>=0 && isfinite(width) && isfinite(height) &&
           isfinite(offset->x) && isfinite(offset->y) &&
           isfinite(clip->x0) && isfinite(clip->y0) &&
           isfinite(clip->x1) && isfinite(clip->y1) &&
           clip->x0<clip->x1 && clip->y0<clip->y1) {
            wb_scene_vec p;calc_pos(&p,obj,1);
            float world=(float)(int32_t)u32(game,0x140)*256.0f;
            if(world<=320.0f)world=320.0f;
            /* Match the original position wrap and 96-unit top offset.
             * A two-unit guard includes both raw and atlas rounding orders. */
            float shifted=p.x+1920.0f,period=world*6.0f+1920.0f;
            /* fmodf is exactly the input inside [0,period). Avoid its full
             * reduction path for the usual in-world decoration, preserving
             * both evaluation/rounding order and signed zero. Negative,
             * nonfinite and wrapped positions still use the original math. */
            p.x=(shifted>=0.0f && shifted<period ? shifted : fmodf(shifted,period))-1920.0f;
            p.y=p.y+96.0f;
            float x0=offset->x+p.x*scale,y0=offset->y+p.y*scale;
            float x1=offset->x+(p.x+width)*scale,y1=offset->y+(p.y+height)*scale;
            /* Sparse's original pre-draw bounds use the unscaled Size.
             * Include that box too; unknown texture metadata need not load. */
            if(x1<x0+width)x1=x0+width;
            if(y1<y0+height)y1=y0+height;
            if(isfinite(x0)&&isfinite(x1)&&isfinite(y0)&&isfinite(y1) &&
               (x1<clip->x0-2.0f || x0>clip->x1+2.0f ||
                y1<clip->y0-2.0f || y0>clip->y1+2.0f)) return;
        }
    }
    draw_original(draw,offset,alpha,scale,layer,obj,dirty,clip);
}
