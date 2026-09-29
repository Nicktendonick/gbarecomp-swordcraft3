#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <iterator>
#include <array>

namespace gbarecomp {
inline bool portable_files_equal(const std::filesystem::path& a,
                                 const std::filesystem::path& b) {
    if (std::filesystem::file_size(a) != std::filesystem::file_size(b)) return false;
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    if (!left || !right) throw std::runtime_error("Cannot compare imported files.");
    std::array<char, 8192> x{}, y{};
    do {
        left.read(x.data(), x.size()); right.read(y.data(), y.size());
        if (left.bad() || right.bad()) throw std::runtime_error("Could not read imported files.");
        if (left.gcount() != right.gcount() ||
            !std::equal(x.begin(), x.begin() + left.gcount(), y.begin())) return false;
    } while (!left.eof());
    return true;
}
// Import without modifying the source. Preserve its readable filename; when
// that name already holds different bytes, use "name (2).ext", never overwrite.
// Relative settings, not fingerprint filenames, make the folder portable.
inline std::filesystem::path portable_import(const std::filesystem::path& root,
                                            const char* category,
                                            const std::filesystem::path& source) {
    namespace fs = std::filesystem;
    const auto base = fs::weakly_canonical(root);
    const auto src = fs::canonical(source);
    const auto relative = src.lexically_relative(base);
    if (!relative.empty() && *relative.begin() != "..") return src;
    if (!fs::is_regular_file(src)) throw std::runtime_error("Select a regular file to import.");
    fs::create_directories(base / category);
    for (unsigned copy = 1; copy <= 10000; ++copy) {
        const auto name = copy == 1 ? source.filename() :
            source.stem().concat(" (" + std::to_string(copy) + ")").concat(source.extension().native());
        const auto dest = base / category / name;
        if (fs::exists(dest)) {
            if (fs::is_regular_file(dest) && portable_files_equal(src, dest)) return dest;
            continue;
        }
        // A stale temporary file is never overwritten. Imports are serialized
        // by the portable starter's single-instance guard.
        auto temp = dest; temp += ".importing";
        if (fs::exists(temp)) continue;
        fs::copy_file(src, temp, fs::copy_options::none);
        fs::rename(temp, dest);
        return dest;
    }
    throw std::runtime_error("Too many files with this name; originals kept.");
}
inline std::string portable_reference(const std::filesystem::path& path,
                                      const std::filesystem::path& cache) {
    if (path.empty()) return {};
    return path.lexically_relative(cache.parent_path()).generic_string();
}
}
