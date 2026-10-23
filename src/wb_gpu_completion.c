#include "wb_gpu_completion.h"
static const volatile uint32_t *submitted,*completed;
static void (*flush_scene)(void);
static int (*wait_latest_scene)(void);
static uint32_t observed;
static int unavailable=1;
void wb_gpu_completion_disable(void) { unavailable=1; }
void wb_gpu_completion_bind(const volatile uint32_t *sent,const volatile uint32_t *done,
                            void (*flush)(void),int (*wait_latest)(void)) {
    submitted=sent;completed=done;flush_scene=flush;wait_latest_scene=wait_latest;
    unavailable=!sent || !done || !flush || !wait_latest;
    observed=sent ? *sent : 0;
}
static int valid(void) {
    if(unavailable) return 0;
    uint32_t sent=*submitted;
    /* Never infer lifetime from wrapping counters. After a reset or before
     * the signed boundary, disable this optimization until context creation.
     * Normal operation can submit over two billion scenes before fallback. */
    if(sent<observed || sent>=0x7ffffffeu || *completed>sent) {
        unavailable=1;return 0;
    }
    observed=sent;return 1;
}
uint32_t wb_gpu_scene(void) { return valid() ? *submitted+1u : 0; }
int wb_gpu_complete(uint32_t scene) {
    if(!scene || !valid() || scene>*completed) return 0;
    /* The GPU writes the notification. Order subsequent CPU writes/frees
     * after that observed completion, including externally shared memory. */
#if defined(__arm__)
    __asm__ volatile("dmb sy" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
#endif
    return 1;
}
int wb_gpu_wait(uint32_t scene) {
    if(!scene || !valid()) return 0;
    if(wb_gpu_complete(scene)) return 1;
    /* A draw in the current open scene has no submitted notification yet.
     * Flush only that case. An older resource need not close a newer scene. */
    if(scene>*submitted) flush_scene();
    if(!valid() || scene>*submitted) return 0;
    if(wb_gpu_complete(scene)) return 1;
    /* Wait for the latest SUBMITTED notification, never an old value that
     * the GPU may already have passed. This is safe even for an equality
     * wait API: this thread cannot submit a newer scene while blocked. */
    if(wait_latest_scene()<0) return 0;
    return wb_gpu_complete(scene);
}
