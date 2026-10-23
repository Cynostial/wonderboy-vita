#include "wb_streams.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

typedef struct { FILE *file; char *buffer; unsigned reserved; } stream_slot;
static stream_slot slots[WB_STREAM_LIMIT];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void acquire(void) { if (pthread_mutex_lock(&lock)) abort(); }
static void release(void) { if (pthread_mutex_unlock(&lock)) abort(); }

void wb_stream_buffer_read(FILE *file) {
    if (!file) return;
    int saved = errno;
    size_t index = WB_STREAM_LIMIT;
    acquire();
    for (size_t i = 0; i < WB_STREAM_LIMIT; ++i) {
        if (slots[i].file == file) { release(); errno = saved; return; }
        if (!slots[i].reserved && index == WB_STREAM_LIMIT) index = i;
    }
    if (index < WB_STREAM_LIMIT) slots[index].reserved = 1;
    release();
    if (index < WB_STREAM_LIMIT) {
        char *buffer = malloc(WB_STREAM_BUFFER_BYTES);
        if (buffer && setvbuf(file, buffer, _IOFBF, WB_STREAM_BUFFER_BYTES)) {
            free(buffer);
            buffer = NULL;
        }
        acquire();
        if (buffer) slots[index] = (stream_slot){file, buffer, 1};
        else slots[index] = (stream_slot){0};
        release();
    }
    errno = saved;
}

FILE *wb_stream_open(const char *path, int buffered) {
    if (!path) { errno = EINVAL; return NULL; }
    size_t index;
    acquire();
    for (index = 0; index < WB_STREAM_LIMIT && slots[index].reserved; ++index) {}
    if (index < WB_STREAM_LIMIT) slots[index].reserved = 1;
    release();
    if (index == WB_STREAM_LIMIT) { errno = EMFILE; return NULL; }
    FILE *file = fopen(path, "rb");
    char *buffer = NULL;
    if (file && buffered) {
        buffer = malloc(WB_STREAM_BUFFER_BYTES);
        if (!buffer) { fclose(file); file = NULL; }
        else if (setvbuf(file, buffer, _IOFBF, WB_STREAM_BUFFER_BYTES)) {
            fclose(file);
            free(buffer);
            buffer = NULL;
            file = NULL;
            errno = EIO;
        }
    }
    acquire();
    slots[index].file = file;
    slots[index].buffer = buffer;
    if (!file) slots[index].reserved = 0;
    release();
    return file;
}

int wb_stream_close(FILE *file) {
    if (!file) { errno = EINVAL; return EOF; }
    size_t index;
    char *buffer = NULL;
    acquire();
    for (index = 0; index < WB_STREAM_LIMIT && slots[index].file != file; ++index) {}
    if (index < WB_STREAM_LIMIT) {
        buffer = slots[index].buffer;
        slots[index].file = NULL;
    }
    release();
    /* Normal unbuffered AndroidFile::open(FileAttribute) uses fopen directly. */
    int result = fclose(file);
    free(buffer);
    if (index < WB_STREAM_LIMIT) {
        acquire();
        slots[index].buffer = NULL;
        slots[index].reserved = 0;
        release();
    }
    return result;
}

size_t wb_stream_buffer_bytes(void) {
    size_t total = 0;
    acquire();
    for (size_t i = 0; i < WB_STREAM_LIMIT; ++i)
        if (slots[i].buffer) total += WB_STREAM_BUFFER_BYTES;
    release();
    return total;
}

FILE *wb_stream_reopen(const char *path,const char *mode,FILE *file) {
    /* freopen consumes the old FILE on success and failure. Its old user
     * buffer must remain alive until that operation has completed. */
    size_t i;
    char *buffer=NULL;
    acquire();
    for(i=0;i<WB_STREAM_LIMIT && slots[i].file!=file;++i) {}
    if(i<WB_STREAM_LIMIT) { buffer=slots[i].buffer; slots[i].file=NULL; }
    release();
    FILE *result=freopen(path,mode,file);
    free(buffer);
    if(i<WB_STREAM_LIMIT) {
        acquire(); slots[i]=(stream_slot){0}; release();
    }
    return result;
}
