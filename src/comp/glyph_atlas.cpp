#include "comp/glyph_atlas.hpp"

#include "core/log.hpp"

namespace avgen::comp {

GlyphAtlas::GlyphAtlas() {
    // 0 is "far outside the glyph", so an unwritten atlas texel draws nothing rather than a block.
    texels_.assign(static_cast<std::size_t>(kSize) * kSize, 0);
}

void GlyphAtlas::clear() {
    texels_.assign(static_cast<std::size_t>(kSize) * kSize, 0);
    entries_.clear();
    shelfX_ = 1;
    shelfY_ = 1;
    shelfHeight_ = 0;
    reportedFull_ = false;
    ++version_;
}

bool GlyphAtlas::place(std::uint32_t w, std::uint32_t h, std::uint32_t& x, std::uint32_t& y) {
    if (w + 2 > kSize || h + 2 > kSize) {
        return false;
    }
    if (shelfX_ + w + 1 > kSize) { // next shelf
        shelfX_ = 1;
        shelfY_ += shelfHeight_ + 1;
        shelfHeight_ = 0;
    }
    if (shelfY_ + h + 1 > kSize) {
        return false;
    }
    x = shelfX_;
    y = shelfY_;
    shelfX_ += w + 1;
    shelfHeight_ = std::max(shelfHeight_, h);
    return true;
}

const GlyphAtlas::Entry* GlyphAtlas::acquire(const FontDesc& font, std::uint16_t glyphId) {
    const std::string key = font.key() + "#" + std::to_string(glyphId);
    if (const auto it = entries_.find(key); it != entries_.end()) {
        return &it->second;
    }
    auto image = fontBackend().glyph(font, glyphId);
    if (!image) {
        return nullptr;
    }
    Entry entry;
    entry.originX = image->originX;
    entry.originY = image->originY;
    entry.sizeX = image->sizeX;
    entry.sizeY = image->sizeY;
    if (image->sdf.empty()) {
        // A space: a real, cached, zero-area entry. Without this every space would re-enter the
        // type engine on every geometry rebuild.
        entry.sizeX = 0.0f;
        entry.sizeY = 0.0f;
        auto [it, inserted] = entries_.emplace(key, entry);
        return &it->second;
    }
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    if (!place(image->sdf.width, image->sdf.height, x, y)) {
        if (!reportedFull_) {
            reportedFull_ = true;
            log::warn("composition: the {}x{} glyph atlas is full after {} glyphs; further glyphs will not draw",
                      kSize, kSize, entries_.size());
        }
        return nullptr;
    }
    for (std::uint32_t row = 0; row < image->sdf.height; ++row) {
        const std::size_t src = static_cast<std::size_t>(row) * image->sdf.width;
        const std::size_t dst = static_cast<std::size_t>(y + row) * kSize + x;
        std::copy_n(image->sdf.texels.begin() + static_cast<std::ptrdiff_t>(src), image->sdf.width,
                    texels_.begin() + static_cast<std::ptrdiff_t>(dst));
    }
    ++version_;
    const auto inv = 1.0f / static_cast<float>(kSize);
    entry.u0 = static_cast<float>(x) * inv;
    entry.v0 = static_cast<float>(y) * inv;
    entry.u1 = static_cast<float>(x + image->sdf.width) * inv;
    entry.v1 = static_cast<float>(y + image->sdf.height) * inv;
    auto [it, inserted] = entries_.emplace(key, entry);
    return &it->second;
}

} // namespace avgen::comp
