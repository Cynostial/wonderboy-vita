#include "wb_utf8.h"
#include <errno.h>
#include <string.h>
static _Thread_local wb_mbstate read_state,write_state,legacy_state;
static size_t invalid(wb_mbstate *s) {
    *s=(wb_mbstate){0}; errno=84; return (size_t)-1; /* Android EILSEQ */
}
size_t wb_mbrtowc(uint32_t *out,const char *text,size_t bytes,wb_mbstate *state) {
    wb_mbstate *s=state ? state : &read_state;
    if(!text) { text=""; bytes=1; out=NULL; }
    if(!bytes) return (size_t)-2;
    size_t used=0;
    unsigned remain=(s->packed>>21)&3,total=((s->packed>>23)&3)+1;
    uint32_t code=s->packed&0x1fffff;
    if((s->packed>>25) || (remain && (total<2 || remain>=total))) return invalid(s);
    if(!remain) {
        unsigned c=(unsigned char)text[used++];
        if(c<128) { *s=(wb_mbstate){0}; if(out) *out=c; return c ? 1 : 0; }
        if(c>=0xc2 && c<=0xdf) { code=c&31; remain=1; total=2; }
        else if(c>=0xe0 && c<=0xef) { code=c&15; remain=2; total=3; }
        else if(c>=0xf0 && c<=0xf4) { code=c&7; remain=3; total=4; }
        else return invalid(s);
    }
    while(remain && used<bytes) {
        unsigned c=(unsigned char)text[used++];
        if((c&0xc0)!=0x80) return invalid(s);
        if(remain==total-1) {
            if(total==3 && ((!code && c<0xa0) || (code==13 && c>=0xa0))) return invalid(s);
            if(total==4 && ((!code && c<0x90) || (code==4 && c>=0x90))) return invalid(s);
        }
        code=(code<<6)|(c&63); --remain;
    }
    s->packed=code|(remain<<21)|((total-1)<<23);
    if(remain) return (size_t)-2;
    uint32_t value=code;
    if(value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return invalid(s);
    if(out) *out=value;
    *s=(wb_mbstate){0};
    return used;
}
size_t wb_mbrlen(const char *s,size_t n,wb_mbstate *p) { return wb_mbrtowc(NULL,s,n,p); }
size_t wb_wcrtomb(char *out,uint32_t value,wb_mbstate *state) {
    wb_mbstate *s=state ? state : &write_state;
    if(!out) { *s=(wb_mbstate){0}; return 1; }
    if(s->packed || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return invalid(s);
    if(value<0x80) { out[0]=(char)value; return 1; }
    if(value<0x800) {
        out[0]=(char)(0xc0|(value>>6)); out[1]=(char)(0x80|(value&63)); return 2;
    }
    if(value<0x10000) {
        out[0]=(char)(0xe0|(value>>12)); out[1]=(char)(0x80|((value>>6)&63));
        out[2]=(char)(0x80|(value&63)); return 3;
    }
    out[0]=(char)(0xf0|(value>>18)); out[1]=(char)(0x80|((value>>12)&63));
    out[2]=(char)(0x80|((value>>6)&63)); out[3]=(char)(0x80|(value&63)); return 4;
}
size_t wb_mbsnrtowcs(uint32_t *out,const char **source,size_t bytes,size_t limit,wb_mbstate *state) {
    if(!source || !*source) { errno=22; return (size_t)-1; }
    wb_mbstate *s=state ? state : &read_state,copy=*s;
    if(!out) s=&copy;
    const char *p=*source; size_t count=0;
    while(bytes && (!out || count<limit)) {
        uint32_t value; size_t n=wb_mbrtowc(&value,p,bytes,s);
        if(n==(size_t)-1) { if(out) *source=p; return n; }
        if(n==(size_t)-2) { if(out) *source=p+bytes; return count; }
        if(!n) { if(out) { out[count]=0; *source=NULL; } return count; }
        if(out) out[count]=value;
        ++count; p+=n; bytes-=n;
    }
    if(out) *source=p;
    return count;
}
size_t wb_mbsrtowcs(uint32_t *out,const char **source,size_t limit,wb_mbstate *state) {
    return wb_mbsnrtowcs(out,source,SIZE_MAX,limit,state);
}
size_t wb_wcsnrtombs(char *out,const uint32_t **source,size_t chars,size_t limit,wb_mbstate *state) {
    if(!source || !*source) { errno=22; return (size_t)-1; }
    wb_mbstate *s=state ? state : &write_state,copy=*s;
    if(!out) s=&copy;
    const uint32_t *p=*source; size_t count=0;
    while(chars) {
        char encoded[4]; wb_mbstate trial=*s;
        size_t n=wb_wcrtomb(encoded,*p,&trial);
        if(n==(size_t)-1) { if(out) *source=p; return n; }
        if(out && n>limit-count) break;
        if(!*p) { if(out) { out[count]=0; *source=NULL; *s=trial; } return count; }
        if(count>SIZE_MAX-n) { errno=75; return (size_t)-1; }
        if(out) memcpy(out+count,encoded,n);
        *s=trial; count+=n; ++p; --chars;
    }
    if(out) *source=p;
    return count;
}
size_t wb_wcstombs(char *out,const uint32_t *source,size_t limit) {
    wb_mbstate state={0}; return wb_wcsnrtombs(out,&source,SIZE_MAX,limit,&state);
}
size_t wb_mbstowcs(uint32_t *out,const char *source,size_t limit) {
    wb_mbstate state={0}; return wb_mbsrtowcs(out,&source,limit,&state);
}
int wb_mbtowc(uint32_t *out,const char *source,size_t bytes) {
    if(!source) { legacy_state=(wb_mbstate){0}; return 0; }
    size_t n=wb_mbrtowc(out,source,bytes,&legacy_state);
    if(n==(size_t)-1 || n==(size_t)-2) { legacy_state=(wb_mbstate){0}; errno=84; return -1; }
    return (int)n;
}
