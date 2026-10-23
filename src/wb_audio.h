#ifndef WB_AUDIO_H
#define WB_AUDIO_H
#include <stdint.h>
typedef struct {
    void (*stream_construct)(void *);
    int (*stream_load)(void *,unsigned char *,unsigned,int,int);
    int (*wav_load)(void *,unsigned char *,unsigned,int,int);
    uintptr_t stream_vtable;
    int (*vorbis_rewind)(void *);
} wb_audio_api;
int wb_audio_bind(const wb_audio_api *);
void wb_audio_load_sample(void *);
double wb_audio_length(void *);
int wb_audio_rewind(void *);
#endif
