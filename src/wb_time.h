#ifndef WB_TIME_H
#define WB_TIME_H
#include "wb_bionic_sync.h"
#include <stddef.h>
typedef struct { int32_t seconds,microseconds; } wb_timeval;
typedef struct {
    int32_t second,minute,hour,day,month,year,weekday,yearday,dst,utc_offset;
    const char *zone;
} wb_tm;
#if defined(__arm__)
_Static_assert(sizeof(wb_tm)==44,"Android ARM struct tm ABI");
#endif
int wb_b_clock_gettime(int,wb_bionic_timespec *);
int wb_b_clock_getres(int,wb_bionic_timespec *);
int wb_b_gettimeofday(wb_timeval *,void *timezone);
int wb_b_nanosleep(const wb_bionic_timespec *,wb_bionic_timespec *);
int wb_b_usleep(uint32_t);
unsigned wb_b_sleep(unsigned);
int32_t wb_b_clock(void);
int32_t wb_b_time(int32_t *);
wb_tm *wb_b_gmtime(const int32_t *);
wb_tm *wb_b_localtime(const int32_t *);
int32_t wb_b_mktime(wb_tm *);
size_t wb_b_strftime(char *,size_t,const char *,const wb_tm *);
#endif
