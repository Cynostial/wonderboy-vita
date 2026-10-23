/* The hazard tracker and packed-offset adapter inspect the same attributes
 * back-to-back. Query each requested property once per draw rather than twice.
 * A fresh scope discards everything, so native rendering between guest draws
 * cannot leave stale attribute state behind. GL remains render-thread only. */
#include "wb_gl_state.h"
static wb_gl_attribute attributes[16];
static unsigned depth,valid;
void wb_gl_state_begin(void) {
    /* A nested scope cannot share its parent's snapshot. Disable caching for
     * both until the outer call ends rather than returning ambiguous state. */
    ++depth;valid=0;
}
void wb_gl_state_end(void) {
    if(depth) --depth;
    /* Also invalidates an outer scope after a nested call restored its GL. */
    valid=0;
}
const wb_gl_attribute *wb_gl_state_attributes(void) {
    if(depth==1 && valid) return attributes;
    for(GLuint i=0;i<16;++i) {
        wb_gl_attribute *a=&attributes[i];
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&a->enabled);
        if(!a->enabled) continue;
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&a->buffer);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&a->stride);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_SIZE,&a->size);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_TYPE,&a->type);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&a->normalized);
        glGetVertexAttribPointerv(i,GL_VERTEX_ATTRIB_ARRAY_POINTER,&a->pointer);
    }
    valid=1;return attributes;
}
