#include "wb_bionic_sync.h"
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <stdatomic.h>

/* Fixed metadata storage; actual native synchronization storage belongs to
 * pthreads. No host pointer is written into a four-byte Android object.
 * The registry lock serializes lazy static initialization and object lifetime. */
#define SYNC_SLOTS 1024
#define SYNC_BUCKETS 2048
enum { MUTEX=1, COND=2 };
typedef struct {
    void *address;
    unsigned kind,next;
    atomic_uint users;
    union { pthread_mutex_t mutex; pthread_cond_t cond; } native;
} slot;
static slot slots[SYNC_SLOTS];
/* Registry operations hold registry_lock. A completed operation only drops
 * its reference; it need not acquire that global mutex a second time. */
static unsigned buckets[SYNC_BUCKETS];
static pthread_mutex_t registry_lock=PTHREAD_MUTEX_INITIALIZER;
static int lock_registry(void) {
    int error=pthread_mutex_lock(&registry_lock);
    return error;
}
static void unlock_registry_lock(void) { pthread_mutex_unlock(&registry_lock);  }

static unsigned bucket(void *address) {
    uintptr_t v=(uintptr_t)address>>2;
    v^=v>>11;v*=0x9e3779b1u;return (unsigned)v&(SYNC_BUCKETS-1);
}
static slot *find(void *address) {
    for(unsigned i=buckets[bucket(address)];i;i=slots[i-1].next)
        if(slots[i-1].address==address)return &slots[i-1];
    return NULL;
}
static int create(void *address,unsigned kind,int type,slot **out) {
    slot *s=NULL;
    for(unsigned i=0;i<SYNC_SLOTS;++i) if(!slots[i].address) { s=&slots[i]; break; }
    if(!s) return EAGAIN;
    int e;
    if(kind==MUTEX) {
        pthread_mutexattr_t attr;
        e=pthread_mutexattr_init(&attr);
        if(e) return e;
        int native_type=type==1 ? PTHREAD_MUTEX_RECURSIVE : type==2 ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL;
        e=pthread_mutexattr_settype(&attr,native_type);
        if(!e) e=pthread_mutex_init(&s->native.mutex,&attr);
        pthread_mutexattr_destroy(&attr);
    } else e=pthread_cond_init(&s->native.cond,NULL);
    if(e) return e;
    unsigned b=bucket(address);
    s->address=address;s->kind=kind;atomic_store_explicit(&s->users,0,memory_order_relaxed);
    s->next=buckets[b];buckets[b]=(unsigned)(s-slots)+1;*out=s;return 0;
}
static int pin(void *address,unsigned kind,slot **out) {
    if(!address) return EINVAL;
    int e=lock_registry(); if(e) return e;
    slot *s=find(address);
    if(s && s->kind!=kind) e=EINVAL;
    if(!s) {
        int32_t value=*(int32_t *)address;
        int type=value==0x4000 ? 1 : value==0x8000 ? 2 : 0;
        if((kind==MUTEX && value!=0 && value!=0x4000 && value!=0x8000) || (kind==COND && value!=0)) e=EINVAL;
        else e=create(address,kind,type,&s);
    }
    if(!e) { atomic_fetch_add_explicit(&s->users,1,memory_order_relaxed);*out=s; }
    unlock_registry_lock(); return e;
}
static void unpin(slot *s) {
    atomic_fetch_sub_explicit(&s->users,1,memory_order_release);
}
int wb_mutexattr_init(int32_t *a) { if(!a) return EINVAL; *a=0; return 0; }
int wb_mutexattr_settype(int32_t *a,int type) { if(!a || type<0 || type>2) return EINVAL; *a=type; return 0; }
int wb_mutexattr_destroy(int32_t *a) { if(!a) return EINVAL; *a=-1; return 0; }
static int initialize(void *address,unsigned kind,int type) {
    if(!address) return EINVAL;
    int e=lock_registry(); if(e) return e;
    slot *s=find(address);
    if(s) e=EBUSY;
    else {
        e=create(address,kind,type,&s);
        if(!e) *(int32_t *)address=kind==MUTEX ? type<<14 : 0;
    }
    unlock_registry_lock(); return e;
}
int wb_mutex_init(wb_bionic_mutex *m,const int32_t *a) {
    if(a && (*a<0 || *a>2)) return ENOTSUP;
    return initialize(m,MUTEX,a ? *a : 0);
}
int wb_cond_init(wb_bionic_cond *c,const int32_t *a) {
    if(a && *a) return ENOTSUP; /* Process shared / non-realtime clock unsupported. */
    return initialize(c,COND,0);
}
static int destroy(void *address,unsigned kind) {
    slot *s; int e=pin(address,kind,&s); if(e) return e;
    lock_registry();
    if(atomic_load_explicit(&s->users,memory_order_acquire)!=1) e=EBUSY;
    else e=kind==MUTEX ? pthread_mutex_destroy(&s->native.mutex) : pthread_cond_destroy(&s->native.cond);
    if(!e) {
        unsigned *link=&buckets[bucket(address)],index=(unsigned)(s-slots)+1;
        while(*link!=index)link=&slots[*link-1].next;
        *link=s->next;
        s->address=NULL;s->kind=0;s->next=0;
        atomic_store_explicit(&s->users,0,memory_order_relaxed);
        *(int32_t *)address=-1;
    } else atomic_fetch_sub_explicit(&s->users,1,memory_order_release);
    unlock_registry_lock(); return e;
}
int wb_mutex_destroy(wb_bionic_mutex *m) { return destroy(m,MUTEX); }
int wb_cond_destroy(wb_bionic_cond *c) { return destroy(c,COND); }
int wb_mutex_lock(wb_bionic_mutex *m) {
    slot *s; int e=pin(m,MUTEX,&s); if(e) return e;
    e=pthread_mutex_lock(&s->native.mutex); unpin(s); return e;
}
int wb_mutex_trylock(wb_bionic_mutex *m) {
    slot *s; int e=pin(m,MUTEX,&s); if(e) return e;
    e=pthread_mutex_trylock(&s->native.mutex); unpin(s); return e;
}
int wb_mutex_unlock(wb_bionic_mutex *m) {
    slot *s; int e=pin(m,MUTEX,&s); if(e) return e;
    e=pthread_mutex_unlock(&s->native.mutex); unpin(s); return e;
}
int wb_cond_signal(wb_bionic_cond *c) {
    slot *s; int e=pin(c,COND,&s); if(e) return e;
    e=pthread_cond_signal(&s->native.cond); unpin(s); return e;
}
int wb_cond_broadcast(wb_bionic_cond *c) {
    slot *s; int e=pin(c,COND,&s); if(e) return e;
    e=pthread_cond_broadcast(&s->native.cond); unpin(s); return e;
}
typedef struct { slot *condition,*mutex; } wait_cleanup;
static void release_wait(void *data) {
    wait_cleanup *w=data; unpin(w->condition); unpin(w->mutex);
}
static int wait_for(wb_bionic_cond *c,wb_bionic_mutex *m,const struct timespec *until) {
    slot *cs,*ms; int e=pin(c,COND,&cs); if(e) return e;
    e=pin(m,MUTEX,&ms); if(e) { unpin(cs); return e; }
    wait_cleanup cleanup={cs,ms};
    pthread_cleanup_push(release_wait,&cleanup);
    e=until ? pthread_cond_timedwait(&cs->native.cond,&ms->native.mutex,until)
            : pthread_cond_wait(&cs->native.cond,&ms->native.mutex);
    pthread_cleanup_pop(1);
    return e;
}
int wb_cond_wait(wb_bionic_cond *c,wb_bionic_mutex *m) { return wait_for(c,m,NULL); }
int wb_cond_timedwait(wb_bionic_cond *c,wb_bionic_mutex *m,const wb_bionic_timespec *until) {
    if(!until || until->nanoseconds<0 || until->nanoseconds>=1000000000) return EINVAL;
    struct timespec native={until->seconds,until->nanoseconds};
    return wait_for(c,m,&native);
}
static void reset_once(void *control) {
    lock_registry();
    __atomic_store_n((volatile int32_t *)control,0,__ATOMIC_RELEASE);
    unlock_registry_lock();
}
int wb_once(volatile int32_t *control,void (*init)(void)) {
    if(!control || !init) return EINVAL;
    if(__atomic_load_n(control,__ATOMIC_ACQUIRE)==2)return 0;
    int e=lock_registry(); if(e) return e;
    /* Run initializers outside the registry lock; preserve cancellation retry. */
    while(__atomic_load_n(control,__ATOMIC_RELAXED)==1) {
        unlock_registry_lock();
        pthread_testcancel(); sched_yield();
        e=lock_registry(); if(e) return e;
    }
    int32_t state=__atomic_load_n(control,__ATOMIC_RELAXED);
    if(e || (state!=0 && state!=2)) { unlock_registry_lock(); return e ? e : EINVAL; }
    if(state==2) { unlock_registry_lock(); return 0; }
    __atomic_store_n(control,1,__ATOMIC_RELAXED);unlock_registry_lock();
    pthread_cleanup_push(reset_once,(void *)control);
    init();
    pthread_cleanup_pop(0);
    lock_registry();__atomic_store_n(control,2,__ATOMIC_RELEASE);
    unlock_registry_lock(); return 0;
}
