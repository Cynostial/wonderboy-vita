#ifndef WB_LOG_VITA_H
#define WB_LOG_VITA_H
int wb_log_start(void);
void wb_log_flush(void);
void wb_log_stats(unsigned *dropped,unsigned *errors);
#endif
