#pragma once

// The text layer (ADR-083): the first layer kind, and the reason the composition system exists.
//
// Geometry is one quad per glyph in **em units** -- the font size is in the per-frame transform,
// not in the vertices -- so a keyframed or beat-driven size animates without re-shaping the string
// or touching the vertex buffer. The string is re-shaped only when the content, the face, the
// tracking, the line spacing or the alignment changes.
//
// The outline, the glow and the drop shadow all come out of the same signed distance field the
// glyph is drawn from, in the same pass. That is the whole argument for an SDF atlas over a
// per-size bitmap: three effects for no extra passes and no extra memory, at any size.

#include "comp/font.hpp"
#include "comp/layer.hpp"

#include <string>

namespace avgen::comp {

enum class TextAlign : std::uint8_t { Left, Center, Right };
[[nodiscard]] const char* textAlignName(TextAlign align);
[[nodiscard]] Result<TextAlign> textAlignFromName(std::string_view name);

class TextLayer final : public Layer {
public:
    TextLayer();

    [[nodiscard]] LayerKind kind() const override { return LayerKind::Text; }
    [[nodiscard]] std::unique_ptr<Layer> clone() const override;

    // ---- content and typography (changing any of these re-shapes) ----
    [[nodiscard]] const std::string& text() const { return text_; }
    void setText(std::string text);
    [[nodiscard]] const FontDesc& font() const { return font_; }
    void setFont(FontDesc font);
    [[nodiscard]] TextAlign align() const { return align_; }
    void setAlign(TextAlign align);
    [[nodiscard]] float tracking() const { return tracking_; }   // em, added between glyphs
    void setTracking(float em);
    [[nodiscard]] float lineSpacing() const { return lineSpacing_; } // multiple of the font's leading
    void setLineSpacing(float multiple);

    // ---- appearance (animatable; no rebuild) ----
    float size = 0.09f;                        // fraction of the frame height, per em
    float outlineWidth = 0.0f;                 // em
    glm::vec4 outlineColor{0.0f, 0.0f, 0.0f, 1.0f};
    float glowRadius = 0.0f;                   // em, capped at the atlas spread
    glm::vec4 glowColor{1.0f, 1.0f, 1.0f, 0.6f};
    glm::vec2 shadowOffset{0.012f, -0.012f};   // em
    float shadowOpacity = 0.0f;                // 0 = no shadow, and no second run

    // ---- Layer ----
    std::uint32_t buildGeometry(std::vector<LayerVertex>& vertices, std::vector<LayerRun>& runs,
                                std::uint32_t firstItem, GlyphAtlas& atlas) override;
    void buildItems(const Frame& frame, double seconds, LayerItem* items) const override;
    [[nodiscard]] float unitScale(const Frame& frame) const override;
    [[nodiscard]] glm::vec2 boxLocal() const override { return box_; }

    // The measured text box in em units, from the last geometry build (tests and the inspector).
    [[nodiscard]] glm::vec2 boxEm() const { return box_; }
    // What the font resolved to at the last build: empty until the layer has been built once.
    [[nodiscard]] const FontResolution& resolvedFont() const { return resolved_; }

protected:
    void attachKind(params::ParameterSet& set) override;
    void detachKind() override;
    void pushAuthoredKind() override;
    void pullAuthoredKind() override;
    void collectPaths(std::vector<std::string>& out) const override;
    void writeJson(nlohmann::json& j) const override;
    [[nodiscard]] Result<void> readJson(const nlohmann::json& j) override;

private:
    [[nodiscard]] float resolvedSize() const;

    std::string text_ = "Lyric";
    FontDesc font_;
    TextAlign align_ = TextAlign::Center;
    float tracking_ = 0.0f;
    float lineSpacing_ = 1.0f;
    glm::vec2 box_{0.0f, 1.0f};
    FontResolution resolved_;
    bool reportedSubstitution_ = false;

    params::Parameter<float>* sizeParam_ = nullptr;
    params::Parameter<float>* outlineParam_ = nullptr;
    params::Parameter<glm::vec4>* outlineColorParam_ = nullptr;
    params::Parameter<float>* glowParam_ = nullptr;
    params::Parameter<glm::vec4>* glowColorParam_ = nullptr;
    params::Parameter<glm::vec2>* shadowOffsetParam_ = nullptr;
    params::Parameter<float>* shadowOpacityParam_ = nullptr;
};

} // namespace avgen::comp
