/* First-launch importer. Paths, hashes and OBB extents are compiled metadata;
 * copyrighted payloads are read only from user-supplied APK/OBB files.
 * Publish the ready marker last; completed files are rehashed on resume. */
#include "wb_setup.h"
#include "wb_convert.h"
#include "../src/wb_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MAKE_DIR(p) _mkdir(p)
#define SYNC_FILE(f) _commit(_fileno(f))
#else
#define MAKE_DIR(p) mkdir(p,0777)
#define SYNC_FILE(f) fsync(fileno(f))
#endif
#define CHUNK 65536u
#define LIMIT (8u*1024u*1024u)
/* Vita newlib defaults to 1 KiB stdio reads even when fread requests 64 KiB.
 * Give bulk streams an explicit bounded buffer to avoid millions of tiny
 * storage transactions. fclose owns/releases the buffer allocated by libc. */
static FILE *open_bulk(const char *path,const char *mode) {
    FILE *f=fopen(path,mode);
    if(f && setvbuf(f,NULL,_IOFBF,CHUNK)) { fclose(f);return NULL; }
    return f;
}
static uint16_t u16(const void *p) { const uint8_t *b=p;return b[0]|((uint16_t)b[1]<<8); }
static uint32_t u32(const void *p) { const uint8_t *b=p;return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24); }
static int error(wb_setup_result *r,const char *msg) { snprintf(r->message,sizeof(r->message),"%s",msg);return -1; }
static int path_join(char out[512],const char *root,const char *rel) {
    if(!root || !rel || !*rel || rel[0]=='/' || strchr(rel,'\\') || strchr(rel,':') || strstr(rel,"..")) return -1;
    int n=snprintf(out,512,"%s/%s",root,rel);return n<0 || n>=512 ? -1:0;
}
static int make_parents(const char *path) {
    char p[512];if(strlen(path)>=sizeof(p))return -1;strcpy(p,path);
    /* ux0: and Windows drive prefixes are devices, not directories. Never
     * stat/mkdir the bare prefix when walking an absolute destination. */
    char *start=strchr(p,':');start=start?start+1:p;
    if(*start=='/')++start;
    for(char *s=start;*s;++s) if(*s=='/') {
        *s=0;struct stat st;
        if(stat(p,&st)) { if(MAKE_DIR(p)) return -1; }
        else if(!S_ISDIR(st.st_mode))return -1;
        *s='/';
    }
    return 0;
}
static int hash_file(const char *path,uint8_t hash[32],uint64_t *size,wb_setup_progress progress,void *arg,const char *label) {
    FILE *f=open_bulk(path,"rb");if(!f)return -1;
    struct stat st;if(fstat(fileno(f),&st) || st.st_size<0 || (uint64_t)st.st_size>UINT32_MAX) { fclose(f);return -1; }
    uint8_t *buffer=malloc(CHUNK);if(!buffer) { fclose(f);return -1; }
    wb_sha256_state s;wb_sha256_init(&s);uint64_t total=0;int result=0;
    for(;;) {
        size_t n=fread(buffer,1,CHUNK,f);if(n) { wb_sha256_update(&s,buffer,n);total+=n; }
        if(progress && progress(label,(unsigned)total,(unsigned)st.st_size,arg)) { result=-2;break; }
        if(n<CHUNK) { if(ferror(f))result=-1;break; }
    }
    free(buffer);fclose(f);
    if(total!=(uint64_t)st.st_size && !result)result=-1;
    if(!result) { wb_sha256_final(&s,hash);if(size)*size=total; }
    return result;
}
static int matches(const char *path,const uint8_t expected[32],uint32_t size) {
    struct stat st;if(stat(path,&st) || st.st_size<0 || (uint64_t)st.st_size!=size || !S_ISREG(st.st_mode))return 0;
    uint8_t hash[32];return !hash_file(path,hash,NULL,NULL,NULL,NULL) && !memcmp(hash,expected,32);
}
static int marker_matches(const char *root,const char *name) {
    char path[512];if(path_join(path,root,name))return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    uint8_t value[41];size_t n=fread(value,1,sizeof(value),f);fclose(f);
    return n==40 && !memcmp(value,"WBSETUP1",8) && !memcmp(value+8,wb_setup_identity,32);
}
static int write_marker(const char *root,const char *name) {
    if(marker_matches(root,name))return 0;
    char path[512],tmp[512];if(path_join(path,root,name) || snprintf(tmp,sizeof(tmp),"%s.tmp",path)>=(int)sizeof(tmp))return -1;
    if(make_parents(tmp))return -1;
    FILE *f=fopen(tmp,"wb");if(!f)return -1;
    int ok=fwrite("WBSETUP1",1,8,f)==8 && fwrite(wb_setup_identity,1,32,f)==32;
    if(fflush(f) || SYNC_FILE(f))ok=0;
    if(fclose(f))ok=0;
    if(!ok)return -1;
    if(rename(tmp,path))return -1;
    return 0;
}
int wb_setup_ready(const char *root) {
    if(!marker_matches(root,".setup-ready"))return 0;
    char path[512];struct stat st;
    for(unsigned i=0;i<wb_setup_count;++i) if(wb_setup_files[i].kind==WB_LIBRARY || wb_setup_files[i].kind==WB_LOGO || wb_setup_files[i].kind==WB_LOADING) {
        if(path_join(path,root,wb_setup_files[i].path) || stat(path,&st) || st.st_size<0 || (uint64_t)st.st_size!=wb_setup_files[i].output_size)return 0;
    }
    return marker_matches(root,"assets/.prepared");
}
int wb_setup_find_inputs(const char *root,wb_setup_result *r) {
    char folder[512];if(path_join(folder,root,"import"))return error(r,"The import folder path is too long.");
    DIR *d=opendir(folder);if(!d)return error(r,"Place your APK and OBB in ux0:data/wonderboy/import/.");
    unsigned apks=0,obbs=0;r->apk[0]=r->obb[0]=0;struct dirent *e;
    while((e=readdir(d))) {
        const char *dot=strrchr(e->d_name,'.');if(!dot)continue;
        int apk=!strcasecmp(dot,".apk"),obb=!strcasecmp(dot,".obb");if(!apk && !obb)continue;
        char path[512];struct stat st;
        if(path_join(path,folder,e->d_name) || stat(path,&st) || !S_ISREG(st.st_mode))continue;
        if(apk) { ++apks;strcpy(r->apk,path); } else { ++obbs;strcpy(r->obb,path); }
    }
    closedir(d);
    if(apks!=1 || obbs!=1)return error(r,"Use exactly one APK and one OBB in the import folder.");
    return 0;
}
/* Read only two named members. CRC, bounds, size and pinned payload hashes are
 * verified; no ZIP filename is ever used as a destination path. */
static uint8_t *zip_member(const char *path,const char *name,uint32_t expected,uint32_t *length) {
    FILE *f=open_bulk(path,"rb");if(!f)return NULL;
    uint8_t *tail=NULL,*compressed=NULL,*out=NULL;int good=0;
    if(fseek(f,0,SEEK_END))goto done;
    long file_size=ftell(f);if(file_size<22 || file_size>512*1024*1024)goto done;
    unsigned n=(unsigned)file_size<65557?(unsigned)file_size:65557;tail=malloc(n);if(!tail)goto done;
    if(fseek(f,file_size-n,SEEK_SET) || fread(tail,1,n,f)!=n)goto done;
    const uint8_t *end=NULL;
    for(int i=(int)n-22;i>=0;--i)if(u32(tail+i)==0x06054b50 && (unsigned)i+22+u16(tail+i+20)==n) { end=tail+i;break; }
    if(!end || u16(end+4) || u16(end+6) || u16(end+8)!=u16(end+10) || u16(end+10)>10000)goto done;
    uint32_t cd=u32(end+16),cd_size=u32(end+12);unsigned count=u16(end+10);
    if((uint64_t)cd+cd_size>(uint64_t)file_size-n+(end-tail))goto done;
    uint32_t cursor=cd;
    for(unsigned i=0;i<count;++i) {
        uint8_t h[46];char entry[1025];
        if((uint64_t)cursor+46>cd+cd_size || fseek(f,cursor,SEEK_SET) || fread(h,1,46,f)!=46 || u32(h)!=0x02014b50)goto done;
        unsigned names=u16(h+28),extra=u16(h+30),comment=u16(h+32);
        if(names>1024 || (uint64_t)cursor+46+names+extra+comment>cd+cd_size || fread(entry,1,names,f)!=names)goto done;
        entry[names]=0;cursor+=46+names+extra+comment;
        if(strlen(entry)!=names || strcmp(entry,name))continue;
        unsigned flags=u16(h+8),method=u16(h+10);uint32_t packed=u32(h+20),plain=u32(h+24),crc=u32(h+16),local=u32(h+42);
        if(flags&1 || (method!=0 && method!=8) || plain!=expected || plain>LIMIT || packed>LIMIT || u16(h+34))goto done;
        uint8_t lh[30];
        if((uint64_t)local+30>cd || fseek(f,local,SEEK_SET) || fread(lh,1,30,f)!=30 || u32(lh)!=0x04034b50 ||
           u16(lh+6)!=flags || u16(lh+8)!=method || u16(lh+26)!=names)goto done;
        if(fread(entry,1,names,f)!=names || memcmp(entry,name,names))goto done;
        uint64_t data=(uint64_t)local+30+names+u16(lh+28);if(data+packed>cd)goto done;
        compressed=malloc(packed?packed:1);out=malloc(plain?plain:1);if(!compressed || !out)goto done;
        if(fseek(f,(long)data,SEEK_SET) || fread(compressed,1,packed,f)!=packed)goto done;
        if(method==0) { if(packed!=plain)goto done;memcpy(out,compressed,plain); }
        else {
            z_stream z={0};z.next_in=compressed;z.avail_in=packed;z.next_out=out;z.avail_out=plain;
            if(inflateInit2(&z,-MAX_WBITS)!=Z_OK)goto done;
            int code=inflate(&z,Z_FINISH);int valid=code==Z_STREAM_END && z.total_in==packed && z.total_out==plain;
            inflateEnd(&z);if(!valid)goto done;
        }
        if((uint32_t)crc32(0,out,plain)!=crc)goto done;
        *length=plain;good=1;break;
    }
done:free(tail);free(compressed);fclose(f);if(!good) { free(out);return NULL; }return out;
}
static int source_read(FILE *obb,const wb_setup_file *entry,uint8_t *data,FILE *out) {
    uint8_t buffer[CHUNK];uint32_t total=0;wb_sha256_state state;wb_sha256_init(&state);
    for(unsigned i=0;i<entry->extent_count;++i) {
        wb_setup_extent e=wb_setup_extents[entry->extent_start+i];
        if((uint64_t)e.offset+e.length>wb_setup_obb_size || e.length>entry->source_size-total || fseek(obb,e.offset,SEEK_SET))return -1;
        uint32_t left=e.length;
        while(left) {
            size_t n=left>CHUNK?CHUNK:left;uint8_t *p=data?data+total:buffer;
            if(fread(p,1,n,obb)!=n)return -1;
            wb_sha256_update(&state,p,n);
            if(out && fwrite(p,1,n,out)!=n)return -1;
            total+=n;left-=n;
        }
    }
    uint8_t hash[32];wb_sha256_final(&state,hash);
    return total==entry->source_size && !memcmp(hash,entry->source_hash,32)?0:-1;
}
static int shader(FILE *out,const wb_setup_file *e,const uint8_t *data) {
    unsigned cursor=0;
    for(unsigned j=0;j<e->edit_count;++j) {
        const wb_setup_edit *edit=&wb_setup_edits[e->edit_start+j];
        if(edit->offset<cursor || edit->offset>e->source_size || edit->remove>e->source_size-edit->offset)return -1;
        size_t n=edit->offset-cursor,m=strlen(edit->insert);
        if(fwrite(data+cursor,1,n,out)!=n || fwrite(edit->insert,1,m,out)!=m)return -1;
        cursor=edit->offset+edit->remove;
    }
    size_t n=e->source_size-cursor;return fwrite(data+cursor,1,n,out)==n?0:-1;
}
int wb_setup_run(const char *root,const char *apk,const char *obb_path,wb_setup_progress progress,void *arg,wb_setup_result *r) {
    r->completed=r->reused=0;r->message[0]=0;
    if(strlen(apk)>=sizeof(r->apk) || strlen(obb_path)>=sizeof(r->obb))return error(r,"Input path is too long.");
    /* Callers may pass these fields back to retry; memmove handles aliasing. */
    memmove(r->apk,apk,strlen(apk)+1);memmove(r->obb,obb_path,strlen(obb_path)+1);
    uint8_t hash[32];uint64_t bytes;int check=hash_file(obb_path,hash,&bytes,progress,arg,"Checking OBB");
    if(check==-2)return error(r,"Setup paused. Press START to resume.");
    if(check || bytes!=wb_setup_obb_size || memcmp(hash,wb_setup_obb_hash,32))
        return error(r,"Unsupported OBB. Use the matching Android 1.1.0 game data.");
    if(hash_file(apk,r->apk_hash,&bytes,progress,arg,"Checking APK"))return error(r,"Could not read the APK, or setup was paused.");
    /* Validate both APK members before writing any installation data. */
    for(unsigned i=0;i<2;++i) {
        uint32_t n;const wb_setup_file *e=&wb_setup_files[i];
        uint8_t *p=zip_member(apk,i?"res/drawable/splashscreen.png":"lib/armeabi-v7a/libwb.so",e->source_size,&n);
        if(!p)return error(r,"Unsupported or damaged APK. Use Android 1.1.0 ARMv7.");
        wb_sha256(p,n,hash);free(p);
        if(memcmp(hash,e->source_hash,32))return error(r,"APK contents do not match the supported Android 1.1.0 ARMv7 release.");
    }
    int resume=marker_matches(root,".setup-in-progress");
    char target[512],tmp[512];
    if(!resume) {
        /* Preserve existing files that are not an output of this importer. */
        for(unsigned i=0;i<wb_setup_count;++i) {
            if(path_join(target,root,wb_setup_files[i].path))return error(r,"Invalid installation path.");
            struct stat st;
            if(!stat(target,&st) && !matches(target,wb_setup_files[i].output_hash,wb_setup_files[i].output_size))
                return error(r,"Different game data already exists. Keep it backed up before using a fresh installation folder.");
        }
        if(write_marker(root,".setup-in-progress"))return error(r,"Could not start setup. Check storage space and write access.");
    }
    FILE *obb=open_bulk(obb_path,"rb");if(!obb)return error(r,"Could not reopen the OBB.");
    int failed=0;
    for(unsigned i=0;i<wb_setup_count;++i) {
        const wb_setup_file *e=&wb_setup_files[i];
        if(progress && progress(e->path,i,wb_setup_count,arg)) { error(r,"Setup paused. Press START to resume.");failed=1;break; }
        if(path_join(target,root,e->path)) { failed=1;break; }
        if(matches(target,e->output_hash,e->output_size)) { ++r->completed;++r->reused;continue; }
        if(snprintf(tmp,sizeof(tmp),"%s.install-tmp",target)>=(int)sizeof(tmp) || make_parents(tmp)) { failed=1;break; }
        FILE *out=open_bulk(tmp,"wb");if(!out) { failed=1;break; }
        int result=-1;uint8_t *data=NULL;
        if(e->kind==WB_COPY) result=source_read(obb,e,NULL,out);
        else {
            if(e->source_size<=LIMIT) {
                uint32_t n=0;
                if(e->kind==WB_LIBRARY || e->kind==WB_LOADING)
                    data=zip_member(apk,e->kind==WB_LIBRARY?"lib/armeabi-v7a/libwb.so":"res/drawable/splashscreen.png",e->source_size,&n);
                else { data=malloc(e->source_size?e->source_size:1);if(data && !source_read(obb,e,data,NULL))n=e->source_size; }
                if(data && n==e->source_size) {
                    /* source_read already checked OBB payload hashes. Only
                     * ZIP members still need their pinned hash here. */
                    int zip=e->kind==WB_LIBRARY || e->kind==WB_LOADING;
                    if(zip)wb_sha256(data,n,hash);
                    if(!zip || !memcmp(hash,e->source_hash,32)) {
                        if(e->kind==WB_LIBRARY)result=fwrite(data,1,n,out)==n?0:-1;
                        else if(e->kind==WB_KTX)result=wb_convert_ktx(data,n,out);
                        else if(e->kind==WB_SHADER)result=shader(out,e,data);
                        else result=wb_convert_png(data,n,out,e->kind==WB_LOADING);
                    }
                }
            }
        }
        free(data);if(fflush(out) || SYNC_FILE(out))result=-1;if(fclose(out))result=-1;
        if(result || !matches(tmp,e->output_hash,e->output_size)) { failed=1;break; }
        /* Only a known output owned by this in-progress transaction can be replaced. */
        struct stat st;if(!stat(target,&st) && (!S_ISREG(st.st_mode) || remove(target))) { failed=1;break; }
        if(rename(tmp,target)) { failed=1;break; }
        ++r->completed;
    }
    fclose(obb);
    if(failed) { if(!r->message[0])snprintf(r->message,sizeof(r->message),"Setup stopped at file %u. Check free space; press START to resume.",r->completed+1);return -1; }
    if(write_marker(root,"assets/.prepared") || write_marker(root,".setup-ready"))return error(r,"Could not finish setup. Press START to resume.");
    snprintf(r->message,sizeof(r->message),"Setup complete. All %u files verified.",wb_setup_count);
    return 0;
}
int wb_setup_cleanup(const char *root,wb_setup_result *r) {
    if(!wb_setup_ready(root) || r->completed!=wb_setup_count)return error(r,"Finish and verify setup before deleting the installer files.");
    char installed[512];
    for(unsigned i=0;i<wb_setup_count;++i)
        if(path_join(installed,root,wb_setup_files[i].path) ||
           !matches(installed,wb_setup_files[i].output_hash,wb_setup_files[i].output_size))
            return error(r,"Prepared data failed verification. The APK and OBB have been kept.");
    char folder[512];if(path_join(folder,root,"import"))return -1;size_t len=strlen(folder);
    for(unsigned i=0;i<2;++i) {
        const char *p=i?r->obb:r->apk;
        if(strncmp(p,folder,len) || p[len]!='/' || !p[len+1] || strchr(p+len+1,'/') || strchr(p+len+1,'\\') || strchr(p+len+1,':'))
            return error(r,"Only the APK and OBB selected from the import folder can be deleted.");
    }
    uint8_t hash[32];uint64_t size;
    if(hash_file(r->apk,hash,&size,NULL,NULL,NULL) || memcmp(hash,r->apk_hash,32) ||
       hash_file(r->obb,hash,&size,NULL,NULL,NULL) || size!=wb_setup_obb_size || memcmp(hash,wb_setup_obb_hash,32))
        return error(r,"Installer files changed or are unavailable. They have been kept.");
    if(remove(r->apk) || remove(r->obb))return error(r,"Setup is complete, but an installer file could not be deleted.");
    snprintf(r->message,sizeof(r->message),"APK and OBB deleted. The game is ready.");return 0;
}
