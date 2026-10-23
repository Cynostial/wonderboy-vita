/* A fixed arena: allocation failure never falls back to the system heap.
 * Counters report allocator block payload, not GPU or native-library memory.
 * All pointers returned across the SDL/Android allocation boundary must use
 * this allocator; native FILE/DIR objects stay with their native libraries. */
#include "wb_memory.h"
#include "tlsf.h"
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_ALIGNMENT _Alignof(max_align_t)
static tlsf_t allocator;
static uintptr_t arena_start, arena_end;
static wb_memory_stats stats;
/* Decoder workers and SDL audio can preempt an arena owner. A busy spin can
 * occupy every available core while the lower-priority owner cannot run.
 * PTE allocates its mutex metadata on the native heap, outside this arena. */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static void lock(void) { if (pthread_mutex_lock(&mutex)) abort(); }
static void unlock(void) { if (pthread_mutex_unlock(&mutex)) abort(); }

int wb_memory_init(void *arena, size_t bytes) {
    size_t overhead = tlsf_size() + tlsf_pool_overhead();
    if (!arena || (uintptr_t)arena % tlsf_align_size() ||
        bytes < overhead + tlsf_block_size_min() ||
        bytes - overhead > tlsf_block_size_max() ||
        (uintptr_t)arena > UINTPTR_MAX - bytes) return EINVAL;
    lock();
    if (allocator) { unlock(); return EBUSY; }
    allocator = tlsf_create_with_pool(arena, bytes);
    if (!allocator) { unlock(); return EINVAL; }
    arena_start = (uintptr_t)arena; arena_end = arena_start + bytes;
    memset(&stats, 0, sizeof(stats));
    stats.capacity = bytes;
    unlock();
    return 0;
}
int wb_memory_shutdown(void) {
    lock();
    if (stats.live_blocks) { unlock(); return EBUSY; }
    if (allocator) tlsf_destroy(allocator);
    allocator = NULL; arena_start = arena_end = 0;
    memset(&stats, 0, sizeof(stats));
    unlock();
    return 0;
}
static void valid_pointer(const void *pointer) {
    /* Freeing interior or already-freed pointers is still undefined, as with
     * malloc. This rejects accidental native/guest allocator mixing. */
    if (!allocator || (uintptr_t)pointer < arena_start + tlsf_size() ||
        (uintptr_t)pointer >= arena_end) abort();
}
static void allocated(void *pointer) {
    if (!pointer) { ++stats.failed_requests; errno = ENOMEM; return; }
    ++stats.live_blocks;
    stats.live_bytes += tlsf_block_size(pointer);
    if (stats.live_bytes > stats.peak_bytes) stats.peak_bytes = stats.live_bytes;
}
void *wb_malloc(size_t bytes) {
    lock();
    /* TLSF's base alignment is only 4 bytes on ARM32. Android malloc/new
     * require alignment suitable for double/int64_t as well. */
    void *pointer = allocator && bytes < tlsf_block_size_max() ?
        tlsf_memalign(allocator, DEFAULT_ALIGNMENT, bytes ? bytes : 1) : NULL;
    allocated(pointer);
    unlock();
    return pointer;
}
void wb_free(void *pointer) {
    if (!pointer) return;
    lock();
    valid_pointer(pointer);
    stats.live_bytes -= tlsf_block_size(pointer);
    --stats.live_blocks;
    tlsf_free(allocator, pointer);
    unlock();
}
void *wb_calloc(size_t count, size_t bytes) {
    if (bytes && count > SIZE_MAX / bytes) {
        lock(); ++stats.failed_requests; unlock();
        errno = ENOMEM; return NULL;
    }
    size_t size = count * bytes;
    void *pointer = wb_malloc(size);
    if (pointer) memset(pointer, 0, size);
    return pointer;
}
void *wb_realloc(void *pointer, size_t bytes) {
    if (!pointer) return wb_malloc(bytes);
    if (!bytes) { wb_free(pointer); return NULL; }
    lock();
    valid_pointer(pointer);
    size_t old_bytes = tlsf_block_size(pointer);
    void *replacement = NULL;
    if (bytes <= old_bytes) {
        /* Shrinking is in place and retains the pointer's alignment. */
        replacement = tlsf_realloc(allocator, pointer, bytes);
    } else if (bytes < tlsf_block_size_max()) {
        /* TLSF realloc may move to a 4-byte-aligned block on ARM32. Use an
         * aligned replacement and budget both blocks during the copy. */
        replacement = tlsf_memalign(allocator, DEFAULT_ALIGNMENT, bytes);
        if (replacement) {
            size_t transient = stats.live_bytes + tlsf_block_size(replacement);
            if (transient > stats.peak_bytes) stats.peak_bytes = transient;
            memcpy(replacement, pointer, old_bytes);
            tlsf_free(allocator, pointer);
        }
    }
    if (replacement) {
        stats.live_bytes -= old_bytes;
        stats.live_bytes += tlsf_block_size(replacement);
        if (stats.live_bytes > stats.peak_bytes) stats.peak_bytes = stats.live_bytes;
    } else {
        ++stats.failed_requests; errno = ENOMEM;
    }
    unlock();
    return replacement;
}
int wb_posix_memalign(void **result, size_t alignment, size_t bytes) {
    if (!result || alignment < sizeof(void *) || (alignment & (alignment - 1)) ||
        alignment % sizeof(void *)) return EINVAL;
    /* TLSF's alignment padding arithmetic must not wrap for extreme input. */
    if (bytes > tlsf_block_size_max() || alignment > tlsf_block_size_max() ||
        bytes > SIZE_MAX - alignment - tlsf_block_size_min() - tlsf_align_size())
        return ENOMEM;
    lock();
    void *pointer = allocator ? tlsf_memalign(allocator, alignment, bytes ? bytes : 1) : NULL;
    allocated(pointer);
    unlock();
    if (!pointer) return ENOMEM;
    *result = pointer;
    return 0;
}
char *wb_strdup(const char *text) {
    size_t bytes = strlen(text) + 1;
    char *copy = wb_malloc(bytes);
    if (copy) memcpy(copy, text, bytes);
    return copy;
}
wb_memory_stats wb_memory_get_stats(void) {
    lock(); wb_memory_stats copy = stats; unlock(); return copy;
}
int wb_memory_check(void) {
    lock();
    int result = allocator ? (tlsf_check(allocator) || tlsf_check_pool(tlsf_get_pool(allocator))) : -1;
    unlock();
    return result;
}
