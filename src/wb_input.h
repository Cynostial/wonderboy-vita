#pragma once
#include <SDL2/SDL.h>
int wb_input_event(SDL_Event *);
void wb_input_initialize(void);
int wb_input_poll(SDL_Event *);
int wb_no_touch_devices(void);
SDL_TouchID wb_no_touch_device(int);
int wb_no_touch_fingers(SDL_TouchID);
