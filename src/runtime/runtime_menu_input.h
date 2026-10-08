#pragma once

#include "recomp_runtime_ui.h"

namespace gbarecomp {

// One fresh Escape press. SDL repeat/release filtering belongs to the host.
// Share the command with scripted diagnostics so they exercise the same Resume
// callback, including clearing explicit Pause, rather than merely hiding UI.
inline void runtime_menu_escape(RecompRuntimeUi* ui) {
    if (!ui) return;
    if (!recomp_runtime_ui_is_open(ui)) {
        recomp_runtime_ui_open(ui);
        return;
    }
#if defined(RECOMP_RUNTIME_UI_HAS_TEXT)
    // The text backend already receives Escape to cancel editing. Do not also
    // dismiss its enclosing menu or send this keystroke to the guest.
    if (recomp_runtime_ui_wants_text_input(ui)) return;
#endif
    if (recomp_runtime_ui_confirmation_pending(ui)) {
        recomp_runtime_ui_handle_input(ui, RECOMP_RUNTIME_UI_INPUT_BACK, 1, 0);
        return;
    }
    recomp_runtime_ui_activate(ui, RECOMP_RUNTIME_UI_KEY_RESUME);
    // Hosts without a Resume action still support closing their menu.
    recomp_runtime_ui_close(ui);
}

} // namespace gbarecomp
