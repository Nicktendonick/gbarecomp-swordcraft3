#pragma once

namespace gbarecomp {

// F10 capture is also a normal portable-launcher feature. Keep its gate
// independent of the debugger's layer isolation, pause and stepping controls.
inline bool debug_capture_enabled(bool visible_debugger, const char* directory) {
    return visible_debugger || (directory && directory[0]);
}

} // namespace gbarecomp
