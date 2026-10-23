#include "wb_setup.h"
#include "wb_setup_screen.h"
#include "wb_setup_display.h"
#include "../src/wb_paths.h"
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned previous;
static uint64_t phase_start,last_draw,last_report;
static int phase=-1,drawn_percent=-1;
static void present(const char *message,unsigned done,unsigned total,int mode) {
    if(wb_setup_present(message,done,total,mode)<0) {
        fprintf(stderr,"SETUP ERROR: Could not present completed frame\n");
        sceKernelExitProcess(1);
    }
}
static unsigned buttons(void) {
    SceCtrlData pad={0};sceCtrlPeekBufferPositive(0,&pad,1);
    unsigned edge=pad.buttons&~previous;previous=pad.buttons;return edge;
}
static int progress(const char *name,unsigned done,unsigned total,void *arg) {
    (void)arg;
    uint64_t now=sceKernelGetProcessTimeWide();
    int current=!strcmp(name,"Checking OBB")?0:!strcmp(name,"Checking APK")?1:2;
    unsigned percent=total?(unsigned)((uint64_t)done*100/total):0;
    int changed=current!=phase;
    if(changed) {
        if(phase>=0)fprintf(stderr,"SETUP phase %d: %llu ms\n",phase,(unsigned long long)((now-phase_start)/1000));
        phase=current;phase_start=now;drawn_percent=-1;
    }
    if(current==2 && now-last_report>10000000) {
        fprintf(stderr,"SETUP file %u/%u: %s\n",done+1,total,name);last_report=now;
    }
    /* Paint only when visible progress changes. Continue polling cancel even
     * when no frame is needed; keep the system awake independently. */
    if((int)percent!=drawn_percent && (changed || now-last_draw>100000 || done==total)) {
        present(name,done,total,1);last_draw=now;drawn_percent=(int)percent;
    }
    static uint64_t last_tick;
    if(now-last_tick>1000000) {
        sceKernelPowerTick(0);
        last_tick=now;
    }
    return (buttons()&SCE_CTRL_CIRCLE)!=0;
}
static uint64_t required_space(void) {
    uint64_t bytes=16u*1024u*1024u;
    char path[512];struct stat st;
    for(unsigned i=0;i<wb_setup_count;++i) {
        snprintf(path,sizeof(path),WB_ROOT "/%s",wb_setup_files[i].path);
        if(stat(path,&st) || st.st_size<0 || (uint64_t)st.st_size!=wb_setup_files[i].output_size)
            bytes+=wb_setup_files[i].output_size;
    }
    return bytes;
}
void wb_setup_on_vita(void) {
    if(wb_setup_ready(WB_ROOT)) { wb_paths_imported(1);return; }
    /* Existing private development installations keep their prepared assets
     * and bundled overlays. Updating their VPK never rewrites saves/data. */
    if(!access(WB_ASSETS "/.prepared",F_OK) && !access(WB_ROOT "/libwb.so",F_OK) &&
       !access("app0:intro/DotEmu_Logo.ktx",F_OK))return;
    mkdir(WB_ROOT "/import",0777);
    uint32_t *screen=NULL;
    SceUID block=sceKernelAllocMemBlock("WB setup screens",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,4u*1024u*1024u,NULL);
    if(block<0 || sceKernelGetMemBlockBase(block,(void **)&screen)<0)sceKernelExitProcess(1);
    wb_setup_display_init(screen,screen+(2u*1024u*1024u)/sizeof(*screen));
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    uint32_t *art=malloc(960u*544u*4u);
    FILE *image=fopen("app0:art/loading.rgba","rb");
    if(art && image && fread(art,4,960u*544u,image)==960u*544u)wb_setup_screen_art(art);
    else { free(art);art=NULL; }
    if(image)fclose(image);
    wb_setup_result result={0};int complete=0,confirm=0;
    for(;;) {
        present(result.message,0,0,complete?(confirm?3:2):0);
        unsigned edge=buttons();
        if(!complete && (edge&SCE_CTRL_CIRCLE))sceKernelExitProcess(0);
        if(complete && confirm && (edge&SCE_CTRL_CIRCLE))confirm=0;
        if(complete && (edge&SCE_CTRL_TRIANGLE)) {
            if(!confirm)confirm=1;
            else {
                present("Verifying",0,1,1);
                wb_setup_cleanup(WB_ROOT,&result);confirm=0;
                previous=~0u;
            }
        }
        if(edge&SCE_CTRL_START) {
            if(complete && !confirm) {
                /* A fresh process releases all installer heap/framebuffer
                 * resources before SDL/GXM and the guest arena are created. */
                int rc=sceAppMgrLoadExec("app0:eboot.bin",NULL,NULL);
                snprintf(result.message,sizeof(result.message),"SETUP COMPLETE. CLOSE AND REOPEN THE GAME. RESTART CODE %X",rc);
            } else if(!complete && !wb_setup_find_inputs(WB_ROOT,&result)) {
                uint64_t max,free_bytes;
                if(sceAppMgrGetDevInfo("ux0:",&max,&free_bytes)>=0 && free_bytes<required_space())
                    snprintf(result.message,sizeof(result.message),"NOT ENOUGH FREE SPACE. ABOUT %u MB MORE REQUIRED.",(unsigned)((required_space()-free_bytes+1048575)/1048576));
                else {
                    phase=-1;drawn_percent=-1;last_draw=last_report=0;
                    uint64_t started=sceKernelGetProcessTimeWide();
                    complete=!wb_setup_run(WB_ROOT,result.apk,result.obb,progress,NULL,&result);
                    uint64_t ended=sceKernelGetProcessTimeWide();
                    fprintf(stderr,"SETUP phase %d: %llu ms; total %llu ms; files %u, reused %u, complete %d\n",
                            phase,(unsigned long long)((ended-phase_start)/1000),
                            (unsigned long long)((ended-started)/1000),result.completed,result.reused,complete);
                }
                previous=~0u;
            }
        }
        sceDisplayWaitVblankStart();
        sceKernelDelayThread(16000);
    }
}
