#pragma once

// One signed-distance atlas for every glyph in the composition (ADR-081).
//
// Shared across layers and across fonts, so a lyric repeated in ten layers rasterises its letters
// once. Entries are keyed by face and glyph id -- not by size -- because the field is scale-free:
// a keyframed font size, or a scale driven by the bass, re-uses the same texels and rebuilds
// nothing. This is what keeps animated text off the per-frame cost sheet entirely.
//
// Packing is a shelf: glyphs arrive in an order nobody controls and are never removed, so the
// simplest packer that does not fragment is the right one. A full atlas drops the glyph and says
// so once, rather than silently drawing the wrong letter.

#include "comp/font.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace avgen::comp {

class GlyphAtlas {
public:
    struct Entry {
        float u0 = 0.0f; // texture coordinates of the glyph's field
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        float originX = 0.0f; // em, relative to the pen
        float originY = 0.0f;
        float sizeX = 0.0f;   // em, the field's extent (the SDF spread margin is inside this)
        float sizeY = 0.0f;
        [[nodiscard]] bool drawable() const { return sizeX > 0.0f && sizeY > 0.0f; }
    };

    static constexpr std::uint32_t kSize = 2048;
    // The distance from the edge that the 0..1 range covers, expressed in em. The shader turns
    // an outline or glow width in em into signed-distance units with this.
    static constexpr float kSpreadEm = kSdfSpreadTexels / static_cast<float>(kSdfEmTexels);

    GlyphAtlas();

    // The entry for a glyph, rasterising and packing it on first use. Null when the font is
    // unavailable or the atlas is full.
    [[nodiscard]] const Entry* acquire(const FontDesc& font, std::uint16_t glyphId);

    [[nodiscard]] std::uint32_t width() const { return kSize; }
    [[nodiscard]] std::uint32_t height() const { return kSize; }
    [[nodiscard]] const std::vector<std::uint8_t>& texels() const { return texels_; }
    // Bumped whenever texels change, so the GPU side knows when to re-upload.
    [[nodiscard]] std::uint64_t version() const { return version_; }
    [[nodiscard]] std::size_t glyphCount() const { return entries_.size(); }
    [[nodiscard]] std::uint32_t usedRows() const { return shelfY_ + shelfHeight_; }
    void clear();

private:
    [[nodiscard]] bool place(std::uint32_t w, std::uint32_t h, std::uint32_t& x, std::uint32_t& y);

    std::vector<std::uint8_t> texels_;
    std::unordered_map<std::string, Entry> entries_;
    std::uint32_t shelfX_ = 1;
    std::uint32_t shelfY_ = 1;
    std::uint32_t shelfHeight_ = 0;
    std::uint64_t version_ = 1;
    bool reportedFull_ = false;
};

} // namespace avgen::comp
