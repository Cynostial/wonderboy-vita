#include "wb_setup_display.h"
#include "wb_setup_screen.h"
#include <psp2/display.h>
#include <stdio.h>
#include <string.h>
static uint32_t *buffers[2];
static unsigned next,previous_done,previous_total;
static int previous_mode,valid;
static char previous_message[256];
void wb_setup_display_init(uint32_t *a,uint32_t *b) {
    buffers[0]=a;buffers[1]=b;next=0;valid=0;
}
int wb_setup_present(const char *message,unsigned done,unsigned total,int mode) {
    if(!buffers[0] || !buffers[1] || buffers[0]==buffers[1])return -1;
    if(!message)message="";
    if(valid && mode==previous_mode && done==previous_done && total==previous_total &&
       !strcmp(message,previous_message))return 0;
    /* Neither the current scanout nor a pending flip is ever a draw target.
     * WaitSetFrameBuf confirms the previous front is available for reuse. */
    wb_setup_screen(buffers[next],message,done,total,mode);
    __sync_synchronize();
    SceDisplayFrameBuf fb={sizeof(fb),buffers[next],960,SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,960,544};
    int result=sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME);
    if(result<0)return result;
    result=sceDisplayWaitSetFrameBuf();
    if(result<0)return result;
    next^=1;
    previous_done=done;previous_total=total;previous_mode=mode;
    snprintf(previous_message,sizeof(previous_message),"%s",message);
    valid=1;return 0;
}
