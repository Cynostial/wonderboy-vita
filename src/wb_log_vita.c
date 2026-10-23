#include "wb_log_vita.h"
#include "wb_log_queue.h"
#include "wb_paths.h"
#include <stdio.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
static wb_log_queue queue;
static int output=-1,started;
static unsigned writing,errors;
static int log_write(struct _reent *r,void *cookie,const char *data,int n) {
    (void)r;(void)cookie;
    if(n>0) wb_log_push(&queue,data,(unsigned)n);
    return n;
}
static int log_worker(unsigned args,void *arg) {
    (void)args;(void)arg;
    unsigned char batch[8192];
    for(;;) {
        __atomic_store_n(&writing,1,__ATOMIC_RELEASE);
        size_t n=wb_log_pop(&queue,batch,sizeof batch),at=0;
        while(at<n) {
            int result=sceIoWrite(output,batch+at,n-at);
            if(result<=0) {
                __atomic_fetch_add(&errors,1,__ATOMIC_RELAXED);break;
            }
            at+=(unsigned)result;
        }
        __atomic_store_n(&writing,0,__ATOMIC_RELEASE);
        if(!n) sceKernelDelayThread(20000);
    }
    return 0;
}
int wb_log_start(void) {
    if(started) return 0;
    fflush(stdout);fflush(stderr);
    output=sceIoOpen(WB_ROOT "/run.log",SCE_O_WRONLY|SCE_O_APPEND,0);
    if(output<0) return -1;
    SceUID thread=sceKernelCreateThread("Wonder log",log_worker,0x10000110,
                                      0x4000,0,SCE_KERNEL_CPU_MASK_USER_ALL,NULL);
    if(thread<0) { sceIoClose(output);output=-1;return -1; }
    if(sceKernelStartThread(thread,0,NULL)<0) {
        sceKernelDeleteThread(thread);sceIoClose(output);output=-1;return -1;
    }
    /* Replace only these two existing newlib FILE write callbacks. Their
     * identity, descriptors, cookies, close/seek operations and thread-shared
     * standard-stream pointers stay intact. freopen resets the callback, and
     * a later reuse of a closed descriptor cannot redirect saves into logs.
     * The worker uses its own kernel handle, never a FILE or its lock. */
    stdout->_write=log_write;stderr->_write=log_write;
    started=1;return 0;
}
void wb_log_flush(void) {
    if(!started) return;
    /* Exit/fatal paths only. A failed device must not prevent termination. */
    uint64_t until=sceKernelGetProcessTimeWide()+500000;
    while(wb_log_pending(&queue) || __atomic_load_n(&writing,__ATOMIC_ACQUIRE)) {
        if(sceKernelGetProcessTimeWide()>=until) break;
        sceKernelDelayThread(1000);
    }
}
void wb_log_stats(unsigned *dropped,unsigned *failed) {
    *dropped=__atomic_load_n(&queue.dropped,__ATOMIC_RELAXED);
    *failed=__atomic_load_n(&errors,__ATOMIC_RELAXED);
}
