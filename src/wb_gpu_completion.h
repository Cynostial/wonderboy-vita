#ifndef WB_GPU_COMPLETION_H
#define WB_GPU_COMPLETION_H
#include <stdint.h>
/* Single render thread, pinned VitaGL scene stream. Zero means unknown and
 * must retain the caller's full glFinish fallback. Capture AFTER native draw:
 * scene_reset inside that draw may have submitted the previous scene. */
void wb_gpu_completion_bind(const volatile uint32_t *submitted,
                            const volatile uint32_t *completed,
                            void (*flush)(void),int (*wait_latest)(void));
void wb_gpu_completion_disable(void);
uint32_t wb_gpu_scene(void);
int wb_gpu_complete(uint32_t scene);
int wb_gpu_wait(uint32_t scene);
#endif
