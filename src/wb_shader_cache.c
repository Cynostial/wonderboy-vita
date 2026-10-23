/* Render-thread only. Cache whole linked programs so pair-specific varying
 * semantics and attribute locations cannot be mixed between shader stages.
 * The directory namespace is generated from the exact linked VitaGL archive.
 * Corrupt, stale, unsupported and oversized entries take the normal linker.
 */
#include "wb_shader_cache.h"
#include "wb_sha256.h"
#include <vitaGL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define HANDLES 512
#define MAX_SOURCE (256u*1024u)
#define MAX_BINARY (1024u*1024u)
static struct { uint8_t hash[32]; GLenum type; int valid; } shaders[HANDLES];
static struct { uint8_t attributes[32]; int valid; } programs[HANDLES];
static char directory[192];
void wb_shader_cache_directory(const char *path) {
    if(path && strlen(path)<sizeof(directory)) strcpy(directory,path);
    else directory[0]=0;
}
static uint32_t word(const unsigned char *p) {
    uint32_t n;memcpy(&n,p,4);return n;
}
static int shader_valid(const unsigned char *b,size_t n) {
    if(n<4) return 0;
    unsigned matrices=word(b);
    if(matrices>256 || n<4u+matrices*4u+156u) return 0;
    size_t start=4u+matrices*4u,bytes=n-start;
    const unsigned char *g=b+start;
    if(word(g)!=0x00505847 || word(g+8)>bytes || bytes-word(g+8)>3) return 0;
    unsigned params=word(g+36),relative=word(g+40);
    if(relative>bytes-40) return 0;
    size_t offset=40u+relative;
    if(params>(bytes-offset)/16) return 0;
    for(unsigned i=0;i<matrices;++i) if(word(b+4+i*4)>=params) return 0;
    for(unsigned i=0;i<params;++i) {
        size_t at=offset+i*16;
        int64_t name=(int64_t)at+(int32_t)word(g+at);
        if(name<0 || (uint64_t)name>=bytes || !memchr(g+name,0,bytes-(size_t)name)) return 0;
    }
    return 1;
}
int wb_shader_binary_valid(const unsigned char *b,size_t n) {
    /* Pinned VitaGL: 16 eight-byte SceGxmVertexAttributes; no
     * HAVE_GLSL_TEXTURE_SIZE. glProgramBinary itself trusts all offsets. */
    if(!b || n<132+8 || n>MAX_BINARY || word(b)>16) return 0;
    size_t offset=132;
    for(unsigned stage=0;stage<2;++stage) {
        if(n-offset<4) return 0;
        size_t size=word(b+offset);offset+=4;
        if(size>n-offset || !shader_valid(b+offset,size)) return 0;
        offset+=size;
    }
    return offset==n;
}
static GLuint create_shader(GLenum type) {
    GLuint id=glCreateShader(type);
    if(id && id<HANDLES) {
        memset(&shaders[id],0,sizeof(shaders[id]));
        shaders[id].type=type;
    }
    return id;
}
static GLuint create_program(void) {
    GLuint id=glCreateProgram();
    if(id && id<HANDLES) {
        memset(&programs[id],0,sizeof(programs[id]));
        programs[id].valid=1;
    }
    return id;
}
static void source(GLuint id,GLsizei count,const GLchar *const *strings,const GLint *lengths) {
    glShaderSource(id,count,strings,lengths);
    if(!id || id>=HANDLES) return;
    shaders[id].valid=0;
    if(count<=0 || !strings) return;
    size_t total=0;
    for(GLsizei i=0;i<count;++i) {
        if(!strings[i]) return;
        size_t n=lengths && lengths[i]>=0 ? (size_t)lengths[i] : strlen(strings[i]);
        if(n>MAX_SOURCE-total) return;
        total+=n;
    }
    unsigned char *all=malloc(total ? total : 1);
    if(!all) return;
    size_t offset=0;
    for(GLsizei i=0;i<count;++i) {
        size_t n=lengths && lengths[i]>=0 ? (size_t)lengths[i] : strlen(strings[i]);
        memcpy(all+offset,strings[i],n);offset+=n;
    }
    wb_sha256(all,total,shaders[id].hash);free(all);
    shaders[id].valid=1;
}
static void bind_attribute(GLuint id,GLuint index,const GLchar *name) {
    glBindAttribLocation(id,index,name);
    if(!id || id>=HANDLES || !programs[id].valid) return;
    /* Hash the ordered bind history. Redundant/different order may miss the
     * cache but can never incorrectly share a binding layout. */
    if(!name || strlen(name)>255) { programs[id].valid=0;return; }
    unsigned char input[32+4+256];
    memcpy(input,programs[id].attributes,32);memcpy(input+32,&index,4);
    size_t size=strlen(name)+1;memcpy(input+36,name,size);
    wb_sha256(input,36+size,programs[id].attributes);
}
static int key_for(GLuint id,uint8_t key[32]) {
    if(!directory[0] || !id || id>=HANDLES || !programs[id].valid) return 0;
    GLuint attached[2];GLsizei count=0;int seen=0;
    glGetAttachedShaders(id,2,&count,attached);
    if(count!=2) return 0;
    uint8_t input[96];
    for(unsigned i=0;i<2;++i) {
        GLuint s=attached[i];
        if(!s || s>=HANDLES || !shaders[s].valid) return 0;
        int stage=shaders[s].type==GL_VERTEX_SHADER ? 0 :
                  shaders[s].type==GL_FRAGMENT_SHADER ? 1 : -1;
        if(stage<0 || (seen&(1<<stage))) return 0;
        seen|=1<<stage;memcpy(input+stage*32,shaders[s].hash,32);
    }
    memcpy(input+64,programs[id].attributes,32);wb_sha256(input,sizeof(input),key);
    return 1;
}
typedef struct { uint32_t magic,size;uint8_t key[32],checksum[32]; } header;
static void cache_path(char path[272],const uint8_t key[32]) {
    char hex[65];
    for(unsigned i=0;i<32;++i) snprintf(hex+i*2,3,"%02x",key[i]);
    snprintf(path,272,"%s/%s.bin",directory,hex);
}
static int load(GLuint id,const char *path,const uint8_t key[32]) {
    FILE *f=fopen(path,"rb");if(!f) return 0;
    header h;
    if(fread(&h,1,sizeof(h),f)!=sizeof(h) || h.magic!=0x31434257 ||
       h.size>MAX_BINARY || h.size<140 || memcmp(h.key,key,32)) { fclose(f);return 0; }
    unsigned char *data=malloc(h.size);if(!data) { fclose(f);return 0; }
    int ok=fread(data,1,h.size,f)==h.size && fgetc(f)==EOF && !ferror(f);
    fclose(f);
    uint8_t checksum[32]={0};
    if(ok) wb_sha256(data,h.size,checksum);
    ok=ok && !memcmp(checksum,h.checksum,32) && wb_shader_binary_valid(data,h.size);
    if(ok) {
        GLuint original[2];GLsizei count=0;
        glGetAttachedShaders(id,2,&count,original);
        /* Retain even shaders already marked for deferred deletion, so a
         * failed binary link can safely restore and compile the originals. */
        GLuint hold=glCreateProgram();
        if(!hold) ok=0;
        else {
            for(int i=0;i<count;++i) glAttachShader(hold,original[i]);
            glProgramBinary(id,0,data,(GLsizei)h.size);
            GLint linked=0;glGetProgramiv(id,GL_LINK_STATUS,&linked);ok=linked!=0;
            if(!ok) for(int i=0;i<count;++i) glAttachShader(id,original[i]);
            glDeleteProgram(hold);
        }
    }
    free(data);return ok;
}
static int save(GLuint id,const char *path,const uint8_t key[32]) {
    GLint size=0;glGetProgramiv(id,GL_PROGRAM_BINARY_LENGTH,&size);
    if(size<140 || size>(GLint)MAX_BINARY) {
        fprintf(stderr,"Shader cache skipped program %u: binary size %d\n",id,size);return 0;
    }
    unsigned char *data=malloc((size_t)size);if(!data) return 0;
    GLsizei actual=0;GLenum format=0;
    glGetProgramBinary(id,size,&actual,&format,data);
    if(actual!=size || !wb_shader_binary_valid(data,(size_t)size)) {
        fprintf(stderr,"Shader cache rejected program %u: size %d returned %d attributes %u\n",
                id,size,actual,word(data));
        size_t offset=132;
        for(unsigned stage=0;stage<2 && offset+8<=(size_t)size;++stage) {
            unsigned bytes=word(data+offset),matrices=word(data+offset+4);
            fprintf(stderr,"Shader cache stage %u: bytes %u matrices %u\n",stage,bytes,matrices);
            if(bytes>(size_t)size-offset-4) break;
            if(matrices<256 && bytes>=4+4*matrices+44) {
                const unsigned char *g=data+offset+8+4*matrices;
                fprintf(stderr,"Shader cache GXP: magic %x declared %u params %u relative %u\n",
                        word(g),word(g+8),word(g+36),word(g+40));
            }
            offset+=4u+bytes;
        }
        free(data);return 0;
    }
    header h={.magic=0x31434257,.size=(uint32_t)size};
    memcpy(h.key,key,32);wb_sha256(data,(size_t)size,h.checksum);
    char temporary[280];snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    int saved=0;
    FILE *f=fopen(temporary,"wb");
    if(f) {
        int ok=fwrite(&h,1,sizeof(h),f)==sizeof(h) &&
               fwrite(data,1,(size_t)size,f)==(size_t)size;
        if(fclose(f)) ok=0;
        if(ok) {
            /* Some targets refuse rename-over-existing. Cache files can be
             * rebuilt; keep the complete temporary until replacement. */
            if(!rename(temporary,path)) saved=1;
            else {
                if((!remove(path) || errno==ENOENT) && !rename(temporary,path)) saved=1;
                else remove(temporary);
            }
        }
        else remove(temporary);
    }
    if(!saved) fprintf(stderr,"Shader cache write failed: program %u errno %d path %s\n",id,errno,path);
    free(data);
    return saved;
}
void wb_cached_link_program(GLuint id) {
    uint8_t key[32];char path[272];
    int eligible=key_for(id,key);
    if(eligible) {
        cache_path(path,key);
        if(load(id,path,key)) {
            /* VitaGL creates temporary CG shader handles internally. Their
             * IDs must not retain fingerprints from previously deleted GLSL. */
            GLuint attached[2];GLsizei count=0;
            glGetAttachedShaders(id,2,&count,attached);
            for(int i=0;i<count;++i)
                if(attached[i]<HANDLES) shaders[attached[i]].valid=0;
            fprintf(stderr,"Shader cache hit: program %u\n",id);
            return;
        }
    }
    glLinkProgram(id);
    GLint linked=0;glGetProgramiv(id,GL_LINK_STATUS,&linked);
    if(eligible && linked) {
        int saved=save(id,path,key);
        fprintf(stderr,"Shader cache miss: program %u compiled, saved=%d\n",id,saved);
    }
}
void *wb_shader_cache_proc(const char *name) {
#define HOOK(n,f) if(!strcmp(name,n)) return (void *)(uintptr_t)f
    HOOK("glCreateShader",create_shader);
    HOOK("glCreateProgram",create_program);
    HOOK("glShaderSource",source);
    HOOK("glBindAttribLocation",bind_attribute);
#undef HOOK
    return NULL;
}
