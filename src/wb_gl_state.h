#ifndef WB_GL_STATE_H
#define WB_GL_STATE_H
#include <vitaGL.h>
typedef struct {
    GLint enabled,buffer,stride,size,type,normalized;
    void *pointer;
} wb_gl_attribute;
/* A snapshot exists only inside a single, synchronous game GL draw wrapper.
 * Outside that scope reads go directly to VitaGL, including native overlays.
 * No GL mutation may occur between begin and the two consumers except the
 * vertex rebase, which deliberately restores from the original snapshot. */
void wb_gl_state_begin(void);
void wb_gl_state_end(void);
const wb_gl_attribute *wb_gl_state_attributes(void);
#endif
