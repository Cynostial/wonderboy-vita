#include "wb_convert.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "../vendor/fastlz/fastlz.h"
#include "../vendor/setup/eac_utils.h"
#define STB_DXT_IMPLEMENTATION
#define STB_DXT_STATIC
#include "../vendor/setup/stb_dxt.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 4096
#define STB_IMAGE_STATIC
#include "../vendor/setup/stb_image.h"
#define LIMIT (8u*1024u*1024u)
static uint32_t word(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static int write_bytes(FILE *f,const void *p,size_t n) { return fwrite(p,1,n,f)==n ? 0 : -1; }
static const unsigned char magic[12]={0xab,'K','T','X',' ','1','1',0xbb,13,10,26,10};
/* Exact ETC-block reuse, including partial-edge shape. A bounded cache avoids
 * decoding/recompressing repeated atlas padding without changing any pixels.
 * Allocation failure simply selects the original converter. */
typedef struct { uint32_t key[4],shape;unsigned char output[16]; } block_cache;
#ifndef WB_CONVERT_CACHE_ENTRIES
#define WB_CONVERT_CACHE_ENTRIES 8192u
#endif
static int write_blocks(FILE *out,const unsigned char *data,unsigned w,unsigned h,int alpha,int etc) {
    unsigned char buffer[16384];size_t used=0;unsigned size=alpha?16:8;
    block_cache *cache=etc && WB_CONVERT_CACHE_ENTRIES ? calloc(WB_CONVERT_CACHE_ENTRIES,sizeof(*cache)) : NULL;
    int result=-1;
    for(unsigned by=0;by<(h+3)/4;++by) for(unsigned bx=0;bx<(w+3)/4;++bx) {
        uint32_t aligned[16];unsigned char *rgba=(unsigned char *)aligned;
        block_cache *slot=NULL;uint32_t key[4]={0},shape=0;
        if(etc) {
            const unsigned char *block=data+((size_t)by*((w+3)/4)+bx)*size;
            if(cache) {
                memcpy(key,block,size);
                unsigned cw=w-bx*4<4?w-bx*4:4,ch=h-by*4<4?h-by*4:4;
                shape=cw|(ch<<3)|((unsigned)alpha<<6);
                uint32_t hash=key[0]*0x9e3779b1u+key[1]*0x85ebca77u+key[2]*0xc2b2ae3du+key[3];
                hash^=hash>>16;
                slot=&cache[hash&(WB_CONVERT_CACHE_ENTRIES-1u)];
                if(slot->shape==shape && !memcmp(slot->key,key,sizeof(key))) {
                    memcpy(buffer+used,slot->output,size);goto encoded;
                }
            }
            bool ok=alpha?detexDecompressBlockETC2_EAC(block,DETEX_MODE_MASK_ALL,0,rgba):
                          detexDecompressBlockETC2(block,DETEX_MODE_MASK_ALL,0,rgba);
            if(!ok) goto done;
            /* Replicate the final valid texels at partial block boundaries. */
            for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
                unsigned sx=bx*4+x<w ? x : w-bx*4-1;
                unsigned sy=by*4+y<h ? y : h-by*4-1;
                aligned[y*4+x]=aligned[sy*4+sx];
            }
        } else {
            for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
                unsigned sx=bx*4+x<w ? bx*4+x : w-1;
                unsigned sy=by*4+y<h ? by*4+y : h-1;
                memcpy(rgba+(y*4+x)*4,data+((size_t)sy*w+sx)*4,4);
            }
        }
        stb_compress_dxt_block(buffer+used,rgba,alpha,STB_DXT_HIGHQUAL);
        if(slot) { memcpy(slot->key,key,sizeof(key));slot->shape=shape;memcpy(slot->output,buffer+used,size); }
encoded:used+=size;
        if(used==sizeof(buffer)) { if(write_bytes(out,buffer,used)) goto done;used=0; }
    }
    result=write_bytes(out,buffer,used);
done:free(cache);return result;
}
int wb_convert_ktx(const unsigned char *source,size_t length,FILE *out) {
    unsigned char *decoded=NULL;const unsigned char *p=source;size_t n=length;int result=-1;
    if(n>LIMIT || n<16) return -1;
    if(!memcmp(p,"KTZ1",4)) {
        unsigned compressed=word(p+4),plain=word(p+8);
        if(plain<64 || plain>LIMIT || compressed!=n-16) return -1;
        if(compressed>=plain) { p+=16;n=plain; }
        else {
            decoded=malloc(plain);if(!decoded) return -1;
            if(fastlz_decompress(p+16,compressed,decoded,plain)!=(int)plain) goto done;
            p=decoded;n=plain;
        }
    }
    if(n<64 || memcmp(p,magic,12)) goto done;
    uint32_t h[13];memcpy(h,p+12,sizeof(h));
    if(h[0]!=0x04030201 || h[1] || h[2]!=1 || h[3] || !h[6] || !h[7] ||
       h[6]>4096 || h[7]>4096 || h[8] || h[9] || h[10]!=1 || !h[11] || h[11]>13 || h[12]>n-64) goto done;
    unsigned original=h[4];int alpha=original==0x9278 || original==0x83f3;
    if(original!=0x9274 && original!=0x9278 && original!=0x83f0 && original!=0x83f3) goto done;
    h[4]=alpha?0x83f3:0x83f0;
    if(write_bytes(out,magic,12) || write_bytes(out,h,sizeof(h)) || write_bytes(out,p+64,h[12])) goto done;
    size_t at=64+h[12];
    for(unsigned mip=0;mip<h[11];++mip) {
        unsigned w=h[6]>>mip,he=h[7]>>mip;if(!w)w=1;if(!he)he=1;
        uint32_t expected=((w+3)/4)*((he+3)/4)*(alpha?16:8);
        if(at>n-4 || word(p+at)!=expected) goto done;
        at+=4;if(expected>n-at) goto done;
        if(write_bytes(out,&expected,4)) goto done;
        if(original==0x83f0 || original==0x83f3) { if(write_bytes(out,p+at,expected)) goto done; }
        else if(write_blocks(out,p+at,w,he,alpha,1)) goto done;
        at+=expected;
    }
    if(at==n) result=0;
done:free(decoded);return result;
}
int wb_convert_png(const unsigned char *source,size_t length,FILE *out,int loading) {
    int w,h,c;unsigned char *rgba=NULL;int result=-1;
    if(length>LIMIT || !stbi_info_from_memory(source,(int)length,&w,&h,&c) || w<1 || h<1 ||
       (uint64_t)w*h>4096u*2048u) return -1;
    rgba=stbi_load_from_memory(source,(int)length,&w,&h,&c,4);if(!rgba) return -1;
    if(loading) {
        unsigned dw=960,dh=(unsigned)((uint64_t)h*960/w);
        if(dh>544) { dh=544;dw=(unsigned)((uint64_t)w*544/h); }
        if(!dw || !dh) goto done;
        unsigned left=(960-dw)/2,top=(544-dh)/2;unsigned char row[960*4];
        for(unsigned y=0;y<544;++y) {
            memset(row,255,sizeof(row));
            if(y>=top && y<top+dh) for(unsigned x=0;x<dw;++x) {
                /* Bilinear resize, composited over white, bounded to one row. */
                uint32_t fx=dw>1?(uint64_t)x*(w-1)*65536/(dw-1):0;
                uint32_t fy=dh>1?(uint64_t)(y-top)*(h-1)*65536/(dh-1):0;
                unsigned x0=fx>>16,y0=fy>>16,x1=x0+1<(unsigned)w?x0+1:x0,y1=y0+1<(unsigned)h?y0+1:y0;
                for(unsigned channel=0;channel<3;++channel) {
                    uint64_t accum=0;
                    for(unsigned dy=0;dy<2;++dy) for(unsigned dx=0;dx<2;++dx) {
                        const unsigned char *s=rgba+((size_t)(dy?y1:y0)*w+(dx?x1:x0))*4;
                        unsigned value=(s[channel]*s[3]+255*(255-s[3])+127)/255;
                        accum+=(uint64_t)value*(dx?(fx&65535):65536-(fx&65535))*(dy?(fy&65535):65536-(fy&65535));
                    }
                    row[(left+x)*4+channel]=(unsigned char)((accum+(UINT64_C(1)<<31))>>32);
                }
            }
            if(write_bytes(out,row,sizeof(row))) goto done;
        }
    } else {
        if(w!=1920 || h!=1080) goto done;
        uint32_t header[]={0x04030201,0,1,0,0x83f3,0x1908,1920,1080,0,0,1,1,0};
        uint32_t size=1920*1080;
        if(write_bytes(out,magic,12) || write_bytes(out,header,sizeof(header)) || write_bytes(out,&size,4) ||
           write_blocks(out,rgba,w,h,1,0)) goto done;
    }
    result=0;
done:stbi_image_free(rgba);return result;
}
int wb_convert_path(int kind,const char *source,const char *dest) {
    FILE *in=fopen(source,"rb");if(!in) return -1;
    if(fseek(in,0,SEEK_END)) { fclose(in);return -1; }
    long size=ftell(in);rewind(in);
    if(size<0 || (unsigned long)size>LIMIT) { fclose(in);return -1; }
    unsigned char *data=malloc((size_t)size);if(!data) { fclose(in);return -1; }
    if(fread(data,1,size,in)!=(size_t)size) { free(data);fclose(in);return -1; }
    fclose(in);FILE *out=fopen(dest,"wb");int result=-1;
    if(out) { result=kind==1?wb_convert_ktx(data,size,out):wb_convert_png(data,size,out,kind==3);if(fclose(out))result=-1; }
    free(data);return result;
}
