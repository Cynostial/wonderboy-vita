#include "wb_setup_screen.h"
#include "wb_setup_font.h"
#include <stdio.h>
#include <string.h>
static const uint32_t *art;
static void rect(uint32_t *p,unsigned x,unsigned y,unsigned w,unsigned h,uint32_t color) {
    for(unsigned j=y;j<y+h && j<544;++j)for(unsigned i=x;i<x+w && i<960;++i)p[j*960+i]=color;
}
void wb_setup_screen_art(uint32_t *pixels) {
    if(pixels)rect(pixels,40,48,384,176,0xffffffff);
    art=pixels;
}
static void glyph(uint32_t *p,unsigned x,unsigned y,unsigned char c,unsigned size,uint32_t color) {
    if(c<32 || c>126)c='?';
    const unsigned char *g=setup_font_pixels+(c-32)*28*32;
    for(unsigned j=0;j<32*size/24 && y+j<544;++j)for(unsigned i=0;i<28*size/24 && x+i<960;++i) {
        unsigned a=g[(j*24/size)*28+i*24/size];
        if(!a)continue;
        uint32_t *q=&p[(y+j)*960+x+i],v=0xff000000;
        for(unsigned s=0;s<24;s+=8)v|=((((color>>s)&255)*a+((*q>>s)&255)*(255-a)+127)/255)<<s;
        *q=v;
    }
}
static unsigned width(const char *s,unsigned size) {
    unsigned n=0;
    for(;*s && *s!=' ' && *s!='\n';++s) {
        unsigned char c=(unsigned char)*s;
        n+=(setup_font_advance[c>=32 && c<=126?c-32:'?'-32]*size+12)/24;
    }
    return n;
}
static unsigned text_pass(uint32_t *p,unsigned x,unsigned y,const char *s,unsigned size,uint32_t color) {
    unsigned left=x,top=y,height=32*size/24,step=height+2;
    int word_start=1;
    for(;*s;++s) {
        if(*s=='\n') { x=left;y+=step;word_start=1;continue; }
        if(*s==' ' && x==left)continue;
        if(word_start && *s!=' ' && x>left && width(s,size)+x>406) { x=left;y+=step; }
        unsigned char c=(unsigned char)*s;
        unsigned advance=(setup_font_advance[c>=32 && c<=126?c-32:'?'-32]*size+12)/24;
        if(x>left && x+advance>406) { x=left;y+=step;if(c==' ')continue; }
        if(p)glyph(p,x,y,c,size,color);
        x+=advance;word_start=c==' ';
    }
    return y-top+height;
}
static void text(uint32_t *p,unsigned x,unsigned y,const char *s,unsigned size,uint32_t color,unsigned bottom) {
    /* Measure the complete wrapped message before drawing. The old loop
     * stopped mid-message and checked font size instead of glyph-cell height. */
    while(size>12 && text_pass(NULL,x,y,s,size,color)>bottom-y)--size;
    text_pass(p,x,y,s,size,color);
}
void wb_setup_screen(uint32_t *p,const char *message,unsigned done,unsigned total,int mode) {
    if(art)memcpy(p,art,960u*544u*4u);else rect(p,0,0,960,544,0xffffffff);
    const uint32_t ink=0xff272c35,muted=0xff656b73;
    if(mode==0) {
        text(p,52,91,"Place APK + OBB in",24,ink,544);
        text(p,52,128,"ux0:data/wonderboy/import/",24,ink,544);
        text(p,52,194,"START   Prepare",24,ink,544);
        if(message && *message)text(p,52,246,message,18,muted,320);
    } else if(mode==1) {
        const char *label="Preparing";
        if(message && !strncmp(message,"Checking ",9))label=message;
        else if(message && !strncmp(message,"Verifying",9))label="Verifying";
        text(p,52,110,label,24,ink,544);
        unsigned percent=total?(unsigned)((uint64_t)done*100/total):0;
        if(percent>100)percent=100;
        rect(p,52,161,340,3,0xffe4e6e8);rect(p,52,161,340*percent/100,3,ink);
        char value[16];snprintf(value,sizeof(value),"%u%%",percent);
        text(p,52,186,value,24,ink,544);
        text(p,52,249,"Circle   Pause",18,muted,544);
    } else if(mode==3) {
        text(p,52,94,"Delete APK + OBB?",24,ink,544);
        text(p,52,149,"Triangle   Delete",24,ink,544);
        text(p,52,194,"Circle   Keep",24,ink,544);
    } else {
        text(p,52,94,"Ready to play",24,ink,544);
        text(p,52,149,"START   Play",24,ink,544);
        text(p,52,202,"Triangle   Delete APK + OBB",18,muted,544);
        if(message && *message && strncmp(message,"Setup complete.",15))
            text(p,52,246,message,18,muted,320);
    }
}
