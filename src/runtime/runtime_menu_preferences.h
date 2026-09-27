#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace gbarecomp {
// Dedicated host-menu preferences, never mixed with game saves or launcher
// settings. Loading does not create a file; callers supply a portable path.
class RuntimeMenuPreferences {
    std::filesystem::path path_;
    bool pause_on_open_ = false;
public:
    void load(std::filesystem::path path, bool fallback) {
        path_ = std::move(path);
        pause_on_open_ = fallback;
        std::ifstream in(path_);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "pause_on_open=0") pause_on_open_ = false;
            else if (line == "pause_on_open=1") pause_on_open_ = true;
        }
    }
    bool pause_on_open() const { return pause_on_open_; }
    bool paused(bool manual, bool menu_open) const {
        return manual || (pause_on_open_ && menu_open);
    }
    bool set_pause_on_open(bool value) {
        if (!path_.empty()) {
            std::error_code ec;
            if (!path_.parent_path().empty()) std::filesystem::create_directories(path_.parent_path(), ec);
            if (ec) return false;
            auto temp = path_; temp += ".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out << "[RuntimeMenu]\npause_on_open=" << (value ? 1 : 0) << '\n';
                out.flush();
                if (!out) return false;
            }
#if defined(_WIN32)
            const bool ok = MoveFileExW(temp.c_str(), path_.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            std::filesystem::rename(temp, path_, ec);
            const bool ok = !ec;
#endif
            if (!ok) {
                std::filesystem::remove(temp, ec);
                return false; // keep the previous preference intact
            }
        }
        pause_on_open_ = value;
        return true;
    }
};
} // namespace gbarecomp
