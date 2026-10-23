#define _GNU_SOURCE
#include "wb_platform.h"
#include "wb_memory.h"
#include "wb_paths.h"
#include "wb_streams.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
static int manager;
typedef struct { FILE *file; int32_t length; } asset;
typedef struct { DIR *directory; char current[256]; } asset_directory;
void *wb_asset_manager(void) { return &manager; }
static void *from_java(void *env,void *object) {
    (void)env;
    return object ? &manager : NULL;
}
static void *asset_open(void *mgr,const char *name,int mode) {
    if(mgr!=&manager || mode<0 || mode>3) { errno=EINVAL; return NULL; }
    char path[WB_PATH_MAX]; int e=wb_path_resolve(name,path,sizeof(path));
    if(e) { errno=e; return NULL; }
    FILE *f=wb_stream_open(path,1); if(!f) return NULL;
    if(fseek(f,0,SEEK_END)) { wb_stream_close(f); return NULL; }
    long size=ftell(f);
    if(size<0 || size>INT32_MAX || fseek(f,0,SEEK_SET)) { wb_stream_close(f); return NULL; }
    asset *a=wb_malloc(sizeof(*a));
    if(!a) { wb_stream_close(f); return NULL; }
    a->file=f; a->length=(int32_t)size; return a;
}
static void asset_close(asset *a) {
    if(a) { wb_stream_close(a->file); wb_free(a); }
}
static int asset_read(asset *a,void *out,size_t n) {
    if(!a || !out || n>INT_MAX) return -1;
    size_t result=fread(out,1,n,a->file);
    return result || !ferror(a->file) ? (int)result : -1;
}
static int32_t asset_seek(asset *a,int32_t offset,int whence) {
    if(!a || fseek(a->file,offset,whence)) return -1;
    return (int32_t)ftell(a->file); /* AAsset_seek returns position, not fseek status. */
}
static void *asset_dir_open(void *mgr,const char *name) {
    if(mgr!=&manager || !name) return NULL;
    char path[WB_PATH_MAX];
    if(wb_path_resolve(name,path,sizeof(path))) return NULL;
    DIR *native=opendir(path); if(!native) return NULL;
    asset_directory *d=wb_calloc(1,sizeof(*d));
    if(!d) { closedir(native); return NULL; }
    d->directory=native; return d;
}
static const char *asset_dir_next(asset_directory *d) {
    if(!d) return NULL;
    struct dirent *n;
    while((n=readdir(d->directory))) {
        if(SCE_S_ISDIR(n->d_stat.st_mode)) continue;
        snprintf(d->current,sizeof(d->current),"%s",n->d_name);
        return d->current;
    }
    return NULL;
}
static void asset_dir_close(asset_directory *d) {
    if(d) { closedir(d->directory); wb_free(d); }
}
static const so_default_dynlib bindings[]={
    {"AAssetManager_fromJava",(uintptr_t)from_java},
    {"AAssetManager_open",(uintptr_t)asset_open},
    {"AAsset_close",(uintptr_t)asset_close},
    {"AAsset_read",(uintptr_t)asset_read},
    {"AAsset_seek",(uintptr_t)asset_seek},
    {"AAssetManager_openDir",(uintptr_t)asset_dir_open},
    {"AAssetDir_getNextFileName",(uintptr_t)asset_dir_next},
    {"AAssetDir_close",(uintptr_t)asset_dir_close},
};
const so_default_dynlib *wb_asset_bindings(size_t *count) {
    if(count) *count=sizeof(bindings)/sizeof(*bindings);
    return bindings;
}
