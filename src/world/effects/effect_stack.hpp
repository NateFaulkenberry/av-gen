#pragma once

// The effect stack (ADR-702): the pure operations on a scene's effect list.
//
// A scene holds ONE list of `EffectInstance`s. Each instance names its owner -- the World, an
// entity, the camera, a light -- and its `order` within that owner's stack. So "the World's
// effects" and "rook's effects" are two views over one list, not two containers, and there is no
// second list for a second family of effect to live in. That is the whole of ADR-702's answer to
// "World Effects" and "Hero effects" having been two systems: they are one list, filtered.
//
// Everything here is a pure function of the list, with no engine, no parameters and no ImGui, so
// the panel's decisions (what may be added where, what moving up does, what a duplicate is called)
// are unit-tested rather than clicked. The engine's one mutation path, `Engine::editEffects`, calls
// these and then re-registers parameters; the panel never edits the list another way.
//
// **Invariants every function here preserves** (and `validateEffects` checks):
//   * ids are non-empty, unique across the WHOLE scene, and contain no '/' -- a parameter path is
//     `fx/<id>/<leaf>`, so an id is half of a path and two effects with one id are two things
//     writing one parameter;
//   * `order` is contiguous 0..n-1 within each owner;
//   * the list is stored grouped by owner (in first-appearance order) and sorted by `order` inside
//     each group, so iterating the list IS iterating every stack top to bottom, and a save writes
//     the order a person sees.

#include "core/error.hpp"
#include "world/effects/effect_instance.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::world {

// The indices of `owner`'s effects in `effects`, top of the stack first. Allocates; a UI and
// editing path, never a per-frame one.
[[nodiscard]] std::vector<std::size_t> effectsOf(std::span<const EffectInstance> effects,
                                                 const EffectOwner& owner);

// Index of the effect with this id, or `effects.size()`.
[[nodiscard]] std::size_t findEffect(std::span<const EffectInstance> effects, std::string_view id);

// An id no effect in `effects` has, derived from `base` (slugged: lower case, spaces to '-', '/'
// removed). `rook` + `Ground Pulse` -> "rook-ground-pulse"; a clash gains "-2", "-3", ...
[[nodiscard]] std::string uniqueEffectId(std::span<const EffectInstance> effects, std::string_view base);

// The id a new effect of `kind` on `owner` is given: the owner's name and the type's display name,
// slugged and made unique. World-owned effects are just the type ("aurora", "aurora-2").
[[nodiscard]] std::string newEffectId(std::span<const EffectInstance> effects, const EffectOwner& owner,
                                      EffectKind kind);

// Whether a `kind` may be attached to an owner of `target` kind. Read from the type's declared
// target capabilities -- the one place that knows -- so neither the panel nor the loader carries
// its own table.
[[nodiscard]] bool effectAllowedOn(EffectKind kind, EffectTarget target);

// The types an owner of this kind may take, in registry order. What the Add Effect menu lists.
[[nodiscard]] std::vector<EffectKind> effectKindsFor(EffectTarget target);

// Makes an instance valid for the owner it has: a type whose factory's source is "my owner" gets
// "whatever the cut is on" when the owner is the World (or a light), which has no position of its
// own to lend. Endpoint-generic -- it reads the schema's endpoint accessors, never a type. Called by
// `insertEffect` and `makeEffect`.
void adaptEffectToOwner(EffectInstance& effect);

// Appends a new instance of `kind` at the BOTTOM of `owner`'s stack, made by the type's factory,
// with a fresh id. Refused, with the list untouched, when the type does not support the owner's
// target kind or the owner is malformed (a World owner with a name, an entity owner without one).
// Returns the new id.
Result<std::string> addEffect(std::vector<EffectInstance>& effects, const EffectOwner& owner, EffectKind kind);

// Appends an already-built instance (a paste, a migration, a test) to the bottom of its owner's
// stack. Gives it a fresh id when its id is empty or taken, and refuses it when its type does not
// support its owner. Returns the id it ended up with.
Result<std::string> insertEffect(std::vector<EffectInstance>& effects, EffectInstance effect);

// Removes one effect and closes the gap in its owner's order. False when there is no such id.
bool removeEffect(std::vector<EffectInstance>& effects, std::string_view id);

// Moves an effect `delta` places within ITS OWNER's stack (negative is up, towards the top),
// clamped at the ends. Never moves an effect into another owner's stack. False when nothing moved.
bool moveEffect(std::vector<EffectInstance>& effects, std::string_view id, int delta);

// Moves an effect to position `order` in its owner's stack (a drag). Clamped. False when nothing moved.
bool moveEffectTo(std::vector<EffectInstance>& effects, std::string_view id, int order);

// A copy of the effect directly below it in the same stack, with a new id and " copy" on its name.
// Returns the new id.
Result<std::string> duplicateEffect(std::vector<EffectInstance>& effects, std::string_view id);

// Removes every effect whose owner is `owner` (the entity it was attached to was deleted: the
// Destroyed half of the lifecycle). Returns how many went.
std::size_t removeEffectsOf(std::vector<EffectInstance>& effects, const EffectOwner& owner);

// Re-points every effect owned by `from` at `to` (an entity was renamed). Returns how many moved.
std::size_t renameEffectOwner(std::vector<EffectInstance>& effects, const EffectOwner& from,
                              const EffectOwner& to);

// Restores the storage invariant above: groups by owner in first-appearance order, stable-sorts each
// group by its current `order`, then renumbers 0..n-1. Idempotent. Every mutator above ends with it.
void normaliseEffectOrder(std::vector<EffectInstance>& effects);

// The whole-list check: every id well-formed and unique, every type registered, every owner
// well-formed and supported by its type, orders contiguous per owner, and each instance's own
// `validate`. Refuses the whole set, naming the first offender.
[[nodiscard]] Result<void> validateEffects(std::span<const EffectInstance> effects);

// The order the evaluator walks the list in: stable by (render stage, stage priority, list
// position). Written into `out` (cleared first) so a caller that caches it allocates only when the
// list changes. List position is the tie-break, so two effects of one stage and priority are
// evaluated in stack order -- deterministic, and the order a person set.
void effectEvaluationOrder(std::span<const EffectInstance> effects, std::vector<std::uint32_t>& out);

} // namespace avgen::world
