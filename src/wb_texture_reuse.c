/* Reuse the game's two alternating SMS texture objects. Only intercept the
 * destroy-then-create pair inside CGame::Update, with the exact loadRaw
 * descriptor and flags verified. All other creates/deletes retain the guest
 * path. No extra textures, ownership aliases or persistent pending deletes. */
#include "wb_texture_reuse.h"
#include <string.h>
typedef struct { void *texture;uint64_t versions[32]; } entry;
static entry slots[2],*pending;
static wb_texture_reuse_api api;
static int active;
static uint32_t word(const void *p,unsigned at) {
    uint32_t value;memcpy(&value,(const unsigned char *)p+at,4);return value;
}
static uint16_t half(const void *p,unsigned at) {
    uint16_t value;memcpy(&value,(const unsigned char *)p+at,2);return value;
}
static int matches(const void *data,unsigned flags) {
    const unsigned char *p=data;
    return p && flags==0x40500 && half(p,4)==128 && half(p,6)==1024 &&
        half(p,8)==1 && half(p,10)==1 && half(p,12)==1 && p[14]==6 &&
        p[20]==1 && !p[21] && !p[22] &&
        word(p,32)==(uint32_t)(uintptr_t)(p+36) &&
        word(p,36)==(uint32_t)((uintptr_t)api.emulator+0x240e4);
}
void wb_texture_reuse_bind(const wb_texture_reuse_api *p) { api=*p; }
void wb_texture_reuse_begin(void) { active=1; }
static void dispose(entry *e) {
    void *texture=e->texture;
    if(pending==e) pending=0;
    e->texture=0;
    api.destroy(texture);
}
void wb_texture_reuse_end(void) {
    active=0;
    /* A future guest change or early return must not silently retain a
     * deletion that was not paired with the expected create. */
    if(pending) dispose(pending);
}
void wb_texture_reuse_destroy(void *texture) {
    for(unsigned i=0;i<2;++i) if(texture && slots[i].texture==texture) {
        if(active && !pending) { pending=&slots[i];return; }
        dispose(&slots[i]);return;
    }
    api.destroy(texture);
}
void *wb_texture_reuse_create(const void *data,unsigned flags) {
    int retro=active && matches(data,flags);
    uint64_t versions[32];
    if(retro) api.versions(versions);
    if(pending) {
        if(retro) {
            entry *e=pending;pending=0;uint32_t changed=0;
            for(unsigned i=0;i<32;++i) if(e->versions[i]!=versions[i]) changed|=1u<<i;
            if(changed) api.update(word(e->texture,0x38),
                                  (const void *)((uintptr_t)api.emulator+0x240e4),changed);
            memcpy(e->versions,versions,sizeof(versions));
            return e->texture;
        }
        dispose(pending);
    }
    void *texture=api.create(data,flags);
    if(texture && retro) for(unsigned i=0;i<2;++i) if(!slots[i].texture) {
        slots[i].texture=texture;memcpy(slots[i].versions,versions,sizeof(versions));break;
    }
    return texture;
}
