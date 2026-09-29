#include "presentation_filters.h"

#include <algorithm>
#include <cstdlib>
#include <new>
#include <stdexcept>

namespace gbarecomp::runtime {

bool valid_presentation_size(int width, int height) noexcept {
    return width > 0 && height > 0 &&
           width <= kMaxPresentationDimension &&
           height <= kMaxPresentationDimension &&
           static_cast<std::size_t>(width) * static_cast<std::size_t>(height) <=
               kMaxPresentationSourcePixels;
}

namespace {

struct Rgb {
    int r, g, b;
};

int difference(const Rgb& a, const Rgb& b) noexcept {
    // Largest channel delta: cheap, symmetric, and does not mistake different
    // saturated colours for matching luminance. All intermediates fit an int.
    return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
}

bool equal(const Rgb& a, const Rgb& b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

constexpr std::uint32_t kInverseWeight[] = {5462, 5042, 4682, 4370, 4096};

constexpr bool normalization_is_exact() noexcept {
    // An arithmetic identity over the complete bounded numerator domain, not
    // an image/framebuffer assertion. Ceil(65536/weight) introduces no rounding
    // differences for any possible input to this integer normalization.
    for (int weight = 12; weight <= 16; ++weight)
        for (int value = 0; value <= weight * 255 + weight / 2; ++value)
            if ((static_cast<std::uint32_t>(value) * kInverseWeight[weight - 12] >> 16) !=
                static_cast<std::uint32_t>(value / weight)) return false;
    return true;
}
static_assert(normalization_is_exact(), "fixed-point normalization must be exact");

std::uint32_t smooth_quadrant(const Rgb& center, const Rgb& horizontal,
                              const Rgb& vertical, const Rgb& diagonal) noexcept {
    if (equal(center, horizontal) && equal(center, vertical) && equal(center, diagonal))
        return (static_cast<std::uint32_t>(center.r) << 16) |
               (static_cast<std::uint32_t>(center.g) << 8) |
               static_cast<std::uint32_t>(center.b);
    int r = center.r * 12;
    int g = center.g * 12;
    int b = center.b * 12;
    int weight = 12;
    auto add = [&](const Rgb& sample, int sample_weight) {
        r += sample.r * sample_weight;
        g += sample.g * sample_weight;
        b += sample.b * sample_weight;
        weight += sample_weight;
    };

    const int hc = difference(horizontal, center);
    const int vc = difference(vertical, center);
    if (hc >= 48 && vc >= 48 && difference(horizontal, vertical) <= 24 &&
        difference(horizontal, diagonal) <= 32 &&
        difference(vertical, diagonal) <= 32) {
        // Three agreeing neighbours describe a corner in this quadrant. Keep
        // 3/4 of the center, with the remaining 1/4 contributed by the edge.
        add(horizontal, 2);
        add(vertical, 2);
    } else {
        // Do not smear contrasting edges. Similar colours receive a small,
        // local interpolation that remains dominated by the center sample.
        if (hc < 64) add(horizontal, 1);
        if (vc < 64) add(vertical, 1);
        if (difference(diagonal, center) < 48) add(diagonal, 1);
    }
    if (weight == 12) { // No neighbours were accepted: the center is unchanged.
        return (static_cast<std::uint32_t>(center.r) << 16) |
               (static_cast<std::uint32_t>(center.g) << 8) |
               static_cast<std::uint32_t>(center.b);
    }
    // Replace three variable integer divisions per quadrant with an exact
    // bounded reciprocal, without changing the smoothing or rounding rules.
    const auto inverse = kInverseWeight[weight - 12];
    const auto normalized_r = static_cast<std::uint32_t>(r + weight / 2) * inverse >> 16;
    const auto normalized_g = static_cast<std::uint32_t>(g + weight / 2) * inverse >> 16;
    const auto normalized_b = static_cast<std::uint32_t>(b + weight / 2) * inverse >> 16;
    return (normalized_r << 16) | (normalized_g << 8) | normalized_b;
}

} // namespace

bool SmoothScaler::scale2x(const std::uint8_t* rgb, int width, int height) noexcept {
    width_ = height_ = 0;
    if (!rgb || !valid_presentation_size(width, height)) {
        output_.clear();
        return false;
    }
    const auto pixels = static_cast<std::size_t>(width) * height;
    // Reject overlapping input/output before a vector resize could invalidate
    // the source. Avoid ordering pointers from unrelated allocations.
    const auto source = reinterpret_cast<std::uintptr_t>(rgb);
    const auto destination = reinterpret_cast<std::uintptr_t>(output_.data());
    const auto destination_bytes = output_.size() * sizeof(std::uint32_t);
    if (destination_bytes &&
        (source >= destination ? source - destination < destination_bytes
                               : destination - source < pixels * 3)) {
        output_.clear();
        return false;
    }
    try {
        output_.resize(pixels * 4);
    } catch (const std::bad_alloc&) {
        output_.clear();
        return false;
    } catch (const std::length_error&) {
        output_.clear();
        return false;
    }

    const int output_width = width * 2;
    const auto sample = [&](int x, int y) -> Rgb {
        const auto offset = (static_cast<std::size_t>(y) * width + x) * 3;
        return {rgb[offset], rgb[offset + 1], rgb[offset + 2]};
    };
    for (int y = 0; y < height; ++y) {
        const int above = std::max(y - 1, 0);
        const int below = std::min(y + 1, height - 1);
        for (int x = 0; x < width; ++x) {
            const int left = std::max(x - 1, 0);
            const int right = std::min(x + 1, width - 1);
            const Rgb center = sample(x, y);
            const Rgb h0 = sample(left, y);
            const Rgb h1 = sample(right, y);
            const Rgb v0 = sample(x, above);
            const Rgb v1 = sample(x, below);
            const auto offset = static_cast<std::size_t>(y * 2) * output_width + x * 2;
            output_[offset] = smooth_quadrant(center, h0, v0, sample(left, above));
            output_[offset + 1] = smooth_quadrant(center, h1, v0, sample(right, above));
            output_[offset + output_width] = smooth_quadrant(center, h0, v1, sample(left, below));
            output_[offset + output_width + 1] = smooth_quadrant(center, h1, v1, sample(right, below));
        }
    }
    width_ = output_width;
    height_ = height * 2;
    return true;
}

bool build_screen_effect_mask(int base_w, int base_h, int effect, int strength,
                              EffectMask& mask) noexcept {
    mask.width = mask.height = 0;
    mask.rgba.clear();
    if (!valid_presentation_size(base_w, base_h) ||
        effect < static_cast<int>(ScreenEffect::None) ||
        effect > static_cast<int>(ScreenEffect::Crt)) return false;

    strength = std::clamp(strength, 0, 100);
    if (effect == static_cast<int>(ScreenEffect::None) || !strength) return true;

    const int width = base_w * 3;
    const int height = base_h * 3;
    const auto byte_count = static_cast<std::size_t>(width) * height * 4;
    try {
        mask.rgba.resize(byte_count, 0);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }

    for (int y = 0; y < height; ++y) {
        // Coordinates normalized to the frame, using integer arithmetic. The
        // edge falloff is static, so it has no temporal flicker or added input lag.
        const int edge_y = (2 * y + 1 - height) * 256 / height;
        for (int x = 0; x < width; ++x) {
            int opacity;
            if (effect == static_cast<int>(ScreenEffect::Lcd)) {
                // A 1/3-pixel seam around each pixel. Corners are slightly darker.
                opacity = (x % 3 == 2 ? 30 : 0) + (y % 3 == 2 ? 42 : 0);
            } else {
                const int edge_x = (2 * x + 1 - width) * 256 / width;
                const int vignette = (edge_x * edge_x + edge_y * edge_y) * 16 /
                                     (256 * 256);
                opacity = (y % 3 == 2 ? 100 : 0) +
                          (x % 3 == 2 ? 15 : 0) + vignette;
            }
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
            mask.rgba[offset + 3] = static_cast<std::uint8_t>(opacity * strength / 100);
        }
    }
    mask.width = width;
    mask.height = height;
    return true;
}

} // namespace gbarecomp::runtime
