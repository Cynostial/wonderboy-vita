#pragma once
#include <stdint.h>
typedef void *(*wb_icon_loader)(void *,const char *,uint64_t,unsigned);
void wb_controls_bind(void *,wb_icon_loader);
const char *wb_control_icon_path(int);
void *wb_control_icon(void *,int,int,unsigned);
