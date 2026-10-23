/* AndroidFileMgr owns and closes the supplied FILE, and returns a guest-heap
 * allocation released by its original delete[]/release path. */
#include "wb_file_load.h"
#include "wb_memory.h"
#include <errno.h>
#include <stdio.h>

void *wb_file_load_open(void *self,const uint32_t *name,uint32_t *out_size,
                       int32_t requested,wb_file *file) {
    (void)self;(void)name;
    if(out_size) *out_size=0;
    if(!file) return NULL;
    unsigned char *data=NULL;
    int32_t length=requested;
    if(requested==-1) {
        if(wb_fseek(file,0,SEEK_END)) goto failed;
        length=wb_ftell(file);
        if(length<0 || wb_fseek(file,0,SEEK_SET)) goto failed;
    }
    if(length<0) { errno=EINVAL;goto failed; }
    data=wb_malloc((size_t)length+1);
    if(!data) {
        wb_memory_stats s=wb_memory_get_stats();
        fprintf(stderr,"File allocation failed: request=%lu live=%u peak=%u capacity=%u failures=%u\n",
                (unsigned long)length+1,(unsigned)s.live_bytes,(unsigned)s.peak_bytes,
                (unsigned)s.capacity,(unsigned)s.failed_requests);
        goto failed;
    }
    if(wb_fread(data,1,(size_t)length,file)!=(size_t)length || wb_ferror(file)) {
        errno=EIO;goto failed;
    }
    data[length]=0;
    if(wb_fclose(file)) { wb_free(data);return NULL; }
    if(out_size) *out_size=(uint32_t)length;
    return data;
failed:
    wb_free(data);
    wb_fclose(file);
    return NULL;
}
void *wb_file_load_named(void *self,const char *name,uint32_t *out_size) {
    void *data=wb_file_load_open(self,NULL,out_size,-1,wb_fopen(name,"rb"));
    if(!data) fprintf(stderr,"File load failed: %s\n",name ? name : "(null)");
    return data;
}
