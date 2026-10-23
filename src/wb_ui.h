#pragma once
#include <stdint.h>
typedef struct { float x,y; } wb_vec2;
typedef struct {
    void (*cursor)(void *,wb_vec2,float,int *,void *);
    unsigned char *inputs,*touch;
} wb_ui_api;
int wb_ui_bind(const wb_ui_api *);
void wb_ui_input_frame(void);
void wb_ui_confirm_edge(void);
void wb_ui_touch_edge(void);
void wb_ui_cursor(void *,wb_vec2,float,int *,void *);
void wb_touch_textures(void *);
int wb_touch_inactive(void *);
void wb_touch_draw(void *,void *,const wb_vec2 *);
int wb_display_dpi(int,float *,float *,float *);
