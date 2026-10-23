#include "wb_sdl_threads.h"
#include "wb_memory.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

/* The pinned guest imports only CreateThread, WaitThread and ThreadID.
 * Its opaque handles never reach native SDL thread-object functions.
 * Vita SDL creates bare kernel threads; PTE's cancellable waits require the
 * per-thread state established by pthread_create, not merely pthread_self.
 */
typedef struct {
    pthread_t native;
    SDL_ThreadFunction function;
    void *argument;
    int result;
    char name[32];
} worker;

static void *run_worker(void *argument) {
    worker *w=argument;
    fprintf(stderr,"Game worker started: %s, entry %p\n",w->name,(void *)(uintptr_t)w->function);
    w->result=w->function(w->argument);
    fprintf(stderr,"Game worker finished: %s, entry %p, result %d\n",
            w->name,(void *)(uintptr_t)w->function,w->result);
    /* SDL errors and TLS keys may have been used inside guest code. Their
     * destructors must run before pthread tears down native TLS. */
    SDL_TLSCleanup();
    return NULL;
}

SDL_Thread *wb_sdl_create_thread(SDL_ThreadFunction function,const char *name,void *argument) {
    if(!function) { SDL_SetError("Missing game thread entry point");return NULL; }
    worker *w=wb_calloc(1,sizeof(*w));
    if(!w) { SDL_SetError("Cannot allocate game thread handle");return NULL; }
    w->function=function;w->argument=argument;
    snprintf(w->name,sizeof(w->name),"%s",name ? name : "Wonder Boy worker");
    pthread_attr_t attributes;
    int error=pthread_attr_init(&attributes);
    if(!error) {
        error=pthread_attr_setstacksize(&attributes,1024u*1024u);
        if(!error) error=pthread_create(&w->native,&attributes,run_worker,w);
        pthread_attr_destroy(&attributes);
    }
    if(error) {
        wb_free(w);SDL_SetError("Cannot create game worker (pthread error %d)",error);return NULL;
    }
    return (SDL_Thread *)w;
}

void wb_sdl_wait_thread(SDL_Thread *thread,int *status) {
    if(!thread) return;
    worker *w=(worker *)thread;
    int error=pthread_join(w->native,NULL);
    if(error) {
        /* Keep the handle: the worker may still own it after a failed join. */
        if(status) *status=-1;
        SDL_SetError("Cannot join game worker (pthread error %d)",error);return;
    }
    if(status) *status=w->result;
    wb_free(w);
}
