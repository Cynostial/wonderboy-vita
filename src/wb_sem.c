#include "wb_sem.h"
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
static wb_sem_ops ops;
static int ready;
static atomic_uint live;
#define SEM_LIMIT 512u
static int valid(wb_sem *s) { return ready && s && s->id>=0; }
int wb_semaphores_init(const wb_sem_ops *backend) {
    if(ready) return EBUSY;
    if(!backend || !backend->create || !backend->destroy || !backend->poll || !backend->wait ||
       !backend->post || !backend->value || !backend->realtime) return EINVAL;
    ops=*backend; ready=1; return 0;
}
int wb_sem_init(wb_sem *s,int pshared,unsigned value) {
    if(!ready || !s || value>INT_MAX) return EINVAL;
    if(pshared) return ENOTSUP;
    if(atomic_fetch_add(&live,1)>=SEM_LIMIT) { atomic_fetch_sub(&live,1); return ENOSPC; }
    int32_t id=-1; int e=ops.create(value,&id,ops.context);
    if(e) { atomic_fetch_sub(&live,1); return e; }
    s->id=id; return 0;
}
int wb_sem_destroy(wb_sem *s) {
    if(!valid(s)) return EINVAL;
    int e=ops.destroy(s->id,ops.context);
    if(!e) { s->id=-1; atomic_fetch_sub(&live,1); }
    return e;
}
int wb_sem_wait(wb_sem *s) { return valid(s) ? ops.wait(s->id,NULL,ops.context) : EINVAL; }
int wb_sem_trywait(wb_sem *s) { return valid(s) ? ops.poll(s->id,ops.context) : EINVAL; }
int wb_sem_post(wb_sem *s) { return valid(s) ? ops.post(s->id,ops.context) : EINVAL; }
int wb_sem_getvalue(wb_sem *s,int *value) {
    return valid(s) && value ? ops.value(s->id,value,ops.context) : EINVAL;
}
int wb_sem_timedwait(wb_sem *s,const wb_bionic_timespec *until) {
    if(!valid(s)) return EINVAL;
    /* POSIX permits immediate acquisition without examining the timeout. */
    int e=ops.poll(s->id,ops.context); if(e!=EAGAIN) return e;
    if(!until || until->nanoseconds<0 || until->nanoseconds>=1000000000) return EINVAL;
    const int64_t deadline=(int64_t)until->seconds*1000000+(until->nanoseconds+999)/1000;
    for(;;) {
        int64_t now;
        e=ops.realtime(&now,ops.context); if(e) return e;
        if(now>=deadline) return ETIMEDOUT;
        /* Re-read realtime at least every 100 ms for wall-clock adjustment,
         * and avoid narrowing a long absolute timeout into a 32-bit wrap. */
        uint32_t remaining=deadline-now>100000 ? 100000u : (uint32_t)(deadline-now);
        e=ops.wait(s->id,&remaining,ops.context);
        if(e!=ETIMEDOUT) return e;
    }
}
