#define SDL_MAIN_HANDLED
#include "wb_port.h"
#include "wb_input.h"
#include "wb_platform.h"
#include "wb_paths.h"
#include "wb_memory.h"
#include "wb_sha256.h"
#include "wb_shader_cache.h"
#include "wb_backend.h"
#include "wb_log_vita.h"
#include "../installer/wb_setup.h"
#include <SDL2/SDL.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/vshbridge.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/stat.h>
#include <unistd.h>
#define MIB (1024u*1024u)
unsigned int _newlib_heap_size_user=48*MIB;
unsigned int _sceLibcHeapSize=4*MIB;
unsigned int _sceLibcHeapExtendedAlloc=0;
static so_module game;
static so_default_dynlib imports[335];
extern void *wb_asset_manager(void);
static _Noreturn void fail(const char *text) {
    fprintf(stderr,"STARTUP ERROR: %s\n",text);fflush(stderr);
    wb_log_flush();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Wonder Boy",text,NULL);
    sceKernelExitProcess(1);
    abort();
}
static void missing(const char *name) { fprintf(stderr,"Missing import: %s\n",name); }
static void mkdir_checked(const char *path) {
    struct stat s;
    if(!stat(path,&s)) { if(!S_ISDIR(s.st_mode)) fail("A data directory is occupied by a file."); return; }
    if(mkdir(path,0777)) fail("Could not create the game save directory.");
}
static size_t bindings(void) {
    typedef const so_default_dynlib *(*provider)(size_t *);
    const provider providers[]={wb_sdl_bindings,wb_runtime_bindings,wb_platform_bindings,wb_asset_bindings,wb_locale_bindings};
    size_t total=0;
    for(size_t i=0;i<sizeof(providers)/sizeof(*providers);++i) {
        size_t n=0;const so_default_dynlib *table=providers[i](&n);
        if(n>335-total) fail("Import table exceeds the pinned library's import count.");
        memcpy(imports+total,table,n*sizeof(*table));total+=n;
    }
    if(total!=335) fail("Incomplete game compatibility library.");
    return total;
}
static void load_game(void) {
    const size_t length=3843592;
    static const uint8_t expected[32]={
        0x56,0x2a,0x0e,0xee,0xca,0xf5,0xdf,0x76,0xfe,0x27,0x30,0x67,0x3c,0x00,0xf0,0x73,
        0x2c,0x8e,0xb3,0x5b,0xb1,0x1b,0x6f,0x4d,0x7a,0x97,0xd4,0x28,0x5a,0x0b,0x48,0x42};
    FILE *f=fopen(WB_ROOT "/libwb.so","rb");
    if(!f) fail("Missing ux0:data/wonderboy/libwb.so.");
    if(fseek(f,0,SEEK_END) || ftell(f)!=(long)length || fseek(f,0,SEEK_SET))
        fail("The game library has an unsupported size.");
    void *image=malloc(length);
    if(!image) fail("Insufficient memory to load the game library.");
    if(fread(image,1,length,f)!=length) fail("Could not read the complete game library.");
    fclose(f);
    uint8_t hash[32];wb_sha256(image,length,hash);
    if(memcmp(hash,expected,32)) fail("Unsupported libwb.so. This port requires the prepared APK 1.1.0 library.");
    /* Hash equality pins all ELF metadata, architecture and relocation types. */
    if(so_mem_load(&game,image,length,0x98000000u)) fail("Could not map the game. Verify kubridge is installed.");
    free(image);
    game.ehdr=NULL;game.phdr=NULL;game.shdr=NULL;game.shstr=NULL;
    size_t n=bindings();
    if(wb_check_imports(&game,imports,n,missing) || wb_relocate(&game,imports,n))
        fail("The native library could not be relocated.");
    wb_platform_set_module(&game);
    if(wb_install_file_hooks(&game) || wb_install_platform_hooks(&game))
        fail("A checked game hook did not match the supported library.");
    /* The original video queue buffers 30 decoded frames. Three frames
     * bound queued video memory without changing frame size or timing. */
    uint8_t *queue=(uint8_t *)(game.load_addr+0x127fb8);
    if(queue[0]!=0x1e || queue[1]!=0x21) fail("Video queue instruction mismatch.");
    queue[0]=3;
    so_flush_caches(&game);
    fprintf(stderr,"libwb.so verified, mapped, relocated; 335 imports bound\n");fflush(stderr);
}
static void allocate_arena(void) {
    SceKernelFreeMemorySizeInfo free_info={.size=sizeof(free_info)};
    if(sceKernelGetFreeMemorySize(&free_info)<0) fail("Could not query available memory.");
    /* Leave 64 MiB for the fixed VitaGL RAM pool plus 48 MiB for kernel
     * stacks and other native allocations, in addition to the newlib heap. */
    size_t available=(unsigned)free_info.size_user;
    size_t reserve=112*MIB,capacity=160*MIB;
    fprintf(stderr,"Free pools: main %u, CDRAM %u, contiguous %u bytes; native heap %u bytes\n",
            (unsigned)free_info.size_user,(unsigned)free_info.size_cdram,
            (unsigned)free_info.size_phycont,_newlib_heap_size_user);
    if(available<=reserve+96*MIB) fail("Not enough application memory for this port.");
    if(capacity>available-reserve) capacity=(available-reserve)&~(MIB-1);
    SceUID uid=sceKernelAllocMemBlock("Wonder Boy arena",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,capacity,NULL);
    void *arena=NULL;
    if(uid<0 || sceKernelGetMemBlockBase(uid,&arena)<0 || wb_memory_init(arena,capacity))
        fail("Could not allocate the game arena.");
    fprintf(stderr,"Game/SDL arena %u MiB, free main memory before allocation %u MiB\n",
            (unsigned)(capacity/MIB),(unsigned)(available/MIB));
}
int main(void) {
    mkdir_checked(WB_ROOT);mkdir_checked(WB_SAVES);
    FILE *log=fopen(WB_ROOT "/run.log","w");
    if(log) fclose(log);
    freopen(WB_ROOT "/run.log","a",stderr);
    freopen(WB_ROOT "/run.log","a",stdout);
    setvbuf(stderr,NULL,_IONBF,0);
    setvbuf(stdout,NULL,_IONBF,0);
    int async_log=wb_log_start();
    fprintf(stderr,"Wonder Boy Vita native launcher 00.34 (VBlank pacing, asynchronous logs)\n");
    fprintf(stderr,"Asynchronous logging: %s\n",async_log ? "unavailable" : "enabled");
    mkdir_checked(WB_SHADER_CACHE_PATH);
    wb_shader_cache_directory(WB_SHADER_CACHE_PATH);
    int search[2];
    if(_vshKernelSearchModuleByName("kubridge",search)<0) fail("kubridge.skprx is not loaded.");
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    if(access("ur0:data/libshacccg.suprx",F_OK) && access("ur0:data/external/libshacccg.suprx",F_OK))
        fail("Install libshacccg.suprx in ur0:data before launching.");
    wb_setup_on_vita();
    setlocale(LC_ALL,"C.UTF-8");
    allocate_arena();
    if(wb_sdl_install_allocator() || wb_platform_init()) fail("Could not initialize the compatibility runtime.");
    SDL_SetMainReady();
    wb_input_initialize();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    load_game();
    fprintf(stderr,"Running game constructors\n");fflush(stderr);
    so_initialize(&game);
    /* Enable the engine's own startup/resource messages through its Android
     * log adapter. This does not change game initialization or its frame loop. */
    int (*enable_log)(int)=(int (*)(int))so_symbol(&game,"_ZN5Pasta11Application15CreateSysLogTTYEb");
    if(!enable_log || !enable_log(0)) fail("Could not enable the game startup log.");
    typedef void (*set_text)(const char *);
    set_text resource=(set_text)so_symbol(&game,"_ZN9JNIBridge15setResourcePathEPKc");
    set_text locale=(set_text)so_symbol(&game,"_ZN9JNIBridge9setLocaleEPKc");
    void (*manager)(void *)=(void (*)(void *))so_symbol(&game,"_ZN9JNIBridge10setManagerEP13AAssetManager");
    int (*entry)(int,char **)=(int (*)(int,char **))so_symbol(&game,"SDL_main");
    if(!resource || !locale || !manager || !entry) fail("Missing game startup entry points.");
    resource(WB_ASSETS "/");locale("en");manager(wb_asset_manager());
    char name[]="wonderboy";char *argv[]={name,NULL};
    fprintf(stderr,"Entering SDL_main\n");fflush(stderr);
    int result=entry(1,argv);
    fprintf(stderr,"Game exited with code %d\n",result);
    fflush(stderr);fflush(stdout);wb_log_flush();
    sceKernelExitProcess(result);
    return result;
}
