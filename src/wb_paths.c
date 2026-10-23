#include "wb_paths.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
static int imported;
void wb_paths_imported(int enabled) { imported=enabled; }
int wb_path_resolve(const char *input,char *out,size_t capacity) {
    if(!input || !out || !capacity) return EINVAL;
    size_t length=strlen(input);
    if(length>=WB_PATH_MAX || strchr(input,'\\')) return ENAMETOOLONG;
    for(const char *s=input;*s;) {
        const char *end=strchr(s,'/'); size_t n=end ? (size_t)(end-s) : strlen(s);
        if(n==2 && s[0]=='.' && s[1]=='.') return EACCES;
        if(!end) break;
        s=end+1;
    }
    int n;
    if(!strncmp(input,WB_ROOT,sizeof(WB_ROOT)-1) &&
       (input[sizeof(WB_ROOT)-1]=='/' || !input[sizeof(WB_ROOT)-1]))
        n=snprintf(out,capacity,"%s",input);
    else {
        if(strchr(input,':') || input[0]=='/') return EACCES;
        n=snprintf(out,capacity,"%s/%s",WB_ASSETS,input);
    }
    if(n<0 || (size_t)n>=capacity) return ENAMETOOLONG;
    /* ResourcePath ends in '/', while the font loader formats "%s/font/%s".
     * Canonicalize repeated separators and '.' before exact overlay matching.
     * '..' was rejected above; the permitted Vita device/root is unchanged. */
    char *read=out,*write=out;
    while(*read) {
        if(*read!='/') { *write++=*read++;continue; }
        *write++=*read++;
        for(;;) {
            while(*read=='/') ++read;
            if(read[0]=='.' && (read[1]=='/' || !read[1])) {
                ++read;continue;
            }
            break;
        }
    }
    *write=0;
    /* SDLTextureLoader identifies KTX by magic, not filename extension.
     * Ship only the three exact intro overlays in app0. This avoids lodepng's
     * multi-megabyte inflate/realloc peak and preserves the source assets. */
    static const char *logos[]={"DotEmu_Logo","LizardCube_Logo","Playdigious_Logo"};
    for(unsigned i=0;i<sizeof(logos)/sizeof(*logos);++i) {
        char original[128];
        snprintf(original,sizeof(original),WB_ASSETS "/video/%s.png",logos[i]);
        /* The game requests Dotemu_Logo; FAT stores DotEmu_Logo. */
        if(!strcasecmp(out,original)) {
            n=snprintf(out,capacity,"%s/%s.ktx",imported?WB_ROOT "/overlays/intro":"app0:intro",logos[i]);
            return n<0 || (size_t)n>=capacity ? ENAMETOOLONG : 0;
        }
    }
    static const char *controls[]={"SelectL","SelectR","Start","Select"};
    for(unsigned i=0;i<sizeof(controls)/sizeof(*controls);++i) {
        char original[128];snprintf(original,sizeof(original),WB_ASSETS "/tex/Vita/%s.png",controls[i]);
        if(!strcmp(out,original)) {
            n=snprintf(out,capacity,"app0:controls/%s.png",controls[i]);
            return n<0 || (size_t)n>=capacity ? ENAMETOOLONG : 0;
        }
    }
    /* Subsets contain every game-text glyph; keep unrelated/custom fonts and
     * the originals on ux0 untouched. Smaller file buffers keep normal lifetime. */
    static const char *fonts[]={"NotoSansCJKjp-Regular.otf","NotoSansCJKjp-Medium.otf",
        "NotoSansCJKsc-Regular.otf","NotoSansCJKsc-Medium.otf",
        "NotoSansCJKtc-Regular.otf","NotoSansCJKtc-Medium.otf"};
    for(unsigned i=0;i<sizeof(fonts)/sizeof(*fonts);++i) {
        char original[128];
        snprintf(original,sizeof(original),WB_ASSETS "/font/%s",fonts[i]);
        if(!strcasecmp(out,original)) {
            n=snprintf(out,capacity,"app0:font/%s",fonts[i]);
            return n<0 || (size_t)n>=capacity ? ENAMETOOLONG : 0;
        }
    }
    return 0;
}
