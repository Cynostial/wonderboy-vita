/* Read only the current title's cached artwork through the Vita filesystem.
 * USB exposes ux0 appmeta as PFS ciphertext. Never patch those encrypted files
 * or app.db from the host. Keep one bounded copy per package revision in ux0
 * so the normal game run can supply evidence without a separate test app. */
#include "wb_paths.h"
#include <psp2/io/fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#define CACHE_OUT WB_ROOT "/shell-cache-00.18"
void wb_shell_cache_capture(void) {
    if(!access(CACHE_OUT "/complete",F_OK)) return;
    mkdir(CACHE_OUT,0777);
    static const char *roots[]={"ur0:appmeta/WBDT00001/livearea/contents",
                                "ux0:appmeta/WBDT00001/livearea/contents"};
    static const char *names[]={"template.xml","bg.png","startup.png"};
    unsigned char bytes[4096];char source[128],destination[128];
    for(unsigned r=0;r<2;++r) for(unsigned n=0;n<3;++n) {
        snprintf(source,sizeof(source),"%s/%s",roots[r],names[n]);
        int fd=sceIoOpen(source,SCE_O_RDONLY,0);
        if(fd<0) { fprintf(stderr,"Shell cache: %s open=0x%08x\n",source,(unsigned)fd);continue; }
        snprintf(destination,sizeof(destination),CACHE_OUT "/%s-%s",r ? "ux0" : "ur0",names[n]);
        FILE *out=fopen(destination,"wb");
        if(!out) { sceIoClose(fd);continue; }
        unsigned total=0;int got=0;
        while(total<512*1024 && (got=sceIoRead(fd,bytes,sizeof(bytes)))>0) {
            if(fwrite(bytes,1,(unsigned)got,out)!=(unsigned)got) { got=-1;break; }
            total+=(unsigned)got;
        }
        int saved=fclose(out);sceIoClose(fd);
        fprintf(stderr,"Shell cache: %s read=%u status=%d saved=%d\n",source,total,got,saved);
    }
    FILE *done=fopen(CACHE_OUT "/complete","wb");
    if(done) fclose(done);
}
