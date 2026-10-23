/* Layout verified against libwb.so SHA256
 * 562a0eeecaf5df76fe2730673c00f0732c8eb35bb11b6f4d7a97d4285a0b4842.
 * Install only before game initialization. The loader must resolve every FILE
 * operation to the same Vita stdio ABI; these hooks alone do not provide that. */
#include "wb_streams.h"
#include "wb_stdio.h"
#include "wb_paths.h"
#include <stdint.h>
#include <string.h>
#include <errno.h>

typedef struct {
    unsigned char opaque[0x24];
    unsigned char name[12]; /* Android libc++ std::string, not Vita std::string */
    uint32_t unknown;
    uint32_t size, position, attribute;
    wb_file *file;
    void *buffer;
} android_file;
_Static_assert(sizeof(void *) == 4, "ARMv7 adapter only");
_Static_assert(offsetof(android_file, file) == 0x40, "AndroidFile FILE layout");
_Static_assert(offsetof(android_file, buffer) == 0x44, "AndroidFile buffer layout");

void wb_android_file_open_buffered(android_file *self, unsigned buffered) {
    const char *path = (const char *)self->name + 1;
    if (self->name[0] & 1) memcpy(&path, self->name + 8, sizeof(path));
    self->file = NULL;
    self->buffer = NULL; /* Owned by wb_streams, never the Android delete[] ABI. */
    self->size = self->position = self->attribute = 0;
    char resolved[WB_PATH_MAX];
    if(wb_path_resolve(path,resolved,sizeof(resolved))) return;
    FILE *f = wb_stream_open(resolved, buffered != 0);
    if (!f) return;
    if (fseek(f, 0, SEEK_END)) { wb_stream_close(f); return; }
    long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET)) { wb_stream_close(f); return; }
    self->size = (uint32_t)length;
    self->file = wb_stdio_adopt(f,"rb");
    if(!self->file) self->size=0;
}

void wb_android_file_close(android_file *self) {
    if (self->file) wb_fclose(self->file);
    self->file = NULL;
    self->buffer = NULL;
}
