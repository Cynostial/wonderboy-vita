#include "so_util.h"
#include <string.h>
#include <stddef.h>
#include "wb_file_load.h"
#include "wb_texture_policy.h"
#include "wb_audio.h"
#include "wb_ui.h"
#include "wb_controls.h"
#include "wb_texture_reuse.h"
#include "wb_effects.h"
#include "wb_fps.h"
#include "wb_settings.h"
#include "wb_intro.h"
#include "wb_blocks.h"
#include "wb_scene.h"
#include "wb_prewarm.h"
#include "wb_prewarm_trim.h"
#include "wb_name_hash.h"
#include "wb_stream_lru.h"
#include "wb_z80_memory.h"
#include "wb_z80_trace.h"
#include "wb_pacing.h"
int wb_water_install(uintptr_t);
#include <psp2/kernel/processmgr.h>
void wb_retro_versions(uint64_t [32]);
void wb_gl_retro_update(unsigned,const void *,uint32_t);
void wb_z80_wait_bind(const unsigned char **,void (*)(void *));
void wb_z80_wait_run(void *);
void wb_android_file_open_buffered(void *, unsigned);
void wb_android_file_close(void *);
void wb_texture_upload_bind(void (*)(void *,const void *));
void wb_texture_upload(void *,const void *);
void wb_texture_memory_bind(unsigned (*)(const void *));
unsigned wb_texture_memory_size(const void *);
int wb_apply_render_quality(void *);
int wb_phase_bind(so_module *);
void wb_retro_unpack(void *,uint16_t);
void wb_retro_unpack_external(void *,uint16_t);
void wb_retro_update_bind(void *,int (*)(const void *),uint32_t (*)(uint8_t));
static void gpu_profile_frame(void *p,unsigned frame) { (void)p;(void)frame; }
static int gpu_profile_marker(void *p,const char *name,unsigned color,void *context) {
    (void)p;(void)name;(void)color;(void)context;return -1;
}
static int gpu_profile_available(const void *p,unsigned frame) { (void)p;(void)frame;return 0; }
struct patch { const char *name; uintptr_t replacement; unsigned char original[16]; };
/* GPU marker timings are unavailable on Vita. Disable their producers and
 * availability reporting rather than forcing glFinish for every marker.
 * CPU profiling and real buffer/fence synchronization remain intact. */
static const struct patch patches[] = {
{"_ZN10CRatingBox16OpenAppStorePageEv", (uintptr_t)wb_fps_toggle, {0x02,0x48,0x78,0x44,0x00,0x68,0x00,0x68,0x27,0xf2,0x3e,0xba,0x36,0xe4,0x2b,0x00}},
{"_ZN8Emulator15UnpackTileToTexEt", (uintptr_t)wb_retro_unpack_external, {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0f,0x87,0xb0,0xa7,0x4a,0x4f,0xf0,0x00,0x09}},
{"_ZN8Emulator18UnpackTileToTexOptEt", (uintptr_t)wb_retro_unpack, {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0f,0x9a,0xb0,0x4f,0xf4,0xfe,0x32,0x04,0x90}},
{"_ZN5Pasta16OGLGpuThreadInfo10startFrameEj", (uintptr_t)gpu_profile_frame, {0xf0,0xb5,0x3,0xaf,0x2d,0xe9,0x0,0xf,0x83,0xb0,0x4,0x46,0x2e,0x48,0x88,0x46}},
{"_ZN5Pasta16OGLGpuThreadInfo10pushMarkerEPKcjPNS_14GraphicContextE", (uintptr_t)gpu_profile_marker, {0xb0,0xb5,0x2,0xaf,0x5,0x46,0x52,0xf6,0xae,0xef,0x4,0x46,0x1,0x30,0xb,0xd0}},
{"_ZN5Pasta16OGLGpuThreadInfo9popMarkerEPKcjPNS_14GraphicContextE", (uintptr_t)gpu_profile_marker, {0xb0,0xb5,0x2,0xaf,0x5,0x46,0x52,0xf6,0x9c,0xef,0x4,0x46,0x1,0x30,0xb,0xd0}},
{"_ZNK5Pasta16OGLGpuThreadInfo16isFrameAvailableEj", (uintptr_t)gpu_profile_available, {0xd0,0xf8,0x54,0x31,0x2,0x46,0x0,0x20,0x8b,0x42,0x98,0xbf,0x70,0x47,0x53,0x69}},
{"_ZN7CInputs22FindPlatformIconForKeyEi11CGameInputsi", (uintptr_t)wb_control_icon, {0x80,0xb5,0x6f,0x46,0x82,0xb0,0x08,0x46,0x8a,0x49,0x8c,0x28,0x79,0x44,0x09,0x68}},
{"_ZN14CTouchControls19UpdateTouchTexturesEv", (uintptr_t)wb_touch_textures, {0xf0,0xb5,0x03,0xaf,0x4d,0xf8,0x04,0xbd,0x82,0xb0,0x04,0x46,0xaa,0x48,0xab,0x49}},
{"_ZN14CTouchControls8IsActiveEv", (uintptr_t)wb_touch_inactive, {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0b,0x04,0x46,0x90,0xf8,0xd8,0x00,0x00,0x28}},
{"_ZN14CTouchControls17TouchControlsDrawEP10ImDrawListRK6ImVec2", (uintptr_t)wb_touch_draw, {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0f,0x9f,0xb0,0x05,0x46,0xf2,0x48,0x8a,0x46}},
{"_ZN5Pasta11AndroidFile4openEj", (uintptr_t)wb_android_file_open_buffered, {0xf0,0xb5,0x3,0xaf,0x4d,0xf8,0x4,0xbd,0xd,0x46,0x4,0x46,0x10,0xf8,0x24,0x1f}},
{"_ZN5Pasta11AndroidFile5closeEv", (uintptr_t)wb_android_file_close, {0xb0,0xb5,0x2,0xaf,0x4,0x46,0x0,0x6c,0x8,0xf7,0x4,0xea,0x60,0x6c,0x0,0x25}},
{"_ZN5Pasta14AndroidFileMgr4loadEPKwPjiP7__sFILE", (uintptr_t)wb_file_load_open, {0xf0,0xb5,0x03,0xaf,0x4d,0xf8,0x04,0xbd,0xbc,0x68,0x34,0xb1,0x15,0x46,0x58,0x1c}},
{"_ZN5Pasta14AndroidFileMgr4loadEPKcPj", (uintptr_t)wb_file_load_named, {0xf0,0xb5,0x03,0xaf,0x4d,0xf8,0x04,0xbd,0x82,0xb0,0x16,0x46,0x0a,0x46,0x09,0x49}},
{"_ZN5Pasta10SDLFileMgr4loadEPKcPj", (uintptr_t)wb_file_load_named, {0xf0,0xb5,0x03,0xaf,0x4d,0xf8,0x04,0xbd,0x82,0xb0,0x16,0x46,0x0a,0x46,0x09,0x49}},
{"_ZN5Pasta10SDLFileMgr4loadEPKwPjiP7__sFILE", (uintptr_t)wb_file_load_open, {0xf0,0xb5,0x03,0xaf,0x4d,0xf8,0x04,0xbd,0xbc,0x68,0x34,0xb1,0x15,0x46,0x58,0x1c}},
{"_ZN5Pasta8ResHBase14asyncGetResultEPNS_11ResourceMgrENS0_4TaskE", (uintptr_t)wb_resource_result, {0x53,0x1c,0x03,0xd0,0x01,0x68,0xcb,0x6a,0x11,0x46,0x18,0x47,0x02,0x68,0x12,0x6f}},
{"_ZN12CAudioSample10LoadSampleEv", (uintptr_t)wb_audio_load_sample, {0xf0,0xb5,0x03,0xaf,0x4d,0xf8,0x04,0x8d,0xa6,0xb0,0x04,0x46,0x3c,0x48,0x3d,0x49}},
{"_ZN6SoLoud3Wav9getLengthEv", (uintptr_t)wb_audio_length, {0x90,0xed,0x02,0x0a,0xb5,0xee,0x40,0x0a,0xf1,0xee,0x10,0xfa,0x02,0xbf,0x9f,0xed}},
{"_ZN6SoLoud17WavStreamInstance6rewindEv", (uintptr_t)wb_audio_rewind, {0xd0,0xb5,0x02,0xaf,0x04,0x46,0xd0,0xf8,0xa4,0x01,0x10,0xb1,0x24,0xf7,0xbc,0xee}},
};
/* Caller must hash-verify the entire APK libwb.so before loading it. */
int wb_install_file_hooks(so_module *mod) {
    if(wb_z80_trace_prepare(mod->load_addr)) return -1;
    if(wb_pacing_prepare(mod->load_addr)) return -1;
    const char *lru_name="_ZN5cache9lru_cacheIyP7CTexRawE3putERKyRKS2_";
    const char *lru_find_name="_ZNSt6__ndk112__hash_tableINS_17__hash_value_typeIyNS_15__list_iteratorINS_4pairIyP7CTexRawEEPvEEEENS_22__unordered_map_hasherIyS9_NS_4hashIyEELb1EEENS_21__unordered_map_equalIyS9_NS_8equal_toIyEELb1EEENS_9allocatorIS9_EEE4findIyEENS_15__hash_iteratorIPNS_11__hash_nodeIS9_S7_EEEERKT_";
    uintptr_t lru_plt=so_trampoline_symbol(mod,lru_name);
    if(!lru_plt || wb_stream_lru_bind((wb_stream_lru_put_fn)so_symbol(mod,lru_name),
        (wb_stream_lru_find_fn)so_symbol(mod,lru_find_name))) return -1;
    uintptr_t hash_original=so_symbol(mod,"_Z10HashName64PKci");
    uintptr_t hash_plt=so_trampoline_symbol(mod,"_Z10HashName64PKci");
    if(!hash_original || !hash_plt) return -1;
    wb_name_hash_bind((uint64_t (*)(const char *,int))hash_original);
    uintptr_t block_build=so_symbol(mod,"_Z15DrawBlocksBuildP10ImDrawListiPK6ImRect");
    uintptr_t special=so_symbol(mod,"_ZNK5CGame20IsInAnySpecialScreenEv");
    const unsigned char block_entry[]={0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0f,0x81,0xb0,0x2d,0xed,0x10,0x8b,0xa4,0xb0};
    if(!block_build||!special||memcmp((void *)(block_build&~(uintptr_t)1),block_entry,16)||
       wb_blocks_bind((void *)so_symbol(mod,"g_Game"),(void *)so_symbol(mod,"g_Emu"),
           (void *)(mod->load_addr+0x531400),(void (*)(void *,int,const void *))block_build,
           (int (*)(const void *))special))return -1;
    if(wb_phase_bind(mod)) return -1;
    const char *dec_name="_Z15DrawLocationDecP10ImDrawListRK6ImVec2ffP14CLocationLayerP7CObjDecRbPK6ImRect";
    uintptr_t dec_plt=so_trampoline_symbol(mod,dec_name);
    if(!dec_plt || wb_scene_bind((void *)so_symbol(mod,"g_Game"),
        (wb_scene_draw_fn)so_symbol(mod,dec_name),
        (wb_scene_pos_fn)so_symbol(mod,"_ZNK7CObjDec7CalcPosEb"))) return -1;
    uintptr_t addresses[sizeof(patches)/sizeof(patches[0])];
    for (size_t i=0;i<sizeof(patches)/sizeof(patches[0]);++i) {
        addresses[i]=so_symbol(mod, patches[i].name);
        if (!addresses[i] || memcmp((void *)(addresses[i]&~(uintptr_t)1),patches[i].original,16)) return -1;
    }
    uintptr_t is_render=so_symbol(mod,"_ZN5Pasta14GraphicContext14IsRenderThreadEv");
    const char *get_name="_ZN11CTextureMgr10GetTextureEPKcy17CTextureLoadFlags";
    const char *anim_preload_name="_ZN8CAnimSet15PreloadTexturesEib";
    uintptr_t get_plt=so_trampoline_symbol(mod,get_name);
    uintptr_t anim_preload_plt=so_trampoline_symbol(mod,anim_preload_name);
    wb_prewarm_api prewarm={
        .manager=(void *)so_symbol(mod,"g_TextureMgr"),
        .get_texture=(void *(*)(void *,const char *,uint64_t,unsigned))so_symbol(mod,get_name),
        .preload_location=(void (*)(void *,int,const void *))so_symbol(mod,"_ZN5CGame10PreloadLocEiP6ImVec2"),
        .preload_animation=(void (*)(void *,int,int))so_symbol(mod,anim_preload_name),
        .finalize=(int (*)(void *,void *))so_symbol(mod,"_ZN11CTextureMgr12LoadFinalizeEP7CTexRaw"),
        .is_render_thread=(int (*)(void))is_render,
        .estimate=wb_prewarm_estimate,.admit=wb_prewarm_admit,.clock_us=sceKernelGetProcessTimeWide,
        .trim_begin=wb_prewarm_trim_begin,.trim=wb_prewarm_trim
    };
    wb_prewarm_trim_api trim={
        .manager=prewarm.manager,.cache=(void *)(mod->load_addr+0x53b224),
        .bytes=(uint32_t *)(mod->load_addr+0x53b220),.deletes=(void *)(mod->load_addr+0x5f8a84),
        .erase=(void (*)(void *,const uint64_t *))so_symbol(mod,"_ZN5cache9lru_cacheIyP7CTexRawE5eraseERKy"),
        .unload=(void (*)(void *,void *))so_symbol(mod,"_ZN11CTextureMgr6UnloadEP7CTexRaw"),
        .drain=(void (*)(void *))so_symbol(mod,"_ZN5Pasta10DeleteListINS_10OGLTextureEE5clearEv"),
        .size=wb_texture_memory_size,.admit=wb_prewarm_admit,.finish=wb_prewarm_gpu_finish
    };
    if(wb_prewarm_trim_bind(&trim)) return -1;
    if(!get_plt || !anim_preload_plt || wb_prewarm_bind(&prewarm)) return -1;
    const char *splash_name="_ZN3CUi16SplashScreenDrawEP10ImDrawListRK6ImVec2S4_";
    const char *intro_name="_ZN3CUi15IntroDrawModernEP10ImDrawListRK6ImVec2";
    uintptr_t splash_plt=so_trampoline_symbol(mod,splash_name),intro_plt=so_trampoline_symbol(mod,intro_name);
    wb_intro_api intro={
        .splash_draw=(void (*)(void *,void *,const wb_vec2 *,const wb_vec2 *))so_symbol(mod,splash_name),
        .intro_draw=(void (*)(void *,void *,const wb_vec2 *))so_symbol(mod,intro_name),
        .next_splash=(void (*)(void *))so_symbol(mod,"_ZN3CUi21SplashScreenNextVideoEv"),
        .stop_intro=(void (*)(void *))so_symbol(mod,"_ZN3CUi9IntroStopEv"),
        .capture=(void (*)(void *))so_symbol(mod,"_ZN7CInputs17ButtonsCaptureAllEv"),
        .inputs=(unsigned char *)so_symbol(mod,"g_Inputs")
    };
    if(!splash_plt || !intro_plt || wb_intro_bind(&intro))return -1;
    const char *options_name="_ZN3CUi15OptionsMenuDrawEP10ImDrawList6ImVec2f";
    const char *add_name="_ZN7CUiMenu7AddItemEjPvPKcif";
    uintptr_t options_plt=so_trampoline_symbol(mod,options_name);
    uintptr_t add_plt=so_trampoline_symbol(mod,add_name);
    if(!options_plt || !add_plt || wb_settings_bind(
        (void (*)(void *,void *,wb_settings_vec2,float))so_symbol(mod,options_name),
        (void (*)(void *,unsigned,void *,const char *,int,float))so_symbol(mod,add_name))) return -1;
    const char *localize_name="_Z18LocalizeGetMessagey";
    uintptr_t localize_plt=so_trampoline_symbol(mod,localize_name);
    if(!localize_plt || wb_fps_bind_localize((const char *(*)(uint64_t))
        so_symbol(mod,localize_name))) return -1;
    uintptr_t upload=so_symbol(mod,"_ZN5Pasta10OGLTexture9LoadQueueEv");
    const char *location_name="_ZN5CGame14LoadLocationNoEi18CLoadLocationFlags";
    uintptr_t location_plt=so_trampoline_symbol(mod,location_name);
    wb_effects_api effects={
        .load_location=(void (*)(void *,int,unsigned))so_symbol(mod,location_name),
        .manager=(void *)so_symbol(mod,"g_TextureMgr"),
        .get_texture=(void *(*)(void *,const char *,uint64_t,unsigned))
            so_symbol(mod,"_ZN11CTextureMgr10GetTextureEPKcy17CTextureLoadFlags"),
        .is_render_thread=(int (*)(void))is_render,
        .clock_us=sceKernelGetProcessTimeWide,
        .prewarm_location=wb_prewarm_location
    };
    if(!location_plt || wb_effects_bind(&effects)) return -1;
    const char *texture_name="_ZN5Pasta10OGLTexture14setTextureDataEPKNS_11TextureDataE";
    uintptr_t texture_upload=so_symbol(mod,texture_name);
    uintptr_t texture_plt=so_trampoline_symbol(mod,texture_name);
    if(!texture_upload || !texture_plt) return -1;
    const char *create_name="_ZN5Pasta14GraphicContext13CreateTextureEPKNS_11TextureDataEj";
    const char *destroy_name="_ZN5Pasta14GraphicContext14DestroyTextureEPNS_7TextureE";
    uintptr_t create_plt=so_trampoline_symbol(mod,create_name);
    uintptr_t destroy_plt=so_trampoline_symbol(mod,destroy_name);
    wb_texture_reuse_api reuse={
        .emulator=(void *)so_symbol(mod,"g_Emu"),
        .create=(void *(*)(const void *,unsigned))so_symbol(mod,create_name),
        .destroy=(void (*)(void *))so_symbol(mod,destroy_name),
        .versions=wb_retro_versions,.update=wb_gl_retro_update
    };
    if(!create_plt || !destroy_plt || !reuse.emulator || !reuse.create || !reuse.destroy) return -1;
    wb_texture_reuse_bind(&reuse);
    void *game_object=(void *)so_symbol(mod,"g_Game");
    uintptr_t inventory=so_symbol(mod,"_ZNK5CGame17IsInInventoryMainEv");
    uintptr_t cram_color=so_symbol(mod,"_ZN5CGame21PaletteCramEntryToRGBEh");
    if(!game_object || !inventory || !cram_color) return -1;
    wb_retro_update_bind(game_object,(int (*)(const void *))inventory,(uint32_t (*)(uint8_t))cram_color);
    const char *z80_name="_ZN10z80_device11execute_runEv";
    uintptr_t z80_original=so_symbol(mod,z80_name),z80_plt=so_trampoline_symbol(mod,z80_name);
    const unsigned char **rom=(const unsigned char **)so_symbol(mod,"g_Rom");
    if(!rom || !z80_original || !z80_plt) return -1;
    wb_z80_wait_bind(rom,(void (*)(void *))z80_original);
    uintptr_t read_plt=so_trampoline_symbol(mod,"_ZN8Emulator7MemReadEt");
    uintptr_t read16_plt=so_trampoline_symbol(mod,"_ZN8Emulator9MemRead16Et");
    uintptr_t write_plt=so_trampoline_symbol(mod,"_ZN8Emulator8MemWriteEth");
    if(!read_plt || !read16_plt || !write_plt || wb_z80_memory_bind(rom,
        (void (*)(void *,uint16_t,uint8_t))so_symbol(mod,"_ZN8Emulator8MemWriteEth"))) return -1;
    const char *atlas_name="_ZN9CTexAtlas7PreloadEv";
    uintptr_t atlas_original=so_symbol(mod,atlas_name),atlas_plt=so_trampoline_symbol(mod,atlas_name);
    if(!atlas_original || !atlas_plt) return -1;
    wb_atlas_preload_bind((void (*)(void *))atlas_original);
    wb_texture_upload_bind((void (*)(void *,const void *))texture_upload);
    uintptr_t memory_size=so_symbol(mod,"_ZNK5Pasta7Texture10getMemSizeEv");
    uintptr_t texture_vtable=so_symbol(mod,"_ZTVN5Pasta10OGLTextureE");
    if(!memory_size || !texture_vtable) return -1;
    uintptr_t *size_slot=(uintptr_t *)(texture_vtable+8+0x30);
    if(*size_slot!=memory_size) return -1;
    wb_texture_memory_bind((unsigned (*)(const void *))memory_size);
    *size_slot=(uintptr_t)wb_texture_memory_size;
    const char *cursor_name="_ZN7CUiMenu12CursorUpdateE6ImVec2fPiP11CUiMenuItem";
    uintptr_t cursor_plt=so_trampoline_symbol(mod,cursor_name);
    wb_ui_api ui={
        .cursor=(void (*)(void *,wb_vec2,float,int *,void *))so_symbol(mod,cursor_name),
        .inputs=(unsigned char *)so_symbol(mod,"g_Inputs"),
        .touch=(unsigned char *)so_symbol(mod,"g_TouchControls")
    };
    if(!cursor_plt || wb_ui_bind(&ui)) return -1;
    uintptr_t icon_load=so_symbol(mod,"_ZN11CTextureMgr10GetTextureEPKcy17CTextureLoadFlags");
    void *texture_manager=(void *)so_symbol(mod,"g_TextureMgr");
    uint8_t *touch_enabled=(uint8_t *)(mod->load_addr+0xe41b0);
    const uint8_t touch_original[]={0x90,0xf8,0xd8,0x00,0x70,0x47};
    if(!icon_load || !texture_manager || memcmp(touch_enabled,touch_original,6)) return -1;
    wb_controls_bind(texture_manager,(wb_icon_loader)icon_load);
    /* The original is only six bytes: do not use a long hook that would
     * overwrite the next function. movs r0,#0; bx lr; nop. */
    memcpy(touch_enabled,"\x00\x20\x70\x47\x00\xbf",6);
    wb_audio_api audio={
        .stream_construct=(void (*)(void *))so_symbol(mod,"_ZN6SoLoud9WavStreamC1Ev"),
        .stream_load=(int (*)(void *,unsigned char *,unsigned,int,int))so_symbol(mod,"_ZN6SoLoud9WavStream7loadMemEPhjbb"),
        .wav_load=(int (*)(void *,unsigned char *,unsigned,int,int))so_symbol(mod,"_ZN6SoLoud3Wav7loadMemEPhjbb"),
        .stream_vtable=so_symbol(mod,"_ZTVN6SoLoud9WavStreamE"),
        .vorbis_rewind=(int (*)(void *))so_symbol(mod,"stb_vorbis_seek_start")
    };
    if(!audio.stream_vtable) return -1;
    audio.stream_vtable+=8;
    if(wb_audio_bind(&audio)) return -1;
    if(!is_render || !upload || wb_apply_texture_patches((void *)mod->load_addr)) return -1;
    if(wb_apply_render_quality((void *)mod->load_addr)) return -1;
    if(wb_water_install(mod->load_addr)) return -1;
    wb_texture_policy_bind((int (*)(void))is_render,(void (*)(void))upload);
    for (size_t i=0;i<sizeof(patches)/sizeof(patches[0]);++i)
        hook_addr(addresses[i],patches[i].replacement);
    /* Intercept the PLT only, retaining the untouched original implementation
     * for the call from wb_ui_cursor. All game callers use this PLT entry. */
    hook_addr(cursor_plt,(uintptr_t)wb_ui_cursor);
    hook_addr(localize_plt,(uintptr_t)wb_fps_localize);
    hook_addr(options_plt,(uintptr_t)wb_settings_draw);
    hook_addr(add_plt,(uintptr_t)wb_settings_add);
    hook_addr(splash_plt,(uintptr_t)wb_intro_splash);
    hook_addr(intro_plt,(uintptr_t)wb_intro_story);
    hook_addr(texture_plt,(uintptr_t)wb_texture_upload);
    hook_addr(create_plt,(uintptr_t)wb_texture_reuse_create);
    hook_addr(destroy_plt,(uintptr_t)wb_texture_reuse_destroy);
    hook_addr(z80_plt,(uintptr_t)wb_z80_wait_run);
    hook_addr(atlas_plt,(uintptr_t)wb_atlas_preload);
    hook_addr(location_plt,(uintptr_t)wb_effects_load_location);
    hook_addr(dec_plt,(uintptr_t)wb_scene_draw);
    hook_addr(get_plt,(uintptr_t)wb_prewarm_texture);
    hook_addr(anim_preload_plt,(uintptr_t)wb_prewarm_animation);
    hook_addr(hash_plt,(uintptr_t)wb_name_hash);
    hook_addr(lru_plt,(uintptr_t)wb_stream_lru_put);
    hook_addr(read_plt,(uintptr_t)wb_z80_memory_read);
    hook_addr(read16_plt,(uintptr_t)wb_z80_memory_read16);
    hook_addr(write_plt,(uintptr_t)wb_z80_memory_write);
    hook_addr(mod->load_addr+0x17c1b1,(uintptr_t)wb_z80_trace_entry);
    hook_addr(mod->load_addr+0x1645ad,(uintptr_t)wb_pacing_entry);
    so_flush_caches(mod);
    return 0;
}
