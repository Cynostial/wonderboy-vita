#ifndef WB_BIONIC_SYNC_H
#define WB_BIONIC_SYNC_H
#include <stdint.h>
/* Android 4.2 ARM ABI: these objects are four bytes, not Vita pthread types. */
typedef struct { int32_t value; } wb_bionic_mutex;
typedef struct { int32_t value; } wb_bionic_cond;
typedef struct { int32_t seconds, nanoseconds; } wb_bionic_timespec;
int wb_mutexattr_init(int32_t *);
int wb_mutexattr_settype(int32_t *,int);
int wb_mutexattr_destroy(int32_t *);
int wb_mutex_init(wb_bionic_mutex *,const int32_t *);
int wb_mutex_destroy(wb_bionic_mutex *);
int wb_mutex_lock(wb_bionic_mutex *);
int wb_mutex_trylock(wb_bionic_mutex *);
int wb_mutex_unlock(wb_bionic_mutex *);
int wb_cond_init(wb_bionic_cond *,const int32_t *);
int wb_cond_destroy(wb_bionic_cond *);
int wb_cond_signal(wb_bionic_cond *);
int wb_cond_broadcast(wb_bionic_cond *);
int wb_cond_wait(wb_bionic_cond *,wb_bionic_mutex *);
int wb_cond_timedwait(wb_bionic_cond *,wb_bionic_mutex *,const wb_bionic_timespec *);
int wb_once(volatile int32_t *,void (*)(void));
#endif
