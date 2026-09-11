#pragma once
#include <cstddef>
#include <cstdint>

namespace gbarecomp {
// Presentation-only RGB888 surface. Guest scanout and snapshot dimensions stay
// native. The runtime restores the authentic center after the callback.
struct HostFrameContext {
    const std::uint8_t* native_rgb;
    std::uint8_t* output_rgb;
    std::uint32_t width, height, native_left;
};
using HostFrameRenderer = bool (*)(const HostFrameContext&);
}
