#pragma once
#include "wb_ui.h"
typedef struct {
    void (*splash_draw)(void *,void *,const wb_vec2 *,const wb_vec2 *);
    void (*intro_draw)(void *,void *,const wb_vec2 *);
    void (*next_splash)(void *),(*stop_intro)(void *),(*capture)(void *);
    unsigned char *inputs;
} wb_intro_api;
int wb_intro_bind(const wb_intro_api *);
void wb_intro_input_frame(void);
void wb_intro_press(void);
void wb_intro_splash(void *,void *,const wb_vec2 *,const wb_vec2 *);
void wb_intro_story(void *,void *,const wb_vec2 *);
