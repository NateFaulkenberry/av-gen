#pragma once

// The shape layer (ADR-081): rectangles, ellipses and lines, filled and/or stroked.
//
// Here because framing is a stated need -- a thin border around the picture, a rule under a lyric,
// a plate behind a caption -- and because it is the cheapest possible proof that the layer
// abstraction is not secretly a text abstraction. It shares the text layer's pass, pipeline,
// vertex format and item format; the only thing it does differently is which distance field the
// fragment shader evaluates.
//
// Its geometry is one quad in box-normalised units, expanded by 30% on each side to leave room for
// the stroke and the glow. That quad is built once and never rebuilt: the size lives in the
// per-frame item, so a shape that grows on the beat uploads 96 bytes and nothing else.

#include "comp/layer.hpp"

namespace avgen::comp {

enum class ShapeKind : std::uint8_t { Rectangle, Ellipse, Line };
[[nodiscard]] const char* shapeKindName(ShapeKind kind);
[[nodiscard]] Result<ShapeKind> shapeKindFromName(std::string_view name);

class ShapeLayer final : public Layer {
public:
    ShapeLayer();

    [[nodiscard]] LayerKind kind() const override { return LayerKind::Shape; }
    [[nodiscard]] std::unique_ptr<Layer> clone() const override;

    ShapeKind shape = ShapeKind::Rectangle;
    // All in height units (a fraction of the frame height, on both axes) so a square is square.
    glm::vec2 size{0.4f, 0.12f};
    float cornerRadius = 0.0f;
    float strokeWidth = 0.0f; // 0 = no stroke
    glm::vec4 strokeColor{1.0f, 1.0f, 1.0f, 1.0f};
    float glowRadius = 0.0f;
    glm::vec4 glowColor{1.0f, 1.0f, 1.0f, 0.5f};

    // How far past the shape's own box the quad reaches, as a fraction of the box. Stroke and glow
    // are clamped to it, because a glow wider than its quad is a glow with a straight edge.
    static constexpr float kMargin = 0.3f;

    std::uint32_t buildGeometry(std::vector<LayerVertex>& vertices, std::vector<LayerRun>& runs,
                                std::uint32_t firstItem, GlyphAtlas& atlas) override;
    void buildItems(const Frame& frame, double seconds, LayerItem* items) const override;
    [[nodiscard]] float unitScale(const Frame& frame) const override { return frame.unit(); }
    [[nodiscard]] glm::vec2 boxLocal() const override { return resolvedSize(); }

    [[nodiscard]] glm::vec2 resolvedSize() const;

protected:
    void attachKind(params::ParameterSet& set) override;
    void detachKind() override;
    void pushAuthoredKind() override;
    void pullAuthoredKind() override;
    void collectPaths(std::vector<std::string>& out) const override;
    void writeJson(nlohmann::json& j) const override;
    [[nodiscard]] Result<void> readJson(const nlohmann::json& j) override;

private:
    params::Parameter<glm::vec2>* sizeParam_ = nullptr;
    params::Parameter<float>* radiusParam_ = nullptr;
    params::Parameter<float>* strokeParam_ = nullptr;
    params::Parameter<glm::vec4>* strokeColorParam_ = nullptr;
    params::Parameter<float>* glowParam_ = nullptr;
    params::Parameter<glm::vec4>* glowColorParam_ = nullptr;
};

} // namespace avgen::comp
