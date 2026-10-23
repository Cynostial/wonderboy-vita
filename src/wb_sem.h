#ifndef WB_SEM_H
#define WB_SEM_H
#include "wb_bionic_sync.h"
/* Android ARM sem_t occupies four bytes. The adapter owns a native kernel ID
 * in that storage; no Vita/PTE pointer or extra bytes are written there. */
typedef struct { int32_t id; } wb_sem;
_Static_assert(sizeof(wb_sem)==4,"Bionic semaphore ABI");
typedef struct {
    int (*create)(unsigned,int32_t *,void *);
    int (*destroy)(int32_t,void *);
    int (*poll)(int32_t,void *);
    int (*wait)(int32_t,const uint32_t *,void *);
    int (*post)(int32_t,void *);
    int (*value)(int32_t,int *,void *);
    int (*realtime)(int64_t *,void *); /* Microseconds since Unix epoch. */
    void *context;
} wb_sem_ops;
/* Initialize on the startup thread before Android constructors. Backend
 * returns positive native errno values; platform adapters convert to -1/errno.
 * Destroy must not race uses of the same semaphore, as required by POSIX. */
int wb_semaphores_init(const wb_sem_ops *);
int wb_sem_init(wb_sem *,int pshared,unsigned);
int wb_sem_destroy(wb_sem *);
int wb_sem_wait(wb_sem *);
int wb_sem_trywait(wb_sem *);
int wb_sem_timedwait(wb_sem *,const wb_bionic_timespec *);
int wb_sem_post(wb_sem *);
int wb_sem_getvalue(wb_sem *,int *);
const wb_sem_ops *wb_sem_vita_ops(void);
#endif
