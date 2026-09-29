#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace gbarecomp {
namespace presentation_preferences_detail {
inline std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return {};
    return text.substr(begin, text.find_last_not_of(" \t\r") - begin + 1);
}

// An owned, exclusively created sibling is the only file ever removed on
// failure. Neither another temporary file nor the original is truncated.
inline bool replace_atomically(const std::filesystem::path& destination,
                               const std::string& contents) {
    static std::atomic<unsigned long long> sequence{0};
    std::filesystem::path temporary;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temporary = destination;
        temporary += ".presentation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(sequence.fetch_add(1)) + ".tmp";
#ifdef _WIN32
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS)
                continue;
            return false;
        }
        bool written = true;
        for (size_t offset = 0; offset < contents.size();) {
            const DWORD count = static_cast<DWORD>((std::min)(
                contents.size() - offset, size_t(1024 * 1024)));
            DWORD bytes = 0;
            if (!WriteFile(file, contents.data() + offset, count, &bytes, nullptr) || !bytes) {
                written = false;
                break;
            }
            offset += bytes;
        }
        written = written && FlushFileBuffers(file) != 0;
        const bool closed = CloseHandle(file) != 0;
        if (written && closed && MoveFileExW(temporary.c_str(), destination.c_str(),
                                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
#else
        const int file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (file < 0) {
            if (errno == EEXIST) continue;
            return false;
        }
        bool written = true;
        for (size_t offset = 0; offset < contents.size();) {
            const auto bytes = ::write(file, contents.data() + offset, contents.size() - offset);
            if (bytes < 0 && errno == EINTR) continue;
            if (bytes <= 0) { written = false; break; }
            offset += static_cast<size_t>(bytes);
        }
        written = written && ::fsync(file) == 0;
        const bool closed = ::close(file) == 0;
        if (written && closed && ::rename(temporary.c_str(), destination.c_str()) == 0)
            return true;
#endif
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return false;
}
} // namespace presentation_preferences_detail

// Update only the specified integer keys in the launcher's [Launcher] section.
// Callers validate their own ranges. The path preserves Unicode; an empty path
// deliberately means session-only.
// Invalid arguments, unreadable/malformed input, and write errors leave the
// existing file untouched. The caller must serialize its own settings writers.
inline bool update_launcher_preferences(const std::filesystem::path& path,
                                       const std::vector<std::string>& keys,
                                       const std::vector<int>& values) {
    using namespace presentation_preferences_detail;
    if (keys.empty() || keys.size() != values.size()) return false;
    if (path.empty()) return true;
    try {
        const auto& destination = path;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(destination, error);
        if (error && error != std::errc::no_such_file_or_directory) return false;
        const bool existed = std::filesystem::exists(status);
        if (existed && !std::filesystem::is_regular_file(status)) return false;
        if (existed && (status.permissions() & (std::filesystem::perms::owner_write |
            std::filesystem::perms::group_write | std::filesystem::perms::others_write)) ==
            std::filesystem::perms::none) return false;
        std::string original;
        if (existed) {
            std::ifstream input(destination, std::ios::binary);
            if (!input) return false;
            std::array<char, 4096> buffer{};
            do {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                original.append(buffer.data(), static_cast<size_t>(input.gcount()));
            } while (input);
            if (input.bad() || !input.eof()) return false;
        }
        if (original.find('\0') != std::string::npos) return false;
        struct Line { std::string body, ending; };
        std::vector<Line> lines;
        std::string newline = "\n";
        for (size_t offset = 0; offset < original.size();) {
            const auto end = original.find('\n', offset);
            const size_t count = end == std::string::npos ? original.size() - offset : end - offset;
            Line line{original.substr(offset, count), end == std::string::npos ? "" : "\n"};
            if (!line.ending.empty() && !line.body.empty() && line.body.back() == '\r') {
                line.body.pop_back();
                line.ending = "\r\n";
            }
            if (lines.empty() && !line.ending.empty()) newline = line.ending;
            lines.push_back(std::move(line));
            if (end == std::string::npos) break;
            offset = end + 1;
        }
        std::vector<bool> found(keys.size(), false);
        bool in_launcher = false, saw_launcher = false;
        size_t insert_at = lines.size();
        for (size_t index = 0; index < lines.size(); ++index) {
            auto& line = lines[index];
            std::string parse = line.body;
            const size_t bom = index == 0 && parse.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
            parse = trim(parse.substr(bom));
            if (parse.empty() || parse[0] == ';' || parse[0] == '#') continue;
            if (parse[0] == '[') {
                const auto close = parse.find(']');
                if (close == std::string::npos) return false;
                const auto suffix = trim(parse.substr(close + 1));
                if (!suffix.empty() && suffix[0] != ';' && suffix[0] != '#') return false;
                if (in_launcher) insert_at = index;
                in_launcher = parse.substr(0, close + 1) == "[Launcher]";
                saw_launcher = saw_launcher || in_launcher;
                if (in_launcher) insert_at = lines.size();
                continue;
            }
            if (!in_launcher) continue;
            const auto equal = line.body.find('=');
            if (equal == std::string::npos) continue;
            const auto key = trim(line.body.substr(bom, equal - bom));
            for (size_t key_index = 0; key_index < keys.size(); ++key_index) {
                if (key != keys[key_index]) continue;
                const auto value_start = line.body.find_first_not_of(" \t", equal + 1);
                const size_t begin = value_start == std::string::npos ? line.body.size() : value_start;
                const auto comment = line.body.find_first_of(";#", begin);
                size_t suffix_start = comment == std::string::npos ? line.body.size() : comment;
                while (suffix_start > begin && (line.body[suffix_start - 1] == ' ' ||
                                                line.body[suffix_start - 1] == '\t')) --suffix_start;
                line.body = line.body.substr(0, begin) + std::to_string(values[key_index]) +
                            line.body.substr(suffix_start);
                found[key_index] = true;
            }
        }
        std::string additions;
        if (!saw_launcher) additions = "[Launcher]" + newline;
        for (size_t index = 0; index < keys.size(); ++index)
            if (!found[index]) additions += keys[index] + " = " + std::to_string(values[index]) + newline;
        std::string updated;
        for (size_t index = 0; index <= lines.size(); ++index) {
            if (index == insert_at && !additions.empty()) {
                if (!updated.empty() && updated.back() != '\n') updated += newline;
                updated += additions;
            }
            if (index < lines.size()) updated += lines[index].body + lines[index].ending;
        }
        if (updated == original) return true;
        return replace_atomically(destination, updated);
    } catch (...) {
        return false;
    }
}
inline bool save_presentation_preferences(const std::filesystem::path& path, int scaling,
                                         int effect, int strength) {
    if (scaling < 0 || scaling > 3 || effect < 0 || effect > 2 || strength < 0 || strength > 100)
        return false;
    return update_launcher_preferences(path,
        {"linear_filter", "sharp_filter", "smooth_filter", "screen_effect", "screen_effect_strength"},
        {scaling == 1, scaling == 2, scaling == 3, effect, strength});
}

inline std::optional<int> read_host_aspect_preference(const std::filesystem::path& path, int count) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        std::optional<int> result;
        bool first = true, section = false;
        size_t bytes = 0;
        std::string line;
        while (std::getline(input, line)) {
            bytes += line.size();
            if (bytes > 1024 * 1024 || line.find('\0') != std::string::npos) return {};
            if (first && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
            first = false;
            line = presentation_preferences_detail::trim(line.substr(0, line.find_first_of(";#")));
            if (line.empty()) continue;
            if (line[0] == '[') {
                if (line.back() != ']' || line.find(']') != line.size() - 1) return {};
                section = line == "[Launcher]";
                continue;
            }
            const auto equal = line.find('=');
            if (!section || equal == std::string::npos ||
                presentation_preferences_detail::trim(line.substr(0, equal)) != "host_aspect_index") continue;
            if (result) return {};
            const auto text = presentation_preferences_detail::trim(line.substr(equal + 1));
            int value = -1;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < 0 || value >= count)
                return {};
            result = value;
        }
        return input.bad() ? std::optional<int>{} : result;
    } catch (...) { return {}; }
}
inline bool save_host_aspect_preference(const std::filesystem::path& path, int index, int count) {
    return index >= 0 && index < count &&
        update_launcher_preferences(path, {"host_aspect_index"}, {index});
}
} // namespace gbarecomp
