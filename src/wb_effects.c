/* Prepare only the two shared interaction atlases at an actual location
 * transition. The original loader may override the requested location, so
 * examine its completed state. Never turn speculative PreloadLoc calls into
 * image loads: those also walk neighboring levels on every game frame.
 *
 * Flags 0 use the original synchronous GetTexture/LoadFinalize path, including
 * the render-thread upload drain and 48 MiB LRU. No added references, pinned
 * textures, speculative queue, or persistent raw pointers. Loaded atlases are
 * reused; evicted atlases can be prepared again at the next transition.
 */
#include "wb_effects.h"
#include <stdio.h>
#include <string.h>
static wb_effects_api api;
static int word(const void *p,unsigned offset) {
    int32_t value;memcpy(&value,(const unsigned char *)p+offset,4);return value;
}
int wb_effects_bind(const wb_effects_api *value) {
    if(!value || !value->load_location || !value->manager ||
       !value->get_texture || !value->is_render_thread) return -1;
    api=*value;return 0;
}
void wb_effects_load_location(void *game,int requested,unsigned flags) {
    api.load_location(game,requested,flags);
    /* CGameMode 3 is gameplay; 2 is the new-game opening, which can enter
     * mode 3 without another LoadLocationNo call. Prepare it here as well.
     * Modes 0/4 are splash and 1 title; neither needs interaction textures.
     * LoadLocationNo accepts locations [0,250); +0x114 is its final location.
     * All offsets/symbols are for the SHA-256 pinned ARMv7 library only. */
    if(!game || (word(game,0x20)!=2 && word(game,0x20)!=3) ||
       (unsigned)word(game,0x114)>=250 ||
       !api.is_render_thread()) return;
    static const char *const paths[]={
        "tex/Objects/HitFX/HitFX01_00.png", /* Objects_FX: hit/landing/impact */
        "tex/Objects/BouncePad/BouncePad00.png" /* Objects: pads/blocks/items */
    };
    uint64_t start=api.clock_us ? api.clock_us() : 0;
    unsigned completed=0;
    for(unsigned i=0;i<sizeof(paths)/sizeof(*paths);++i)
        completed+=api.get_texture(api.manager,paths[i],0,0)!=NULL;
    uint64_t elapsed=api.clock_us ? api.clock_us()-start : 0;
    fprintf(stderr,"Common effects prewarm: location=%d textures=%u/2 time=%llu us\n",
            word(game,0x114),completed,(unsigned long long)elapsed);
    if(api.prewarm_location) api.prewarm_location(game);
}
