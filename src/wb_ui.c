/* Pinned libwb.so menu ABI. Retain input edges within one SDL event frame,
 * then let the game's own menu select/activate/save. Never create save data
 * here, change a disabled item, or replay a held button into a new screen. */
#include "wb_ui.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static wb_ui_api api;
static int confirm_edge;
static uint32_t word(const void *p,size_t at) {
    uint32_t v;memcpy(&v,(const unsigned char *)p+at,4);return v;
}
static float number(const void *p,size_t at) {
    float v;memcpy(&v,(const unsigned char *)p+at,4);return v;
}
static void set_number(void *p,size_t at,float v) { memcpy((unsigned char *)p+at,&v,4); }
int wb_ui_bind(const wb_ui_api *a) {
    if(!a || !a->cursor || !a->inputs || !a->touch) return -1;
    api=*a;return 0;
}
void wb_ui_input_frame(void) { confirm_edge=0; }
void wb_ui_confirm_edge(void) { confirm_edge=1; }
void wb_ui_touch_edge(void) { /* Touch is intentionally disabled. */ }
void wb_ui_cursor(void *menu,wb_vec2 position,float scale,int *selected,void *item) {
    uint32_t count=word(menu,0),index=word(menu,0xa94);
    unsigned char *entry=NULL;
    if(index<count) entry=(unsigned char *)(uintptr_t)word(menu,8)+index*0xe0;
    uint32_t action=entry ? word(entry,4) : 0;
    int create_menu=action>=7 && action<=9;
    int enabled=((unsigned char *)menu)[0xac4] && entry && !(word(entry,0x70)&4);
    int restored=0;
    if(create_menu && enabled) {
        if(confirm_edge && !api.inputs[0xed]) { api.inputs[0xed]=1;restored|=1; }
        confirm_edge=0;
    }
    api.cursor(menu,position,scale,selected,item);
    int activated=(int32_t)word(menu,0xac8);
    /* Root menu's Play press must not also confirm a save slot this frame. */
    if(activated>=0) confirm_edge=0;
    if(create_menu && (restored || activated>=0))
        fprintf(stderr,"Save menu: action=%u selected=%d activated=%d restored=%d\n",
                (unsigned)action,*selected,activated,restored);
}
/* Vita has a physical gamepad. Touch events are blocked before the guest; omit the Android virtual-gamepad renderer and
 * its unconditional 33 MiB texture preload. The geometry assignments are
 * the non-resource side effects of UpdateTouchTextures (guest 0xe0488). */
void wb_touch_textures(void *touch) {
    float w=number(touch,0xd0)*(float)(int32_t)word(touch,0xdc);
    float h=number(touch,0xd4)*(float)(int32_t)word(touch,0xe0);
    set_number(touch,0x10c,0);set_number(touch,0x110,h*0.25f);
    set_number(touch,0x114,w*0.5f);set_number(touch,0x118,h);
}
int wb_touch_inactive(void *touch) { (void)touch;return 0; }
void wb_touch_draw(void *touch,void *draw,const wb_vec2 *position) {
    (void)touch;(void)draw;(void)position;
}
/* Both retail Vita panels are 960x544 over a 5-inch diagonal. SDL's Vita
 * driver has no GetDisplayDPI callback; the guest ignores its error return. */
int wb_display_dpi(int display,float *diagonal,float *horizontal,float *vertical) {
    if(display!=0) return -1;
    if(diagonal) *diagonal=220.67026f;
    if(horizontal) *horizontal=220.67026f;
    if(vertical) *vertical=220.67026f;
    return 0;
}
