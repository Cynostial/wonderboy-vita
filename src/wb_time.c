#define _POSIX_C_SOURCE 200809L
#include "wb_time.h"
#include "wb_errno.h"
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
static _Thread_local wb_tm result_tm;
static int fail(int error) { errno=error; wb_errno_publish(); return -1; }
static int clock_id(int android,clockid_t *native) {
    switch(android) {
    case 0:*native=CLOCK_REALTIME; return 0;
    case 1:*native=CLOCK_MONOTONIC; return 0;
#ifdef CLOCK_PROCESS_CPUTIME_ID
    case 2:*native=CLOCK_PROCESS_CPUTIME_ID; return 0;
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
    case 3:*native=CLOCK_THREAD_CPUTIME_ID; return 0;
#endif
    default:return EINVAL;
    }
}
static int convert(const struct timespec *native,wb_bionic_timespec *out) {
    if(native->tv_sec<INT32_MIN || native->tv_sec>INT32_MAX || native->tv_nsec<0 || native->tv_nsec>=1000000000)
        return fail(EOVERFLOW);
    *out=(wb_bionic_timespec){(int32_t)native->tv_sec,(int32_t)native->tv_nsec}; return 0;
}
int wb_b_clock_gettime(int id,wb_bionic_timespec *out) {
    if(!out) return fail(EFAULT);
    clockid_t native; int error=clock_id(id,&native); if(error) return fail(error);
    struct timespec value; if(clock_gettime(native,&value)<0) { wb_errno_publish(); return -1; }
    return convert(&value,out);
}
int wb_b_clock_getres(int id,wb_bionic_timespec *out) {
    clockid_t native; int error=clock_id(id,&native); if(error) return fail(error);
    struct timespec value; if(clock_getres(native,&value)<0) { wb_errno_publish(); return -1; }
    return out ? convert(&value,out) : 0;
}
int wb_b_gettimeofday(wb_timeval *out,void *timezone) {
    if(timezone) return fail(ENOTSUP);
    struct timeval value;
    if(gettimeofday(&value,NULL)<0) { wb_errno_publish(); return -1; }
    if(value.tv_sec<INT32_MIN || value.tv_sec>INT32_MAX) return fail(EOVERFLOW);
    if(out) *out=(wb_timeval){(int32_t)value.tv_sec,(int32_t)value.tv_usec};
    return 0;
}
int wb_b_nanosleep(const wb_bionic_timespec *request,wb_bionic_timespec *remaining) {
    if(!request) return fail(EFAULT);
    if(request->seconds<0 || request->nanoseconds<0 || request->nanoseconds>=1000000000) return fail(EINVAL);
    struct timespec native={request->seconds,request->nanoseconds},rest={0};
    int error=nanosleep(&native,&rest);
    if(error<0) {
        int saved=errno;
        if(saved==EINTR && remaining && convert(&rest,remaining)) return -1;
        return fail(saved);
    }
    return 0;
}
int wb_b_usleep(uint32_t us) {
    wb_bionic_timespec request={(int32_t)(us/1000000),(int32_t)(us%1000000)*1000};
    return wb_b_nanosleep(&request,NULL);
}
unsigned wb_b_sleep(unsigned seconds) {
    if(seconds>INT32_MAX) { fail(EINVAL); return seconds; }
    wb_bionic_timespec request={(int32_t)seconds,0},remaining={0};
    if(!wb_b_nanosleep(&request,&remaining)) return 0;
    return errno==EINTR ? (unsigned)remaining.seconds+(remaining.nanoseconds!=0) : seconds;
}
int32_t wb_b_clock(void) {
    clock_t ticks=clock(); if(ticks==(clock_t)-1) { wb_errno_publish(); return -1; }
    uint64_t us=(uint64_t)ticks*1000000/CLOCKS_PER_SEC;
    if(us>INT32_MAX) return fail(EOVERFLOW);
    return (int32_t)us;
}
int32_t wb_b_time(int32_t *out) {
    time_t value=time(NULL);
    if(value==(time_t)-1) { wb_errno_publish(); return -1; }
    if(value<INT32_MIN || value>INT32_MAX) return fail(EOVERFLOW);
    if(out) *out=(int32_t)value;
    return (int32_t)value;
}
/* Calendar arithmetic is used only to derive local UTC offset. The native
 * conversion remains responsible for time-zone and daylight-saving rules. */
static int64_t calendar_seconds(const struct tm *t) {
    int64_t year=(int64_t)t->tm_year+1900,days=0;
    for(int64_t y=1970;y<year;++y) days+=365+(y%4==0 && (y%100!=0 || y%400==0));
    for(int64_t y=year;y<1970;++y) days-=365+(y%4==0 && (y%100!=0 || y%400==0));
    static const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(int m=0;m<t->tm_mon;++m) days+=lengths[m]+(m==1 && year%4==0 && (year%100!=0 || year%400==0));
    return (days+t->tm_mday-1)*86400+(int64_t)t->tm_hour*3600+t->tm_min*60+t->tm_sec;
}
static void export_tm(const struct tm *t,int32_t stamp,int local,wb_tm *out) {
    *out=(wb_tm){t->tm_sec,t->tm_min,t->tm_hour,t->tm_mday,t->tm_mon,t->tm_year,
        t->tm_wday,t->tm_yday,t->tm_isdst,(int32_t)(calendar_seconds(t)-stamp),"GMT"};
    if(local) {
#ifdef _WIN32
        out->zone=_tzname[t->tm_isdst>0];
#else
        out->zone=tzname[t->tm_isdst>0];
#endif
    }
}
static struct tm import_tm(const wb_tm *t) {
    struct tm out={0}; out.tm_sec=t->second; out.tm_min=t->minute; out.tm_hour=t->hour;
    out.tm_mday=t->day; out.tm_mon=t->month; out.tm_year=t->year; out.tm_wday=t->weekday;
    out.tm_yday=t->yearday; out.tm_isdst=t->dst; return out;
}
static wb_tm *break_time(const int32_t *stamp,int local) {
    if(!stamp) { fail(EFAULT); return NULL; }
    time_t value=*stamp; struct tm native;
    if(!(local ? localtime_r(&value,&native) : gmtime_r(&value,&native))) { wb_errno_publish(); return NULL; }
    export_tm(&native,*stamp,local,&result_tm); return &result_tm;
}
wb_tm *wb_b_gmtime(const int32_t *stamp) { return break_time(stamp,0); }
wb_tm *wb_b_localtime(const int32_t *stamp) { return break_time(stamp,1); }
int32_t wb_b_mktime(wb_tm *t) {
    if(!t) return fail(EFAULT);
    struct tm native=import_tm(t); errno=0; time_t value=mktime(&native);
    if(value==(time_t)-1 && errno) { wb_errno_publish(); return -1; }
    if(value<INT32_MIN || value>INT32_MAX) return fail(EOVERFLOW);
    export_tm(&native,(int32_t)value,1,t); return (int32_t)value;
}
size_t wb_b_strftime(char *out,size_t capacity,const char *format,const wb_tm *t) {
    if(!t) { fail(EFAULT); return 0; }
    struct tm native=import_tm(t); return strftime(out,capacity,format,&native);
}
