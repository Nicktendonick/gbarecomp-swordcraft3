// Control-flow/context contract only. Never inspect or compare rendered output.
#include "gba_raster_capture.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
static void check(bool ok) { if(!ok) throw std::runtime_error("replay policy state assertion"); }
struct Probe { mutable unsigned calls=0; int action=0; };
static int entry(const void* p,const gba::WsBgMarginContext* c,std::uint16_t* tile) {
    const auto& probe=*static_cast<const Probe*>(p);
    check(c && tile && c->layer==0 && c->output_width==384 && c->native_left==72);
    check(c->screen_y<160 && (c->output_x<72 || c->output_x>=312));
    check(c->io[0x10]==c->screen_y && c->vram[0x100]==0x63);
    ++probe.calls; *tile=0x4080; return probe.action;
}
int main() {
    try {
        auto capture=std::make_unique<gba::GbaRasterCapture>();
        auto ppu=std::make_unique<gba::GbaPpu>();
        std::array<std::uint8_t,0x400> io{},oam{},pal{};
        std::array<std::uint8_t,0x18000> vram{};
        vram[0x100]=0x63;
        for(unsigned y=0;y<160;++y) {
            io[0x10]=y;
            check(capture->capture({ppu.get(),y,0x0100,io.data(),vram.data(),oam.data(),pal.data()}));
        }
        // Prove callbacks receive owned captured state, not mutable live RAM.
        io.fill(0xff); vram.fill(0xff);
        std::vector<std::uint8_t> output(384*160*3);
        Probe probe;
        gba::GbaReplayViewPolicy policy; policy.user=&probe; policy.text_margin_entry=entry;
        for(int action:{-1,0,1}) {
            probe.action=action;probe.calls=0;
            check(capture->draw_view(output.data(),output.size(),384,policy));
            check(probe.calls==144*160);
        }
        policy.text_margin_entry=nullptr; probe.calls=0;
        check(capture->draw_view(output.data(),output.size(),384,policy)); check(probe.calls==0);
        capture->reset();
        check(!capture->draw_view(output.data(),output.size(),384,policy));
        std::puts("immutable replay margin callback/context/reset: PASS (no pixel assertions)");
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
