#include "host_presentation.h"
#include "gba_text_sample.h"
#include <cstdio>
#include <stdexcept>
#include <memory>

static void check(bool ok) { if(!ok) throw std::runtime_error("host presentation assertion"); }
static unsigned calls=0;
static bool accept=true;
static bool paint(const gbarecomp::HostFrameContext& c) {
    ++calls;
    std::fill(c.output_rgb,c.output_rgb+c.width*c.height*3,0xAB);
    return accept;
}
int main() {
    try {
        auto ppu=std::make_unique<gba::GbaPpu>();
        for(unsigned w : {241u,284u,320u,384u,480u}) {
            gbarecomp::HostPresentation surface(w,paint);
            for(bool allowed : {false,true}) for(bool succeeds : {false,true}) {
                accept=succeeds;
                std::vector<std::uint8_t> rgb(gba::GbaPpu::kFramebufferBytes);
                for(unsigned i=0;i<rgb.size();++i) rgb[i]=std::uint8_t(i*13);
                const auto native=rgb;
                surface.compose(rgb,*ppu,allowed);
                check(rgb.size()==w*160*3 && ppu->render_width()==240);
                unsigned left=(w-240)/2;
                for(unsigned y=0;y<160;++y) for(unsigned x=0;x<w;++x)
                    for(unsigned c=0;c<3;++c) {
                        unsigned expected=(allowed&&succeeds)?0xAB:0;
                        if(x>=left && x<left+240) expected=native[(y*240+x-left)*3+c];
                        check(rgb[(y*w+x)*3+c]==expected);
                    }
            }
        }
        for(unsigned width : {0u,239u,240u,481u,65535u}) {
            gbarecomp::HostPresentation surface(width,paint);
            check(!surface.active(*ppu) && surface.width(*ppu)==240);
        }
        ppu->set_view_margins(22,22,0,0);
        gbarecomp::HostPresentation surface(384,paint);
        check(!surface.active(*ppu) && surface.width(*ppu)==284);
        check(calls==10);
        std::array<std::uint8_t,0x18000> vram{};
        std::array<std::uint8_t,0x400> pal{};
        pal[2]=0x1f;pal[66]=0xe0;pal[67]=3;
        vram[32]=0x01;
        std::uint16_t color=0,entry=0;
        auto sample=[&](unsigned cnt,unsigned e,unsigned x,unsigned y) {
            return gba::sample_text_entry(cnt,e,x,y,vram.data(),vram.size(),pal.data(),pal.size(),color);
        };
        check(sample(0,1,0,0)&&color==31);
        check(!sample(0,1,1,0));
        check(sample(0,1|0x400,7,0)&&color==31);
        check(sample(0,1|0x800,0,7)&&color==31);
        check(sample(0,1|0x2000,0,0)&&color==0x3e0);
        vram[64]=1;
        check(sample(0x80,1|0xf000,0,0)&&color==31);
        check(!gba::sample_text_entry(0,1,0,0,vram.data(),10,pal.data(),pal.size(),color));
        check(!gba::sample_text_entry(0,1,0,0,vram.data(),vram.size(),pal.data(),2,color));
        vram[0x1800]=0x34;vram[0x1801]=0x12;
        check(gba::text_ring_entry(0xc000,256,256,vram.data(),vram.size(),entry)&&entry==0x1234);
        // Compare the standalone text sampler with the stock PPU for every
        // text-map geometry and color depth, including palette banks/flips and
        // wrapping scroll coordinates. No game assets or expected PNGs needed.
        ppu->set_view_margins(0,0,0,0);
        std::array<std::uint8_t,0x400> io{},oam{};
        std::vector<std::uint8_t> reference(gba::GbaPpu::kFramebufferBytes);
        for(unsigned i=0;i<vram.size();++i) vram[i]=std::uint8_t(i*7+i/13);
        for(unsigned i=0;i<pal.size();++i) pal[i]=std::uint8_t(i*3);
        auto write16=[](auto& data,unsigned off,unsigned value) { data[off]=std::uint8_t(value);data[off+1]=std::uint8_t(value>>8); };
        for(unsigned depth : {0u,0x80u}) for(unsigned geometry=0;geometry<4;++geometry) {
            const unsigned cnt=0x1000|depth|(geometry<<14);
            write16(io,8,cnt);write16(io,0x10,509);write16(io,0x12,257);
            ppu->render(reference.data(),0x100,io.data(),vram.data(),oam.data(),pal.data());
            for(unsigned y=0;y<160;++y) for(unsigned x=0;x<240;++x) {
                check(gba::text_ring_entry(cnt,x+509,y+257,vram.data(),vram.size(),entry));
                color=gba::text_u16(pal.data());
                gba::sample_text_entry(cnt,entry,x+509,y+257,vram.data(),vram.size(),pal.data(),pal.size(),color);
                std::uint8_t rgb[3];gba::text_rgb888(color,rgb);
                check(!std::memcmp(rgb,reference.data()+(y*240+x)*3,3));
            }
        }
        std::puts("host-only geometry, exact center, fallback, text palette/flips/bounds PASS");
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
