#ifndef WB_SETTINGS_H
#define WB_SETTINGS_H
typedef struct { float x,y; } wb_settings_vec2;
int wb_settings_bind(void (*draw)(void *,void *,wb_settings_vec2,float),
                     void (*add)(void *,unsigned,void *,const char *,int,float));
void wb_settings_draw(void *,void *,wb_settings_vec2,float);
void wb_settings_add(void *,unsigned,void *,const char *,int,float);
#endif
