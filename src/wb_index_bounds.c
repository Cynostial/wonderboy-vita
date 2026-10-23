/* Scan exact index bounds without per-index uncached loads. NEON batches
 * contiguous inputs; the tail uses memcpy, including unaligned indices.
 * Never read beyond the requested count into an adjacent mapped range. */
#include <stdint.h>
#include <string.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
void wb_index_bounds(const unsigned char *p,unsigned count,unsigned bytes,
                     uintptr_t *out_low,uintptr_t *out_high) {
    uintptr_t low=UINTPTR_MAX,high=0;unsigned i=0;
#ifdef __ARM_NEON
    if(bytes==2 && count>=8) {
        uint16x8_t lo=vdupq_n_u16(UINT16_MAX),hi=vdupq_n_u16(0);
        for(;count-i>=8;i+=8) {
            uint16x8_t v=vld1q_u16((const uint16_t *)(p+i*2));lo=vminq_u16(lo,v);hi=vmaxq_u16(hi,v);
        }
        uint16x4_t l=vmin_u16(vget_low_u16(lo),vget_high_u16(lo));
        uint16x4_t h=vmax_u16(vget_low_u16(hi),vget_high_u16(hi));
        l=vpmin_u16(l,l);l=vpmin_u16(l,l);h=vpmax_u16(h,h);h=vpmax_u16(h,h);
        low=vget_lane_u16(l,0);high=vget_lane_u16(h,0);
    } else if(bytes==4 && count>=4) {
        uint32x4_t lo=vdupq_n_u32(UINT32_MAX),hi=vdupq_n_u32(0);
        for(;count-i>=4;i+=4) {
            uint32x4_t v=vld1q_u32((const uint32_t *)(p+i*4));lo=vminq_u32(lo,v);hi=vmaxq_u32(hi,v);
        }
        uint32x2_t l=vmin_u32(vget_low_u32(lo),vget_high_u32(lo));
        uint32x2_t h=vmax_u32(vget_low_u32(hi),vget_high_u32(hi));
        low=vget_lane_u32(vpmin_u32(l,l),0);high=vget_lane_u32(vpmax_u32(h,h),0);
    } else if(bytes==1 && count>=16) {
        uint8x16_t lo=vdupq_n_u8(UINT8_MAX),hi=vdupq_n_u8(0);
        for(;count-i>=16;i+=16) {
            uint8x16_t v=vld1q_u8(p+i);lo=vminq_u8(lo,v);hi=vmaxq_u8(hi,v);
        }
        uint8x8_t l=vmin_u8(vget_low_u8(lo),vget_high_u8(lo));
        uint8x8_t h=vmax_u8(vget_low_u8(hi),vget_high_u8(hi));
        for(unsigned n=0;n<3;++n) { l=vpmin_u8(l,l);h=vpmax_u8(h,h); }
        low=vget_lane_u8(l,0);high=vget_lane_u8(h,0);
    }
#endif
#define TAIL(T) do { for(;i<count;++i) { T v;memcpy(&v,p+i*sizeof(T),sizeof(T)); \
    if(v<low) { low=v; } if(v>high) { high=v; } } } while(0)
    switch(bytes) { case 1:TAIL(uint8_t);break;case 2:TAIL(uint16_t);break;case 4:TAIL(uint32_t);break; }
#undef TAIL
    *out_low=low;*out_high=high;
}
