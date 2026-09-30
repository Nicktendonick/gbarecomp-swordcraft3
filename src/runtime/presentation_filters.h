// Host-only presentation effects. These functions never read or alter guest
// state, and must be applied after game composition and before native UI.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gbarecomp::runtime {

// Values are persisted by the launcher. Do not reorder them.
enum class ScreenEffect : int { None = 0, Lcd = 1, Crt = 2 };

// Keep malformed dimensions from allocating arbitrarily large buffers. The
// normal GBA surfaces (240x160 and 384x160) are well within these limits.
constexpr int kMaxPresentationDimension = 4096;
constexpr std::size_t kMaxPresentationSourcePixels = 1024 * 1024;

bool valid_presentation_size(int width, int height) noexcept;

// Lightweight, original edge-aware 2x interpolation, not xBRZ/xBR/HQx. The
// algorithm softly rounds a high-contrast corner only when both adjacent
// samples and the diagonal agree, and gently mixes nearby colours elsewhere.
// It deliberately preserves most of each source sample; fine lettering may
// nevertheless look different, so this is an opt-in presentation preference.
class SmoothScaler {
public:
    // Input is immutable, tightly packed RGB24, at least width*height*3 bytes.
    // It must not alias output(). Guest framebuffer storage is never replaced.
    // Output uses packed 0x00RRGGBB suitable for an ARGB8888 texture with blending
    // disabled. The backing allocation is reused across frames and resizes.
    // Failure clears dimensions/output; callers should present their raw input.
    bool scale2x(const std::uint8_t* rgb, int width, int height) noexcept;
    const std::vector<std::uint32_t>& output() const noexcept { return output_; }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }

private:
    std::vector<std::uint32_t> output_;
    // Current-call RGB32 scratch with duplicated edge samples. Reused storage,
    // never retained frame content: every entry is rebuilt before it is read.
    std::vector<std::uint32_t> packed_;
    int width_ = 0;
    int height_ = 0;
};

struct EffectMask {
    // Byte order is R,G,B,A, independent of host endian. Use SDL_PIXELFORMAT_RGBA32
    // with normal alpha blending. Black translucent masks only darken the game;
    // they cannot introduce bright halos or change the native menu's colours.
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
};

// Creates a static, game-pixel-aligned 3x overlay. LCD draws a fine pixel grid;
// CRT draws scanlines/aperture grille and a gentle edge vignette. It is not a
// full physical display simulation. Strength is clamped to 0..100. Build only
// on size/effect/strength changes and reuse the uploaded texture every frame.
// A disabled effect/zero strength returns true with an empty mask; malformed
// sizes/effects or allocation failure return false and also clear the mask.
bool build_screen_effect_mask(int base_w, int base_h, int effect, int strength,
                              EffectMask& mask) noexcept;

} // namespace gbarecomp::runtime
