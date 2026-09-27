#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <iterator>

namespace gbarecomp {
// Import without modifying the source. Content-addressed destinations avoid
// collisions and survive moving the entire portable folder to another drive.
inline std::filesystem::path portable_import(const std::filesystem::path& root,
                                            const char* category,
                                            const std::filesystem::path& source) {
    namespace fs = std::filesystem;
    const auto base = fs::weakly_canonical(root);
    const auto src = fs::canonical(source);
    const auto relative = src.lexically_relative(base);
    if (!relative.empty() && *relative.begin() != "..") return src;
    std::ifstream in(src, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read selected file.");
    std::uint64_t hash = 14695981039346656037ull;
    char buffer[8192];
    while (in.read(buffer, sizeof buffer) || in.gcount())
        for (std::streamsize i = 0; i < in.gcount(); ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]); hash *= 1099511628211ull;
        }
    if (!in.eof()) throw std::runtime_error("Could not finish reading selected file.");
    fs::create_directories(base / category);
    const auto dest = base / category /
        (std::to_string(hash) + source.extension().string());
    // Hash is a filename, NOT a trust gate. Compare existing content before reuse.
    if (fs::exists(dest)) {
        std::ifstream a(src, std::ios::binary), b(dest, std::ios::binary);
        if (fs::file_size(src) != fs::file_size(dest) ||
            !std::equal(std::istreambuf_iterator<char>(a), {},
                        std::istreambuf_iterator<char>(b)))
            throw std::runtime_error("Imported file collision; original files kept.");
        return dest;
    }
    const auto temp = dest.string() + ".importing";
    fs::copy_file(src, temp, fs::copy_options::overwrite_existing);
    fs::rename(temp, dest);
    return dest;
}
inline std::string portable_reference(const std::filesystem::path& path,
                                      const std::filesystem::path& cache) {
    if (path.empty()) return {};
    return path.lexically_relative(cache.parent_path()).generic_string();
}
}
