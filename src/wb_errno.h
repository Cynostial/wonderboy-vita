#ifndef WB_ERRNO_H
#define WB_ERRNO_H
int wb_bionic_error(int native_error);
int *wb_errno_location(void);
void wb_errno_publish(void);
#endif
