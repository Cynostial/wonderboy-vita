#ifndef WB_UTF8_H
#define WB_UTF8_H
#include <stdint.h>
#include <stddef.h>
/* Bionic ARM32 has four bytes (__seq[4]); only LP64 reserves four more.
 * Keep partial code point (21 bits), remaining count and total in one word. */
typedef struct { uint32_t packed; } wb_mbstate;
_Static_assert(sizeof(wb_mbstate)==4,"Bionic ARM32 mbstate_t");
size_t wb_mbrtowc(uint32_t *,const char *,size_t,wb_mbstate *);
size_t wb_mbrlen(const char *,size_t,wb_mbstate *);
size_t wb_wcrtomb(char *,uint32_t,wb_mbstate *);
size_t wb_mbsnrtowcs(uint32_t *,const char **,size_t,size_t,wb_mbstate *);
size_t wb_mbsrtowcs(uint32_t *,const char **,size_t,wb_mbstate *);
size_t wb_wcsnrtombs(char *,const uint32_t **,size_t,size_t,wb_mbstate *);
size_t wb_wcstombs(char *,const uint32_t *,size_t);
size_t wb_mbstowcs(uint32_t *,const char *,size_t);
int wb_mbtowc(uint32_t *,const char *,size_t);
#endif
