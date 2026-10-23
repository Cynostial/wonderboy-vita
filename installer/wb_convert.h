#pragma once
#include <stddef.h>
#include <stdio.h>
/* Bounded, CPU-only conversion. All source data comes from the user's files. */
int wb_convert_ktx(const unsigned char *,size_t,FILE *);
int wb_convert_png(const unsigned char *,size_t,FILE *,int loading_screen);
int wb_convert_path(int,const char *,const char *);
