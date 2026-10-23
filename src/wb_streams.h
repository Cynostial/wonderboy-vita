#ifndef WB_STREAMS_H
#define WB_STREAMS_H
#include <stdio.h>
#include <stddef.h>

#define WB_STREAM_LIMIT 32
#define WB_STREAM_BUFFER_BYTES (64u * 1024u)
/* Call only through these paired functions. Buffers stay alive through fclose.
 * Each FILE has one owner; concurrent operations on the same FILE are invalid. */
FILE *wb_stream_open(const char *path, int buffered);
/* Best-effort read buffer for a newly opened FILE, before any I/O. Shares the
 * 2 MiB pool with AndroidFile streams. Failure leaves the FILE usable. */
void wb_stream_buffer_read(FILE *file);
int wb_stream_close(FILE *file);
FILE *wb_stream_reopen(const char *path,const char *mode,FILE *file);
size_t wb_stream_buffer_bytes(void);
#endif
