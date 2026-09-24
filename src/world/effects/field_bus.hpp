#pragma once

// One field, many subscribers (the brief's §68).
//
// ## What was wrong
//
// This engine had, before this file, exactly two spatial fields -- ADR-055's wind and ADR-388's
// vortex -- both built to the same excellent shape: a pure function of (packed uniforms, position,
// time), a WGSL transliteration beside the C++, and a test that compares the two through the packed
// form. Neither of them had a way for a *third party* to ask a question of a field it did not
// itself own.
//
// The consequence was measured rather than assumed. `wind::sampleWind` has four consumers in the
// whole tree (`spatial/vegetation_sim.cpp`, `rendering/debug_visualizer.cpp`, and on the GPU side
// `shaders/wind.wgsl` and `particles.wgsl::particleWindAt`), and every one of them is vegetation or
// particles. **No atmospheric effect samples the wind at all.** A comet's wisps drift at
// `CometAppearance::flowSpeed`, an aurora's curtain at `AuroraShape::flowSpeed` and its folds at
// `driftSpeed`, and a vortex breathes at `Vortex::breathSpeed` -- five private clocks, none of
// which knows that the valley below has weather, and none of which can be made to agree with
// another except by an artist typing the same number into five boxes.
//
// That is the thing the brief says should stop, and ADR-230's family is where it should stop first,
// because ADR-387/392 established that this family **is** the environmental simulation layer.
//
// ## What this is
//
// A publish/subscribe layer over fields that already exist, and deliberately nothing more:
//
//   * A **publisher** hands the bus a name and the *packed uniforms* of a field it already owns.
//     Packed, not authored, for ADR-055's reason: both sides of any future CPU/GPU comparison then
//     start from bytes that are identical by construction, so a disagreement can only be about the
//     maths. The bus never re-implements a field's arithmetic -- `sample` dispatches to
//     `wind::sampleWind` or `vortex::sampleVortex`, which is why there is no third transliteration
//     here to drift from the other two.
//
//   * A **subscriber** names a field in its own serialised data and gets back a `FlowSample` at its
//     own position and time. Two subscribers naming the same field get the same answer, which is
//     the whole point and is what five private `flowSpeed`s cannot do.
//
// ## The two properties that make it safe
//
// **It is pure, and therefore scrub-safe (ADR-091).** `sample` reads no frame counter, no wall
// clock and no previous frame; it is a function of (uniforms, position, time) because both fields
// it dispatches to are. Nothing here accumulates, so the relaxation ADR-091 grants particle systems
// is not needed and is not taken.
//
// **A subscription that names nothing is LOUD.** This repository's signature defect is a name that
// resolves to nothing and is thereafter indistinguishable from a setting nobody used: ADR-392 found
// a modulation route aimed at three paths a vortex does not register, ADR-375 found sixteen
// parameters no panel named, ADR-385 found a function with no caller whose comment said otherwise.
// A field bus is an invitation to add a fourth. So `resolve` returns `kNoField` -- a value
// distinct from every valid handle, rather than a silently zero sampler -- and `unresolved()`
// reports every subscription that named a field the scene does not publish, by
// subscriber and by the name it asked for. The engine logs that list; `effect_conformance` fails on
// it. A dead subscription is a stated problem, not a still picture.

#include "core/vortex.hpp"
#include "core/wind.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::world::fields {

// ---- what a subscriber gets back ----------------------------------------------------------------

// What `FlowSample::flow` is measured in. Two publishers, two answers, and no way to find out from
// the vector itself -- so the vector carries the answer.
enum class FlowUnits : std::uint8_t {
    Normalised,      // ADR-055's wind: a direction times a dimensionless strength (1 ~ a fresh breeze)
    MetresPerSecond, // ADR-388's vortex: the medium's actual velocity
};
[[nodiscard]] const char* flowUnitsName(FlowUnits units);

// What the medium is doing at one point at one time, in the one vocabulary every publisher can
// answer in. Deliberately the intersection of `wind::WindSample` and `vortex::VortexSample` rather
// than their union: a subscriber that has to know which kind of field answered it is a subscriber
// coupled to a publisher, which is the coupling this file removes.
struct FlowSample {
    // Which of those two `flow` is in. Carried rather than assumed because assuming is how this
    // codebase has produced a unit bug four times (ADR-374's density, ADR-379's spill, ADR-381's
    // comet-on-fog, ADR-389's coefficient) and because the two publishers genuinely differ: the
    // wind's `speed` is documented as "dimensionless strength of the steady flow (0 calm, 1 a fresh
    // breeze)", while the vortex's velocity is metres per second of actual medium. A subscriber
    // that integrates a displacement MUST check this; one that only wants a direction and a
    // relative magnitude -- which is every subscriber the atmospheric family has -- need not, and
    // says so where it reads the sample.
    FlowUnits units = FlowUnits::Normalised;
    // The medium's flow at this point, in `units`. The wind's is its direction times its strength
    // in the XZ plane (the wind field is columnar -- what a thing rooted in the ground
    // experiences), so its Y is always 0; the vortex's is its full three-dimensional swirl.
    glm::vec3 flow{0.0f};
    // How hard, with no direction: >= 0, and 0 exactly where the field does not reach. For the wind
    // this is the regionally modulated steady strength; for the vortex it is the normalised shape,
    // which is 0 outside the funnel by construction.
    float strength = 0.0f;
    // The travelling-front envelope, 0..1. The wind's gust; the vortex has no fronts and answers 0,
    // which is a fact a subscriber may rely on rather than a placeholder.
    float gust = 0.0f;
    // A *spatial* phase in radians: the same number at the same place, and a different one a few
    // metres away. This is what stops two subscribers of one field moving in lockstep, which is the
    // single most recognisable tell of a shared clock pretending to be a shared field.
    float phase = 0.0f;
};

// ---- publishing ---------------------------------------------------------------------------------

// Which field's arithmetic answers. Not a knob: it is decided by which `publish` call was made, and
// it exists so `sample` can dispatch. The switch over it has no `default`, and `fieldSourceName`
// is what a report prints.
enum class FieldSource : std::uint8_t {
    Wind,   // ADR-055: the world's air. Columnar, gusty, travelling.
    Vortex, // ADR-388: a funnel's medium. Three-dimensional, turning, bounded.
};
[[nodiscard]] const char* fieldSourceName(FieldSource source);

// The name the world's wind publishes under. A constant rather than a literal at each call site
// because it is exactly ADR-387's "a parameter path is three things at once" in miniature: the
// default subscription names it, the engine publishes it, and the panel offers it.
inline constexpr std::string_view kWindField = "wind";

// The name a vortex effect publishes its own field under. `atmos/<name>` deliberately matches the
// prefix that effect's parameters register under (`world::effectParameterPrefix`), so the name
// an artist sees in the Parameters panel and the name they subscribe to are the same string.
[[nodiscard]] std::string vortexFieldName(std::string_view effectName);

// One turn, for the phase arithmetic subscribers do with `FlowSample::phase`. Spelled here rather
// than reached for out of `wind::kTau` or `vortex::kTau` so a subscriber need not include a
// publisher's header to wrap a number the bus handed it.
inline constexpr float kFlowTau = 6.28318530718f;

// A handle into the bus. `kNoField` is what an unpublished name resolves to, and it is a distinct
// value rather than 0 so that "the first field" and "no field" cannot be confused by a caller that
// forgot to check -- which is how a dead subscription becomes a silent zero.
using FieldHandle = int;
inline constexpr FieldHandle kNoField = -1;

// ---- subscribing --------------------------------------------------------------------------------

// What a subscriber serialises. `field` is structural -- it names a publisher and comes from the
// file, like an effect's `kind` -- and `influence` is a value, so it is a table row, registered,
// modulatable and keyable like every other number in the family.
//
// `influence` defaults to 0 and 0 means off, which is the load-bearing default: every scene and
// project written before this file existed loads with no subscription active and renders the frame
// it rendered before. ADR-388 established that a new coefficient earns its default by measurement
// of both ends, and this one is proved at both ends the same way.
struct Subscription {
    std::string field;      // a published field's name; empty means "not subscribed", which is legal
    float influence = 0.0f; // 0 is off and is the default

    // Empty is the honest "no subscription" and is not a failure. A *non-empty* name that the bus
    // does not publish is, and that is the distinction `FieldBus::unresolved` reports on.
    [[nodiscard]] bool requested() const { return !field.empty(); }
    [[nodiscard]] bool active() const { return requested() && influence != 0.0f; }
};

// One subscription that asked for a field nobody published. Carries the subscriber's name as well
// as the field's, because "some effect wants a field called `gale`" is a puzzle and "the comet
// `Opening Streak` wants a field called `gale`" is an instruction.
struct DeadSubscription {
    std::string subscriber;
    std::string field;
};

// ---- the bus ------------------------------------------------------------------------------------

// Rebuilt from scratch each frame by whoever owns the fields -- it holds packed uniforms by value
// and no pointers, so there is nothing to dangle and nothing to invalidate. Cheap enough to do
// that: a scene publishes one wind and a handful of vortices.
class FieldBus {
public:
    void clear();

    // Publishing the same name twice replaces the first, and is not an error: a scene that renames
    // a vortex mid-session would otherwise accumulate ghosts of every name it ever had.
    void publishWind(std::string name, const wind::WindUniforms& uniforms);
    void publishVortex(std::string name, const vortex::VortexUniforms& uniforms);

    [[nodiscard]] std::size_t size() const { return entries_.size(); }
    [[nodiscard]] bool empty() const { return entries_.empty(); }

    // `kNoField` when nothing of that name is published, including for an empty name.
    [[nodiscard]] FieldHandle resolve(std::string_view name) const;
    // The name and source behind a handle, for a report or a combo box. Empty / `Wind` for
    // `kNoField`, which a caller should not be asking about in the first place.
    [[nodiscard]] std::string_view nameOf(FieldHandle handle) const;
    [[nodiscard]] FieldSource sourceOf(FieldHandle handle) const;
    // Every published name, in publication order, for a UI that offers them.
    [[nodiscard]] std::vector<std::string_view> names() const;

    // The field's answer at a point and a time. `kNoField` answers a zero sample -- which is the
    // right behaviour once a caller has been told the handle is dead, and the wrong behaviour as a
    // way of finding out, which is why `resolve` reports and this does not.
    [[nodiscard]] FlowSample sample(FieldHandle handle, const glm::vec3& position, float t) const;

    // Convenience for a caller that resolves and samples in one breath and has already checked, or
    // for a test. Same purity, same zero for an unpublished name.
    [[nodiscard]] FlowSample sample(std::string_view name, const glm::vec3& position, float t) const;

    // ---- the loud half --------------------------------------------------------------------------

    // Every subscription in `subs` that asked for a field this bus does not publish. `subscribers`
    // and `subs` are parallel and must be the same length; a shorter `subscribers` names the
    // missing ones `"<unnamed>"` rather than reading off the end.
    [[nodiscard]] std::vector<DeadSubscription> unresolved(std::span<const std::string> subscribers,
                                                           std::span<const Subscription> subs) const;

private:
    struct Entry {
        std::string name;
        FieldSource source = FieldSource::Wind;
        wind::WindUniforms wind{};
        vortex::VortexUniforms vortex{};
    };
    std::vector<Entry> entries_;
};

} // namespace avgen::world::fields
