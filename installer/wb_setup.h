#pragma once
#include <stdint.h>
#include <stddef.h>
enum { WB_COPY,WB_KTX,WB_LOGO,WB_LOADING,WB_SHADER,WB_LIBRARY };
typedef struct { uint32_t offset,length; } wb_setup_extent;
typedef struct { uint32_t offset,remove;const char *insert; } wb_setup_edit;
typedef struct {
    const char *path;uint32_t kind,source_size,output_size;
    uint8_t source_hash[32],output_hash[32];
    uint32_t extent_start,extent_count,edit_start,edit_count;
} wb_setup_file;
extern const wb_setup_file wb_setup_files[];
extern const unsigned wb_setup_count;
extern const wb_setup_extent wb_setup_extents[];
extern const wb_setup_edit wb_setup_edits[];
extern const uint8_t wb_setup_obb_hash[32],wb_setup_identity[32];
extern const uint32_t wb_setup_obb_size;
extern const uint64_t wb_setup_output_bytes;
/* Progress returns nonzero only for a user's cancellation request. */
typedef int (*wb_setup_progress)(const char *,unsigned,unsigned,void *);
typedef struct { char message[256];char apk[512],obb[512];uint8_t apk_hash[32];unsigned completed,reused; } wb_setup_result;
int wb_setup_run(const char *,const char *,const char *,wb_setup_progress,void *,wb_setup_result *);
int wb_setup_ready(const char *);
int wb_setup_cleanup(const char *,wb_setup_result *);
int wb_setup_find_inputs(const char *,wb_setup_result *);
void wb_setup_on_vita(void);
