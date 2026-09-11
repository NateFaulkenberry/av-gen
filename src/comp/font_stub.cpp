// The font backend for builds without a platform type engine (ADR-083). Text layers keep their
// content, their parameters and their place in the timeline; they draw nothing, and say so once.

#include "comp/font.hpp"

#include "core/log.hpp"

namespace avgen::comp {

namespace {

class StubBackend final : public FontBackend {
public:
    const std::vector<std::string>& families() override { return families_; }
    FontResolution resolve(const FontDesc&) override { return {}; }
    Result<ShapedText> shape(const FontDesc&, std::string_view) override {
        warnOnce();
        return fail("this build has no font backend");
    }
    Result<GlyphImage> glyph(const FontDesc&, std::uint16_t) override {
        warnOnce();
        return fail("this build has no font backend");
    }
    [[nodiscard]] bool available() const override { return false; }

private:
    void warnOnce() {
        if (!warned_) {
            warned_ = true;
            log::warn("composition: this build has no font backend; text layers will not draw");
        }
    }
    std::vector<std::string> families_;
    bool warned_ = false;
};

} // namespace

FontBackend& fontBackend() {
    static StubBackend backend;
    return backend;
}

} // namespace avgen::comp
