#pragma once
#include "host_frame.h"
#include "gba_ppu.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace gbarecomp {
// Fixed-width first milestone. An independent host surface must never opt the
// guest PPU into expanded scanout, OAM overrides, or different game execution.
class HostPresentation {
    unsigned requested_;
    HostFrameRenderer draw_;
    std::array<std::uint8_t, gba::GbaPpu::kFramebufferBytes> native_{};
public:
    HostPresentation(unsigned width, HostFrameRenderer draw)
        : requested_(width), draw_(draw) {}
    bool active(const gba::GbaPpu& ppu) const {
        return draw_ && !ppu.view_expanded() && requested_ > 240 && requested_ <= 480;
    }
    unsigned width(const gba::GbaPpu& ppu) const {
        return active(ppu) ? requested_ : ppu.render_width();
    }
    std::size_t bytes(const gba::GbaPpu& ppu) const {
        return std::size_t(width(ppu)) * ppu.render_height() * 3;
    }
    // fb initially contains native scanout contiguously at its beginning.
    void compose(std::vector<std::uint8_t>& fb, const gba::GbaPpu& ppu, bool permit=true) {
        if (!active(ppu) || fb.size() < native_.size()) return;
        std::memcpy(native_.data(), fb.data(), native_.size());
        fb.assign(bytes(ppu), 0);
        const unsigned left=(requested_-240)/2;
        if (permit && !draw_({native_.data(),fb.data(),requested_,160,left}))
            std::fill(fb.begin(),fb.end(),0); // partial/invalid callbacks fail closed
        for (unsigned y=0;y<160;++y)
            std::memcpy(fb.data()+(y*requested_+left)*3,native_.data()+y*240*3,240*3);
    }
};
}
