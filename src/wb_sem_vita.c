#include "wb_sem.h"
#include <psp2/kernel/threadmgr/semaphore.h>
#include <psp2/kernel/error.h>
#include <sys/time.h>
#include <errno.h>
#include <limits.h>
static int error(int result) {
    if(result>=0) return 0;
    switch((uint32_t)result) {
    case SCE_KERNEL_ERROR_WAIT_TIMEOUT:return ETIMEDOUT;
    case SCE_KERNEL_ERROR_SEMA_ZERO:return EAGAIN;
    case SCE_KERNEL_ERROR_SEMA_OVF:return EOVERFLOW;
    case SCE_KERNEL_ERROR_WAIT_CANCEL:return EINTR;
    case SCE_KERNEL_ERROR_UNKNOWN_SEMA_ID:
    case SCE_KERNEL_ERROR_INVALID_UID:
    case SCE_KERNEL_ERROR_WAIT_DELETE:
    case SCE_KERNEL_ERROR_ILLEGAL_COUNT:return EINVAL;
    default:return EIO;
    }
}
static int create(unsigned count,int32_t *id,void *context) {
    (void)context;
    SceUID result=sceKernelCreateSema("wb_sem",0,(int)count,INT_MAX,NULL);
    if(result<0) return ENOMEM;
    *id=result; return 0;
}
static int destroy(int32_t id,void *context) {
    (void)context;
    SceKernelSemaInfo info={0}; info.size=sizeof(info);
    int e=error(sceKernelGetSemaInfo(id,&info)); if(e) return e;
    if(info.numWaitThreads) return EBUSY;
    return error(sceKernelDeleteSema(id));
}
static int poll(int32_t id,void *context) { (void)context; return error(sceKernelPollSema(id,1)); }
static int wait(int32_t id,const uint32_t *timeout,void *context) {
    (void)context; SceUInt us=timeout ? *timeout : 0;
    return error(sceKernelWaitSema(id,1,timeout ? &us : NULL));
}
static int post(int32_t id,void *context) { (void)context; return error(sceKernelSignalSema(id,1)); }
static int value(int32_t id,int *out,void *context) {
    (void)context;
    SceKernelSemaInfo info={0}; info.size=sizeof(info);
    int e=error(sceKernelGetSemaInfo(id,&info));
    if(!e) *out=info.currentCount;
    return e;
}
static int realtime(int64_t *us,void *context) {
    (void)context; struct timeval tv;
    if(gettimeofday(&tv,NULL)<0) return errno;
    *us=(int64_t)tv.tv_sec*1000000+tv.tv_usec; return 0;
}
const wb_sem_ops *wb_sem_vita_ops(void) {
    static const wb_sem_ops backend={create,destroy,poll,wait,post,value,realtime,NULL}; return &backend;
}
