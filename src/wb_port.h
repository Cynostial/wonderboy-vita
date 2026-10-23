#ifndef WB_PORT_H
#define WB_PORT_H
#include "so_util.h"
#include <stddef.h>
void *wb_gl_get_proc_address(const char *name);
const so_default_dynlib *wb_sdl_bindings(size_t *count);
const so_default_dynlib *wb_runtime_bindings(size_t *count);
int wb_sdl_install_allocator(void);
int wb_install_file_hooks(so_module *module);
int wb_install_platform_hooks(so_module *module);
int wb_relocate(so_module *,const so_default_dynlib *,size_t);
/* Returns the unresolved count. Constructors MUST NOT run unless zero. */
size_t wb_check_imports(const so_module *module,
                       const so_default_dynlib *bindings, size_t count,
                       void (*report_missing)(const char *name));
#endif
