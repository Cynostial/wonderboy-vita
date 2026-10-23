#define _GNU_SOURCE
#include "wb_platform.h"
#include "wb_paths.h"
#include "wb_memory.h"
#include "wb_stdio.h"
#include "wb_io.h"
#include "wb_time.h"
#include "wb_errno.h"
#include "wb_sem.h"
#include "wb_bionic_sync.h"
#include "wb_log_vita.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <time.h>
#include <sys/stat.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>

/* SDK setjmp only saves integer registers. Use our ARM adapter to also save
 * d8-d15/FPSCR for Android code using NEON/VFP. No POSIX signal API is imported
 * by this game, and the Vita host has no guest signal-mask state to restore. */
_Static_assert(sizeof(jmp_buf)<=256,"jump buffer must fit Android ARM storage");
extern int wb_guest_setjmp(void *) __attribute__((returns_twice));
extern _Noreturn void wb_guest_longjmp(void *,int);
_Static_assert(sizeof(pthread_t)==4 && sizeof(pthread_key_t)==4,"ARM thread/key IDs");
_Static_assert(sizeof(fpos_t)==4,"Android funopen seek ABI");
static so_module *game;
void wb_platform_set_module(so_module *m) { game=m; }
static int negative(int result) { if(result<0) wb_errno_publish(); return result; }
static int sem_result(int e) { if(e) { errno=wb_bionic_error(e); return -1; } return 0; }
static int b_sem_init(wb_sem *s,int shared,unsigned n) { return sem_result(wb_sem_init(s,shared,n)); }
static int b_sem_destroy(wb_sem *s) { return sem_result(wb_sem_destroy(s)); }
static int b_sem_wait(wb_sem *s) { return sem_result(wb_sem_wait(s)); }
static int b_sem_post(wb_sem *s) { return sem_result(wb_sem_post(s)); }
static int b_create(pthread_t *id,const uint32_t *a,void *(*entry)(void *),void *argument) {
    pthread_attr_t native;
    int e=pthread_attr_init(&native);
    if(e) return wb_bionic_error(e);
    /* Android's default is a 1 MiB stack. Vita's much smaller default is not
     * appropriate for the engine's decoder and worker call stacks. */
    size_t stack=a && a[2] ? a[2] : 1024*1024;
    if(a && (a[1] || a[4] || a[5] || (a[0]&~3u))) e=ENOTSUP;
    if(!e) e=pthread_attr_setstacksize(&native,stack);
    if(!e && a && (a[0]&1)) e=pthread_attr_setdetachstate(&native,PTHREAD_CREATE_DETACHED);
    if(!e) e=pthread_create(id,&native,entry,argument);
    pthread_attr_destroy(&native);
    return wb_bionic_error(e);
}
/* POSIX thread routines return error numbers; translate, don't publish errno. */
#define P1(name,type,target) static int b_##name(type a) { return wb_bionic_error(target(a)); }
#define P2(name,t1,t2,target) static int b_##name(t1 a,t2 b) { return wb_bionic_error(target(a,b)); }
typedef void (*key_destructor)(void *);
P2(pthread_join,pthread_t,void **,pthread_join)
P1(pthread_detach,pthread_t,pthread_detach)
P2(pthread_key_create,pthread_key_t *,key_destructor,pthread_key_create)
P1(pthread_key_delete,pthread_key_t,pthread_key_delete)
P2(pthread_setspecific,pthread_key_t,const void *,pthread_setspecific)
P1(pthread_mutexattr_init,int32_t *,wb_mutexattr_init)
P1(pthread_mutexattr_destroy,int32_t *,wb_mutexattr_destroy)
P2(pthread_mutexattr_settype,int32_t *,int,wb_mutexattr_settype)
P2(pthread_mutex_init,wb_bionic_mutex *,const int32_t *,wb_mutex_init)
P1(pthread_mutex_destroy,wb_bionic_mutex *,wb_mutex_destroy)
P1(pthread_mutex_lock,wb_bionic_mutex *,wb_mutex_lock)
P1(pthread_mutex_trylock,wb_bionic_mutex *,wb_mutex_trylock)
P1(pthread_mutex_unlock,wb_bionic_mutex *,wb_mutex_unlock)
P1(pthread_cond_destroy,wb_bionic_cond *,wb_cond_destroy)
P1(pthread_cond_signal,wb_bionic_cond *,wb_cond_signal)
P1(pthread_cond_broadcast,wb_bionic_cond *,wb_cond_broadcast)
P2(pthread_cond_wait,wb_bionic_cond *,wb_bionic_mutex *,wb_cond_wait)
static int b_pthread_cond_timedwait(wb_bionic_cond *c,wb_bionic_mutex *m,const wb_bionic_timespec *t) {
    return wb_bionic_error(wb_cond_timedwait(c,m,t));
}
static int b_pthread_once(volatile int32_t *c,void (*f)(void)) { return wb_bionic_error(wb_once(c,f)); }

static int b_open(const char *p,int flags,...) {
    unsigned mode=0;
    if(flags&0x40) { va_list args; va_start(args,flags); mode=va_arg(args,unsigned); va_end(args); }
    return negative(wb_io_open(p,flags,mode));
}
static int b_stat(const char *p,wb_android_stat *s) { return negative(wb_io_stat(p,s)); }
static int b_fstat(int fd,wb_android_stat *s) { return negative(wb_io_fstat(fd,s)); }
static int b_mkdir(const char *p,unsigned m) { return negative(wb_io_mkdir(p,m)); }
static int b_access(const char *p,int m) { return negative(wb_io_access(p,m)); }
static int b_rename(const char *a,const char *b) { return negative(wb_io_rename(a,b)); }
static int b_path_operation(const char *p,int (*fn)(const char *)) {
    char path[WB_PATH_MAX]; int e=wb_path_resolve(p,path,sizeof(path));
    if(e) { errno=wb_bionic_error(e); return -1; }
    return negative(fn(path));
}
static int b_remove(const char *p) { return b_path_operation(p,remove); }
static int b_rmdir(const char *p) { return b_path_operation(p,rmdir); }
static int b_close(int fd) { return negative(close(fd)); }
static int32_t b_read(int fd,void *p,size_t n) {
    if(n>INT32_MAX) { errno=22; return -1; }
    return negative(read(fd,p,n));
}
static int32_t b_sendfile(int out,int in,int32_t *offset,size_t count) {
    unsigned char buffer[16384];
    off_t saved=-1;
    if(offset) {
        saved=lseek(in,0,SEEK_CUR);
        if(saved<0 || lseek(in,*offset,SEEK_SET)<0) { wb_errno_publish(); return -1; }
    }
    size_t total=0; int failure=0;
    while(total<count && total<INT32_MAX) {
        size_t n=count-total; if(n>sizeof(buffer)) n=sizeof(buffer);
        ssize_t got=read(in,buffer,n);
        if(got<=0) { if(got<0) failure=errno; break; }
        size_t done=0;
        while(done<(size_t)got) {
            ssize_t put=write(out,buffer+done,(size_t)got-done);
            if(put<=0) { failure=put<0 ? errno : EIO; break; }
            done+=(size_t)put;
        }
        total+=done;
        if(done<(size_t)got) { (void)lseek(in,-((off_t)got-(off_t)done),SEEK_CUR); break; }
    }
    if(offset) { *offset+=(int32_t)total; (void)lseek(in,saved,SEEK_SET); }
    if(failure && !total) { errno=wb_bionic_error(failure); return -1; }
    return (int32_t)total;
}
typedef struct {
    uint64_t inode; int64_t offset; uint16_t length; unsigned char type; char name[256]; unsigned char padding[5];
} __attribute__((packed)) b_dirent;
_Static_assert(offsetof(b_dirent,name)==19 && sizeof(b_dirent)==280,"Android dirent ABI");
typedef struct { DIR *native; b_dirent entry; int64_t cursor; } b_directory;
static void *b_opendir(const char *p) {
    char path[WB_PATH_MAX]; int e=wb_path_resolve(p,path,sizeof(path));
    if(e) { errno=wb_bionic_error(e); return NULL; }
    DIR *d=opendir(path); if(!d) { wb_errno_publish(); return NULL; }
    b_directory *b=wb_calloc(1,sizeof(*b));
    if(!b) { closedir(d); return NULL; } b->native=d; return b;
}
static void *b_readdir(b_directory *d) {
    if(!d) { errno=9; return NULL; }
    errno=0; struct dirent *n=readdir(d->native);
    if(!n) { if(errno) wb_errno_publish(); return NULL; }
    memset(&d->entry,0,sizeof(d->entry));
    d->entry.offset=++d->cursor;
    d->entry.inode=(uint64_t)d->cursor; /* FAT provider exposes no inode. */
    d->entry.length=sizeof(b_dirent);
    d->entry.type=SCE_S_ISDIR(n->d_stat.st_mode) ? 4 : 8;
    snprintf(d->entry.name,sizeof(d->entry.name),"%s",n->d_name);
    return &d->entry;
}
static int b_closedir(b_directory *d) {
    if(!d) { errno=9; return -1; }
    int r=closedir(d->native); wb_free(d); return negative(r);
}
static double b_difftime(int32_t a,int32_t b) { return (double)a-(double)b; }
static long b_sysconf(int name) {
    /* Android bionic's numeric sysconf IDs, not newlib's constants. */
    switch(name) {
    case 0x27: return 4096; /* _SC_PAGESIZE */
    case 0x60: case 0x61: return 3; /* processors configured/online available to homebrew */
    case 0x62: return (512L*1024*1024)/4096;
    case 0x63: {
        SceKernelFreeMemorySizeInfo i={.size=sizeof(i)};
        if(sceKernelGetFreeMemorySize(&i)<0) { errno=5; return -1; }
        return i.size_user/4096;
    }
    default: errno=22; return -1;
    }
}
static char *b_getenv(const char *name) {
    if(!name) return NULL;
    if(!strcmp(name,"HOME") || !strcmp(name,"TMPDIR") || !strcmp(name,"XDG_CONFIG_HOME")) return WB_SAVES;
    if(!strcmp(name,"LANG")) return "en_US.UTF-8";
    return NULL;
}
static int b_system(const char *command) {
    if(!command) return 0; /* No command interpreter on Vita. */
    errno=38; return -1;
}
static int b_property(const char *key,char *value) {
    const char *v="";
    if(!strcmp(key,"ro.product.model")) v="PlayStation Vita";
    else if(!strcmp(key,"ro.product.manufacturer")) v="Sony";
    else if(!strcmp(key,"ro.product.cpu.abi")) v="armeabi-v7a";
    else if(!strcmp(key,"ro.build.version.sdk")) v="21";
    strcpy(value,v); return (int)strlen(v);
}
static int b_log(int priority,const char *tag,const char *format,va_list args) {
    /* This debug-only splash dimension message occurs every rendered frame.
     * Synchronous writes to removable storage otherwise stall every frame. */
    if(format && (!strcmp(format,"size=%f,%f") || !strcmp(format,"size=%f,%f\n"))) return 0;
    char line[2048];
    int prefix=snprintf(line,sizeof(line),"[%u ms %d %s] ",
        (unsigned)(sceKernelGetProcessTimeWide()/1000),priority,tag ? tag : "");
    if(prefix<0 || (size_t)prefix>=sizeof(line)-2) return 0;
    int result=vsnprintf(line+prefix,sizeof(line)-(size_t)prefix-1,format,args);
    size_t size=strlen(line);
    if(!size || line[size-1]!='\n') line[size++]='\n';
    fwrite(line,1,size,stderr);
    return result;
}
static _Noreturn void b_assert(const char *file,int line,const char *function,const char *message) {
    fprintf(stderr,"ASSERT %s:%d %s: %s\n",file,line,function,message); fflush(stderr); wb_log_flush(); abort();
}
static uintptr_t guard=0x7394b251;
static _Noreturn void b_stack_fail(void) { b_assert("libwb",0,"stack","stack canary changed"); }
/* Guest destructors belong to the loaded image, not the Vita CRT's DSO list. */
static struct { void (*fn)(void *); void *arg,*dso; } destructors[512];
static size_t destructor_count;
static pthread_mutex_t destructor_lock=PTHREAD_MUTEX_INITIALIZER;
static int b_atexit(void (*f)(void *),void *arg,void *dso) {
    pthread_mutex_lock(&destructor_lock);
    if(destructor_count==512) { pthread_mutex_unlock(&destructor_lock); return -1; }
    destructors[destructor_count].fn=f; destructors[destructor_count].arg=arg;
    destructors[destructor_count++].dso=dso;
    pthread_mutex_unlock(&destructor_lock); return 0;
}
static void b_finalize(void *dso) {
    pthread_mutex_lock(&destructor_lock);
    for(size_t i=destructor_count;i>0;--i) {
        if(destructors[i-1].fn && (!dso || destructors[i-1].dso==dso)) {
            void (*f)(void *)=destructors[i-1].fn; void *arg=destructors[i-1].arg;
            destructors[i-1].fn=NULL;
            pthread_mutex_unlock(&destructor_lock); f(arg); pthread_mutex_lock(&destructor_lock);
        }
    }
    pthread_mutex_unlock(&destructor_lock);
}
static _Noreturn void b_exit(int code) {
    b_finalize(NULL);fflush(stdout);fflush(stderr);wb_log_flush();exit(code);
}
extern const uint32_t __exidx_start[],__exidx_end[];
uintptr_t __gnu_Unwind_Find_exidx(uintptr_t pc,int *count) {
    if(game && pc>=game->text_base && pc<game->text_base+game->text_size) {
        *count=game->exidx_size/8; return game->exidx_base;
    }
    *count=(int)((__exidx_end-__exidx_start)/2);
    return (uintptr_t)__exidx_start;
}
typedef struct { const char *filename; void *base; const char *symbol; void *address; } b_dl_info;
static int b_dladdr(void *address,b_dl_info *info) {
    if(!game || !info) return 0;
    uintptr_t pc=(uintptr_t)address;
    if(pc<game->text_base || pc>=game->text_base+game->text_size) return 0;
    *info=(b_dl_info){"libwb.so",(void *)game->load_addr,NULL,NULL};
    uintptr_t best=0;
    for(uint32_t i=0;i<game->num_dynsym;++i) {
        const Elf32_Sym *s=game->dynsym+i;
        uintptr_t p=game->load_addr+s->st_value;
        if(s->st_shndx!=SHN_UNDEF && s->st_name && p<=pc && p>best) {
            best=p; info->symbol=game->dynstr+s->st_name; info->address=(void *)p;
        }
    }
    return 1;
}
static int b_vasprintf(char **out,const char *format,va_list args) {
    if(!out) { errno=22; return -1; } *out=NULL;
    va_list copy; va_copy(copy,args); int n=vsnprintf(NULL,0,format,copy); va_end(copy);
    if(n<0) return n;
    char *s=wb_malloc((size_t)n+1); if(!s) return -1;
    int r=vsnprintf(s,(size_t)n+1,format,args);
    if(r<0 || r>n) { wb_free(s); return -1; } *out=s; return r;
}
static int b_putchar(int c) { return wb_fputc(c,&wb_stdio_handles[1]); }
int wb_platform_init(void) {
    int e=wb_stdio_init();
    if(!e) e=wb_semaphores_init(wb_sem_vita_ops());
    return e;
}
static const so_default_dynlib bindings[]={
#define MAP(name,fn) {name,(uintptr_t)fn}
#define DIRECT(fn) MAP(#fn,fn)
 MAP("__sF",wb_stdio_handles),MAP("__errno",wb_errno_location),
 MAP("fopen",wb_fopen),MAP("fclose",wb_fclose),MAP("fread",wb_fread),MAP("fwrite",wb_fwrite),
 MAP("fseek",wb_fseek),MAP("fseeko",wb_fseek),MAP("ftell",wb_ftell),MAP("ftello",wb_ftell),
 MAP("fflush",wb_fflush),MAP("fgets",wb_fgets),MAP("fputc",wb_fputc),MAP("getc",wb_fgetc),
 MAP("fputs",wb_fputs),MAP("ungetc",wb_ungetc),MAP("clearerr",wb_clearerr),MAP("setvbuf",wb_setvbuf),
 MAP("fprintf",wb_fprintf),MAP("fscanf",wb_fscanf),MAP("vfprintf",wb_vfprintf),
 MAP("printf",wb_printf),MAP("puts",wb_puts),MAP("putchar",b_putchar),
 MAP("feof",wb_feof),MAP("ferror",wb_ferror),MAP("rewind",wb_rewind),
 MAP("freopen",wb_freopen),MAP("tmpfile",wb_tmpfile),MAP("funopen",wb_funopen),
 MAP("open",b_open),MAP("close",b_close),MAP("read",b_read),MAP("stat",b_stat),MAP("fstat",b_fstat),
 MAP("mkdir",b_mkdir),MAP("access",b_access),MAP("rename",b_rename),MAP("remove",b_remove),
 MAP("rmdir",b_rmdir),MAP("sendfile",b_sendfile),MAP("opendir",b_opendir),MAP("readdir",b_readdir),MAP("closedir",b_closedir),
 MAP("pthread_create",b_create),MAP("pthread_join",b_pthread_join),MAP("pthread_detach",b_pthread_detach),
 DIRECT(pthread_self),DIRECT(pthread_equal),DIRECT(pthread_getspecific),
 MAP("pthread_key_create",b_pthread_key_create),MAP("pthread_key_delete",b_pthread_key_delete),
 MAP("pthread_setspecific",b_pthread_setspecific),MAP("pthread_once",b_pthread_once),
 MAP("pthread_mutexattr_init",b_pthread_mutexattr_init),MAP("pthread_mutexattr_destroy",b_pthread_mutexattr_destroy),
 MAP("pthread_mutexattr_settype",b_pthread_mutexattr_settype),MAP("pthread_mutex_init",b_pthread_mutex_init),
 MAP("pthread_mutex_destroy",b_pthread_mutex_destroy),MAP("pthread_mutex_lock",b_pthread_mutex_lock),
 MAP("pthread_mutex_trylock",b_pthread_mutex_trylock),MAP("pthread_mutex_unlock",b_pthread_mutex_unlock),
 MAP("pthread_cond_destroy",b_pthread_cond_destroy),MAP("pthread_cond_signal",b_pthread_cond_signal),
 MAP("pthread_cond_broadcast",b_pthread_cond_broadcast),MAP("pthread_cond_wait",b_pthread_cond_wait),
 MAP("pthread_cond_timedwait",b_pthread_cond_timedwait),
 MAP("sem_init",b_sem_init),MAP("sem_destroy",b_sem_destroy),MAP("sem_wait",b_sem_wait),MAP("sem_post",b_sem_post),
 MAP("time",wb_b_time),MAP("clock",wb_b_clock),MAP("clock_gettime",wb_b_clock_gettime),
 MAP("gettimeofday",wb_b_gettimeofday),MAP("nanosleep",wb_b_nanosleep),MAP("usleep",wb_b_usleep),
 MAP("gmtime",wb_b_gmtime),MAP("localtime",wb_b_localtime),MAP("mktime",wb_b_mktime),
 MAP("strftime",wb_b_strftime),MAP("difftime",b_difftime),
 MAP("sysconf",b_sysconf),MAP("getenv",b_getenv),MAP("system",b_system),DIRECT(sched_yield),
 MAP("__system_property_get",b_property),MAP("__android_log_vprint",b_log),
 MAP("__assert2",b_assert),MAP("__stack_chk_guard",&guard),MAP("__stack_chk_fail",b_stack_fail),
 MAP("__cxa_atexit",b_atexit),MAP("__cxa_finalize",b_finalize),MAP("exit",b_exit),DIRECT(abort),
 MAP("__gnu_Unwind_Find_exidx",__gnu_Unwind_Find_exidx),MAP("dladdr",b_dladdr),MAP("vasprintf",b_vasprintf),
 MAP("setjmp",wb_guest_setjmp),MAP("longjmp",wb_guest_longjmp),
#undef MAP
#undef DIRECT
};
const so_default_dynlib *wb_platform_bindings(size_t *count) {
    if(count) *count=sizeof(bindings)/sizeof(*bindings);
    return bindings;
}
