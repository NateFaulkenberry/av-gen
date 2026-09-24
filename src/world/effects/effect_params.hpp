#pragma once

// Effect parameters (ADR-230, ADR-500, ADR-702): every meaningful number on every effect instance,
// exposed through the parameter system so it can be automated, keyed, preset, cued and modulated by
// exactly the routes everything else in the engine uses.
//
// **There is no audio hook on any effect, and there is not going to be one.** An aurora that answers
// the bass is an `audio.bass -> fx/<id>/curtainHeight` route, because that is how a hero reacts, how
// a light rig reacts and how a post chain reacts. The one exception is the aurora's *spectrum
// vector* -- sixteen numbers no scalar route can carry -- documented in `atmospherics.hpp` and
// carried by `EffectContext::spectrum` rather than smuggled in here. Entity-derived modulation (a
// UFO's velocity driving its Space Warp) belongs in the same place for the same reason: a signal on
// the bus and a route onto `fx/<id>/<leaf>`, not a field an effect reads off its owner.
//
// **The path is `fx/<effect id>/<property>`.** ADR-702 replaced the two prefixes this used to have --
// `worldfx/<name>/` for ADR-207's waves and `atmos/<name>/` for ADR-230's sky and medium effects,
// registered by two registrars with different lifetimes -- with one, keyed by the instance's stable
// id rather than its display name. Renaming an effect therefore orphans no route, track, preset or
// control binding, and reordering a stack changes no path at all.
//
// **The registrar is table-driven.** Each parameter is one row of the type's schema with an accessor
// pair, and register/apply/capture are three loops over the rows, so a row that exists is a
// parameter that works in all three directions or in none.
//
// **Lifecycle.** The engine owns the registered set (not `scene::Composition`, whose hand-written
// pointer lists have gone stale before): everything registered here is recorded in
// `EffectParameters::registered` and released wholesale by `unregisterEffectParameters`, and the
// cached pointers live only in the `EffectParameters` that is released with them.

#include "params/modulation.hpp"
#include "params/parameter.hpp"
#include "world/effects/effect_instance.hpp"

#include <span>
#include <string_view>
#include <string>
#include <vector>

namespace avgen::params {
class ParameterSet;
}

namespace avgen::world {

// `fx/<id>/`.
[[nodiscard]] std::string effectParameterPrefix(std::string_view effectId);

// The parameters of one instance, as pointers cached at registration.
//
// Parallel to the schema's rows rather than named members: the whole point of the table is that
// nothing names a parameter twice. Null before registration and after `unregister`, which is what
// `applyEffectParameters` tests before it touches anything.
struct EffectParams {
    std::string id;
    EffectKind kind = EffectKind::Comet;
    params::Parameter<bool>* enabled = nullptr;
    // One entry per row of `effectSchema(kind)->fields`, then one per APPLICABLE row of
    // `sharedEffectFields()` (see `sharedFieldApplies`), in that order.
    std::vector<params::IParameter*> values;
};

struct EffectParameters {
    std::vector<EffectParams> effects;
    // Every path registered, for an exact unregister -- the rule every registrar in the engine
    // follows since a suffix table left parameters behind.
    std::vector<std::string> registered;
    [[nodiscard]] bool empty() const { return effects.empty(); }
    [[nodiscard]] const EffectParams* find(std::string_view id) const;
};

// Registers `fx/<id>/...` for every instance, with the instance's authored values as defaults.
[[nodiscard]] EffectParameters registerEffectParameters(params::ParameterSet& params,
                                                        std::span<const EffectInstance> effects);
void unregisterEffectParameters(params::ParameterSet& params, EffectParameters& registered);

// Copies each parameter's FINAL value (base + routes + automation) into the live instance of the
// same id. Every frame, before evaluation.
void applyEffectParameters(const EffectParameters& registered, std::span<EffectInstance> live);
// Copies each parameter's BASE value into the authored instance of the same id. Before a
// structural edit, so re-registration does not throw away every slider somebody moved.
void captureEffectParameters(const EffectParameters& registered, std::span<EffectInstance> authored);

// The type's default audio routes, aimed at this instance's paths, with each SOURCE exactly as the
// schema declares it -- an `owner.` source is left unresolved. What the conformance probe checks the
// targets of; a caller that is going to install the routes wants the overload below.
[[nodiscard]] std::vector<params::ModRoute> defaultEffectRoutes(std::string_view effectId, EffectKind kind);

// ---- ADR-703: entity-derived signals and the `owner.` alias ----------------------------------------
//
// A preset cannot name its owner, because it is written before it is attached. So a schema's default
// route may use the source prefix `owner.` (Trail's `owner.speed -> opacity`, Space Warp's
// `owner.speed -> strength`), and it is resolved against the instance's owner when the routes are
// ATTACHED:
//
//   Entity  `owner.speed` -> `entity.<owner name>.speed`
//   Camera  `owner.speed` -> `camera.speed`
//   World   refused, by name: the World has no position, so it has no speed
//   Light   refused, by name: no `light.*` signals are published yet
//
// The routes are stored RESOLVED (they are ordinary authored routes from then on, saved with the
// project like any other). Renaming the owner therefore has to move them, which is what
// `renameEntitySignalSources` does and `Engine::renameEffectOwner` calls.
//
// The engine publishes these for every entity some effect or route subscribes to, before the routes
// run, from the last completed simulation step (`Engine::publishEntitySignals`).

inline constexpr std::string_view kOwnerSignalPrefix = "owner.";
// The leaves `entity.<name>.` carries. Metres, metres per second, metres per second squared.
inline constexpr std::string_view kEntitySignalLeaves[] = {"speed",        "velocity.x", "velocity.y",
                                                           "velocity.z",   "acceleration", "cameraDistance"};
// `entity.<name>.`.
[[nodiscard]] std::string entitySignalPrefix(std::string_view entity);
// What `owner.` means for this owner, or the refusal naming why it means nothing.
[[nodiscard]] Result<std::string> ownerSignalPrefix(const EffectOwner& owner);
// The type's default routes aimed at this instance's paths, `owner.` resolved against its owner. A
// route the owner cannot resolve refuses the whole set, naming the route and the owner -- a route
// that silently bound to nothing is the failure ADR-392 was about.
[[nodiscard]] Result<std::vector<params::ModRoute>> defaultEffectRoutes(const EffectInstance& effect);
// Re-points every route whose source is one of `entity.<from>.*` at `entity.<to>.*`. Returns how
// many moved. An entity rename's half for routes (its half for effects is `renameEffectOwner`).
std::size_t renameEntitySignalSources(std::vector<params::ModRoute>& routes, std::string_view from,
                                      std::string_view to);

} // namespace avgen::world
