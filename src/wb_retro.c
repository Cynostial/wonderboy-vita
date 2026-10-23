/* The SMS atlas contains four 128x256 RGBA planes: two palettes, then
 * their index-zero-transparent variants. Keep the original atlas layout
 * and every pixel; decode eight planar pixels together on Cortex-A9. */
#include <stdint.h>
#include <string.h>
#include <arm_neon.h>

/* g_Emu is a process-lifetime object in the pinned guest. Only its Reset,
 * savestate loader and the two unpack routines write the CPU atlas. Cache
 * input bytes, not GPU pointers. The phase adapter invalidates on Reset and
 * LoadSavestate; an unpack call outside UpdateVramTex also invalidates.
 * Compare real input bytes rather than trusting the guest's dirty flags:
 * save restore and ROM hacks can write VRAM without PortWrite. */
static uint8_t previous_vram[0x4000],previous_palette[128];
static uint32_t valid[16];
static uint16_t used_colors[512];
static void *cache_owner,*cache_active;
static int cache_pending;
static uint32_t decoded,unchanged;
static uint64_t revision,band_revision[32];
static int batch_changed;
static void *game_object;
static int (*in_inventory)(const void *);
static uint32_t (*cram_color)(uint8_t);
void wb_retro_update_bind(void *game,int (*inventory)(const void *),uint32_t (*color)(uint8_t)) {
    game_object=game;in_inventory=inventory;cram_color=color;
}
static int equal16(uint8x16_t a,uint8x16_t b) {
    uint64x2_t difference=vreinterpretq_u64_u8(veorq_u8(a,b));
    return !(vgetq_lane_u64(difference,0)|vgetq_lane_u64(difference,1));
}
void wb_retro_invalidate(void) {
    cache_owner=cache_active=0;batch_changed=0;++revision;
    for(unsigned i=0;i<32;i+=2) vst1q_u64(band_revision+i,vdupq_n_u64(revision));
}
void wb_retro_versions(uint64_t out[32]) {
    for(unsigned i=0;i<32;i+=2) vst1q_u64(out+i,vld1q_u64(band_revision+i));
}
static void prepare(void *emulator) {
    uint8_t *palette=(uint8_t *)emulator+0xa4170;
    unsigned changed=0;
    /* Either palette can change, including inventory highlight entries. A
     * tile only needs rebuilding if it actually uses a changed color index.
     * All four atlas planes are still generated together. */
    for(unsigned i=0;i<16;++i) {
        uint32_t a,b,c,d;
        memcpy(&a,palette+4*i,4);memcpy(&b,previous_palette+4*i,4);
        memcpy(&c,palette+64+4*i,4);memcpy(&d,previous_palette+64+4*i,4);
        if((a^b)|(c^d)) changed|=1u<<i;
    }
    if(cache_owner!=emulator) {
        for(unsigned i=0;i<16;i+=4) vst1q_u32(valid+i,vdupq_n_u32(0));
    } else if(changed) {
        for(unsigned i=0;i<512;++i) if(used_colors[i]&changed) valid[i>>5]&=~(1u<<(i&31));
    }
    for(unsigned i=0;i<128;i+=16) {
        uint8x16_t current=vld1q_u8(palette+i);
        vst1q_u8(previous_palette+i,current);
    }
    cache_owner=emulator;cache_pending=0;
}
/* UpdateVramTex changes inventory palette entries before its first tile. */
void wb_retro_begin(void *emulator) { cache_active=emulator;cache_pending=1;batch_changed=0; }
void wb_retro_end(void) { cache_active=0; }
void wb_retro_stats(uint32_t *changed,uint32_t *same) { *changed=decoded;*same=unchanged; }

void wb_retro_unpack(void *emulator, uint16_t address) {
    uint8_t *e=emulator;
    const uint8_t *src=e+0x200e4+address;
    int tracked=cache_active==emulator && address<0x4000 && !(address&31);
    if(tracked) {
        if(cache_pending) prepare(emulator);
        unsigned tile=address>>5,word=tile>>5,bit=1u<<(tile&31);
        uint8x16_t a=vld1q_u8(src),b=vld1q_u8(src+16);
        if((valid[word]&bit) && equal16(a,vld1q_u8(previous_vram+address)) &&
           equal16(b,vld1q_u8(previous_vram+address+16))) { ++unchanged;return; }
        vst1q_u8(previous_vram+address,a);vst1q_u8(previous_vram+address+16,b);
        valid[word]|=bit;
        if(!batch_changed) { ++revision;batch_changed=1; }
        band_revision[address>>9]=revision;
    } else wb_retro_invalidate();
    ++decoded;
    uint8x16x4_t palette0=vld4q_u8(e+0xa4170);
    uint8x16x4_t palette1=vld4q_u8(e+0xa41b0);
    uint8x8x2_t table0[4],table1[4];
    for(unsigned c=0;c<4;++c) {
        table0[c].val[0]=vget_low_u8(palette0.val[c]);
        table0[c].val[1]=vget_high_u8(palette0.val[c]);
        table1[c].val[0]=vget_low_u8(palette1.val[c]);
        table1[c].val[1]=vget_high_u8(palette1.val[c]);
    }
    const int8_t shifts[8]={-7,-6,-5,-4,-3,-2,-1,0};
    int8x8_t shift=vld1_s8(shifts);
    uint8x8_t one=vdup_n_u8(1),zero=vdup_n_u8(0);
    uint16x8_t colors=vdupq_n_u16(0);
    unsigned base=((address&0xfe00u)<<1)|((address>>2)&0x78u);
    for(unsigned y=0;y<8;++y) {
        uint8x8_t index=vand_u8(vshl_u8(vdup_n_u8(src[0]),shift),one);
        index=vorr_u8(index,vshl_n_u8(vand_u8(vshl_u8(vdup_n_u8(src[1]),shift),one),1));
        index=vorr_u8(index,vshl_n_u8(vand_u8(vshl_u8(vdup_n_u8(src[2]),shift),one),2));
        index=vorr_u8(index,vshl_n_u8(vand_u8(vshl_u8(vdup_n_u8(src[3]),shift),one),3));
        colors=vorrq_u16(colors,vshlq_u16(vdupq_n_u16(1),vreinterpretq_s16_u16(vmovl_u8(index))));
        uint8x8_t opaque=vcgt_u8(index,zero);
        uint8x8x4_t color0,color1,masked0,masked1;
        for(unsigned c=0;c<4;++c) {
            color0.val[c]=vtbl2_u8(table0[c],index);
            color1.val[c]=vtbl2_u8(table1[c],index);
            masked0.val[c]=vand_u8(color0.val[c],opaque);
            masked1.val[c]=vand_u8(color1.val[c],opaque);
        }
        uint8_t *dst=e+0x240e4+(base+y*128)*4;
        vst4_u8(dst,color0);vst4_u8(dst+0x20000,color1);
        vst4_u8(dst+0x40000,masked0);vst4_u8(dst+0x60000,masked1);
        src+=4;
    }
    if(tracked) {
        uint16x4_t mask=vorr_u16(vget_low_u16(colors),vget_high_u16(colors));
        mask=vorr_u16(mask,vext_u16(mask,mask,2));
        mask=vorr_u16(mask,vext_u16(mask,mask,1));
        used_colors[address>>5]=vget_lane_u16(mask,0);
    }
}
/* The alternate exported decoder has no PLT entry in this release. It has
 * the same pixel semantics, so replace it directly and invalidate cached
 * inputs even if a caller invokes it during an UpdateVramTex batch. */
void wb_retro_unpack_external(void *emulator,uint16_t address) {
    wb_retro_invalidate();wb_retro_unpack(emulator,address);
}
void wb_retro_update(void *emulator) {
    uint8_t *e=emulator;
    /* Exact UpdateVramTex inventory palette fixups, including alpha. Use the
     * original game predicate and CRAM conversion rather than approximations. */
    if(in_inventory(game_object)) {
        uint32_t color=(cram_color(e[0xa416d])&0xffffffu)|0xc0000000u;
        memcpy(e+0xa41e4,&color,4);memcpy(e+0xa41a4,&color,4);
        color=(cram_color(e[0xa4151])&0xffffffu)|0xc0000000u;
        memcpy(e+0xa4174,&color,4);
    }
    wb_retro_begin(emulator);prepare(emulator);
    for(unsigned band=0;band<32;++band) {
        unsigned address=band*512,mask=0xffffu<<((band&1)*16);
        if((valid[band>>1]&mask)==mask) {
            uint8x16_t diff=vdupq_n_u8(0);
            for(unsigned i=0;i<512;i+=16)
                diff=vorrq_u8(diff,veorq_u8(vld1q_u8(e+0x200e4+address+i),
                                          vld1q_u8(previous_vram+address+i)));
            if(equal16(diff,vdupq_n_u8(0))) { unchanged+=16;continue; }
        }
        for(unsigned i=0;i<512;i+=32) wb_retro_unpack(emulator,(uint16_t)(address+i));
    }
    /* The original clears sixteen 32-bit dirty groups after decoding. */
    for(unsigned i=0;i<64;i+=16) vst1q_u8(e+0xa4110+i,vdupq_n_u8(0));
    wb_retro_end();
}
