#ifndef WB_FILE_LOAD_H
#define WB_FILE_LOAD_H
#include "wb_stdio.h"
void *wb_file_load_open(void *,const uint32_t *,uint32_t *,int32_t,wb_file *);
void *wb_file_load_named(void *,const char *,uint32_t *);
#endif
