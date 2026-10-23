#ifndef WB_SHADER_CACHE_H
#define WB_SHADER_CACHE_H
#include <stddef.h>
void *wb_shader_cache_proc(const char *);
void wb_cached_link_program(unsigned);
void wb_shader_cache_directory(const char *);
int wb_shader_binary_valid(const unsigned char *,size_t);
#endif
