#include "wb_io.h"
#include "wb_paths.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#endif
static int bad(int e) { errno=e; return -1; }
int wb_io_flags(int flags,int *native) {
    if(!native || (flags&3)==3) return EINVAL;
    /* Exact Android 4.2 ARM values; native O_* values differ. O_LARGEFILE is
     * accepted only as the ABI flag: the Vita off_t limit is still checked by
     * the lseek/mapping adapters. CLOEXEC is meaningful only across exec. */
    const unsigned known=3|0x40|0x80|0x100|0x200|0x400|0x800|0x8000|0x80000;
    if((unsigned)flags&~known) return ENOTSUP;
    if(flags&0x80000) return ENOTSUP; /* No exec/descriptor inheritance backend. */
    int result=(flags&3)==0 ? O_RDONLY : (flags&3)==1 ? O_WRONLY : O_RDWR;
    if(flags&0x40) result|=O_CREAT;
    if(flags&0x80) result|=O_EXCL;
    if(flags&0x200) result|=O_TRUNC;
    if(flags&0x400) result|=O_APPEND;
    if(flags&0x800) {
#ifdef O_NONBLOCK
        result|=O_NONBLOCK;
#else
        return ENOTSUP;
#endif
    }
    /* O_NOCTTY has no effect on regular game files. */
#ifdef O_BINARY
    result|=O_BINARY;
#endif
    *native=result; return 0;
}
int wb_io_open(const char *path,int flags,unsigned mode) {
    int native; int e=wb_io_flags(flags,&native); if(e) return bad(e);
    char resolved[WB_PATH_MAX]; e=wb_path_resolve(path,resolved,sizeof(resolved));
    if(e) return bad(e);
    return open(resolved,native,(mode_t)mode);
}
static int convert(const struct stat *s,wb_android_stat *out) {
    if(!out) return bad(EFAULT);
    wb_android_stat result={0};
    result.device=s->st_dev; result.inode=s->st_ino; result.legacy_inode=(uint32_t)s->st_ino;
    result.mode=s->st_mode; result.links=s->st_nlink; result.uid=s->st_uid; result.gid=s->st_gid;
    result.rdevice=s->st_rdev; result.size=s->st_size;
#ifdef _WIN32
    result.block_size=4096; result.blocks=((uint64_t)s->st_size+511)/512;
#else
    result.block_size=s->st_blksize; result.blocks=s->st_blocks;
#endif
    result.accessed=(uint32_t)s->st_atime; result.modified=(uint32_t)s->st_mtime; result.changed=(uint32_t)s->st_ctime;
    /* Vita's available stat provider exposes only whole-second timestamps. */
    *out=result; return 0;
}
int wb_io_stat(const char *path,wb_android_stat *out) {
    if(!out) return bad(EFAULT);
    char resolved[WB_PATH_MAX]; int e=wb_path_resolve(path,resolved,sizeof(resolved));
    if(e) return bad(e);
    struct stat s; if(stat(resolved,&s)<0) return -1; return convert(&s,out);
}
int wb_io_fstat(int fd,wb_android_stat *out) {
    if(!out) return bad(EFAULT);
    struct stat s; if(fstat(fd,&s)<0) return -1; return convert(&s,out);
}
int wb_io_mkdir(const char *path,unsigned mode) {
    char resolved[WB_PATH_MAX]; int e=wb_path_resolve(path,resolved,sizeof(resolved));
    if(e) return bad(e);
#ifdef _WIN32
    (void)mode; return _mkdir(resolved);
#else
    return mkdir(resolved,(mode_t)mode);
#endif
}
int wb_io_unlink(const char *path) {
    char resolved[WB_PATH_MAX]; int e=wb_path_resolve(path,resolved,sizeof(resolved));
    return e ? bad(e) : unlink(resolved);
}
int wb_io_rename(const char *from,const char *to) {
    char a[WB_PATH_MAX],b[WB_PATH_MAX]; int e=wb_path_resolve(from,a,sizeof(a));
    if(!e) e=wb_path_resolve(to,b,sizeof(b));
    return e ? bad(e) : rename(a,b);
}
int wb_io_access(const char *path,int mode) {
    if(mode&~7) return bad(EINVAL);
    char resolved[WB_PATH_MAX]; int e=wb_path_resolve(path,resolved,sizeof(resolved));
    return e ? bad(e) : access(resolved,mode);
}
