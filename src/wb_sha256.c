/* SHA-256 compression and padding as specified by FIPS 180-4. */
#include "wb_sha256.h"
#include <string.h>
static const uint32_t k[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static uint32_t rotr(uint32_t x,unsigned n) { return (x>>n)|(x<<(32-n)); }
static void block(uint32_t h[8],const uint8_t *p) {
    uint32_t w[64];
    for(unsigned i=0;i<16;++i)
        w[i]=((uint32_t)p[4*i]<<24)|((uint32_t)p[4*i+1]<<16)|((uint32_t)p[4*i+2]<<8)|p[4*i+3];
    for(unsigned i=16;i<64;++i) {
        uint32_t a=w[i-15],b=w[i-2];
        w[i]=w[i-16]+(rotr(a,7)^rotr(a,18)^(a>>3))+w[i-7]+(rotr(b,17)^rotr(b,19)^(b>>10));
    }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
    /* Rotate register roles over eight rounds, rather than shuffling all
     * eight working words on every round. Identical FIPS-180 arithmetic. */
#define ROUND(A,B,C,D,E,F,G,H,I) do { \
    uint32_t t=(H)+(rotr(E,6)^rotr(E,11)^rotr(E,25))+(((E)&(F))^(~(E)&(G)))+k[I]+w[I]; \
    (D)+=t; \
    (H)=t+(rotr(A,2)^rotr(A,13)^rotr(A,22))+(((A)&(B))^((A)&(C))^((B)&(C))); \
} while(0)
    for(unsigned i=0;i<64;i+=8) {
        ROUND(a,b,c,d,e,f,g,z,i);ROUND(z,a,b,c,d,e,f,g,i+1);
        ROUND(g,z,a,b,c,d,e,f,i+2);ROUND(f,g,z,a,b,c,d,e,i+3);
        ROUND(e,f,g,z,a,b,c,d,i+4);ROUND(d,e,f,g,z,a,b,c,i+5);
        ROUND(c,d,e,f,g,z,a,b,i+6);ROUND(b,c,d,e,f,g,z,a,i+7);
    }
#undef ROUND
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
}
void wb_sha256(const void *data,size_t length,uint8_t out[32]) {
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const uint8_t *p=data;
    uint64_t bits=(uint64_t)length*8;
    while(length>=64) { block(h,p);p+=64;length-=64; }
    uint8_t last[128]={0};
    if(length) memcpy(last,p,length);
    last[length]=0x80;
    size_t n=length<56 ? 64 : 128;
    for(unsigned i=0;i<8;++i) last[n-1-i]=(uint8_t)(bits>>(i*8));
    block(h,last); if(n==128) block(h,last+64);
    for(unsigned i=0;i<32;++i) out[i]=(uint8_t)(h[i/4]>>(24-8*(i%4)));
}
void wb_sha256_init(wb_sha256_state *s) {
    static const uint32_t initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s->h,initial,sizeof(initial));s->bytes=0;s->used=0;
}
void wb_sha256_update(wb_sha256_state *s,const void *data,size_t n) {
    const uint8_t *p=data;s->bytes+=n;
    if(s->used) {
        size_t take=64-s->used;if(take>n) take=n;
        if(take) memcpy(s->tail+s->used,p,take);
        s->used+=take;p+=take;n-=take;
        if(s->used==64) { block(s->h,s->tail);s->used=0; }
    }
    while(n>=64) { block(s->h,p);p+=64;n-=64; }
    if(n) { memcpy(s->tail,p,n);s->used=n; }
}
void wb_sha256_final(wb_sha256_state *s,uint8_t out[32]) {
    uint8_t last[128]={0};memcpy(last,s->tail,s->used);last[s->used]=0x80;
    size_t n=s->used<56 ? 64 : 128;uint64_t bits=s->bytes*8;
    for(unsigned i=0;i<8;++i) last[n-1-i]=(uint8_t)(bits>>(i*8));
    block(s->h,last);if(n==128) block(s->h,last+64);
    for(unsigned i=0;i<32;++i) out[i]=(uint8_t)(s->h[i/4]>>(24-8*(i%4)));
}
