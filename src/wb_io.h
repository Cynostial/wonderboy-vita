#ifndef WB_IO_H
#define WB_IO_H
#include <stddef.h>
#include <stdint.h>
/* Android ARM stat uses stat64's layout even for the stat/fstat imports.
 * Explicit padding preserves the ABI on host compilers used for verification. */
typedef struct {
    uint64_t device;
    uint32_t pad0,legacy_inode,mode,links,uid,gid;
    uint64_t rdevice;
    uint32_t pad1,pad2;
    int64_t size;
    uint32_t block_size,pad3;
    uint64_t blocks;
    uint32_t accessed,accessed_ns,modified,modified_ns,changed,changed_ns;
    uint64_t inode;
} wb_android_stat;
_Static_assert(sizeof(wb_android_stat)==104,"Android ARM stat size");
_Static_assert(offsetof(wb_android_stat,size)==48,"Android ARM stat size offset");
_Static_assert(offsetof(wb_android_stat,blocks)==64,"Android ARM stat blocks offset");
_Static_assert(offsetof(wb_android_stat,inode)==96,"Android ARM stat inode offset");
/* Return ordinary POSIX results/native errno. Platform bindings translate errno. */
int wb_io_flags(int android_flags,int *native_flags);
int wb_io_open(const char *,int,unsigned mode);
int wb_io_stat(const char *,wb_android_stat *);
int wb_io_fstat(int,wb_android_stat *);
int wb_io_mkdir(const char *,unsigned);
int wb_io_unlink(const char *);
int wb_io_rename(const char *,const char *);
int wb_io_access(const char *,int);
#endif
