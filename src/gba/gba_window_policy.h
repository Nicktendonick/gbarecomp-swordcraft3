#pragma once

#include <cstdint>

namespace gba::detail {

constexpr bool replay_margin_layer_allowed(unsigned mask, unsigned layer,
                                            int hx, int native_width) {
    return (hx >= 0 && hx < native_width) ||
           (layer < 5 && (mask & (1u << layer)) != 0);
}

// Pure control-state helpers shared by wide replay and its state-only tests.
// Signed extensions never alter horizontal membership in native columns.
constexpr bool replay_window_horizontal_contains(std::uint16_t guest_bounds,
                                                 int hx, int native_width,
                                                 bool extended, int left, int right) {
    if ((hx < 0 || hx >= native_width) && extended && right > left)
        return hx >= left && hx < right;
    const int x1 = (guest_bounds >> 8) & 0xff;
    int x2 = guest_bounds & 0xff;
    if (x2 > native_width || x1 > x2) x2 = native_width;
    return hx >= x1 && hx < x2;
}

// All six controls travel together: BG0..3, OBJ, and color-effect enable.
// Membership has already applied enable flags and vertical/horizontal bounds.
constexpr std::uint16_t replay_window_control(bool any_window, bool win0,
                                              bool win1, bool obj_window,
                                              std::uint16_t winin,
                                              std::uint16_t winout) {
    if (!any_window) return 0x3f;
    if (win0) return winin & 0x3f;
    if (win1) return (winin >> 8) & 0x3f;
    if (obj_window) return (winout >> 8) & 0x3f;
    return winout & 0x3f;
}

} // namespace gba::detail
