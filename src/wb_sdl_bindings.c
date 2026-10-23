#include <SDL2/SDL.h>
#include "so_util.h"
#include <stddef.h>
#include "wb_memory.h"
#include "wb_paths.h"
#include "wb_sdl_threads.h"
#include "wb_ui.h"
#include "wb_input.h"
#include <stdio.h>
void wb_swap_window(SDL_Window *);
int wb_set_swap_interval(int);
static int display_dpi(int index,float *d,float *h,float *v) {
    if(index!=0) return SDL_GetDisplayDPI(index,d,h,v);
    return wb_display_dpi(index,d,h,v);
}
static char *pref_path(const char *org,const char *app) {
    (void)org;(void)app; return wb_strdup(WB_SAVES "/");
}
static SDL_RWops *rw_file(const char *path,const char *mode) {
    char resolved[WB_PATH_MAX];
    if(wb_path_resolve(path,resolved,sizeof(resolved))) {
        SDL_SetError("Invalid game asset path"); return NULL;
    }
    return SDL_RWFromFile(resolved,mode);
}
static SDL_Window *create_window(const char *title,int x,int y,int w,int h,Uint32 flags) {
    (void)x;(void)y;(void)w;(void)h;
    return SDL_CreateWindow(title,SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
                            960,544,(flags|SDL_WINDOW_FULLSCREEN)&~SDL_WINDOW_RESIZABLE);
}
static int gl_attribute(SDL_GLattr attr,int value) {
    /* The shader assets target VitaGL's GLES2-compatible path. SDL's Vita
     * backend creates precisely one fixed-size VitaGL context. */
    if(attr==SDL_GL_CONTEXT_MAJOR_VERSION) value=2;
    if(attr==SDL_GL_CONTEXT_MINOR_VERSION) value=0;
    if(attr==SDL_GL_CONTEXT_PROFILE_MASK) value=SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
    if(attr==SDL_GL_MULTISAMPLEBUFFERS || attr==SDL_GL_MULTISAMPLESAMPLES) value=0;
    return SDL_GL_SetAttribute(attr,value);
}
void *wb_gl_get_proc_address(const char *);
/* Before the first SDL allocation, so returned strings/RWops/audio buffers and
 * the game's free/realloc imports all share the same fixed arena. */
int wb_sdl_install_allocator(void) {
    if (!wb_memory_get_stats().capacity) return -1;
    return SDL_SetMemoryFunctions(wb_malloc, wb_calloc, wb_realloc, wb_free);
}
static const so_default_dynlib bindings[] = {
    {"SDL_Log", (uintptr_t)SDL_Log},
    {"SDL_ShowMessageBox", (uintptr_t)SDL_ShowMessageBox},
    {"SDL_GL_GetCurrentWindow", (uintptr_t)SDL_GL_GetCurrentWindow},
    {"SDL_GL_GetDrawableSize", (uintptr_t)SDL_GL_GetDrawableSize},
    {"SDL_GetDisplayDPI", (uintptr_t)display_dpi},
    {"SDL_GetWindowDisplayIndex", (uintptr_t)SDL_GetWindowDisplayIndex},
    {"SDL_GetWindowSize", (uintptr_t)SDL_GetWindowSize},
    {"SDL_GetCurrentDisplayMode", (uintptr_t)SDL_GetCurrentDisplayMode},
    {"SDL_GetPlatform", (uintptr_t)SDL_GetPlatform},
    {"SDL_CloseAudio", (uintptr_t)SDL_CloseAudio},
    {"SDL_OpenAudio", (uintptr_t)SDL_OpenAudio},
    {"SDL_PauseAudio", (uintptr_t)SDL_PauseAudio},
    {"SDL_CreateThread", (uintptr_t)wb_sdl_create_thread},
    {"SDL_Init", (uintptr_t)SDL_Init},
    {"SDL_Quit", (uintptr_t)SDL_Quit},
    {"SDL_SetClipboardText", (uintptr_t)SDL_SetClipboardText},
    {"SDL_GetClipboardText", (uintptr_t)SDL_GetClipboardText},
    {"SDL_GetCPUCount", (uintptr_t)SDL_GetCPUCount},
    {"SDL_PollEvent", (uintptr_t)wb_input_poll},
    {"SDL_GetPrefPath", (uintptr_t)pref_path},
    {"SDL_GameControllerOpen", (uintptr_t)SDL_GameControllerOpen},
    {"SDL_GameControllerClose", (uintptr_t)SDL_GameControllerClose},
    {"SDL_SetHint", (uintptr_t)SDL_SetHint},
    {"SDL_GetKeyName", (uintptr_t)SDL_GetKeyName},
    {"SDL_ShowSimpleMessageBox", (uintptr_t)SDL_ShowSimpleMessageBox},
    {"SDL_ShowCursor", (uintptr_t)SDL_ShowCursor},
    {"SDL_RWFromFile", (uintptr_t)rw_file},
    {"SDL_memcpy", (uintptr_t)SDL_memcpy},
    {"SDL_ThreadID", (uintptr_t)SDL_ThreadID},
    {"SDL_WaitThread", (uintptr_t)wb_sdl_wait_thread},
    {"SDL_GetPerformanceCounter", (uintptr_t)SDL_GetPerformanceCounter},
    {"SDL_GetPerformanceFrequency", (uintptr_t)SDL_GetPerformanceFrequency},
    {"SDL_Delay", (uintptr_t)SDL_Delay},
    {"SDL_GetNumTouchDevices", (uintptr_t)wb_no_touch_devices},
    {"SDL_GetTouchDevice", (uintptr_t)wb_no_touch_device},
    {"SDL_GetNumTouchFingers", (uintptr_t)wb_no_touch_fingers},
    {"SDL_GetNumDisplayModes", (uintptr_t)SDL_GetNumDisplayModes},
    {"SDL_GetDisplayMode", (uintptr_t)SDL_GetDisplayMode},
    {"SDL_CreateWindow", (uintptr_t)create_window},
    {"SDL_GetWindowFlags", (uintptr_t)SDL_GetWindowFlags},
    {"SDL_SetWindowPosition", (uintptr_t)SDL_SetWindowPosition},
    {"SDL_SetWindowSize", (uintptr_t)SDL_SetWindowSize},
    {"SDL_ShowWindow", (uintptr_t)SDL_ShowWindow},
    {"SDL_HideWindow", (uintptr_t)SDL_HideWindow},
    {"SDL_SetWindowFullscreen", (uintptr_t)SDL_SetWindowFullscreen},
    {"SDL_DestroyWindow", (uintptr_t)SDL_DestroyWindow},
    {"SDL_EnableScreenSaver", (uintptr_t)SDL_EnableScreenSaver},
    {"SDL_DisableScreenSaver", (uintptr_t)SDL_DisableScreenSaver},
    {"SDL_GL_GetProcAddress", (uintptr_t)wb_gl_get_proc_address},
    {"SDL_GL_SetAttribute", (uintptr_t)gl_attribute},
    {"SDL_GL_CreateContext", (uintptr_t)SDL_GL_CreateContext},
    {"SDL_GL_MakeCurrent", (uintptr_t)SDL_GL_MakeCurrent},
    {"SDL_GL_SetSwapInterval", (uintptr_t)wb_set_swap_interval},
    {"SDL_GL_GetSwapInterval", (uintptr_t)SDL_GL_GetSwapInterval},
    {"SDL_GL_SwapWindow", (uintptr_t)wb_swap_window},
    {"SDL_GL_DeleteContext", (uintptr_t)SDL_GL_DeleteContext},
    {"SDL_GameControllerAddMappingsFromRW", (uintptr_t)SDL_GameControllerAddMappingsFromRW},
    {"SDL_GameControllerFromInstanceID", (uintptr_t)SDL_GameControllerFromInstanceID},
    {"SDL_SetWindowResizable", (uintptr_t)SDL_SetWindowResizable},
    {"SDL_snprintf", (uintptr_t)SDL_snprintf},
};
const so_default_dynlib *wb_sdl_bindings(size_t *count) {
    if (count) *count = sizeof(bindings) / sizeof(bindings[0]);
    return bindings;
}
