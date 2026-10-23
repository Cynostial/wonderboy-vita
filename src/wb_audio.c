/* Pinned guest ABI, verified with Ghidra and an actual ARM decoder replay.
 * Only long Atmos OGG sources change type. SoLoud's virtual instance and
 * destruction paths support both sources; the nonvirtual Wav length query
 * needs the type-aware adapter below. Never reinterpret a 100-byte Wav as
 * a 124-byte WavStream.
 */
#include "wb_audio.h"
#include "wb_file_load.h"
#include "wb_memory.h"
#include "wb_paths.h"
#include <stdio.h>
#include <string.h>

static wb_audio_api api;
_Static_assert(sizeof(void *)==4,"Pinned ARMv7 audio layout");
static void *pointer(const void *object,unsigned offset) {
    void *p;memcpy(&p,(const unsigned char *)object+offset,4);return p;
}
static void destroy(void *source) {
    if(source) {
        void **vtable=pointer(source,0);
        ((void (*)(void *))vtable[2])(source); /* deleting virtual destructor */
    }
}
static int vorbis_header(const unsigned char *data,uint32_t bytes) {
    if(bytes<58 || memcmp(data,"OggS",4) || data[4] || !data[26]) return 0;
    unsigned offset=27u+data[26];
    if(offset>bytes || bytes-offset<30 || data[27]<30 ||
       memcmp(data+offset,"\1vorbis",7)) return 0;
    uint32_t version,rate;
    memcpy(&version,data+offset+7,4);
    memcpy(&rate,data+offset+12,4);
    return !version && data[offset+11]>=1 && data[offset+11]<=2 &&
           rate>=8000 && rate<=192000;
}
int wb_audio_bind(const wb_audio_api *value) {
    if(!value || !value->stream_construct || !value->stream_load ||
       !value->wav_load || !value->stream_vtable || !value->vorbis_rewind) return -1;
    api=*value;
    return 0;
}
void wb_audio_load_sample(void *sample) {
    unsigned char *s=sample;
    const char *name=pointer(s,0);
    void *old=pointer(s,0x50);
    s[0x54]=1;
    if(!name || !old) return;
    char path[WB_PATH_MAX];
    int n=snprintf(path,sizeof(path),WB_ASSETS "/sfx/%s",name);
    if(n<0 || (size_t)n>=sizeof(path)) return;
    uint32_t bytes=0;
    unsigned char *data=wb_file_load_named(NULL,path,&bytes);
    if(!data) return;
    size_t length=strlen(name);
    int stream=!strncmp(name,"Atmos/",6) && length>4 &&
               !strcmp(name+length-4,".ogg");
    if(stream) {
        /* Verified largest ambience input is 600,944 bytes. Bound retained
         * compressed data; decode state is allocated only for active voices. */
        if(bytes>1024u*1024u || !vorbis_header(data,bytes)) {
            wb_free(data);return;
        }
        void *replacement=wb_malloc(0x7c);
        if(!replacement) { wb_free(data);return; }
        api.stream_construct(replacement);
        int result=api.stream_load(replacement,data,bytes,1,0);
        wb_free(data);
        if(result) { destroy(replacement);return; }
        /* Preserve source configuration, including loop/volume/filter flags.
         * Sample rate and channel count come from the parsed stream. The
         * original object is an inactive preload source, never a live voice. */
        memcpy((unsigned char *)replacement+4,(unsigned char *)old+4,4);
        memcpy((unsigned char *)replacement+12,(unsigned char *)old+12,4);
        memcpy((unsigned char *)replacement+20,(unsigned char *)old+20,0x5c-20);
        destroy(old);
        memcpy(s+0x50,&replacement,4);
        s[0x54]=0;
        wb_memory_stats memory=wb_memory_get_stats();
        fprintf(stderr,"Streamed ambience %s: compressed=%u arena=%u peak=%u failures=%u\n",
                name,bytes,(unsigned)memory.live_bytes,(unsigned)memory.peak_bytes,
                (unsigned)memory.failed_requests);
    } else {
        s[0x54]=(unsigned char)(api.wav_load(old,data,bytes,1,0)!=0);
        wb_free(data);
    }
}
double wb_audio_length(void *source) {
    float rate;uint32_t samples;
    memcpy(&rate,(unsigned char *)source+8,4);
    unsigned offset=(uintptr_t)pointer(source,0)==api.stream_vtable ? 0x74 : 0x60;
    memcpy(&samples,(unsigned char *)source+offset,4);
    return rate==0.0f ? 0.0 : (double)((float)samples/rate);
}
int wb_audio_rewind(void *instance) {
    unsigned char *s=instance;
    void *decoder=pointer(s,0x1a4);
    if(decoder) {
        if(!api.vorbis_rewind(decoder)) return 3;
        /* The pinned implementation rewinds stb but retains its old decoded
         * frame cursor. Discard that frame before reading from sample zero. */
        uint32_t zero=0;
        memcpy(s+0x1a8,&zero,4);
        memcpy(s+0x1ac,&zero,4);
        memcpy(s+0x1b0,&zero,4);
    } else {
        void *file=pointer(s,0x1a0);
        if(file) {
            void **vtable=pointer(file,0);
            uint32_t offset;
            memcpy(&offset,(unsigned char *)pointer(s,0x198)+0x6c,4);
            ((void (*)(void *,int))vtable[5])(file,(int)offset);
        }
    }
    uint32_t zero=0;
    memcpy(s+0x48,&zero,4);memcpy(s+0x4c,&zero,4);
    memcpy(s+0x19c,&zero,4);
    return 0;
}
