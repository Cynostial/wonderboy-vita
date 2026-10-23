/* Scalar C/ARM EABI imports with compatible 32-bit softfp calling conventions.
 * Deliberately exclude FILE, stat, dirent, pthread, locale_t, mbstate_t and
 * jmp_buf until their Android layouts/ownership have proper adapters.
 * The remaining import audit must pass before any .init_array or SDL_main. */
#define _GNU_SOURCE
#include "wb_port.h"
#include "wb_memory.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <wctype.h>

/* References, not C calls: these libgcc routines have ARM EABI signatures. */
#define EABI_DECL(n) extern void n(void)
EABI_DECL(__aeabi_memcpy); EABI_DECL(__aeabi_memcpy4); EABI_DECL(__aeabi_memcpy8);
EABI_DECL(__aeabi_memmove); EABI_DECL(__aeabi_memmove4); EABI_DECL(__aeabi_memmove8);
EABI_DECL(__aeabi_memclr); EABI_DECL(__aeabi_memclr4); EABI_DECL(__aeabi_memclr8);
EABI_DECL(__aeabi_memset); EABI_DECL(__aeabi_memset4); EABI_DECL(__aeabi_memset8);
EABI_DECL(__aeabi_idiv); EABI_DECL(__aeabi_idivmod);
EABI_DECL(__aeabi_uidiv); EABI_DECL(__aeabi_uidivmod);
EABI_DECL(__aeabi_ldivmod); EABI_DECL(__aeabi_uldivmod);
EABI_DECL(__aeabi_ui2d); EABI_DECL(__aeabi_ul2d); EABI_DECL(__aeabi_ul2f);
EABI_DECL(__aeabi_l2d); EABI_DECL(__aeabi_l2f);
EABI_DECL(__aeabi_f2d); EABI_DECL(__aeabi_dsub);
#undef EABI_DECL

static const so_default_dynlib bindings[] = {
#define BIND(n) {#n, (uintptr_t)n}
    {"malloc", (uintptr_t)wb_malloc}, {"free", (uintptr_t)wb_free},
    {"calloc", (uintptr_t)wb_calloc}, {"realloc", (uintptr_t)wb_realloc},
    {"posix_memalign", (uintptr_t)wb_posix_memalign}, {"strdup", (uintptr_t)wb_strdup},
    BIND(__aeabi_memcpy), BIND(__aeabi_memcpy4), BIND(__aeabi_memcpy8),
    BIND(__aeabi_memmove), BIND(__aeabi_memmove4), BIND(__aeabi_memmove8),
    BIND(__aeabi_memclr), BIND(__aeabi_memclr4), BIND(__aeabi_memclr8),
    BIND(__aeabi_memset), BIND(__aeabi_memset4), BIND(__aeabi_memset8),
    BIND(__aeabi_idiv), BIND(__aeabi_idivmod),
    BIND(__aeabi_uidiv), BIND(__aeabi_uidivmod),
    BIND(__aeabi_ldivmod), BIND(__aeabi_uldivmod),
    BIND(__aeabi_ui2d), BIND(__aeabi_ul2d), BIND(__aeabi_ul2f),
    BIND(__aeabi_l2d), BIND(__aeabi_l2f), BIND(__aeabi_f2d), BIND(__aeabi_dsub),
    BIND(memcpy), BIND(memset), BIND(memcmp), BIND(memchr),
    BIND(strlen), BIND(strcmp), BIND(strncmp), BIND(strcasecmp), BIND(strncasecmp),
    BIND(strchr), BIND(strrchr), BIND(strstr), BIND(strcasestr),
    BIND(strcpy), BIND(strncpy), BIND(strcat), BIND(strncat),
    BIND(strspn), BIND(strpbrk),
    BIND(atoi), BIND(atof), BIND(strtol), BIND(strtoul), BIND(strtoll), BIND(strtoull),
    BIND(strtod), BIND(strtof),
    BIND(sprintf), BIND(snprintf), BIND(vsnprintf), BIND(sscanf), BIND(vsscanf),
    BIND(qsort), BIND(rand), BIND(srand),
    BIND(isalnum), BIND(isalpha), BIND(isblank), BIND(iscntrl), BIND(isgraph),
    BIND(islower), BIND(isprint), BIND(ispunct), BIND(isspace), BIND(isupper),
    BIND(isxdigit), BIND(tolower), BIND(toupper),
    BIND(wcslen), BIND(wmemcpy), BIND(wmemmove), BIND(wmemcmp), BIND(wmemset), BIND(wmemchr),
    BIND(wcstod), BIND(wcstof), BIND(wcstol), BIND(wcstoll), BIND(wcstoul), BIND(wcstoull),
    BIND(iswalpha), BIND(iswcntrl), BIND(iswdigit), BIND(iswlower), BIND(iswprint),
    BIND(iswpunct), BIND(iswspace), BIND(iswupper), BIND(iswxdigit),
    BIND(towlower), BIND(towupper),
    BIND(acos), BIND(acosf), BIND(asin), BIND(asinf), BIND(atan), BIND(atan2), BIND(atan2f),
    BIND(cos), BIND(cosf), BIND(cosh), BIND(sin), BIND(sinf), BIND(sinh),
    BIND(tan), BIND(tanf), BIND(tanh),
    BIND(exp), BIND(exp2), BIND(log), BIND(logf), BIND(log10),
    BIND(pow), BIND(powf), BIND(sqrt), BIND(sqrtf),
    BIND(ceil), BIND(ceilf), BIND(floor), BIND(floorf),
    BIND(fmod), BIND(fmodf), BIND(roundf), BIND(rint), BIND(rintf),
    BIND(frexp), BIND(frexpf), BIND(ldexp), BIND(ldexpf), BIND(modf),
#undef BIND
};
const so_default_dynlib *wb_runtime_bindings(size_t *count) {
    if (count) *count = sizeof(bindings) / sizeof(bindings[0]);
    return bindings;
}
