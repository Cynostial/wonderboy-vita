/* Offline platform services: retain the engine's achievement manager and
 * local game saves. No Java VM or Google Play login is claimed. */
#include "wb_port.h"
#include "wb_paths.h"
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
static void **manager_slot;
static uint32_t trophies;
static pthread_mutex_t trophy_lock=PTHREAD_MUTEX_INITIALIZER;
static void disconnect(void) {
    if(manager_slot && *manager_slot) ((unsigned char *)*manager_slot)[6]=0;
}
static void init_offline(void) {
    disconnect();
    FILE *f=fopen(WB_SAVES "/trophies.bin","rb");
    uint32_t record[2]={0};
    if(f) {
        if(fread(record,sizeof(record),1,f)==1 && record[0]==0x57425431)
            trophies=record[1]&0x1fff;
        fclose(f);
    }
    fprintf(stderr,"Platform: offline; local saves and local trophy record\n");
}
static void init_java(void *self,int enabled) { (void)self;(void)enabled;init_offline(); }
static int connected(void) { disconnect(); return 0; }
static void unavailable(void) {
    disconnect();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,"Wonder Boy",
        "Google Play is unavailable on PS Vita. Your game saves are stored locally.",NULL);
}
static void cloud_unavailable(void) {
    disconnect();
    fprintf(stderr,"Google Play cloud operation unavailable; local save remains authoritative\n");
}
static void unlock_trophy(void *self,int index) {
    (void)self;
    if(index<0 || index>=13) return;
    pthread_mutex_lock(&trophy_lock);
    uint32_t mask=1u<<index;
    if(!(trophies&mask)) {
        uint32_t record[2]={0x57425431,trophies|mask};
        FILE *f=fopen(WB_SAVES "/trophies.tmp","wb");
        if(f) {
            int wrote=fwrite(record,sizeof(record),1,f)==1;
            int closed=fclose(f)==0;
            if(wrote && closed && !rename(WB_SAVES "/trophies.tmp",WB_SAVES "/trophies.bin"))
                trophies=record[1];
            else fprintf(stderr,"Could not persist local trophy %d\n",index);
        }
    }
    pthread_mutex_unlock(&trophy_lock);
}
static void unlock_id(void *self,const char *id,int progress,int total) {
    if(!self || !id || total<=0 || progress<total) return;
    unsigned count=*(uint32_t *)((char *)self+8);
    const unsigned char *entries=*(const unsigned char **)((char *)self+16);
    if(count>13 || !entries) return;
    for(unsigned i=0;i<count;++i) {
        const char *name=*(const char **)(entries+76*i+4);
        if(name && !strcmp(name,id)) { unlock_trophy(self,(int)i); return; }
    }
}
static void show_trophies(void) {
    unsigned n=0;
    pthread_mutex_lock(&trophy_lock);
    for(unsigned i=0;i<13;++i) n+=(trophies>>i)&1;
    pthread_mutex_unlock(&trophy_lock);
    char text[128]; snprintf(text,sizeof(text),"%u of 13 achievements recorded on this Vita.",n);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,"Local achievements",text,NULL);
}
typedef struct { const char *name; uintptr_t replacement; unsigned char original[16]; } platform_patch;
static const platform_patch patches[]={
#include "wb_offline_patches.inc"
};
int wb_install_platform_hooks(so_module *m) {
    manager_slot=(void **)so_symbol(m,"_ZN5Pasta15AchievementsMgr9singletonE");
    if(!manager_slot) return -1;
    uintptr_t addresses[sizeof(patches)/sizeof(*patches)];
    for(size_t i=0;i<sizeof(patches)/sizeof(*patches);++i) {
        addresses[i]=so_symbol(m,patches[i].name);
        if(!addresses[i] || memcmp((void *)(addresses[i]&~(uintptr_t)1),patches[i].original,16)) return -1;
    }
    for(size_t i=0;i<sizeof(patches)/sizeof(*patches);++i)
        hook_addr(addresses[i],patches[i].replacement);
    return 0;
}
