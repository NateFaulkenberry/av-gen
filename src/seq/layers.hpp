#pragma once

// The seam between the sequencer and the 2D composition / text system (spec 22-27).
//
// ## Why there is a seam here at all
//
// The composition system (ADR-083) owns layers, fonts, glyph atlases and the compositor that draws
// typography over the 3D frame. The sequencer does not render text, does not own a font, and does
// not know what a layer is made of. What it owns is *timing*: which words appear when, in what
// order, with what animation, and how that timing survives a scrub and an offline render.
//
// ## The shape of the seam
//
// An `OverlayCue` is a timed intention: "this line, from 12.4 s to 15.8 s, bottom centre, fading".
// A `LayerSink` turns one into a layer and hands back an id. The sequencer then addresses that
// layer's animatable properties **by parameter path and component**, through the same
// `params::ParameterSet` that already carries `camera/position` and `nodes/<name>/rotation` --
// because the spec's third section is emphatic that text opacity and camera position must be
// animated by one mechanism, and this engine's one mechanism is a parameter path.
//
// That is the whole contract. Two methods:
//
//   realise(cue)            -> a layer id ("" = declined)
//   target(id, property)    -> { "layers/7/position", component 1 }, or an empty path
//
// The property vocabulary below is deliberately the *composition system's* vocabulary, split per
// axis. An earlier draft of this file invented `offsetX`/`offsetY`, which no layer has: the two
// halves of the seam disagreed about spelling and the presets would have bound to nothing and said
// nothing, which is the exact failure ADR-075 records.
//
// ## Implementations
//
//   CompositionLayerSink  the real one, over comp::LayerStack (seq/layer_sink.hpp)
//   NullLayerSink         declines everything; the bake records the cue as deferred and warns
//   PrefixLayerSink       test-only: issues paths under a prefix without registering anything

#include "core/error.hpp"
#include "params/timeline.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::seq {

// spec 22: the layer kinds a sequence can ask for. The sequencer models no more of a layer than it
// needs to place one in time and in order.
enum class OverlayKind : std::uint8_t { Text, Shape, Image };
[[nodiscard]] const char* overlayKindName(OverlayKind kind);
[[nodiscard]] std::optional<OverlayKind> overlayKindFromName(std::string_view name);

// spec 25: a preset is not an animation engine. It creates keyframes on the generic property
// system, and an author may then edit them like any others.
enum class OverlayPreset : std::uint8_t { None, FadeIn, FadeOut, FadeInOut, ScalePop, SlideUp, SlideDown };
[[nodiscard]] const char* overlayPresetName(OverlayPreset preset);
[[nodiscard]] std::optional<OverlayPreset> overlayPresetFromName(std::string_view name);

// A timed overlay intention (spec 23, 24, 26, 27).
struct OverlayCue {
    std::string id;               // stable; how the sequence refers to the layer it becomes
    OverlayKind kind = OverlayKind::Text;
    std::string content;          // the line of text, or an image path, or a shape name
    std::string style;            // a named style the composition system owns (font, weight, case)
    double startSeconds = 0.0;
    double endSeconds = 0.0;      // outside [start, end] the layer is invisible (spec 23)
    int order = 0;                // spec 26: explicit, ascending, drawn over the 3D frame
    glm::vec2 anchor{0.5f, 0.85f}; // normalised position in the frame
    glm::vec2 pivot{0.5f, 0.5f};   // normalised point within the layer that sits on the anchor
    float size = 1.0f;
    float rotationDegrees = 0.0f;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    OverlayPreset preset = OverlayPreset::None;
    double presetSeconds = 0.45;  // how long the preset's move takes at each end
    // Anything the layer system wants that the sequencer deliberately does not model. Carried
    // verbatim through JSON and handed to the sink untouched.
    nlohmann::json extra;

    [[nodiscard]] double durationSeconds() const { return endSeconds - startSeconds; }
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<OverlayCue> fromJson(const nlohmann::json& j);
};

// The properties a sink is expected to expose, spelled the way the composition system spells them
// and split per axis because a track keys one component. Named constants rather than loose strings
// so the two sides of the seam cannot disagree about spelling.
namespace overlay_property {
inline constexpr std::string_view kOpacity = "opacity";      // layers/<id>/opacity
inline constexpr std::string_view kScaleX = "scale.x";       // layers/<id>/scale, component 0
inline constexpr std::string_view kScaleY = "scale.y";
inline constexpr std::string_view kPositionX = "position.x"; // layers/<id>/position, component 0
inline constexpr std::string_view kPositionY = "position.y";
inline constexpr std::string_view kRotation = "rotation";
} // namespace overlay_property

// Where one of a layer's animatable properties lives in the parameter set.
struct LayerTarget {
    std::string path;   // empty = this sink does not expose that property
    int component = -1; // -1 = every component; 0/1 for one axis of a vec2
    [[nodiscard]] bool valid() const { return !path.empty(); }
};

// The seam. Implemented by whoever owns layers.
class LayerSink {
public:
    virtual ~LayerSink() = default;

    // Creates or updates the layer for this cue and returns its id. An empty id means the sink
    // declined -- not an error, a statement that this cue cannot be realised.
    [[nodiscard]] virtual Result<std::string> realise(const OverlayCue& cue) = 0;

    // Where one of the layer's animatable properties lives. Must be a path the sink has registered
    // in the engine's `params::ParameterSet`, or the tracks written against it will bind to nothing
    // and do nothing silently (ADR-075).
    [[nodiscard]] virtual LayerTarget target(std::string_view layerId,
                                             std::string_view property) const = 0;

    // Forget every layer this sink made. Called before a re-bake so a removed cue's layer goes.
    virtual void clear() = 0;
};

// Accepts nothing, remembers what it was asked for, and says so. Used by tests that care about
// timing rather than typography, and by any tool with no composition to draw into.
class NullLayerSink final : public LayerSink {
public:
    [[nodiscard]] Result<std::string> realise(const OverlayCue& cue) override;
    [[nodiscard]] LayerTarget target(std::string_view layerId,
                                     std::string_view property) const override;
    void clear() override { declined_.clear(); }
    // The cues it was handed, in order, for the editor to list as unrealised.
    [[nodiscard]] const std::vector<std::string>& declined() const { return declined_; }

private:
    std::vector<std::string> declined_;
};

// A sink that issues paths under a prefix without registering anything. Not for production -- it
// exists so the preset machinery below can be tested without a font engine.
class PrefixLayerSink final : public LayerSink {
public:
    explicit PrefixLayerSink(std::string prefix = "layers/") : prefix_(std::move(prefix)) {}
    [[nodiscard]] Result<std::string> realise(const OverlayCue& cue) override;
    [[nodiscard]] LayerTarget target(std::string_view layerId,
                                     std::string_view property) const override;
    void clear() override { realised_.clear(); }
    [[nodiscard]] const std::vector<std::string>& realised() const { return realised_; }

private:
    std::string prefix_;
    std::vector<std::string> realised_;
};

// spec 25: the keyframes a preset means, against paths the sink issues.
//
// Every preset is visibility plus one other property, and every one of them writes an explicit
// zero-opacity key outside the cue's range: spec 23 says a layer is invisible outside its timing,
// and a track holds its last value forever, so the only way "invisible afterwards" is true is to
// key it.
[[nodiscard]] std::vector<params::Track> overlayPresetTracks(const OverlayCue& cue,
                                                             const LayerSink& sink,
                                                             std::string_view layerId);

} // namespace avgen::seq
