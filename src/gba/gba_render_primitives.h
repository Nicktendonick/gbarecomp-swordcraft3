#pragma once
#include "gba_text_sample.h"
#include <algorithm>
#include <cstdint>

namespace gba::render {
// Stateless decoding primitives for an externally owned compositor. No PPU,
// guest execution, global presentation hooks, scene policy, or output surface.
struct Texel {
    unsigned palette=0, address=0;
    bool opaque=false;
};
inline int s16(const std::uint8_t* p) { return static_cast<std::int16_t>(text_u16(p)); }
inline int s28(const std::uint8_t* p) {
    const unsigned v=unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);
    return static_cast<std::int32_t>(v<<4)>>4;
}
inline Texel tile(const std::uint8_t* vram,unsigned address,unsigned px,unsigned py,
                  bool color256,unsigned bank) {
    address+=py*(color256?8:4)+(color256?px:px/2);
    if(address>=0x18000) return {};
    const unsigned raw=vram[address];
    const unsigned index=color256 ? raw : ((px&1)?raw>>4:raw&15);
    return {index+(color256?0:bank*16),address,index!=0};
}
inline Texel text(const std::uint8_t* vram,unsigned cnt,int x,int y,
                  bool replace=false,std::uint16_t replacement=0) {
    std::uint16_t entry=0;
    if(!text_ring_entry(cnt,x,y,vram,0x18000,entry)) return {};
    if(replace) entry=replacement;
    unsigned px=unsigned(x)&7,py=unsigned(y)&7;
    if(entry&0x400) px=7-px;
    if(entry&0x800) py=7-py;
    const bool color256=(cnt&0x80)!=0;
    return tile(vram,((cnt>>2)&3)*0x4000+(entry&1023)*(color256?64:32),
                px,py,color256,entry>>12);
}
inline Texel affine(const std::uint8_t* vram,unsigned cnt,int x,int y) {
    const unsigned size=128u<<(cnt>>14);
    if(!(cnt&0x2000) && (x<0 || y<0 || unsigned(x)>=size || unsigned(y)>=size)) return {};
    const unsigned tx=unsigned(x)&(size-1),ty=unsigned(y)&(size-1);
    const unsigned map=((cnt>>8)&31)*0x800+(ty/8)*(size/8)+tx/8;
    if(map>=0x18000) return {};
    return tile(vram,((cnt>>2)&3)*0x4000+vram[map]*64,tx&7,ty&7,true,0);
}
struct Object {
    int x=0,y=0,width=0,height=0,box_width=0,box_height=0;
    unsigned index=0,attr0=0,attr1=0,attr2=0;
    bool valid=false;
    unsigned mode() const { return (attr0>>10)&3; }
    int key() const { return int(((attr2>>10)&3)*256+index); }
};
inline Object object(const std::uint8_t* oam,unsigned index,int resolved_x) {
    Object obj; obj.index=index;
    if(index>=128) return obj;
    obj.attr0=text_u16(oam+index*8); obj.attr1=text_u16(oam+index*8+2); obj.attr2=text_u16(oam+index*8+4);
    const unsigned shape=obj.attr0>>14,size=obj.attr1>>14;
    if(shape==3 || (!(obj.attr0&0x100) && (obj.attr0&0x200)) || obj.mode()==3) return obj;
    constexpr int dims[3][4][2]={{{8,8},{16,16},{32,32},{64,64}},{{16,8},{32,8},{32,16},{64,32}},{{8,16},{8,32},{16,32},{32,64}}};
    obj.width=dims[shape][size][0]; obj.height=dims[shape][size][1];
    const bool doubled=(obj.attr0&0x300)==0x300;
    obj.box_width=obj.width*(doubled?2:1); obj.box_height=obj.height*(doubled?2:1);
    obj.x=resolved_x; obj.y=int(obj.attr0&255); if(obj.y>=160) obj.y-=256;
    obj.valid=true; return obj;
}
inline Texel object_texel(const Object& obj,const std::uint8_t* vram,const std::uint8_t* oam,
                          unsigned display,int x,int y) {
    if(!obj.valid) return {};
    int tx=x-obj.x,ty=y-obj.y;
    if(tx<0 || ty<0 || tx>=obj.box_width || ty>=obj.box_height) return {};
    if(obj.attr0&0x100) {
        const auto* matrix=oam+((obj.attr1>>9)&31)*32;
        const int dx=tx-obj.box_width/2,dy=ty-obj.box_height/2;
        tx=((s16(matrix+6)*dx+s16(matrix+14)*dy)>>8)+obj.width/2;
        ty=((s16(matrix+22)*dx+s16(matrix+30)*dy)>>8)+obj.height/2;
    } else {
        if(obj.attr1&0x1000) tx=obj.width-1-tx;
        if(obj.attr1&0x2000) ty=obj.height-1-ty;
    }
    if(tx<0 || ty<0 || tx>=obj.width || ty>=obj.height) return {};
    const bool color256=(obj.attr0&0x2000)!=0;
    const unsigned base=obj.attr2&1023;
    if((display&7)>=3 && base<512) return {};
    const unsigned step=color256?2:1;
    const unsigned id=base+(display&0x40 ? (ty/8*(obj.width/8)+tx/8)*step : ty/8*32+tx/8*step);
    auto result=tile(vram,0x10000+id*32,unsigned(tx)&7,unsigned(ty)&7,color256,obj.attr2>>12);
    result.palette+=256; return result;
}
struct Candidate {
    int key=0x7fffffff;
    unsigned layer=5,palette=0;
    bool semi=false,valid=false;
};
struct Stack {
    Candidate first{0x70000000,5,0,false,true},second{};
    void submit(Candidate candidate) {
        if(!candidate.valid) return;
        if(candidate.key<first.key) { second=first; first=candidate; }
        else if(candidate.key<second.key) second=candidate;
    }
};
enum class Effect { none,alpha,brighten,darken };
inline Effect effect(const Stack& stack,unsigned blend,unsigned control) {
    if(!(control&32)) return Effect::none;
    const unsigned mode=(blend>>6)&3;
    const bool target=(blend&(1u<<stack.first.layer))!=0;
    if(((mode==1 && target) || stack.first.semi) && stack.second.valid &&
       (blend&(0x100u<<stack.second.layer)) && !(stack.first.layer==4 && stack.second.layer==4))
        return Effect::alpha;
    if(target && mode==2) return Effect::brighten;
    if(target && mode==3) return Effect::darken;
    return Effect::none;
}
// Preserve the pinned engine's existing native-color arithmetic. These pure
// helpers are not an independent claim of hardware color accuracy.
inline std::uint16_t color_effect(std::uint16_t a,std::uint16_t b,Effect effect,unsigned alpha,unsigned fade) {
    unsigned r=a&31,g=((a>>4)&62)|(a>>15),bl=(a>>10)&31;
    fade=std::min(16u,fade&31);
    if(effect==Effect::alpha) {
        const unsigned eva=std::min(16u,alpha&31),evb=std::min(16u,(alpha>>8)&31);
        r=std::min(31u,(r*eva+(b&31)*evb+8)>>4);
        g=std::min(63u,(g*eva+(((b>>4)&62)|(b>>15))*evb+8)>>4);
        bl=std::min(31u,(bl*eva+((b>>10)&31)*evb+8)>>4);
    } else if(effect==Effect::brighten) {
        r+=((31-r)*fade+8)>>4; g+=((63-g)*fade+8)>>4; bl+=((31-bl)*fade+8)>>4;
    } else if(effect==Effect::darken) {
        r-=(r*fade+7)>>4; g-=(g*fade+7)>>4; bl-=(bl*fade+7)>>4;
    }
    return std::uint16_t(r|((g>>1)<<5)|(bl<<10));
}
inline void rgb(std::uint16_t color,std::uint8_t* destination) {
    for(unsigned n=0;n<3;++n) { const unsigned c=(color>>(5*n))&31; destination[n]=std::uint8_t((c<<3)|(c>>2)); }
}
} // namespace gba::render
