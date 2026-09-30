#pragma once
#include <cstdint>

namespace gbarecomp {
// Optional exact-instruction value substitution for CPU RAM writes. Timing,
// tracing and the actual write still belong to the normal bus path. DMA and
// MMIO are deliberately excluded. A game must authenticate its code layout.
using RamWriteOverride = bool (*)(std::uint32_t pc, std::uint32_t address,
                                 unsigned width, std::uint32_t original,
                                 std::uint32_t* replacement);
extern RamWriteOverride ram_write_override;
inline std::uint32_t mapped_ram_write(RamWriteOverride callback,
        std::uint32_t pc, std::uint32_t address, unsigned width, std::uint32_t value) {
    if (!callback || (width != 1 && width != 2 && width != 4)) return value;
    const bool ewram = address >= 0x02000000 && address <= 0x02040000 - width;
    const bool iwram = address >= 0x03000000 && address <= 0x03008000 - width;
    if (!ewram && !iwram) return value;
    std::uint32_t replacement = value;
    if (!callback(pc, address, width, value, &replacement)) return value;
    return width == 1 ? replacement & 0xffu : width == 2 ? replacement & 0xffffu : replacement;
}
}
