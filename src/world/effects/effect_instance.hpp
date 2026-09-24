#pragma once

// An effect instance (ADR-702): one effect of one type, attached to one owner.
//
// Before ADR-702 a scene had two effect lists, `worldEffects` (ADR-207's surface waves: the camera
// travel beam and the hero pulse) and `atmosphericEffects` (ADR-230's sky and medium kinds: comet,
// aurora, meteor shower, vortex, fog, tornado), with two parameter registrars, two serialisers, two
// panels and two evaluators -- and neither had any notion of *what the effect belonged to*. A hero's
// pulse was a scene-wide record that happened to follow "whoever is in focus".
//
// Now there is one list of these. The distinctions ADR-702 draws are:
//
//   * **Type vs instance.** `kind` names the type -- its schema in the registry (effect_registry.hpp)
//     says what it is called, which category it sits in, which owners it may attach to, which
//     render stage it contributes to and which parameters it has. The instance is one use of that
//     type: its own id, its own values, its own modulation. Three UFOs with a Space Warp each are
//     three instances of one type.
//   * **Owner.** Every instance names what it is attached to. The World is an owner like any other;
//     "a world effect" is just an effect whose owner is the World.
//   * **Identity.** `id` is stable and unique across the scene. It, not the display name and not an
//     array index, is what parameter paths (`fx/<id>/<leaf>`), routes, timeline tracks, presets and
//     the editor's selection refer to, so renaming an effect or reordering a stack orphans nothing.
//   * **Order.** `order` is the instance's position in its owner's stack, persisted.
//
// The payloads are held side by side rather than in a variant, which is ADR-230/500's convention
// and is kept for a reason that survives the unification: a kind declared after ADR-500 keeps its
// values in `values` (a keyed store) and needs NO edit to this struct, so the next effect -- a
// Space Warp, a Motion Trail -- is one file in `effects/kinds/` and two lines in the registry, not a
// change to a shared header. Only the kinds that predate the registry keep typed members, because a
// member access is what made their port provably behaviour-neutral.

#include "core/error.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_kind.hpp"
#include "world/effects/field_bus.hpp"
#include "world/wave_effect.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace avgen::world {

// What an effect can be attached to. A type declares a mask of these; see `effectAllowedOn`.
enum class EffectTarget : std::uint8_t {
    World,  // the scene itself: no name
    Entity, // a named scene entity -- a hero, a composition node
    Camera, // a camera, by name; empty is "the active camera"
    Light,  // an authored light, by name
};
inline constexpr std::size_t kEffectTargetCount = 4;
using EffectTargetMask = std::uint8_t;
[[nodiscard]] constexpr EffectTargetMask targetBit(EffectTarget t) {
    return static_cast<EffectTargetMask>(1u << static_cast<unsigned>(t));
}
[[nodiscard]] const char* effectTargetName(EffectTarget t);
[[nodiscard]] std::optional<EffectTarget> effectTargetFromName(std::string_view name);

struct EffectOwner {
    EffectTarget kind = EffectTarget::World;
    std::string name; // empty for the World; the entity's / camera's / light's name otherwise

    [[nodiscard]] static EffectOwner world() { return {}; }
    [[nodiscard]] static EffectOwner entity(std::string n) { return {EffectTarget::Entity, std::move(n)}; }
    [[nodiscard]] static EffectOwner camera(std::string n = {}) { return {EffectTarget::Camera, std::move(n)}; }
    [[nodiscard]] static EffectOwner light(std::string n) { return {EffectTarget::Light, std::move(n)}; }

    [[nodiscard]] bool isWorld() const { return kind == EffectTarget::World; }
    [[nodiscard]] bool operator==(const EffectOwner&) const = default;
    // "World", "rook", "camera", "light key" -- for a panel header and a log line.
    [[nodiscard]] std::string label() const;
    // A World owner has no name; an Entity or Light owner must have one; a Camera may omit it.
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<EffectOwner> fromJson(const nlohmann::json& j);
};

struct EffectInstance {
    std::string id;       // stable, scene-unique, no '/': the `fx/<id>/` parameter prefix
    EffectKind kind = EffectKind::Comet; // the effect TYPE; see effect_registry.hpp
    std::string name;     // what the panel calls it; free text, need not be unique
    EffectOwner owner;
    bool enabled = true;
    int order = 0;        // position in the owner's stack, 0 at the top; see effect_stack.hpp
    std::string style;    // the preset it was made from, for the UI; changes nothing on its own

    // When the effect exists at all (ADR-207's activation and timing, shared by every type).
    Activation activation = Activation::Always;
    Timing timing;

    // ---- payloads (see the header) ---------------------------------------------------------------
    WaveEffect wave;      // GroundPulse, TravelBeam (ADR-207)
    Comet comet;          // Comet, MeteorShower (ADR-230)
    Aurora aurora;        // Aurora
    Vortex vortex;        // Vortex (ADR-387)
    Tornado tornado;      // Tornado (ADR-580)
    GroundIllumination ground; // the sky kinds that light the valley (ADR-230 §6)
    fields::Subscription flow; // ADR-420 §68
    EffectValueStore values;   // every kind declared after ADR-500

    [[nodiscard]] Result<void> validate() const;
    // `{id, type, name, owner, enabled, order, style, activation, timing, ..., parameters: {...}}`.
    // Only this instance's own type is written: the type of an instance never changes.
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<EffectInstance> fromJson(const nlohmann::json& j);
};

// What happened to one instance on the last evaluated frame. Written by the evaluator for every
// instance, read by the Effects panel -- which is the whole point: before ADR-702 the evaluator
// counted effects it could not draw in `AtmosphericCounts::dropped` and nothing ever read it.
enum class EffectStatus : std::uint8_t {
    Disabled,   // enabled is off
    Dormant,    // enabled, but outside its activation window (or its envelope is zero)
    Drawn,      // contributed records to the frame
    Dropped,    // active, but its render stage's GPU capacity was full: it drew nothing
    Orphaned,   // its owner does not exist in this scene (an entity that was deleted or renamed)
    // ADR-703. Drew, but not all of it: a multi-part type lost one part to a full budget (a glow
    // whose spill light did not fit) or its owner cannot give it everything (no bounds to fit a
    // proxy to). `Engine::effectStatusReason` says which part and why.
    Partial,
};
[[nodiscard]] const char* effectStatusName(EffectStatus s);

} // namespace avgen::world
