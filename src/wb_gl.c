/* SDL_GL_GetProcAddress target for the prepared GLES100 assets.
 * Render-thread only. No GLES3 capability string is fabricated here.
 * Unknown functions remain NULL for the engine's capability checks.
 */
#include <vitaGL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wb_shader_cache.h"
#include "wb_gl_state.h"
#include "wb_gpu_completion.h"

#define TEXTURES 4096
#define UNITS 16
typedef struct { GLuint id; GLsizei width, height,reported_width,reported_height; size_t storage_width,storage_height,levels[16]; uint32_t scene; } texture_size;
static texture_size textures[TEXTURES];
static texture_size *recent_textures[64];
static size_t texture_bytes,texture_peak;
static GLuint bound[UNITS], active, program;
static uint32_t draw_calls;
uint32_t wb_gl_draw_calls(void) { return draw_calls; }
static uint64_t (*draw_clock)(void);
static uint64_t setup_us,submit_us;
static uint64_t image_us,subimage_us,image_bytes,subimage_bytes;
static uint32_t image_calls,subimage_calls;
static uint64_t clear_us,attachment_us;
static uint32_t clear_calls,attachment_calls;
void wb_gl_scene_stats(uint64_t *clear,uint64_t *attachment,uint32_t *clears,uint32_t *attachments) {
    *clear=clear_us;*attachment=attachment_us;*clears=clear_calls;*attachments=attachment_calls;
}
void wb_gl_bind_clock(uint64_t (*clock)(void)) { draw_clock=clock; }
void wb_gl_draw_times(uint64_t *setup,uint64_t *submit) { *setup=setup_us;*submit=submit_us; }
void wb_gl_upload_stats(uint64_t *image,uint64_t *subimage,uint32_t *images,uint32_t *subimages,
                        uint64_t *image_size,uint64_t *subimage_size) {
    *image=image_us;*subimage=subimage_us;*images=image_calls;*subimages=subimage_calls;
    *image_size=image_bytes;*subimage_size=subimage_bytes;
}
unsigned wb_texture_upload_skip(void);
void wb_vertex_draw_arrays(GLenum,GLint,GLsizei);
void wb_vertex_draw_elements(GLenum,GLsizei,GLenum,const void *);
void wb_buffer_draw_arrays(GLint,GLsizei);
void wb_buffer_draw_elements(GLsizei,GLenum,const void *);
void wb_buffer_draw_submitted(uint32_t);
void wb_buffer_draw_conservative(int);
void wb_buffer_sync_scene(uint32_t);
typedef struct { GLuint id; GLint mask_unit,mask_location,size_location; int deleted,locations_ready; } program_info;
static program_info programs[256];
static void fatal(const char *reason) { fprintf(stderr, "Wonder Boy GL: %s\n", reason); abort(); }
static texture_size *find(GLuint id, int create) {
    texture_size *empty = NULL;
    if (!id) return NULL;
    texture_size **recent=&recent_textures[(id^(id>>6))%64];
    if(*recent && (*recent)->id==id) return *recent;
    for (size_t i = 0; i < TEXTURES; ++i) {
        if (textures[i].id == id) { *recent=&textures[i];return *recent; }
        if (!textures[i].id && !empty) empty = &textures[i];
    }
    if (create && empty) { empty->id = id;*recent=empty;return empty; }
    if (create) fatal("texture metadata capacity exceeded");
    return NULL;
}
static void active_texture(GLenum unit) {
    if (unit < GL_TEXTURE0 || unit >= GL_TEXTURE0 + UNITS) fatal("unsupported texture unit");
    active = unit - GL_TEXTURE0;
    glActiveTexture(unit);
}
static void bind_texture(GLenum target, GLuint id) {
    if (target == GL_TEXTURE_2D) bound[active] = id;
    glBindTexture(target, id);
}
static void remember(GLenum target, GLint level, GLsizei w, GLsizei h) {
    if (target != GL_TEXTURE_2D || level != 0) return;
    if (w <= 0 || h <= 0) fatal("invalid texture dimensions");
    texture_size *t = find(bound[active], 1);
    if (!t) fatal("texture image without a bound texture");
    t->width = w; t->height = h;
}
static void reserve_texture(GLenum target,GLint level,GLsizei w,GLsizei h,size_t bytes) {
    if(target!=GL_TEXTURE_2D || level<0 || level>=16 || w<=0 || h<=0 || w>4096 || h>4096)
        fatal("unsupported texture target/dimensions/level");
    texture_size *t=find(bound[active],1);
    if(!t) fatal("texture upload without a bound object");
    size_t other=texture_bytes-t->levels[level];
    if(other>80u*1024u*1024u || bytes>80u*1024u*1024u-other)
        fatal("80 MiB texture residency limit exceeded");
    texture_bytes=other+bytes;t->levels[level]=bytes;
    if(texture_bytes>texture_peak) texture_peak=texture_bytes;
}
static void image_2d(GLenum target, GLint level, GLint format, GLsizei w, GLsizei h,
                     GLint border, GLenum pixel_format, GLenum type, const void *pixels) {
    remember(target, level, w, h);
    /* Conservative 32-bit storage accounting for uncompressed surfaces. */
    reserve_texture(target,level,w,h,(size_t)w*(size_t)h*4);
    /* Dynamic engine textures are orphaned every frame, too. Log a bounded
     * number of first/new dimensions, never each repeated allocation. */
    if(!pixels && level==0) {
        static unsigned reports;
        texture_size *t=find(bound[active],0);
        if(t && (t->reported_width!=w || t->reported_height!=h)) {
            t->reported_width=w;t->reported_height=h;
            if(reports++<24)
                fprintf(stderr,"Render surface: id=%u %dx%d format=0x%x\n",(unsigned)bound[active],w,h,(unsigned)format);
        }
    }
    uint64_t begin=draw_clock ? draw_clock() : 0;
    glTexImage2D(target, level, format, w, h, border, pixel_format, type, pixels);
    if(draw_clock) image_us+=draw_clock()-begin;
    ++image_calls;image_bytes+=(uint64_t)(unsigned)w*(unsigned)h*4;
}
static void subimage_2d(GLenum target,GLint level,GLint x,GLint y,GLsizei w,GLsizei h,
                        GLenum format,GLenum type,const void *pixels) {
    uint64_t begin=draw_clock ? draw_clock() : 0;
    glTexSubImage2D(target,level,x,y,w,h,format,type,pixels);
    if(draw_clock) subimage_us+=draw_clock()-begin;
    ++subimage_calls;
    if(w>0 && h>0) subimage_bytes+=(uint64_t)(unsigned)w*(unsigned)h*4;
}
void wb_gl_retro_update(unsigned id,const void *pixels,uint32_t bands) {
    if(!bands) return;
    texture_size *t=find(id,0);
    if(!t || t->width!=128 || t->height!=1024) fatal("invalid retained SMS texture");
    /* A retained texture may still be sampled by queued draws. Its exact
     * last scene must complete BEFORE any CPU write, independently of slot
     * alternation. Unknown tracking keeps the original full-finish fallback. */
    wb_buffer_sync_scene(t->scene);
    GLuint old_unit=active,old_texture=bound[0];
    active_texture(GL_TEXTURE0);bind_texture(GL_TEXTURE_2D,id);
    /* This exact 128x1024 RGBA8 image is linear RGBA in the pinned backend
     * (checked against its actual ARM image/subimage implementation). The
     * public pointer API avoids SubImage's frame-age based orphan/copy after
     * our explicit completion. Only this descriptor can reach this path. */
    unsigned char *destination=vglGetTexDataPointer(GL_TEXTURE_2D);
    if(!destination) fatal("retained SMS texture has no mapped storage");
    uint64_t begin=draw_clock ? draw_clock() : 0;
    unsigned runs=0,bits=0;
    for(unsigned i=0;i<32;++i) if(bands&(1u<<i)) {
        ++bits;if(!i || !(bands&(1u<<(i-1)))) ++runs;
    }
    /* Sparse bands share the same row layout in each of the four palettes.
     * Bound driver calls for scattered changes; dense changes upload once. */
    if(bits>=16 || runs>3) {
        memcpy(destination,pixels,128u*1024u*4u);
        ++subimage_calls;subimage_bytes+=128u*1024u*4u;
    } else for(unsigned plane=0;plane<4;++plane) {
        for(unsigned first=0;first<32;) {
            if(!(bands&(1u<<first))) { ++first;continue; }
            unsigned end=first+1;
            while(end<32 && (bands&(1u<<end))) ++end;
            unsigned y=plane*256+first*8,height=(end-first)*8;
            memcpy(destination+y*512,(const unsigned char *)pixels+y*512,height*512);
            ++subimage_calls;subimage_bytes+=height*512;
            first=end;
        }
    }
    if(draw_clock) subimage_us+=draw_clock()-begin;
    bind_texture(GL_TEXTURE_2D,old_texture);active_texture(GL_TEXTURE0+old_unit);
}
static void clear(GLbitfield mask) {
    uint64_t begin=draw_clock ? draw_clock() : 0;
    glClear(mask);
    if(draw_clock) clear_us+=draw_clock()-begin;
    ++clear_calls;
}
static void framebuffer_texture(GLenum target,GLenum attachment,GLenum texture_target,GLuint texture,GLint level) {
    uint64_t begin=draw_clock ? draw_clock() : 0;
    glFramebufferTexture2D(target,attachment,texture_target,texture,level);
    if(draw_clock) attachment_us+=draw_clock()-begin;
    ++attachment_calls;
}
static void compressed_2d(GLenum target, GLint level, GLenum format, GLsizei w, GLsizei h,
                          GLint border, GLsizei size, const void *pixels) {
    if(target!=GL_TEXTURE_2D || level<0 || level>=16 || w<=0 || h<=0 || w>4096 || h>4096)
        fatal("invalid compressed image geometry");
    /* Require prepared BC1/BC3 assets: ETC2 fallback expands to RGBA in VitaGL. */
    /* The guest normalizes some RGB BC1 KTXs to RGBA BC1 at upload. Both
     * enums describe 8-byte blocks and are supported natively by VitaGL. */
    int bc1 = format == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ||
              format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    if (!bc1 && format != GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)
        fatal("unprepared compressed texture");
    remember(target, level, w, h);
    size_t blocks=(((size_t)w+3)/4)*(((size_t)h+3)/4);
    size_t bytes=blocks*(bc1 ? 8 : 16);
    if(size<0 || (size_t)size!=bytes) fatal("compressed texture byte count mismatch");
    unsigned skip=wb_texture_upload_skip();
    if((unsigned)level<skip) return;
    /* GXM swizzled BC storage rounds each dimension to a power of two,
     * including sub-4x4 tail mips. File block sizes undercount this storage. */
    texture_size *t=find(bound[active],1);
    if(!t) fatal("compressed image without bound texture");
    unsigned native_level=(unsigned)level-skip;
    if(!native_level) {
        t->storage_width=t->storage_height=1;
        while(t->storage_width<(size_t)w) t->storage_width*=2;
        while(t->storage_height<(size_t)h) t->storage_height*=2;
    }
    if(!t->storage_width || !t->storage_height) fatal("compressed mip before base image");
    size_t pw=t->storage_width>>native_level,ph=t->storage_height>>native_level;
    if(pw<4) pw=4;
    if(ph<4) ph=4;
    reserve_texture(target,(GLint)native_level,w,h,(pw/4)*(ph/4)*(bc1 ? 8 : 16));
    /* Mip growth may move the old allocation. Finish transfers before the
     * native realloc can release memory still referenced by the GPU. */
    /* glFinish waits for drawing, not the GXM transfer queue. */
    if((unsigned)level>skip && sceGxmTransferFinish()<0) fatal("texture transfer completion failed");
    glCompressedTexImage2D(target,(GLint)native_level,format,w,h,border,size,pixels);
}
static void delete_textures(GLsizei n, const GLuint *ids) {
    glDeleteTextures(n, ids);
    for (GLsizei i = 0; i < n; ++i) {
        texture_size *t = find(ids[i], 0);
        if (t) {
            for(unsigned level=0;level<16;++level) texture_bytes-=t->levels[level];
            memset(t, 0, sizeof(*t));
        }
        for (size_t unit = 0; unit < UNITS; ++unit)
            if (bound[unit] == ids[i]) bound[unit] = 0;
    }
}
static void use_program(GLuint id) {
    GLuint previous = program;
    glUseProgram(id); program = id;
    if (previous != id)
        for (size_t i = 0; i < 256; ++i)
            if (programs[i].id == previous && programs[i].deleted)
                memset(&programs[i], 0, sizeof(programs[i]));
}
static program_info *program_record(GLuint id) {
    program_info *empty = NULL;
    for (size_t i = 0; i < 256; ++i) {
        if (programs[i].id == id && id) return &programs[i];
        if (!programs[i].id && !empty) empty = &programs[i];
    }
    if (!empty || !id) fatal("program metadata capacity exceeded");
    empty->id = id; empty->mask_unit = 0; /* GL sampler default */
    return empty;
}
static program_info *program_locations(GLuint id) {
    program_info *p=program_record(id);
    if(!p->locations_ready) {
        p->mask_location=glGetUniformLocation(id,"uTexture1");
        p->size_location=glGetUniformLocation(id,"uWbTexture1InvSize");
        p->locations_ready=1;
    }
    return p;
}
static void uniform_1i(GLint location, GLint value) {
    if (program && location >= 0 &&
        location == program_locations(program)->mask_location)
        program_record(program)->mask_unit = value;
    glUniform1i(location, value);
}
static void uniform_1iv(GLint location, GLsizei count, const GLint *values) {
    if (program && location >= 0 && count > 0 && values &&
        location == program_locations(program)->mask_location)
        program_record(program)->mask_unit = values[0];
    glUniform1iv(location, count, values);
}
static void link_program(GLuint id) {
    wb_cached_link_program(id);
    program_record(id)->mask_unit = 0;
    program_record(id)->locations_ready = 0;
}
static void delete_program(GLuint id) {
    glDeleteProgram(id);
    if (!id) return; /* GL treats deletion of zero as a no-op. */
    for (size_t i = 0; i < 256; ++i)
        if (programs[i].id == id) {
            if (program == id) programs[i].deleted = 1;
            else memset(&programs[i], 0, sizeof(programs[i]));
        }
    /* OpenGL retains a deleted program while it remains current. Keep the
     * record if deletion happens before the caller unbinds that program. */
}
static void before_draw(void) {
    if (!program) return;
    program_info *p=program_locations(program);
    GLint uniform = p->size_location;
    if (uniform < 0) return;
    GLint unit = p->mask_unit;
    if (unit < 0 || unit >= UNITS) fatal("invalid mask sampler unit");
    texture_size *t = find(bound[unit], 0);
    if (!t || !t->width || !t->height) fatal("mask texture dimensions unavailable");
    glUniform2f(uniform, 1.0f / (float)t->width, 1.0f / (float)t->height);
}
static void submitted_draw(void) {
    uint32_t scene=wb_gpu_scene();
    wb_buffer_draw_submitted(scene);
    /* Mark all bound images conservatively; shader sampler liveness is not
     * assumed. Native submission may have opened a new framebuffer scene. */
    for(unsigned i=0;i<UNITS;++i) {
        texture_size *t=find(bound[i],0);
        if(t) t->scene=scene;
    }
}
static void draw_arrays(GLenum mode, GLint first, GLsizei count) {
    ++draw_calls;
    uint64_t begin=draw_clock ? draw_clock() : 0;
    before_draw(); wb_gl_state_begin();wb_buffer_draw_arrays(first,count);
    uint64_t ready=draw_clock ? draw_clock() : 0;
    wb_vertex_draw_arrays(mode, first, count);
    submitted_draw();
    wb_gl_state_end();
    if(draw_clock) { setup_us+=ready-begin;submit_us+=draw_clock()-ready; }
}
static void draw_elements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    ++draw_calls;
    uint64_t begin=draw_clock ? draw_clock() : 0;
    before_draw(); wb_gl_state_begin();wb_buffer_draw_elements(count,type,indices);
    uint64_t ready=draw_clock ? draw_clock() : 0;
    wb_vertex_draw_elements(mode, count, type, indices);
    submitted_draw();
    wb_gl_state_end();
    if(draw_clock) { setup_us+=ready-begin;submit_us+=draw_clock()-ready; }
}
static void draw_arrays_instanced(GLenum mode,GLint first,GLsizei count,GLsizei instances) {
    ++draw_calls;
    uint64_t begin=draw_clock ? draw_clock() : 0;
    before_draw();wb_gl_state_begin();wb_buffer_draw_conservative(0);
    uint64_t ready=draw_clock ? draw_clock() : 0;
    /* Preserve native instance/divisor semantics; no packed-offset rewrite
     * is assumed for this separate draw path. */
    glDrawArraysInstanced(mode,first,count,instances);
    submitted_draw();wb_gl_state_end();
    if(draw_clock) { setup_us+=ready-begin;submit_us+=draw_clock()-ready; }
}
static void draw_elements_instanced(GLenum mode,GLsizei count,GLenum type,const void *indices,GLsizei instances) {
    ++draw_calls;
    uint64_t begin=draw_clock ? draw_clock() : 0;
    before_draw();wb_gl_state_begin();wb_buffer_draw_conservative(1);
    uint64_t ready=draw_clock ? draw_clock() : 0;
    glDrawElementsInstanced(mode,count,type,indices,instances);
    submitted_draw();wb_gl_state_end();
    if(draw_clock) { setup_us+=ready-begin;submit_us+=draw_clock()-ready; }
}
void *wb_gl_get_proc_address(const char *name) {
    if (!name) return NULL;
    /* The pinned native resolver strips these suffixes. Apply the same
     * rule before our handlers so aliases cannot bypass lifetime/shadows. */
    char canonical[128];size_t length=strlen(name);
    if(length>3 && length<sizeof(canonical) &&
       (!strcmp(name+length-3,"EXT") || !strcmp(name+length-3,"ARB") || !strcmp(name+length-3,"OES"))) {
        memcpy(canonical,name,length-3);canonical[length-3]=0;name=canonical;
    }
#define HOOK(n, fn) if (!strcmp(name, n)) return (void *)(uintptr_t)fn
    HOOK("glActiveTexture", active_texture);
    HOOK("glBindTexture", bind_texture);
    HOOK("glTexImage2D", image_2d);
    HOOK("glTexSubImage2D", subimage_2d);
    HOOK("glClear", clear);
    HOOK("glFramebufferTexture2D", framebuffer_texture);
    HOOK("glCompressedTexImage2D", compressed_2d);
    HOOK("glDeleteTextures", delete_textures);
    HOOK("glUseProgram", use_program);
    HOOK("glUniform1i", uniform_1i);
    HOOK("glUniform1iv", uniform_1iv);
    HOOK("glLinkProgram", link_program);
    HOOK("glDeleteProgram", delete_program);
    HOOK("glDrawArrays", draw_arrays);
    HOOK("glDrawElements", draw_elements);
    HOOK("glDrawArraysInstanced", draw_arrays_instanced);
    HOOK("glDrawElementsInstanced", draw_elements_instanced);
#undef HOOK
    extern void *wb_gl_extra_proc(const char *);
    extern void *wb_gl_buffer_proc(const char *);
    void *p=wb_shader_cache_proc(name);
    if(!p) p=wb_gl_buffer_proc(name);
    if(!p) p=wb_gl_extra_proc(name);
    if(!p) p=(void *)vglGetProcAddress(name);
    if(!p) fprintf(stderr,"GL capability unavailable: %s\n",name);
    return p;
}
size_t wb_gl_texture_bytes(void) { return texture_bytes; }
size_t wb_gl_texture_peak(void) { return texture_peak; }
int wb_gl_texture_storage(GLuint id,size_t *bytes) {
    texture_size *t=find(id,0);
    if(!t || !t->width || !t->height) return 0;
    *bytes=0;
    for(unsigned i=0;i<16;++i) *bytes+=t->levels[i];
    return 1;
}
