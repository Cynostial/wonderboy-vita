#ifndef WB_TEXTURE_POLICY_H
#define WB_TEXTURE_POLICY_H
#include <stdint.h>
int wb_apply_texture_patches(void *image);
void wb_texture_policy_bind(int (*is_render_thread)(void), void (*upload_queue)(void));
void wb_atlas_preload_bind(void (*original)(void *));
void wb_atlas_preload(void *atlas);
void *wb_resource_result(void *manager, uint32_t request, int32_t resource, uint32_t flags);
#endif
