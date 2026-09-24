// Source-address, ordering and effect-control tests; no framebuffer assertions.
#include "gba_render_primitives.h"
#include "host_frame.h"
#include <array>
#include <stdexcept>
#include <cstdio>
static unsigned checks=0;
static void check(bool ok) { ++checks; if(!ok) throw std::runtime_error("render primitive state assertion"); }
int main() {
    using namespace gba::render;
    std::array<std::uint8_t,0x18000> vram{};
    std::array<std::uint8_t,0x400> oam{};
    // BG text-map last tile in a 512-wide row, with negative source phase.
    vram[0x1000+62]=1; vram[32+3]=0x70;
    const auto t=text(vram.data(),0x4100,-1,0);
    check(t.address==35 && t.palette==7 && t.opaque);
    const auto empty=text(vram.data(),0x4100,0,0);
    check(!empty.opaque);
    check(!affine(vram.data(),0,-1,0).opaque);
    check(!tile(vram.data(),0x18000,0,0,true,0).opaque);
    // OBJ body coordinates and texel provenance do not change at native edges.
    oam[0]=20; oam[2]=0; oam[3]=0x40; // 16x16, x resolved separately
    vram[0x10000]=0x11;
    for(int x : {-72,-1,0,239,240,311}) {
        const auto obj=object(oam.data(),0,x);
        check(obj.valid && obj.width==16 && obj.x==x);
        const auto sample=object_texel(obj,vram.data(),oam.data(),0x1040,x,20);
        check(sample.opaque && sample.address==0x10000 && sample.palette==257);
        check(!object_texel(obj,vram.data(),oam.data(),0x1040,x-1,20).opaque);
    }
    Stack stack;
    stack.submit({3*256+128,0,1,false,true});
    stack.submit({3*256+5,4,257,true,true});
    check(stack.first.layer==4 && stack.second.layer==0);
    check(effect(stack,0x100,0x3f)==Effect::alpha);
    check(effect(stack,0x100,0x1f)==Effect::none);
    stack.submit({2*256+128,1,2,false,true});
    check(stack.first.layer==1 && stack.second.layer==4);
    check(effect(stack,0x82,0x3f)==Effect::brighten);
    check(effect(stack,0xc2,0x3f)==Effect::darken);
    using namespace gbarecomp;
    check(!host_frame_owns_center(false,HostFrameOwnership::complete_frame));
    check(!host_frame_owns_center(true,HostFrameOwnership::native_center));
    check(host_frame_owns_center(true,HostFrameOwnership::complete_frame));
    std::printf("%u source/control assertions passed\n",checks);
}
