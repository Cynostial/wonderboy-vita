/* VitaGL's packed VBO path stores offsets in uint16_t GXM attributes.
 * Move the common whole-vertex offset into first/baseVertex for large slices.
 * Keep all GL state and the engine's indices unchanged after each draw. */
#include <vitaGL.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include "wb_gl_state.h"
static const wb_gl_attribute *saved;
static GLint array_buffer;
static GLint rebase(void) {
    uintptr_t low=UINTPTR_MAX,high=0;GLint buffer=0,stride=0;
    saved=wb_gl_state_attributes();
    for(unsigned i=0;i<16;++i) {
        const wb_gl_attribute *a=&saved[i];
        if(!a->enabled) continue;
        if(!a->buffer || a->stride<=0) return 0;
        if(buffer && (buffer!=a->buffer || stride!=a->stride)) return 0;
        buffer=a->buffer;stride=a->stride;
        uintptr_t offset=(uintptr_t)a->pointer;
        if(offset<low) low=offset;
        if(offset>high) high=offset;
    }
    if(!buffer || high<=65535 || high-low>=(uintptr_t)stride) return 0;
    uintptr_t vertices=low/(unsigned)stride,base=vertices*(unsigned)stride;
    if(!vertices || vertices>INT_MAX || high-base>65535) return 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array_buffer);
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)buffer);
    for(unsigned i=0;i<16;++i) {
        const wb_gl_attribute *a=&saved[i];if(!a->enabled) continue;
        glVertexAttribPointer(i,a->size,(GLenum)a->type,(GLboolean)a->normalized,a->stride,
                              (const void *)((uintptr_t)a->pointer-base));
    }
    static unsigned reported;
    if(reported++<4) fprintf(stderr,"Rebased packed vertex slice: offset=%u stride=%d baseVertex=%u\n",
                              (unsigned)low,stride,(unsigned)vertices);
    return (GLint)vertices;
}
static void restore(void) {
    for(unsigned i=0;i<16;++i) {
        const wb_gl_attribute *a=&saved[i];if(!a->enabled) continue;
        glVertexAttribPointer(i,a->size,(GLenum)a->type,(GLboolean)a->normalized,a->stride,a->pointer);
    }
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)array_buffer);
}
void wb_vertex_draw_elements(GLenum mode,GLsizei count,GLenum type,const void *indices) {
    GLint base=count>0 ? rebase() : 0;
    if(base) { glDrawElementsBaseVertex(mode,count,type,indices,base);restore(); }
    else glDrawElements(mode,count,type,indices);
}
void wb_vertex_draw_arrays(GLenum mode,GLint first,GLsizei count) {
    GLint base=first>=0 && count>0 ? rebase() : 0;
    if(base) {
        if(first<=INT_MAX-base) glDrawArrays(mode,first+base,count);
        else { restore();glDrawArrays(mode,first,count);return; }
        restore();
    } else glDrawArrays(mode,first,count);
}
