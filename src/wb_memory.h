#ifndef WB_MEMORY_H
#define WB_MEMORY_H
#include <stddef.h>
typedef struct {
    size_t capacity, live_blocks, live_bytes, peak_bytes, failed_requests;
} wb_memory_stats;
/* Caller owns the aligned arena and keeps it alive until shutdown.
 * Initialize before SDL_SetMemoryFunctions and before ELF constructors. */
int wb_memory_init(void *arena, size_t bytes);
int wb_memory_shutdown(void);
int wb_memory_check(void);
wb_memory_stats wb_memory_get_stats(void);
void *wb_malloc(size_t bytes);
void *wb_calloc(size_t count, size_t bytes);
void *wb_realloc(void *pointer, size_t bytes);
void wb_free(void *pointer);
int wb_posix_memalign(void **result, size_t alignment, size_t bytes);
char *wb_strdup(const char *text);
#endif
