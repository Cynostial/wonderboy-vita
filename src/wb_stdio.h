#ifndef WB_STDIO_H
#define WB_STDIO_H
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
/* Public Android 4.2 ARM FILE prefix, with no native pointers embedded.
 * Read/write counts stay zero, forcing any inline getc/putc down slow paths.
 * Fields are named differently to avoid native stdio implementation macros. */
typedef struct {
    uint32_t position;
    int32_t read_count,write_count;
    uint16_t flags;
    int16_t descriptor;
    uint32_t buffer; int32_t buffer_size,line_buffer_size;
    uint32_t cookie,close_fn,read_fn,seek_fn,write_fn;
    uint32_t extension; int32_t extension_size;
    uint32_t saved_position; int32_t saved_read;
    unsigned char unget_buffer[3],tiny_buffer[1];
    uint32_t line_buffer; int32_t line_size,block_size,offset;
} wb_file;
_Static_assert(sizeof(wb_file)==84,"Android ARM FILE layout");
_Static_assert(offsetof(wb_file,flags)==12,"Android FILE flags offset");
/* The first three entries have exactly the stride expected by __sF+1/+2. */
extern wb_file wb_stdio_handles[128];
int wb_stdio_init(void);
wb_file *wb_stdio_adopt(FILE *,const char *);
int wb_feof(wb_file *);
int wb_ferror(wb_file *);
void wb_rewind(wb_file *);
wb_file *wb_freopen(const char *,const char *,wb_file *);
wb_file *wb_tmpfile(void);
wb_file *wb_funopen(const void *,int (*)(void *,char *,int),
                   int (*)(void *,const char *,int),fpos_t (*)(void *,fpos_t,int),int (*)(void *));
wb_file *wb_fopen(const char *,const char *);
wb_file *wb_fdopen(int,const char *);
int wb_fclose(wb_file *);
size_t wb_fread(void *,size_t,size_t,wb_file *);
size_t wb_fwrite(const void *,size_t,size_t,wb_file *);
int wb_fseek(wb_file *,int32_t,int);
int32_t wb_ftell(wb_file *);
int wb_fflush(wb_file *);
int wb_fgetc(wb_file *);
int wb_fputc(int,wb_file *);
int wb_fputs(const char *,wb_file *);
char *wb_fgets(char *,int,wb_file *);
int wb_ungetc(int,wb_file *);
void wb_clearerr(wb_file *);
int wb_setvbuf(wb_file *,char *,int,size_t);
int wb_vfprintf(wb_file *,const char *,va_list);
int wb_vfscanf(wb_file *,const char *,va_list);
int wb_fprintf(wb_file *,const char *,...);
int wb_fscanf(wb_file *,const char *,...);
int wb_printf(const char *,...);
int wb_puts(const char *);
#endif
