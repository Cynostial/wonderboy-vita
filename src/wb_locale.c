#define _GNU_SOURCE
#include "wb_platform.h"
#include "wb_utf8.h"
#include "wb_errno.h"
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
static const int categories[]={LC_CTYPE,LC_NUMERIC,LC_TIME,LC_COLLATE,LC_MONETARY,LC_MESSAGES,LC_ALL};
static char *b_setlocale(int category,const char *name) {
    if(category<0 || category>6) { errno=22; return NULL; }
    char *result=setlocale(categories[category],name);
    if(!result) wb_errno_publish();
    return result;
}
static locale_t b_newlocale(int mask,const char *name,locale_t base) {
    if((unsigned)mask&~63u) { errno=22; return NULL; }
    int native=0;
    for(unsigned i=0;i<6;++i) if(mask&(1<<i)) native|=1<<categories[i];
    locale_t l=newlocale(native,name,base);
    if(!l) wb_errno_publish();
    return l;
}
/* Only opaque handles cross the boundary; bionic masks/categories never
 * reach newlib directly. Unicode state is handled separately in wb_utf8. */
static long double b_strtold_l(const char *s,char **end,locale_t l) {
    int saved=errno; errno=0;
    long double v=strtold_l(s,end,l); if(errno) wb_errno_publish(); else errno=saved; return v;
}
static long long b_strtoll_l(const char *s,char **end,int base,locale_t l) {
    int saved=errno; errno=0;
    long long v=strtoll_l(s,end,base,l); if(errno) wb_errno_publish(); else errno=saved; return v;
}
static unsigned long long b_strtoull_l(const char *s,char **end,int base,locale_t l) {
    int saved=errno; errno=0;
    unsigned long long v=strtoull_l(s,end,base,l); if(errno) wb_errno_publish(); else errno=saved; return v;
}
static uint32_t b_btowc(int c) { return c>=0 && c<128 ? (uint32_t)c : UINT32_MAX; }
static int b_wctob(uint32_t c) { return c<128 ? (int)c : EOF; }
typedef struct {
    char *decimal_point,*thousands_sep,*grouping,*int_curr_symbol,*currency_symbol;
    char *mon_decimal_point,*mon_thousands_sep,*mon_grouping,*positive_sign,*negative_sign;
    char int_frac_digits,frac_digits,p_cs_precedes,p_sep_by_space,n_cs_precedes,n_sep_by_space,p_sign_posn,n_sign_posn;
    char int_p_cs_precedes,int_p_sep_by_space,int_n_cs_precedes,int_n_sep_by_space,int_p_sign_posn,int_n_sign_posn;
} b_lconv;
static _Thread_local b_lconv converted;
static void *b_localeconv(void) {
    struct lconv *s=localeconv();
#define COPY(n) converted.n=s->n
    COPY(decimal_point);COPY(thousands_sep);COPY(grouping);COPY(int_curr_symbol);COPY(currency_symbol);
    COPY(mon_decimal_point);COPY(mon_thousands_sep);COPY(mon_grouping);COPY(positive_sign);COPY(negative_sign);
    COPY(int_frac_digits);COPY(frac_digits);COPY(p_cs_precedes);COPY(p_sep_by_space);COPY(n_cs_precedes);
    COPY(n_sep_by_space);COPY(p_sign_posn);COPY(n_sign_posn);COPY(int_p_cs_precedes);COPY(int_p_sep_by_space);
    COPY(int_n_cs_precedes);COPY(int_n_sep_by_space);COPY(int_p_sign_posn);COPY(int_n_sign_posn);
#undef COPY
    return &converted;
}
static int native_error(int code) {
    switch(code) {
    case 35:return EDEADLK; case 36:return ENAMETOOLONG; case 38:return ENOSYS;
    case 39:return ENOTEMPTY; case 75:return EOVERFLOW; case 84:return EILSEQ;
    case 95:return ENOTSUP; case 110:return ETIMEDOUT;
    default:return code;
    }
}
static char *b_strerror(int code) { return strerror(native_error(code)); }
static int b_strerror_r(int code,char *out,size_t size) {
    const char *text=b_strerror(code); size_t len=strlen(text);
    if(!out || !size) return 34;
    size_t n=len<size ? len : size-1; memcpy(out,text,n); out[n]=0;
    return len<size ? 0 : 34;
}
static const so_default_dynlib bindings[]={
#define MAP(name,fn) {name,(uintptr_t)fn}
#define DIRECT(fn) MAP(#fn,fn)
 MAP("setlocale",b_setlocale),MAP("newlocale",b_newlocale),DIRECT(freelocale),DIRECT(uselocale),
 MAP("localeconv",b_localeconv),MAP("strtold_l",b_strtold_l),
 MAP("strtoll_l",b_strtoll_l),MAP("strtoull_l",b_strtoull_l),
 MAP("btowc",b_btowc),MAP("wctob",b_wctob),
 MAP("mbrlen",wb_mbrlen),MAP("mbrtowc",wb_mbrtowc),MAP("mbsnrtowcs",wb_mbsnrtowcs),
 MAP("mbsrtowcs",wb_mbsrtowcs),MAP("mbstowcs",wb_mbstowcs),MAP("mbtowc",wb_mbtowc),
 MAP("wcrtomb",wb_wcrtomb),MAP("wcsnrtombs",wb_wcsnrtombs),MAP("wcstombs",wb_wcstombs),
 MAP("strerror",b_strerror),MAP("strerror_r",b_strerror_r),
 DIRECT(strcoll),DIRECT(strxfrm),DIRECT(wcscoll),DIRECT(wcsxfrm),DIRECT(swprintf),
#undef MAP
#undef DIRECT
};
const so_default_dynlib *wb_locale_bindings(size_t *count) {
    if(count) *count=sizeof(bindings)/sizeof(*bindings);
    return bindings;
}
