#include "wb_errno.h"
#include <errno.h>
#include <stddef.h>
int *wb_errno_location(void) { return &errno; }
int wb_bionic_error(int e) {
    /* Linux/Android errno numbers differ from Vita newlib and host CRT. */
    static const struct { int native, android; } errors[]={
        {EPERM,1},{ENOENT,2},{ESRCH,3},{EINTR,4},{EIO,5},{ENXIO,6},{E2BIG,7},
        {ENOEXEC,8},{EBADF,9},{ECHILD,10},{EAGAIN,11},{ENOMEM,12},{EACCES,13},
        {EFAULT,14},{EBUSY,16},{EEXIST,17},{EXDEV,18},{ENODEV,19},{ENOTDIR,20},
        {EISDIR,21},{EINVAL,22},{ENFILE,23},{EMFILE,24},{ENOTTY,25},{EFBIG,27},
        {ENOSPC,28},{ESPIPE,29},{EROFS,30},{EMLINK,31},{EPIPE,32},{EDOM,33},
        {ERANGE,34},{EDEADLK,35},{ENAMETOOLONG,36},{ENOSYS,38},{ENOTEMPTY,39},
        {EILSEQ,84},{EOVERFLOW,75},{ENOTSUP,95},{ETIMEDOUT,110}
    };
    if(!e) return 0;
    for(size_t i=0;i<sizeof(errors)/sizeof(errors[0]);++i)
        if(errors[i].native==e) return errors[i].android;
    return 5;
}
void wb_errno_publish(void) { errno=wb_bionic_error(errno); }
