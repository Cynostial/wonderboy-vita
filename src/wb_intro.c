#include "wb_intro.h"
static wb_intro_api api;
static int press;
int wb_intro_bind(const wb_intro_api *p) {
    if(!p || !p->splash_draw || !p->intro_draw || !p->next_splash ||
       !p->stop_intro || !p->capture || !p->inputs)return -1;
    api=*p;return 0;
}
void wb_intro_input_frame(void) { press=0; }
void wb_intro_press(void) { press=1; }
static int skip(void) {
    if(!press && !api.inputs[0xed] && !api.inputs[0x101])return 0;
    press=0;api.capture(api.inputs);
    /* Also consume the retained confirmation so it cannot choose Play or a
     * save slot when the original game transition draws its next screen. */
    wb_ui_input_frame();return 1;
}
void wb_intro_splash(void *ui,void *draw,const wb_vec2 *a,const wb_vec2 *b) {
    if(((unsigned char *)ui)[0x3c] && skip()) {
        ((unsigned char *)ui)[0x3d]=1;
        api.next_splash(ui);return;
    }
    api.splash_draw(ui,draw,a,b);
}
void wb_intro_story(void *ui,void *draw,const wb_vec2 *position) {
    if(((unsigned char *)ui)[0x280] && skip()) { api.stop_intro(ui);return; }
    api.intro_draw(ui,draw,position);
}
