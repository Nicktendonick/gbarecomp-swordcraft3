#pragma once
#include <string_view>

namespace gbarecomp {
// Stable launcher/CLI indices, shared by live presentation and persistence.
inline constexpr int screen_model_count = 5;
inline constexpr const char* screen_model_tokens[] = {"raw", "unlit", "frontlit", "backlit", "classic"};
inline constexpr const char* screen_model_labels[] = {"Raw", "Unlit", "Frontlit", "Backlit", "Classic"};
inline constexpr const char* screen_model_descriptions[] = {
    "Unmodified game colours. Changes the game picture only; other filters stay as chosen.",
    "A darker colour simulation of the original unlit handheld screen. Other filters stay as chosen.",
    "Colour simulation of a front-lit handheld screen. Other filters stay as chosen.",
    "Colour simulation of a later backlit handheld screen. Other filters stay as chosen.",
    "A traditional GBA colour-correction look. Other filters stay as chosen."
};
inline bool valid_screen_model(int index) { return index >= 0 && index < screen_model_count; }
inline int screen_model_from_token(std::string_view token) {
    for (int i = 0; i < screen_model_count; ++i)
        if (token == screen_model_tokens[i]) return i;
    return -1;
}
}
