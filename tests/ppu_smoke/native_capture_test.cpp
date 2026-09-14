// Generic capture isolation tests, independent of any game adapter.
#include "gba_raster_capture.h"
#include "gba_obj_sample.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

static void check(bool ok) { if (!ok) throw std::runtime_error("native capture assertion"); }
static unsigned replay_postprocess_calls=0;
static void poison_postprocess(std::uint8_t*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t) {
    ++replay_postprocess_calls;
}
int main() {
    try {
        auto capture=std::make_unique<gba::GbaRasterCapture>();
        auto ppu=std::make_unique<gba::GbaPpu>();
        std::array<std::uint8_t,0x400> io{},pal{},oam{};
        std::array<std::uint8_t,0x18000> vram{};
        // Independent regular OBJ decoder: 4/8bpp, flips, bounds and guards.
        std::uint16_t color=0;
        vram[0x10000]=0x21; pal[0x202]=31; pal[0x204]=0xe0; pal[0x205]=3;
        auto sample=[&](unsigned a0,unsigned a1,unsigned a2,int x,int y) {
            return gba::sample_regular_obj(0x1040,a0,a1,a2,x,y,vram.data(),vram.size(),pal.data(),pal.size(),color);
        };
        check(sample(0,0,0,0,0) && color==31);
        check(sample(0,0,0,1,0) && color==0x3e0);
        check(sample(0,0x1000,0,7,0) && color==31);
        check(sample(0,0x2000,0,0,7) && color==31);
        check(!sample(0,0,0,-1,0) && !sample(0,0,0,8,0) && !sample(0,0,0,0,8));
        for(unsigned flags : {0x100u,0x200u,0x400u,0x800u,0x1000u,0xc000u}) check(!sample(flags,0,0,0,0));
        vram[0x10000]=1;
        check(sample(0x2000,0,1,0,0) && color==31); // 8bpp tile bit zero ignored
        check(!sample(0,0,0,1,0)); // transparent index zero
        std::puts("regular OBJ sampling guards PASS");
        std::array<std::uint8_t,gba::GbaPpu::kFramebufferBytes> replay{},again{};
        std::array<std::uint8_t,284*160*3> wide{};
        gba::GbaReplayViewPolicy view_policy;
        check(!capture->draw_native(replay.data(),replay.size()));
        check(!capture->draw_view(wide.data(),wide.size(),284,view_policy));
        for(unsigned mode=0; mode<=5; ++mode) {
            ppu->reset(); capture->reset();
            io.fill(0); pal.fill(0); vram.fill(0); oam.fill(0);
            for(unsigned i=0;i<vram.size();++i) vram[i]=static_cast<std::uint8_t>(i*13+i/19);
            for(unsigned i=0;i<pal.size();++i) pal[i]=static_cast<std::uint8_t>(i*7);
            for(unsigned i=0;i<128;++i) oam[i*8+1]=2; // disable unused OBJs
            oam[0]=50;oam[1]=0;oam[2]=60; // one visible sprite
            io[0x20+1]=1;io[0x26+1]=1;io[0x30+1]=1;io[0x36+1]=1;
            for(unsigned y=0;y<160;++y) {
                io[0x10]=static_cast<std::uint8_t>(y/3);
                pal[2]=static_cast<std::uint8_t>(y);
                io[0x50]=static_cast<std::uint8_t>(y<80 ? 0 : 0xFF);
                io[0x54]=static_cast<std::uint8_t>(y%17);
                const std::uint16_t display=0x1F40|mode|(y==159 ? 0x80 : 0);
                check(capture->capture({ppu.get(),y,display,io.data(),vram.data(),oam.data(),pal.data()}));
                ppu->render_scanline(y,display,io.data(),vram.data(),oam.data(),pal.data());
            }
            ppu->mark_framebuffer_latched();
            check(capture->draw_native(replay.data(),replay.size()));
            check(!std::memcmp(replay.data(),ppu->latched_framebuffer(),replay.size()));
            io.fill(0xAA);vram.fill(0x55);oam.fill(0xCC);pal.fill(0xFF);ppu->reset();
            check(capture->draw_native(again.data(),again.size()));
            check(replay==again); // no borrowed state survives capture
            const auto saved_postprocess=gba::g_ws_frame_postprocess;
            const int saved_pillarbox=gba::g_ws_pillarbox;
            gba::g_ws_frame_postprocess=poison_postprocess;
            gba::g_ws_pillarbox=1;
            check(capture->draw_view(wide.data(),wide.size(),284,view_policy));
            check(replay_postprocess_calls==0 && gba::g_ws_pillarbox==1 &&
                  gba::g_ws_frame_postprocess==poison_postprocess);
            gba::g_ws_frame_postprocess=saved_postprocess;
            gba::g_ws_pillarbox=saved_pillarbox;
            for(unsigned y=0;y<160;++y)
                check(!std::memcmp(wide.data()+(y*284+22)*3,replay.data()+y*240*3,240*3));
            check(!capture->draw_view(wide.data(),wide.size(),240,view_policy));
            check(!capture->draw_view(wide.data(),wide.size(),481,view_policy));
            check(!capture->draw_view(wide.data(),1,284,view_policy));
            check(!capture->draw_native(again.data(),1));
            check(!capture->capture({ppu.get(),12,0,io.data(),vram.data(),oam.data(),pal.data()}));
            check(!capture->draw_native(again.data(),again.size()));
            std::printf("mode %u: native raster replay, immutable input and sequence guards PASS\n",mode);
        }
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
