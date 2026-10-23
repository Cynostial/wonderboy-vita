#pragma once
#include <stdint.h>
typedef void (*wb_rect)(int,int,int,int,uint32_t);
void wb_splash_art(wb_rect,unsigned);
