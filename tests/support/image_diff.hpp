#pragma once

// Comparing two rendered frames without killing the process that found the difference (ADR-362).
//
// `REQUIRE(a.rgba == b.rgba)` over a 1920x1080 frame is eight megabytes of `std::vector<uint8_t>`,
// and on failure Catch2 stringifies BOTH operands into the report. ADR-358 recorded the result:
// `avgen_render_tests` dies with a bus error in a full-suite run, and the last thing it prints is
// the `FAILED:` line for the assertion, with no `with expansion:` after it. The suite that found a
// real difference then destroys the evidence and every test after it.
//
// So a frame comparison here reports a COUNT and a FIRST OFFSET instead. That survives printing,
// and it is strictly more useful than the equality was: "1,742 of 8,294,400 bytes differ, first at
// 3,118,092" says where to look, and `a == b` never said anything at all.
//
// This is the form `tests/rendering/test_gpu.cpp` and `test_resource_lifetime_gpu.cpp` already
// used. It is here so the other call sites can use it without copying it a seventh time.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace avgen::testing {

struct ByteDiff {
    bool sameSize = false;
    std::size_t sizeA = 0;
    std::size_t sizeB = 0;
    std::size_t differing = 0;       // 0 and sameSize means identical
    std::ptrdiff_t firstOffset = -1; // -1 when nothing differs
    int maxDelta = 0;                // the largest absolute difference at any byte

    [[nodiscard]] bool identical() const { return sameSize && differing == 0; }

    // Short enough for an INFO line and specific enough to act on.
    [[nodiscard]] std::string describe() const {
        if (!sameSize) {
            return "sizes differ: " + std::to_string(sizeA) + " vs " + std::to_string(sizeB);
        }
        if (differing == 0) {
            return "identical (" + std::to_string(sizeA) + " bytes)";
        }
        return std::to_string(differing) + " of " + std::to_string(sizeA) +
               " bytes differ, first at " + std::to_string(firstOffset) +
               ", max delta " + std::to_string(maxDelta);
    }
};

[[nodiscard]] inline ByteDiff byteDiff(std::span<const std::uint8_t> a,
                                       std::span<const std::uint8_t> b) {
    ByteDiff d;
    d.sizeA = a.size();
    d.sizeB = b.size();
    d.sameSize = a.size() == b.size();
    if (!d.sameSize) {
        return d;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] == b[i]) {
            continue;
        }
        if (d.firstOffset < 0) {
            d.firstOffset = static_cast<std::ptrdiff_t>(i);
        }
        ++d.differing;
        const int delta = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
        if (delta > d.maxDelta) {
            d.maxDelta = delta;
        }
    }
    return d;
}

[[nodiscard]] inline ByteDiff byteDiff(const std::vector<std::uint8_t>& a,
                                       const std::vector<std::uint8_t>& b) {
    return byteDiff(std::span<const std::uint8_t>(a), std::span<const std::uint8_t>(b));
}

} // namespace avgen::testing
