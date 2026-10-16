#pragma once

// What this process holds in memory, as the operating system counts it (ADR-1392).
//
// Two numbers, because they answer different questions on a Mac. `resident` is the pages mapped in
// right now. `footprint` is what Activity Monitor calls Memory and what the kernel charges the
// process for: it includes compressed and swapped pages and -- on Apple silicon, where the GPU shares
// the memory -- every Metal buffer and texture the process allocated. So a renderer that keeps a
// previous project's textures alive shows up in `footprint` even though nothing on the CPU side
// grew, which is the case a project-switch leak check needs to see.
//
// Zero when the platform does not report them; a caller compares two readings of the same kind,
// never a reading against a constant.

#include <cstdint>

namespace avgen::core {

struct ProcessMemory {
    std::uint64_t resident = 0;  // bytes
    std::uint64_t footprint = 0; // bytes (phys_footprint on macOS; resident elsewhere)
};

[[nodiscard]] ProcessMemory processMemory();

} // namespace avgen::core
