#include "wb_controls.h"
#include <stddef.h>
static void *manager;
static wb_icon_loader load;
void wb_controls_bind(void *m,wb_icon_loader l) { manager=m;load=l; }
/* Pasta::Key values verified against SDLInputMgr::SDLControllerButton and
 * the game's default mapping. Face/D-pad/shoulder artwork comes from the
 * original PS4 icon atlas. Extra labels describe the Vita's SELECT chords. */
const char *wb_control_icon_path(int key) {
#define ICON(n) "tex/UI/Platforms/PS4/Icons/PS4_" n ".png"
    switch(key) {
    case 0:return ICON("Dpad_Left");case 1:return ICON("Dpad_Down");
    case 2:return ICON("Dpad_Right");case 3:return ICON("Dpad_Up");
    case 4:case 5:case 6:case 7:case 8:case 9:return ICON("Left_Stick");
    case 10:case 11:case 12:case 13:case 14:case 15:return ICON("Right_Stick");
    case 16:return ICON("Triangle");case 17:return ICON("Circle");
    case 18:return ICON("Cross");case 19:return ICON("Square");
    case 20:return ICON("L1");case 21:return ICON("R1");
    case 22:return "tex/Vita/SelectL.png";case 23:return "tex/Vita/SelectR.png";
    case 26:return "tex/Vita/Start.png";case 27:return "tex/Vita/Select.png";
    case 141:return ICON("Dpad");case 142:return ICON("Dpad_LeftRight");
    case 143:return ICON("Dpad_UpDown");default:return NULL;
    }
#undef ICON
}
void *wb_control_icon(void *inputs,int key,int action,unsigned flags) {
    (void)inputs;
    if(key==140) {
        if(action==19) key=141;
        if(action==20) key=142;
        if(action==21) key=143;
        if(action==22) key=143;
        if(action==23) key=142;
    }
    const char *path=wb_control_icon_path(key);
    return path && load ? load(manager,path,0,flags) : NULL;
}
