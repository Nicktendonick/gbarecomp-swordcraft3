// Generic capture isolation tests, independent of any game adapter.
#include "gba_raster_capture.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

static void check(bool ok) { if (!ok) throw std::runtime_error("native capture assertion"); }
int main() {
    try {
        auto capture=std::make_unique<gba::GbaRasterCapture>();
        auto ppu=std::make_unique<gba::GbaPpu>();
        std::array<std::uint8_t,0x400> io{},pal{},oam{};
        std::array<std::uint8_t,0x18000> vram{};
        std::array<std::uint8_t,gba::GbaPpu::kFramebufferBytes> replay{},again{};
        check(!capture->draw_native(replay.data(),replay.size()));
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
            check(!capture->draw_native(again.data(),1));
            check(!capture->capture({ppu.get(),12,0,io.data(),vram.data(),oam.data(),pal.data()}));
            check(!capture->draw_native(again.data(),again.size()));
            std::printf("mode %u: native raster replay, immutable input and sequence guards PASS\n",mode);
        }
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
