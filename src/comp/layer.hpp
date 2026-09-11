#pragma once

// The 2D composition layer model (ADR-083).
//
// A composition is an ordered stack of layers drawn over the finished 3D frame. Layer 0 of the
// *picture* is the 3D render; every layer in this list composites above it, in list order, and the
// compositor knows nothing about the scene behind it. That is the whole point of the separation:
// the base image can become a video, a still, a shader or another composition without any of this
// changing.
//
// ---- coordinates ---------------------------------------------------------------------------
// Resolution independent, and deliberately two different rules for the two different jobs:
//
//   * **Position** is normalised per axis, origin at the **bottom left**, y up. (0.5, 0.5) is the
//     centre of the frame at every resolution and every aspect ratio; "10% from the left, 90% from
//     the bottom" is (0.1, 0.9) and stays there at 1280x720 and at 3840x2160.
//   * **Size** -- font size, shape extents, corner radii, stroke widths -- is in **height units**:
//     a fraction of the frame's height, on both axes. So a circle stays circular, a square stays
//     square, and text keeps its proportion of the picture when the frame gets wider.
//
// Normalising position per axis and size by height is the only combination that gets both
// properties. Normalising everything per axis makes circles into ellipses; normalising everything
// by height makes "centre" move when the aspect changes.
//
// Angles are degrees, counter-clockwise, about the layer's anchor. The anchor is a fraction of the
// layer's own box: (0, 0) bottom left, (0.5, 0.5) centre, (1, 1) top right.
//
// ---- animation -----------------------------------------------------------------------------
// Every animatable property is a registered params::Parameter under "layers/<id>/<property>".
// It therefore keyframes on the *existing* timeline, modulates from the *existing* signal routes,
// saves in presets, and appears in the parameter panel, with no code here that knows about any of
// those systems. The authored value in this struct is the parameter's base; the value used to draw
// is the parameter's final. With no ParameterSet attached (tests, headless tools) the authored
// value is used directly, so the model is meaningful on its own.

#include "core/error.hpp"
#include "params/parameter_set.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::comp {

enum class LayerKind : std::uint8_t { Text, Shape };
[[nodiscard]] const char* layerKindName(LayerKind kind);
[[nodiscard]] Result<LayerKind> layerKindFromName(std::string_view name);

// Normal first, because it is what nearly everything wants. The other three are one blend-state
// each in the compositor and no new pass, which is the only reason they are here at all.
enum class BlendMode : std::uint8_t { Normal, Additive, Screen, Multiply };
[[nodiscard]] const char* blendModeName(BlendMode mode);
[[nodiscard]] Result<BlendMode> blendModeFromName(std::string_view name);

// The frame a composition is drawn into.
struct Frame {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    [[nodiscard]] float aspect() const {
        return height == 0 ? 1.0f : static_cast<float>(width) / static_cast<float>(height);
    }
    // One height unit, in pixels.
    [[nodiscard]] float unit() const { return static_cast<float>(height); }
    // Normalised composition position (bottom-left origin, y up) to render-target pixels (top-left
    // origin, y down), which is what the rasteriser wants.
    [[nodiscard]] glm::vec2 toPixels(glm::vec2 normalised) const {
        return {normalised.x * static_cast<float>(width), (1.0f - normalised.y) * static_cast<float>(height)};
    }
};

// ---- what the compositor draws --------------------------------------------------------------
// Geometry is built in the layer's own local units and cached; it is rebuilt only when the content
// or the typography changes. Everything that animates -- position, scale, rotation, anchor,
// colour, opacity, widths -- lives in the per-frame item and costs no rebuild at all.

struct LayerVertex {
    glm::vec2 local;   // layer-local units, y up, origin at the bottom left of the layer's box
    glm::vec2 uv;      // glyph: atlas texture coordinates. shape: box coordinates in [-1, 1]
    std::uint32_t item = 0;
};

// Matches ItemStyle in shaders/composite.wgsl. Every animated quantity is in here, which is why
// animating a layer costs 96 bytes of upload and no rebuild of anything.
struct LayerItem {
    glm::vec4 xform0{1.0f, 0.0f, 0.0f, 1.0f}; // local -> pixels 2x2, row major (xx, xy, yx, yy)
    glm::vec4 xform1{0.0f};                   // xy = translation in pixels; zw = box half extent (shapes)
    glm::vec4 color{1.0f};                    // fill, display-referred, straight alpha, opacity folded in
    glm::vec4 accent{0.0f};                   // glyph outline / shape stroke colour, same convention
    glm::vec4 glow{0.0f};                     // glow colour, same convention
    // x = ItemShader. The other three are in signed-distance units for a glyph and in the layer's
    // own local units for a shape, because that is the space each one's distance field lives in.
    glm::vec4 params{0.0f};                   // y = outline/stroke half width, z = corner radius, w = glow extent
};
static_assert(sizeof(LayerItem) == 96);

// The shader's `kind`. Deliberately small: one pipeline, one pass, a switch on an integer.
enum class ItemShader : std::uint32_t { Glyph = 0, Box = 1, Ellipse = 2 };

// Where one layer's geometry sits in the shared vertex buffer, and which items draw it. A layer
// with a drop shadow contributes two runs over the same glyphs.
struct LayerRun {
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t item = 0;
    // A run that is usually switched off -- the drop shadow of a text layer that has none. The
    // stack packs these at the end of the vertex buffer so that skipping them does not break the
    // contiguity of everything else, which is what lets a hundred plain text layers merge into a
    // single draw call. Draw order is unaffected: it is the order the runs are listed in.
    bool secondary = false;
};

class GlyphAtlas;

// Small JSON helpers the layer kinds share, so the vector conventions are stated once.
[[nodiscard]] Result<glm::vec2> layerReadVec2(const nlohmann::json& j, const char* what);
[[nodiscard]] Result<glm::vec4> layerReadVec4(const nlohmann::json& j, const char* what);
[[nodiscard]] nlohmann::json layerVec2Json(glm::vec2 v);
[[nodiscard]] nlohmann::json layerVec4Json(glm::vec4 v);

// ---- the layer ------------------------------------------------------------------------------

class Layer {
public:
    virtual ~Layer() = default;
    [[nodiscard]] virtual LayerKind kind() const = 0;
    [[nodiscard]] virtual std::unique_ptr<Layer> clone() const = 0;

    // ---- identity and state (every layer kind has these) ----
    std::uint32_t id = 0;      // unique within the composition; the parameter path uses it
    std::string name = "Layer";
    bool enabled = true;       // the eye toggle: off means the layer draws nothing
    BlendMode blend = BlendMode::Normal;
    double startTime = 0.0;    // seconds; the layer is absent before this
    double endTime = 0.0;      // seconds; <= startTime means "until the end"

    // ---- transform and appearance (all animatable) ----
    glm::vec2 position{0.5f, 0.5f}; // normalised, bottom-left origin
    glm::vec2 scale{1.0f, 1.0f};
    float rotation = 0.0f;          // degrees, counter-clockwise
    glm::vec2 anchor{0.5f, 0.5f};   // fraction of this layer's own box
    float opacity = 1.0f;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // display-referred sRGB, straight alpha

    // Within the layer's time range. The one rule about time, in one place.
    [[nodiscard]] bool liveAt(double seconds) const {
        if (seconds < startTime) {
            return false;
        }
        return endTime <= startTime || seconds < endTime;
    }

    // ---- parameters ----
    // Registers this layer's animatable properties. Called before the timeline binds, always:
    // a track naming a parameter that does not exist yet is a track that silently does nothing,
    // and that failure has cost this project two features already (ADR-075, ADR-080).
    void attach(params::ParameterSet& set);
    void detach();
    [[nodiscard]] bool attached() const { return position_ != nullptr; }
    // Copies the authored values into the parameter bases (after an edit in the inspector).
    void pushAuthored();
    // Copies the parameter bases back into the authored values (before saving).
    void pullAuthored();
    [[nodiscard]] std::string parameterPath(std::string_view property) const;
    // Every path this layer registers, in registration order. Used by the inspector, by removal,
    // and by the test that proves nothing keyable is missing from the parameter set.
    [[nodiscard]] std::vector<std::string> parameterPaths() const;

    // ---- geometry ----
    // Rebuilds `vertices` (appending) and reports the runs. `firstItem` is the index the first of
    // this layer's items will occupy. Returns the number of items the layer will fill in.
    virtual std::uint32_t buildGeometry(std::vector<LayerVertex>& vertices, std::vector<LayerRun>& runs,
                                        std::uint32_t firstItem, GlyphAtlas& atlas) = 0;
    // Fills this layer's items for one frame. `items` is indexed from the layer's first item.
    virtual void buildItems(const Frame& frame, double seconds, LayerItem* items) const = 0;
    // Pixels per local unit, which is what turns the layer's own coordinates into the frame's.
    [[nodiscard]] virtual float unitScale(const Frame& frame) const = 0;
    // The layer's box in local units; the anchor is a fraction of this.
    [[nodiscard]] virtual glm::vec2 boxLocal() const = 0;
    // Bumped whenever something that invalidates cached geometry changes.
    [[nodiscard]] std::uint64_t geometryVersion() const { return geometryVersion_; }
    void markGeometryDirty() { ++geometryVersion_; }

    // ---- resolved (post-modulation) values ----
    [[nodiscard]] glm::vec2 resolvedPosition() const;
    [[nodiscard]] glm::vec2 resolvedScale() const;
    [[nodiscard]] float resolvedRotation() const;
    [[nodiscard]] glm::vec2 resolvedAnchor() const;
    [[nodiscard]] float resolvedOpacity() const;
    [[nodiscard]] glm::vec4 resolvedColor() const;

    // ---- JSON ----
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<std::unique_ptr<Layer>> fromJson(const nlohmann::json& j);

protected:
    // The 2x2 and translation that take this layer's local units to target pixels this frame.
    void fillTransform(const Frame& frame, LayerItem& item) const;
    virtual void attachKind(params::ParameterSet& set) = 0;
    virtual void detachKind() = 0;
    virtual void pushAuthoredKind() = 0;
    virtual void pullAuthoredKind() = 0;
    virtual void collectPaths(std::vector<std::string>& out) const = 0;
    virtual void writeJson(nlohmann::json& j) const = 0;
    [[nodiscard]] virtual Result<void> readJson(const nlohmann::json& j) = 0;
    void copyCommonTo(Layer& other) const;

    // Registration helpers shared by the kinds: one place that knows the label convention.
    params::Parameter<float>& addFloat(params::ParameterSet& set, std::string_view property, float value, float lo,
                                       float hi);
    params::Parameter<glm::vec2>& addVec2(params::ParameterSet& set, std::string_view property, glm::vec2 value,
                                          float lo, float hi);
    params::Parameter<glm::vec4>& addColor(params::ParameterSet& set, std::string_view property, glm::vec4 value);

    std::uint64_t geometryVersion_ = 1;

private:
    params::Parameter<glm::vec2>* position_ = nullptr;
    params::Parameter<glm::vec2>* scale_ = nullptr;
    params::Parameter<float>* rotation_ = nullptr;
    params::Parameter<glm::vec2>* anchor_ = nullptr;
    params::Parameter<float>* opacity_ = nullptr;
    params::Parameter<glm::vec4>* color_ = nullptr;
};

} // namespace avgen::comp
