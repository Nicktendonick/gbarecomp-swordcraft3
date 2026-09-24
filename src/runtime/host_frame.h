#pragma once
#include <cstddef>
#include <cstdint>

namespace gbarecomp {
// Presentation-only RGB888 surface. Guest scanout and snapshot dimensions stay
// native. Legacy callbacks preserve the center; full-frame renderers explicitly
// claim ownership for each successfully completed callback (never sticky).
enum class HostFrameOwnership { native_center, complete_frame };
constexpr bool host_frame_owns_center(bool succeeded, HostFrameOwnership ownership) {
    return succeeded && ownership == HostFrameOwnership::complete_frame;
}
struct HostFrameContext {
    const std::uint8_t* native_rgb;
    std::uint8_t* output_rgb;
    std::uint32_t width, height, native_left;
    HostFrameOwnership* ownership = nullptr;
};
using HostFrameRenderer = bool (*)(const HostFrameContext&);
}
