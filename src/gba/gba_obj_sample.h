#pragma once
#include "gba_text_sample.h"

namespace gba {
// Immutable regular OBJ sampling. The caller resolves signed/widened X and
// composition priority. Affine, mosaic and effect objects deliberately decline.
inline bool sample_regular_obj(std::uint16_t dispcnt, std::uint16_t a0,
        std::uint16_t a1,std::uint16_t a2,int x,int y,
        const std::uint8_t* vram,std::size_t vram_size,
        const std::uint8_t* pal,std::size_t pal_size,std::uint16_t& color) {
    if (!(dispcnt&0x1000) || (dispcnt&7)>2 || (a0&0x1f00) || !vram || !pal) return false;
    constexpr unsigned dimensions[3][4][2]={{{8,8},{16,16},{32,32},{64,64}},
        {{16,8},{32,8},{32,16},{64,32}},{{8,16},{8,32},{16,32},{32,64}}};
    unsigned shape=a0>>14,size=a1>>14;
    if(shape>=3) return false;
    int w=dimensions[shape][size][0],h=dimensions[shape][size][1];
    if(x<0 || y<0 || x>=w || y>=h) return false;
    if(a1&0x1000) x=w-1-x;
    if(a1&0x2000) y=h-1-y;
    bool bpp8=(a0&0x2000)!=0;
    unsigned tile=(a2&1023)&(bpp8?~1u:~0u);
    tile+=(dispcnt&0x40)?((y/8)*(w/8)+x/8)*(bpp8?2:1):((y/8)*32+(x/8)*(bpp8?2:1));
    unsigned addr=0x10000+tile*32+(y&7)*(bpp8?8:4)+(bpp8?(x&7):(x&7)/2);
    if(addr>=vram_size) return false;
    unsigned index=vram[addr];
    if(!bpp8) index=(index>>(4*(x&1)))&15;
    if(!index) return false;
    if(!bpp8) index+=(a2>>12)*16;
    unsigned p=0x200+index*2;
    if(p+2>pal_size) return false;
    color=text_u16(pal+p); return true;
}
}
