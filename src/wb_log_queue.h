#ifndef WB_LOG_QUEUE_H
#define WB_LOG_QUEUE_H
#include <stddef.h>
#include <stdint.h>
#define WB_LOG_CAPACITY 65536u
typedef struct {
    unsigned lock,read,used,dropped;
    unsigned char bytes[WB_LOG_CAPACITY];
} wb_log_queue;
/* Multiple producers, one consumer. A busy/full queue drops diagnostics;
 * neither side holds the lock during storage IO. Never wait on a producer. */
void wb_log_push(wb_log_queue *,const void *,size_t);
size_t wb_log_pop(wb_log_queue *,void *,size_t);
unsigned wb_log_pending(const wb_log_queue *);
#endif
