#include "wb_settings.h"
static void (*original_draw)(void *,void *,wb_settings_vec2,float);
static void (*original_add)(void *,unsigned,void *,const char *,int,float);
/* Both functions run on the game's UI/render thread. Action IDs are local
 * to each menu: filter only during OptionsMenuDraw, never other screens. */
static unsigned options_depth;
int wb_settings_bind(void (*draw)(void *,void *,wb_settings_vec2,float),
                     void (*add)(void *,unsigned,void *,const char *,int,float)) {
    if(!draw || !add) return -1;
    original_draw=draw;original_add=add;return 0;
}
void wb_settings_draw(void *ui,void *list,wb_settings_vec2 position,float scale) {
    ++options_depth;original_draw(ui,list,position,scale);--options_depth;
}
void wb_settings_add(void *menu,unsigned action,void *value,const char *label,int flags,float scale) {
    if(options_depth && (action==17 || action==19)) return;
    original_add(menu,action,value,label,flags,scale);
}
