#define _GNU_SOURCE
#include "wb_stdio.h"
#include "wb_paths.h"
#include "wb_streams.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
wb_file wb_stdio_handles[128];
static struct { FILE *native; int used; } files[128];
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int ready;
static int index_of(wb_file *file) {
    uintptr_t at=(uintptr_t)file,start=(uintptr_t)wb_stdio_handles;
    if(at<start || at-start>=sizeof(wb_stdio_handles) || (at-start)%sizeof(wb_file)) return -1;
    return (int)((at-start)/sizeof(wb_file));
}
static FILE *lookup(wb_file *file) {
    int index=index_of(file); FILE *native=NULL;
    pthread_mutex_lock(&lock);
    if(index>=0 && files[index].used) native=files[index].native;
    pthread_mutex_unlock(&lock);
    if(!native) errno=EBADF;
    return native;
}
static void sync_flags(wb_file *file,FILE *native) {
    uint16_t flags=file->flags & ~(0x20u|0x40u);
    if(feof(native)) flags|=0x20;
    if(ferror(native)) flags|=0x40;
    __atomic_store_n(&file->flags,flags,__ATOMIC_RELAXED);
    file->read_count=file->write_count=0;
}
int wb_stdio_init(void) { 
    pthread_mutex_lock(&lock);
    if(ready) { pthread_mutex_unlock(&lock); return EBUSY; }
    FILE *standard[]={stdin,stdout,stderr};
    for(int i=0;i<3;++i) {
        files[i].native=standard[i]; files[i].used=1;
        wb_stdio_handles[i].flags=i ? 0x0a : 0x06;
        wb_stdio_handles[i].descriptor=(int16_t)fileno(standard[i]);
    }
    ready=1; pthread_mutex_unlock(&lock); return 0;
}
wb_file *wb_stdio_adopt(FILE *native,const char *mode) {
    if(!native) return NULL;
    int fd=fileno(native);
    if(fd>INT16_MAX) { wb_stream_close(native); errno=EOVERFLOW; return NULL; }
    pthread_mutex_lock(&lock);
    int slot=-1;
    if(ready) for(int i=3;i<128;++i) if(!files[i].used) { slot=i; break; }
    if(slot>=0) {
        files[slot].used=1; files[slot].native=native;
        wb_file *f=&wb_stdio_handles[slot]; memset(f,0,sizeof(*f));
        f->flags=0x02 | (strchr(mode,'+') ? 0x10 : mode[0]=='r' ? 0x04 : 0x08);
        f->descriptor=(int16_t)fd;
    }
    pthread_mutex_unlock(&lock);
    if(slot<0) { wb_stream_close(native); errno=EMFILE; return NULL; }
    return &wb_stdio_handles[slot];
}
static int mode_string(const char *input,char out[5]) {
    if(!input || !*input || !strchr("rwa",*input)) return EINVAL;
    int plus=0,binary=0;
    for(const char *p=input+1;*p;++p) {
        if(*p=='+' && !plus) plus=1;
        else if(*p=='b' && !binary) binary=1;
        else return EINVAL;
    }
    out[0]=input[0]; out[1]='b'; out[2]=plus ? '+' : 0; out[3]=0; return 0;
}
wb_file *wb_fopen(const char *path,const char *mode) { 
    char native_mode[5],resolved[WB_PATH_MAX]; int e=mode_string(mode,native_mode);
    if(!e) e=wb_path_resolve(path,resolved,sizeof(resolved));
    if(e) { errno=e; return NULL; }
    FILE *native=fopen(resolved,native_mode);
    /* Game resource loads use this path, separately from the installer and
     * AndroidFile::openBuffered. Vita newlib otherwise refills just 1 KiB at
     * a time even when the guest requests the entire texture in one fread.
     * Keep writes/update streams and fdopen descriptor semantics unchanged. */
    if(!strcmp(native_mode,"rb")) wb_stream_buffer_read(native);
    return wb_stdio_adopt(native,native_mode);
}
wb_file *wb_fdopen(int fd,const char *mode) { 
    char native_mode[5]; int e=mode_string(mode,native_mode);
    if(e) { errno=e; return NULL; }
    if(fd>INT16_MAX) { errno=fd<0 ? EBADF : EOVERFLOW; return NULL; }
    /* Reserve the wrapper before native fdopen. Success must retain exactly
     * the supplied fd; failure must leave it open and owned by the caller. */
    pthread_mutex_lock(&lock); int slot=-1;
    if(ready) for(int i=3;i<128;++i) if(!files[i].used) { slot=i; files[i].used=1; break; }
    pthread_mutex_unlock(&lock);
    if(slot<0) { errno=EMFILE; return NULL; }
    FILE *native=fdopen(fd,native_mode);
    if(!native) {
        int saved=errno; pthread_mutex_lock(&lock); files[slot].used=0; pthread_mutex_unlock(&lock);
        errno=saved; return NULL;
    }
    /* If native buffering cannot be disabled, preserve the valid stream and
     * its descriptor. Native metadata/buffer overhead remains in headroom. */
    (void)setvbuf(native,NULL,_IONBF,0);
    wb_file *f=&wb_stdio_handles[slot]; memset(f,0,sizeof(*f));
    f->flags=0x02 | (strchr(mode,'+') ? 0x10 : mode[0]=='r' ? 0x04 : 0x08);
    f->descriptor=(int16_t)fd;
    pthread_mutex_lock(&lock); files[slot].native=native; pthread_mutex_unlock(&lock);
    return f;
}
int wb_fclose(wb_file *file) { 
    FILE *native=lookup(file); if(!native) return EOF;
    int result=wb_stream_close(native),saved=errno,index=index_of(file);
    /* fclose consumes the stream even when flushing fails. */
    pthread_mutex_lock(&lock); files[index].native=NULL; files[index].used=0;
    memset(file,0,sizeof(*file)); file->descriptor=-1; pthread_mutex_unlock(&lock);
    errno=saved; return result;
}
size_t wb_fread(void *out,size_t size,size_t count,wb_file *file) { 
    if(size && count>SIZE_MAX/size) { errno=EOVERFLOW; return 0; }
    FILE *native=lookup(file); if(!native) return 0;
    size_t result=fread(out,size,count,native); sync_flags(file,native); return result;
}
size_t wb_fwrite(const void *data,size_t size,size_t count,wb_file *file) { 
    if(size && count>SIZE_MAX/size) { errno=EOVERFLOW; return 0; }
    FILE *native=lookup(file); if(!native) return 0;
    size_t result=fwrite(data,size,count,native); sync_flags(file,native); return result;
}
int wb_fseek(wb_file *file,int32_t offset,int origin) { 
    FILE *native=lookup(file); if(!native) return -1;
    int result=fseek(native,offset,origin); sync_flags(file,native); return result;
}
int32_t wb_ftell(wb_file *file) { 
    FILE *native=lookup(file); if(!native) return -1;
    long result=ftell(native);
    if(result>INT32_MAX) { errno=EOVERFLOW; return -1; }
    return (int32_t)result;
}
int wb_fflush(wb_file *file) { 
    if(!file) return fflush(NULL);
    FILE *native=lookup(file); if(!native) return EOF;
    int result=fflush(native); sync_flags(file,native); return result;
}
int wb_fgetc(wb_file *file) { 
    FILE *native=lookup(file); if(!native) return EOF;
    int result=fgetc(native); sync_flags(file,native); return result;
}
int wb_fputc(int value,wb_file *file) { 
    FILE *native=lookup(file); if(!native) return EOF;
    int result=fputc(value,native); sync_flags(file,native); return result;
}
int wb_fputs(const char *text,wb_file *file) { 
    FILE *native=lookup(file); if(!native) return EOF;
    int result=fputs(text,native); sync_flags(file,native); return result;
}
char *wb_fgets(char *out,int size,wb_file *file) { 
    FILE *native=lookup(file); if(!native) return NULL;
    char *result=fgets(out,size,native); sync_flags(file,native); return result;
}
int wb_ungetc(int value,wb_file *file) { 
    FILE *native=lookup(file); if(!native) return EOF;
    int result=ungetc(value,native); sync_flags(file,native); return result;
}
void wb_clearerr(wb_file *file) {  FILE *native=lookup(file); if(native) { clearerr(native); sync_flags(file,native); } }
int wb_setvbuf(wb_file *file,char *buffer,int mode,size_t size) { 
    FILE *native=lookup(file); if(!native) return -1;
    int modes[]={_IOFBF,_IOLBF,_IONBF};
    if(mode<0 || mode>2) { errno=EINVAL; return -1; }
    if(mode!=2 && (!buffer || size>65536)) { errno=ENOTSUP; return -1; }
    /* Android FILE never exposes the native buffer to inline stdio access. */
    return setvbuf(native,buffer,modes[mode],size);
}
int wb_vfprintf(wb_file *file,const char *format,va_list args) { 
    FILE *native=lookup(file); if(!native) return -1;
    int result=vfprintf(native,format,args); sync_flags(file,native); return result;
}
int wb_vfscanf(wb_file *file,const char *format,va_list args) { 
    FILE *native=lookup(file); if(!native) return EOF;
    int result=vfscanf(native,format,args); sync_flags(file,native); return result;
}
int wb_fprintf(wb_file *file,const char *format,...) { 
    va_list args; va_start(args,format); int result=wb_vfprintf(file,format,args); va_end(args); return result;
}
int wb_fscanf(wb_file *file,const char *format,...) { 
    va_list args; va_start(args,format); int result=wb_vfscanf(file,format,args); va_end(args); return result;
}
int wb_printf(const char *format,...) { 
    va_list args; va_start(args,format); int result=wb_vfprintf(&wb_stdio_handles[1],format,args); va_end(args); return result;
}
int wb_puts(const char *text) { 
    if(wb_fputs(text,&wb_stdio_handles[1])<0) return EOF;
    return wb_fputc('\n',&wb_stdio_handles[1]);
}
int wb_feof(wb_file *f) { FILE *n=lookup(f); return n ? feof(n) : 0; }
int wb_ferror(wb_file *f) { FILE *n=lookup(f); return n ? ferror(n) : 1; }
void wb_rewind(wb_file *f) { FILE *n=lookup(f); if(n) { rewind(n); sync_flags(f,n); } }
wb_file *wb_freopen(const char *path,const char *mode,wb_file *f) {
    char resolved[WB_PATH_MAX],native_mode[5]; int e=wb_path_resolve(path,resolved,sizeof(resolved));
    if(!e) e=mode_string(mode,native_mode);
    if(e) { errno=e; return NULL; }
    FILE *n=lookup(f); if(!n) return NULL;
    int i=index_of(f);
    n=wb_stream_reopen(resolved,native_mode,n);
    pthread_mutex_lock(&lock);
    files[i].native=n; files[i].used=n!=NULL;
    memset(f,0,sizeof(*f));
    if(n) { f->descriptor=(int16_t)fileno(n); f->flags=strchr(mode,'+') ? 0x10 : mode[0]=='r' ? 4 : 8; }
    pthread_mutex_unlock(&lock);
    return n ? f : NULL;
}
wb_file *wb_tmpfile(void) {
    /* Native tmpfile may target a nonexistent /tmp on Vita. Use the private
     * save directory and unlink after opening, where the filesystem permits. */
    char name[WB_PATH_MAX];
    snprintf(name,sizeof(name),WB_SAVES "/tmp-XXXXXX");
    int fd=mkstemp(name);
    if(fd<0) return NULL;
    FILE *native=fdopen(fd,"w+b");
    if(!native) { close(fd); remove(name); return NULL; }
    if(remove(name)) { fclose(native); return NULL; }
    return wb_stdio_adopt(native,"w+b");
}
#ifndef _WIN32
wb_file *wb_funopen(const void *cookie,int (*r)(void *,char *,int),
                   int (*w)(void *,const char *,int),fpos_t (*s)(void *,fpos_t,int),int (*c)(void *)) {
    return wb_stdio_adopt(funopen(cookie,r,w,s,c),w ? "w+b" : "rb");
}
#endif
