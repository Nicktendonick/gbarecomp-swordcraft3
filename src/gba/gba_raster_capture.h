#pragma once
#include "gba_ppu.h"
#include <array>
#include <cstring>
#include <memory>

namespace gba {
// Native GBA bridge for immutable, per-line rendering. Inspired by the
// SuperMetroidRecomp capture/replay architecture; no SNES decoding is used.
// Phase one deliberately reuses the native GBA pixel kernel. This proves
// isolation/timing, not an independent renderer's visual fidelity.
class GbaRasterCapture {
public:
    struct Line {
        std::array<std::uint8_t,0x400> io, oam, pal;
        std::array<std::uint8_t,0x18000> vram;
        std::array<GbaPpu::AffineLineState,2> affine;
        std::uint16_t dispcnt;
    };
private:
    std::unique_ptr<std::array<Line,160>> lines_ = std::make_unique<std::array<Line,160>>();
    unsigned captured_=0;
    bool valid_=true;
public:
    void reset() { captured_=0; valid_=true; }
    bool complete() const { return valid_ && captured_==160; }
    // Owned immutable memory, available only for complete frames. Callers must
    // finish reading before the next capture/reset. No mutable guest pointers.
    const Line* line(unsigned y) const {
        return complete() && y<160 ? &(*lines_)[y] : nullptr;
    }
    bool capture(const NativeRasterLineContext& c) {
        if (c.y==0) reset();
        if (!valid_ || c.y>=160 || c.y!=captured_ || !c.ppu ||
            c.ppu->view_expanded() || !c.io || !c.vram || !c.oam || !c.pal) {
            valid_=false; return false;
        }
        auto& l=(*lines_)[c.y];
        std::memcpy(l.io.data(),c.io,l.io.size());
        std::memcpy(l.vram.data(),c.vram,l.vram.size());
        std::memcpy(l.oam.data(),c.oam,l.oam.size());
        std::memcpy(l.pal.data(),c.pal,l.pal.size());
        l.dispcnt=c.dispcnt; l.affine=c.ppu->affine_line_;
        ++captured_; return true;
    }
    bool draw_native(std::uint8_t* output, std::size_t bytes) const {
        if (!complete() || !output || bytes!=GbaPpu::kFramebufferBytes) return false;
        auto replay=std::make_unique<GbaPpu>();
        replay->presentation_callbacks_=false;
        for (unsigned y=0; y<160; ++y) {
            const auto& l=(*lines_)[y];
            replay->affine_line_=l.affine;
            replay->render_scanline(y,l.dispcnt,l.io.data(),l.vram.data(),l.oam.data(),l.pal.data());
        }
        replay->mark_framebuffer_latched();
        std::memcpy(output,replay->latched_framebuffer(),bytes);
        return true;
    }
    // Replay the same immutable rows through the wide pixel kernel, with an
    // instance-local policy. No guest execution or global policy mutation.
    // Callers still authenticate scenes and compare the original viewport.
    bool draw_view(std::uint8_t* output, std::size_t bytes, unsigned width,
                   const GbaReplayViewPolicy& policy) const {
        if(!complete() || !output || width<=240 || width>GbaPpu::kMaxRenderWidth ||
           bytes!=std::size_t(width)*160*3) return false;
        auto replay=std::make_unique<GbaPpu>();
        replay->presentation_callbacks_=false;
        replay->replay_view_policy_=&policy;
        const unsigned left=(width-240)/2;
        replay->set_view_margins(left,width-240-left,0,0);
        for(unsigned y=0;y<160;++y) {
            const auto& l=(*lines_)[y];
            replay->affine_line_=l.affine;
            replay->render_scanline(y,l.dispcnt,l.io.data(),l.vram.data(),l.oam.data(),l.pal.data());
        }
        replay->mark_framebuffer_latched();
        std::memcpy(output,replay->latched_framebuffer(),bytes);
        return true;
    }
};
}
