#ifndef WB_SDL_THREADS_H
#define WB_SDL_THREADS_H
#include <SDL2/SDL.h>
SDL_Thread *wb_sdl_create_thread(SDL_ThreadFunction function,const char *name,void *argument);
void wb_sdl_wait_thread(SDL_Thread *thread,int *status);
#endif
