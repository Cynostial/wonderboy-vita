#include <stdint.h>
#include <string.h>
#include <stdio.h>
static void (*original_upload)(void *,const void *);
static unsigned skipped_levels;
static unsigned (*original_memory_size)(const void *);
int wb_gl_texture_storage(unsigned,size_t *);
void wb_texture_memory_bind(unsigned (*function)(const void *)) { original_memory_size=function; }
unsigned wb_texture_memory_size(const void *texture) {
    /* OGLTexture's GL object is +0x38. LoadFinalize drains uploads before
     * inserting into the LRU. Charge exactly the resident padded mip chain,
     * both at insertion and eviction; keep source dimensions/data untouched. */
    unsigned id;size_t bytes;
    memcpy(&id,(const unsigned char *)texture+0x38,4);
    if(wb_gl_texture_storage(id,&bytes)) return (unsigned)bytes;
    return original_memory_size(texture);
}
void wb_texture_upload_bind(void (*function)(void *,const void *)) { original_upload=function; }
unsigned wb_texture_upload_skip(void) { return skipped_levels; }
void wb_texture_upload(void *texture,const void *data) {
    /* Exact OGLTexture header from the hash-pinned libwb.so. Do not resize
     * engine geometry/atlas metadata: only select existing GPU mip images. */
    const unsigned char *p=texture;uint16_t width,height;
    memcpy(&width,p+0x16,2);memcpy(&height,p+0x18,2);
    unsigned mips=p[0x1e];skipped_levels=0;
    /* Only precomputed compressed 2D chains. One-level intro logos and
     * uncompressed font/render surfaces retain every pixel. */
    if(p[0x15]==0 && p[0x14]>=0x20 && p[0x14]<=0x30 && mips<=13)
        while(mips>skipped_levels+1 && (width>1024 || height>1024)) {
            width=width>1 ? width/2 : 1;height=height>1 ? height/2 : 1;++skipped_levels;
        }
    original_upload(texture,data);
    skipped_levels=0;
}
