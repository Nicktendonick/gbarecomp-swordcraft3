#pragma once
#include <cstddef>
#include <cstdint>

namespace gba {
inline std::uint16_t text_u16(const std::uint8_t* p) {
    return std::uint16_t(p[0] | unsigned(p[1])<<8);
}
// Regular BG tile decoding from immutable render inputs. Coordinates are
// pixel offsets within an already-resolved map entry; map ownership is external.
inline bool sample_text_entry(std::uint16_t cnt, std::uint16_t entry, unsigned x, unsigned y,
                              const std::uint8_t* vram, std::size_t vram_size,
                              const std::uint8_t* pal, std::size_t pal_size,
                              std::uint16_t& color) {
    if (!vram || !pal) return false;
    unsigned px=x&7,py=y&7;
    if(entry&0x400) px=7-px;
    if(entry&0x800) py=7-py;
    const bool bpp8=(cnt&0x80)!=0;
    const unsigned addr=((cnt>>2)&3)*0x4000+(entry&1023)*(bpp8?64:32)+py*(bpp8?8:4)+(bpp8?px:px/2);
    if(addr>=vram_size) return false;
    unsigned idx=vram[addr];
    if(!bpp8) idx=(idx>>(4*(px&1)))&15;
    if(!idx) return false;
    if(!bpp8) idx|=(entry>>12)*16;
    if(idx*2+1>=pal_size) return false;
    color=text_u16(pal+idx*2);
    return true;
}
// Original hardware text map entry, including screen-block arrangement.
inline bool text_ring_entry(std::uint16_t cnt, unsigned x, unsigned y,
                            const std::uint8_t* vram, std::size_t size, std::uint16_t& entry) {
    const unsigned w=(cnt&0x4000)?64:32,h=(cnt&0x8000)?64:32;
    const unsigned tx=(x/8)&(w-1),ty=(y/8)&(h-1);
    const unsigned offset=((cnt>>8)&31)*0x800+((tx/32)+(ty/32)*(w/32))*0x800+((ty&31)*32+(tx&31))*2;
    if(!vram || offset+1>=size) return false;
    entry=text_u16(vram+offset);
    return true;
}
inline void text_rgb888(std::uint16_t color,std::uint8_t* out) {
    for(unsigned c=0;c<3;++c) { unsigned v=(color>>(c*5))&31; out[c]=std::uint8_t((v<<3)|(v>>2)); }
}
}
