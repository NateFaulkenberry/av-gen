#pragma once

// The Effects section's decisions, separated from its drawing (ADR-702).
//
// The Effects section draws ONE owner's effect stack -- the World's, a hero's, the camera's, a
// light's -- with the same code for every owner and every type. Everything it shows is read from
// the registry (`effect_registry.hpp`): which types an owner may take, which rows a card shows at
// which disclosure level, which structural pickers a type has. None of it names a type. The
// questions with exact answers are asked here, without a window, and checked in
// tests/unit/test_effects_panel.cpp; `effects_panel.cpp` draws the answers.
//
// The same split as `lights_panel_logic.hpp`, for the same reason: a panel that asks the registry a
// question inside an ImGui call is a panel whose answer no test can reach.

#include "core/error.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/wave_effect.hpp"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

class EditHistory;

// ---- the Add Effect menu -------------------------------------------------------------------------

struct AddEffectEntry {
    world::EffectKind kind = world::EffectKind::Comet;
    const char* label = ""; // the schema's displayName
    const char* tip = "";   // the schema's addTip
};

struct AddEffectGroup {
    world::EffectCategory category = world::EffectCategory::Atmosphere;
    const char* name = ""; // effectCategoryName(category)
    std::vector<AddEffectEntry> entries;
};

// The types an owner of `target` kind may take, grouped by category. Groups appear in the order
// their first type appears in the registry and types keep registry order inside a group, so the menu
// is a pure function of the registry: adding a type file adds a menu entry and nothing else. Empty
// when no type supports the target -- the panel then says so rather than offering an empty menu.
[[nodiscard]] std::vector<AddEffectGroup> addEffectMenu(world::EffectTarget target);

// ---- a card's rows -------------------------------------------------------------------------------

// Which rows a card draws where. Pointers into the schema's rows and `sharedEffectFields()`, both of
// which are static, so a `CardRows` never dangles.
//
//   * `main` / `advanced` -- the type's own rows by `EffectField::page`; `Hidden` rows nowhere.
//   * `timing` -- the shared `timing/...` rows that apply; the activation window's two rows only
//     when the activation IS a window, because a start and a length that mean nothing under
//     "always" are two sliders that appear to do nothing.
//   * `groundMain` / `groundAdvanced` -- the shared `ground/...` rows, only for a type that lights the
//     ground (`EffectSchema::groundGlow`) and only while its glow is not Off.
//   * `flow` -- the shared `flow/...` rows, only where `sharedFieldApplies` says the type is sampled
//     against a field.
//
// Every shared row that applies lands in exactly one list, so a registered parameter the panel
// cannot reach is a failing test rather than a slider nobody can find.
struct CardRows {
    std::vector<const world::EffectField*> main;
    std::vector<const world::EffectField*> advanced;
    std::vector<const world::EffectField*> timing;
    std::vector<const world::EffectField*> groundMain;
    std::vector<const world::EffectField*> groundAdvanced;
    std::vector<const world::EffectField*> flow;
};
[[nodiscard]] CardRows effectCardRows(const world::EffectSchema& schema, const world::EffectInstance& effect);

// Does the card offer the "Follows" field picker? True when any shared `flow/...` row applies.
[[nodiscard]] bool effectHasFlowPicker(const world::EffectSchema& schema);

// ---- the status badge ----------------------------------------------------------------------------

enum class BadgeSeverity { Muted, Ok, Warning };

struct StatusBadge {
    const char* label = "";       // the few words on the card header
    const char* explanation = ""; // the sentence the card body and the tooltip say
    BadgeSeverity severity = BadgeSeverity::Muted;
};
// Dropped and Orphaned are warnings, and their explanation says why the effect draws nothing:
// before ADR-702 a dropped effect was a counter nobody read.
[[nodiscard]] StatusBadge effectStatusBadge(world::EffectStatus status);

// ---- the stack -----------------------------------------------------------------------------------

// Where one effect sits in its owner's stack, and whether it can move. `count` is 0 and both moves
// are false for an id that is not in the list.
struct StackPosition {
    std::size_t index = 0; // 0 is the top
    std::size_t count = 0;
    bool canMoveUp = false;
    bool canMoveDown = false;
};
[[nodiscard]] StackPosition effectStackPosition(std::span<const world::EffectInstance> effects,
                                                std::string_view id);

// Puts the type factory's values back on every one of the type's own rows (`schema.fields`), leaving
// identity, owner, order, enabled, style, activation, timing, the shared rows and the endpoints
// alone -- "reset the look", not "replace the effect". False for a kind with no schema or factory.
bool resetEffectParameters(world::EffectInstance& effect);

// ---- endpoints -----------------------------------------------------------------------------------

// The endpoint kinds the Source / Target pickers offer, in enum order, and what each is called.
[[nodiscard]] std::span<const world::SourceKind> endpointKinds();
[[nodiscard]] const char* endpointKindLabel(world::SourceKind kind);
// Node and Hero name a thing; World is a position. The rest need neither.
[[nodiscard]] bool endpointNeedsName(world::SourceKind kind);
[[nodiscard]] bool endpointNeedsPosition(world::SourceKind kind);

// ---- activation ----------------------------------------------------------------------------------

[[nodiscard]] std::span<const char* const> activationLabels(); // indexed by `world::Activation`
// CameraTravel and HeroFocus gate on the director's cut; a scene with no cut never fires them.
[[nodiscard]] bool activationNeedsCut(world::Activation activation);

// ---- beat response -------------------------------------------------------------------------------
//
// The **Beat response** slider owns one ordinary modulation route, `beat.pulse` onto the effect's
// `beatLeaf`: visible and editable in the Modulation panel, saved with the project. Not a hidden
// audio hook.
[[nodiscard]] std::string beatResponseSource();
// `fx/<id>/<beatLeaf>`, or empty for a type that declares no beat leaf.
[[nodiscard]] std::string beatResponseTarget(std::string_view effectId, const world::EffectSchema& schema);
// How much of the parameter's own soft range one unit of beat response is worth.
[[nodiscard]] float beatResponseDepth(float amount, float softRange);

// ---- the edits, with undo ------------------------------------------------------------------------

using EffectEdit = std::function<Result<void>(std::vector<world::EffectInstance>&)>;

// A structural edit, recorded. Captures `engine.capturedEffects()` before, runs the edit through
// `Engine::editEffects` (the one mutation path), captures after, and pushes one `EffectChange` onto
// `history` when it is non-null and the edit succeeded. A refused edit changes nothing and records
// nothing.
Result<void> commitEffectEdit(app::Engine& engine, EditHistory* history, std::string label,
                              const EffectEdit& edit);

// "+ Add Effect": appends a new instance of `kind` to `owner`'s stack and attaches the type's default
// audio routes (a gesture, not a load -- see `Engine::addDefaultEffectRoutes`), recorded as ONE
// undoable edit covering both, so an undone add takes its routes with it. Returns the new id.
Result<std::string> addEffectTo(app::Engine& engine, EditHistory* history, const world::EffectOwner& owner,
                                world::EffectKind kind);

// The authored list the panel draws: the composition's when there is one, else the engine's.
[[nodiscard]] const std::vector<world::EffectInstance>& authoredEffects(const app::Engine& engine);

} // namespace avgen::ui
