/* Vita-only input. SELECT is a modifier: SELECT+L = retro audio,
 * SELECT+R = retro visuals. Plain L/R retain previous/next item selection.
 * The guest already maps trigger axes to those two retro actions. */
#include "wb_input.h"
#include "wb_ui.h"
#include "wb_intro.h"
#include <string.h>
static int select_held;
static unsigned char shoulder_role[2];
int wb_input_event(SDL_Event *e) {
    switch(e->type) {
    case SDL_FINGERDOWN: case SDL_FINGERUP: case SDL_FINGERMOTION:
    case SDL_MULTIGESTURE: case SDL_DOLLARGESTURE: case SDL_DOLLARRECORD:
    case SDL_MOUSEMOTION: case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL: case SDL_CONTROLLERTOUCHPADDOWN:
    case SDL_CONTROLLERTOUCHPADUP: case SDL_CONTROLLERTOUCHPADMOTION:
        return 0;
    case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: {
        unsigned button=e->cbutton.button;
        int down=e->type==SDL_CONTROLLERBUTTONDOWN;
        if(button==SDL_CONTROLLER_BUTTON_BACK) { select_held=down;return 0; }
        if(button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER || button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            unsigned side=button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
            if(down) shoulder_role[side]=select_held ? 2 : 1;
            unsigned role=shoulder_role[side];
            if(!down) shoulder_role[side]=0;
            if(role==2) {
                Uint32 stamp=e->cbutton.timestamp;SDL_JoystickID id=e->cbutton.which;
                memset(e,0,sizeof(*e));e->type=SDL_CONTROLLERAXISMOTION;
                e->caxis.timestamp=stamp;e->caxis.which=id;
                e->caxis.axis=side ? SDL_CONTROLLER_AXIS_TRIGGERRIGHT : SDL_CONTROLLER_AXIS_TRIGGERLEFT;
                e->caxis.value=down ? 32767 : 0;
            }
        }
        return 1;
    }
    default:return 1;
    }
}
int wb_input_poll(SDL_Event *event) {
    static int begin_frame=1;
    if(!event) return SDL_PollEvent(NULL); /* SDL's documented queue peek */
    if(begin_frame) { wb_ui_input_frame();wb_intro_input_frame();begin_frame=0; }
    while(SDL_PollEvent(event)) {
        if(!wb_input_event(event)) continue;
        if(event->type==SDL_CONTROLLERBUTTONDOWN && event->cbutton.button==SDL_CONTROLLER_BUTTON_A)
            wb_ui_confirm_edge();
        if(event->type==SDL_CONTROLLERBUTTONDOWN &&
           (event->cbutton.button==SDL_CONTROLLER_BUTTON_A || event->cbutton.button==SDL_CONTROLLER_BUTTON_START))
            wb_intro_press();
        return 1;
    }
    begin_frame=1;return 0;
}
void wb_input_initialize(void) {
    /* Verified getenv gates in this exact SDL Vita backend stop polling both
     * panels. Also prevent mouse synthesis, and filter queued events above. */
    SDL_setenv("VITA_DISABLE_TOUCH_FRONT","1",1);
    SDL_setenv("VITA_DISABLE_TOUCH_BACK","1",1);
    SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS,"0",SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_MOUSE_TOUCH_EVENTS,"0",SDL_HINT_OVERRIDE);
    SDL_EventState(SDL_FINGERDOWN,SDL_IGNORE);
    SDL_EventState(SDL_FINGERUP,SDL_IGNORE);
    SDL_EventState(SDL_FINGERMOTION,SDL_IGNORE);
}
int wb_no_touch_devices(void) { return 0; }
SDL_TouchID wb_no_touch_device(int index) { (void)index;return 0; }
int wb_no_touch_fingers(SDL_TouchID id) { (void)id;return 0; }
