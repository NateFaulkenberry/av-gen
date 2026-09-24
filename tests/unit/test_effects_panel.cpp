// The Effects section's decisions (ADR-702), asked without a window.
//
// The section draws ONE owner's stack for every owner and every type, reading everything from the
// registry. So the useful questions are about that reading: does the Add Effect menu offer exactly
// what the registry allows on that owner, can every registered parameter of every type be reached
// from some list on its card, does a dropped or orphaned effect say so, and does a structural edit
// undo and redo through the editor's history. Each invariant below was seen to fail by breaking
// the code it guards (see the note on each).

#include "app/engine.hpp"
#include "params/modulation.hpp"
#include "ui/edit_history.hpp"
#include "ui/effects_panel_logic.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace avgen;

namespace {

std::vector<world::EffectKind> flatten(const std::vector<ui::AddEffectGroup>& menu) {
    std::vector<world::EffectKind> out;
    for (const ui::AddEffectGroup& g : menu) {
        for (const ui::AddEffectEntry& e : g.entries) {
            out.push_back(e.kind);
        }
    }
    return out;
}

std::vector<world::EffectKind> sorted(std::vector<world::EffectKind> v) {
    std::sort(v.begin(), v.end());
    return v;
}

// Every row a card would show, whatever list it landed in.
std::vector<const world::EffectField*> everyRow(const ui::CardRows& r) {
    std::vector<const world::EffectField*> out;
    for (const auto* list : {&r.main, &r.advanced, &r.timing, &r.groundMain, &r.groundAdvanced, &r.flow}) {
        out.insert(out.end(), list->begin(), list->end());
    }
    return out;
}

std::vector<std::string> idsOf(const std::vector<world::EffectInstance>& list) {
    std::vector<std::string> out;
    for (const world::EffectInstance& e : list) {
        out.push_back(e.id);
    }
    return out;
}

} // namespace

// Guard: the menu is the registry's answer, not a list of its own. Seen red by making
// `addEffectMenu` skip the last type of each group.
TEST_CASE("the Add Effect menu offers exactly the types the registry allows on each owner", "[effects][ui]") {
    for (const world::EffectTarget target : {world::EffectTarget::World, world::EffectTarget::Entity,
                                             world::EffectTarget::Camera, world::EffectTarget::Light}) {
        INFO("target " << world::effectTargetName(target));
        const std::vector<ui::AddEffectGroup> menu = ui::addEffectMenu(target);
        const std::vector<world::EffectKind> offered = flatten(menu);
        CHECK(sorted(offered) == sorted(world::effectKindsFor(target)));
        for (const world::EffectKind kind : offered) {
            CHECK(world::effectAllowedOn(kind, target));
        }
        std::set<std::string_view> groupNames;
        for (const ui::AddEffectGroup& g : menu) {
            CHECK_FALSE(g.entries.empty());
            // One group per category, named by the registry's own word for it.
            CHECK(groupNames.insert(g.name).second);
            CHECK(std::string_view(g.name) == world::effectCategoryName(g.category));
            for (const ui::AddEffectEntry& e : g.entries) {
                const world::EffectSchema* schema = world::effectSchema(e.kind);
                REQUIRE(schema != nullptr);
                CHECK(schema->category == g.category);
                CHECK(std::string_view(e.label) == schema->displayName);
                CHECK(std::string_view(e.tip) == schema->addTip);
            }
        }
    }
}

TEST_CASE("the Add Effect menu keeps registry order, and is empty for an owner no type supports", "[effects][ui]") {
    // Registry order within a group: the position of each kind in `effectKindsFor` only increases.
    const std::vector<world::EffectKind> registry = world::effectKindsFor(world::EffectTarget::World);
    for (const ui::AddEffectGroup& g : ui::addEffectMenu(world::EffectTarget::World)) {
        std::size_t last = 0;
        for (const ui::AddEffectEntry& e : g.entries) {
            const auto at = static_cast<std::size_t>(
                std::distance(registry.begin(), std::find(registry.begin(), registry.end(), e.kind)));
            CHECK(at >= last);
            last = at;
        }
    }
    // A hero takes a Ground Pulse; a light takes nothing today, and the panel says so rather than
    // offering an empty menu -- which is only possible if this is empty rather than a group of none.
    const auto entity = flatten(ui::addEffectMenu(world::EffectTarget::Entity));
    CHECK(std::find(entity.begin(), entity.end(), world::EffectKind::GroundPulse) != entity.end());
    CHECK(ui::addEffectMenu(world::EffectTarget::Light).empty() ==
          world::effectKindsFor(world::EffectTarget::Light).empty());
}

// Guard: a registered parameter no card list holds is a slider nobody can find. Seen red by making
// `effectCardRows` drop the `flow/` rows.
TEST_CASE("every parameter an effect registers is reachable from some list on its card", "[effects][ui]") {
    for (const world::EffectSchema* schema : world::effectSchemas()) {
        INFO("type " << schema->displayName);
        world::EffectInstance e = world::makeEffect(schema->kind, schema->displayName);
        // The most-disclosed state: a window activation and the ground lit, so the conditional rows
        // are all on the card.
        e.activation = world::Activation::Window;
        e.ground.mode = world::GroundGlow::Strong;

        const std::vector<const world::EffectField*> rows = everyRow(ui::effectCardRows(*schema, e));
        std::set<std::string_view> leaves;
        for (const world::EffectField* f : rows) {
            // Each row once: two sliders on one path is two controls that fight.
            CHECK(leaves.insert(f->leaf).second);
        }
        for (const world::EffectField& f : schema->fields) {
            if (f.page != world::FieldPage::Hidden) {
                INFO("own row " << f.leaf);
                CHECK(leaves.count(f.leaf) == 1);
            }
        }
        for (const world::EffectField& f : world::sharedEffectFields()) {
            const bool ground = std::string_view(f.jsonPath).starts_with("ground/");
            const bool expected = world::sharedFieldApplies(*schema, f) && (!ground || schema->groundGlow);
            INFO("shared row " << f.leaf);
            CHECK((leaves.count(f.leaf) == 1) == expected);
        }
        // Control: a leaf nothing declares is not on the card, so the checks above cannot pass by
        // answering yes to everything.
        CHECK(leaves.count("nonesuch") == 0);
    }
}

TEST_CASE("a card hides the rows its current settings make meaningless", "[effects][ui]") {
    for (const world::EffectSchema* schema : world::effectSchemas()) {
        INFO("type " << schema->displayName);
        world::EffectInstance e = world::makeEffect(schema->kind, schema->displayName);
        e.activation = world::Activation::Always;
        e.ground.mode = world::GroundGlow::Off;
        const ui::CardRows rows = ui::effectCardRows(*schema, e);
        for (const world::EffectField* f : rows.timing) {
            CHECK_FALSE(std::string_view(f->jsonPath).starts_with("timing/window"));
        }
        CHECK(rows.groundMain.empty());
        CHECK(rows.groundAdvanced.empty());
        // The flow picker is offered exactly when a flow row is.
        e.activation = world::Activation::Window;
        CHECK(ui::effectHasFlowPicker(*schema) == !ui::effectCardRows(*schema, e).flow.empty());
    }
}

// Guard: the two statuses that mean "draws nothing" are warnings and say why. Seen red by making
// Dropped a Muted badge.
TEST_CASE("a dropped or orphaned effect warns, and says why", "[effects][ui]") {
    const ui::StatusBadge dropped = ui::effectStatusBadge(world::EffectStatus::Dropped);
    CHECK(dropped.severity == ui::BadgeSeverity::Warning);
    CHECK(std::string_view(dropped.explanation).find("GPU capacity is full") != std::string_view::npos);
    const ui::StatusBadge orphaned = ui::effectStatusBadge(world::EffectStatus::Orphaned);
    CHECK(orphaned.severity == ui::BadgeSeverity::Warning);
    CHECK(std::string_view(orphaned.explanation).find("owner is not in this scene") != std::string_view::npos);
    for (const world::EffectStatus s :
         {world::EffectStatus::Disabled, world::EffectStatus::Dormant, world::EffectStatus::Drawn}) {
        CHECK(ui::effectStatusBadge(s).severity != ui::BadgeSeverity::Warning);
        CHECK(std::string_view(ui::effectStatusBadge(s).label).size() > 0);
    }
}

// Guard: move up/down are offered only where they would move something, per OWNER. Seen red by
// computing the index over the whole list rather than the owner's stack.
TEST_CASE("move up and move down are enabled by the effect's place in its owner's stack", "[effects][ui]") {
    std::vector<world::EffectInstance> list;
    const auto worldOwner = world::EffectOwner::world();
    const auto rook = world::EffectOwner::entity("rook");
    const std::string a = *world::addEffect(list, worldOwner, world::EffectKind::Aurora);
    const std::string p = *world::addEffect(list, rook, world::EffectKind::GroundPulse);
    const std::string b = *world::addEffect(list, worldOwner, world::EffectKind::Comet);
    const std::string c = *world::addEffect(list, worldOwner, world::EffectKind::Vortex);

    const ui::StackPosition top = ui::effectStackPosition(list, a);
    CHECK(top.count == 3);
    CHECK(top.index == 0);
    CHECK_FALSE(top.canMoveUp);
    CHECK(top.canMoveDown);
    const ui::StackPosition mid = ui::effectStackPosition(list, b);
    CHECK(mid.index == 1);
    CHECK((mid.canMoveUp && mid.canMoveDown));
    const ui::StackPosition bottom = ui::effectStackPosition(list, c);
    CHECK(bottom.index == 2);
    CHECK(bottom.canMoveUp);
    CHECK_FALSE(bottom.canMoveDown);
    // Alone in its stack, whatever sits beside it in the list.
    const ui::StackPosition alone = ui::effectStackPosition(list, p);
    CHECK(alone.count == 1);
    CHECK_FALSE(alone.canMoveUp);
    CHECK_FALSE(alone.canMoveDown);
    const ui::StackPosition missing = ui::effectStackPosition(list, "nonesuch");
    CHECK(missing.count == 0);
    CHECK_FALSE((missing.canMoveUp || missing.canMoveDown));
}

// Guard: Reset parameters restores the type's values and nothing else. Seen red by resetting from a
// default-constructed instance instead of the factory's.
TEST_CASE("reset parameters puts the factory's values back and leaves identity and timing alone", "[effects][ui]") {
    for (const world::EffectSchema* schema : world::effectSchemas()) {
        INFO("type " << schema->displayName);
        const world::EffectInstance factory = world::makeEffect(schema->kind, "probe");
        world::EffectInstance e = factory;
        e.id = "my-effect";
        e.name = "Mine";
        e.timing.delay = 3.5;
        const world::EffectField* moved = nullptr;
        for (const world::EffectField& f : schema->fields) {
            if (f.type == world::FieldType::Float && f.hardMax > f.hardMin) {
                moved = &f;
                break;
            }
        }
        REQUIRE(moved != nullptr);
        const float original = world::fieldFloat(*moved, *schema, factory);
        const float other = original == moved->hardMin ? moved->hardMax : moved->hardMin;
        world::setFieldFloat(*moved, *schema, e, other);
        REQUIRE(world::fieldFloat(*moved, *schema, e) == other);

        REQUIRE(ui::resetEffectParameters(e));
        CHECK(world::fieldFloat(*moved, *schema, e) == original);
        CHECK(e.id == "my-effect");
        CHECK(e.name == "Mine");
        CHECK(e.timing.delay == 3.5);
    }
}

TEST_CASE("the beat response route aims at the type's declared beat leaf on this instance", "[effects][ui]") {
    for (const world::EffectSchema* schema : world::effectSchemas()) {
        INFO("type " << schema->displayName);
        const std::string target = ui::beatResponseTarget("some-id", *schema);
        if (schema->beatLeaf[0] == '\0') {
            CHECK(target.empty());
        } else {
            CHECK(target == world::effectParameterPrefix("some-id") + schema->beatLeaf);
        }
    }
    CHECK(ui::beatResponseSource() == "beat.pulse");
    CHECK(ui::beatResponseDepth(0.25f, 8.0f) == 2.0f);
    CHECK(ui::beatResponseDepth(-1.0f, 8.0f) == 0.0f);
    CHECK(ui::beatResponseDepth(2.0f, 8.0f) == 8.0f);
}

// Guard: a structural edit is ONE undo step that puts the list back, and redo puts it forward again.
// Seen red by removing the `command.effects` block from `applyEdit` (undo then changed nothing).
TEST_CASE("add, move and remove are undone and redone through the editor history", "[effects][ui]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setEffects({}).has_value());
    ui::EditHistory history;
    const auto worldOwner = world::EffectOwner::world();
    const std::size_t routesAtStart = engine.modulator().routes().size();

    auto aurora = ui::addEffectTo(engine, &history, worldOwner, world::EffectKind::Aurora);
    REQUIRE(aurora.has_value());
    auto comet = ui::addEffectTo(engine, &history, worldOwner, world::EffectKind::Comet);
    REQUIRE(comet.has_value());
    CHECK(history.undoSize() == 2);
    CHECK(idsOf(engine.effects()) == std::vector<std::string>{*aurora, *comet});
    // Adding registered the new instance's parameters.
    CHECK(engine.params().find(world::effectParameterPrefix(*comet) + "enabled") != nullptr);

    // A slider moved after the add survives the next structural edit and its undo: the snapshots
    // are captured lists, not the authored ones.
    const std::string enabledPath = world::effectParameterPrefix(*aurora) + "enabled";
    REQUIRE(engine.params().find(enabledPath) != nullptr);
    engine.params().find(enabledPath)->setBaseComponent(0, 0.0f);

    const std::string cometId = *comet;
    REQUIRE(ui::commitEffectEdit(engine, &history, "Move comet up",
                                 [&](std::vector<world::EffectInstance>& l) -> Result<void> {
                                     if (!world::moveEffect(l, cometId, -1)) {
                                         return fail("did not move");
                                     }
                                     return {};
                                 })
                .has_value());
    CHECK(idsOf(engine.effects()) == std::vector<std::string>{*comet, *aurora});
    CHECK(engine.params().find(enabledPath)->baseComponent(0) == 0.0f);
    CHECK(history.undoLabel() == "Move comet up");

    CHECK(history.undo(engine).ok());
    CHECK(idsOf(engine.effects()) == std::vector<std::string>{*aurora, *comet});
    CHECK(engine.params().find(enabledPath)->baseComponent(0) == 0.0f);
    CHECK(history.redo(engine).ok());
    CHECK(idsOf(engine.effects()) == std::vector<std::string>{*comet, *aurora});

    // Remove, then undo brings the same instance back under the same id.
    REQUIRE(ui::commitEffectEdit(engine, &history, "Remove comet",
                                 [&](std::vector<world::EffectInstance>& l) -> Result<void> {
                                     if (!world::removeEffect(l, cometId)) {
                                         return fail("not there");
                                     }
                                     return {};
                                 })
                .has_value());
    CHECK(idsOf(engine.effects()) == std::vector<std::string>{*aurora});
    CHECK(engine.params().find(world::effectParameterPrefix(cometId) + "enabled") == nullptr);
    const ui::EditApply undone = history.undo(engine);
    CHECK(undone.ok());
    CHECK(undone.effectListsInstalled == 1);
    CHECK(idsOf(engine.effects()) == std::vector<std::string>{*comet, *aurora});
    CHECK(engine.params().find(world::effectParameterPrefix(cometId) + "enabled") != nullptr);

    // All the way back to nothing, one step per gesture -- and an undone add takes the default
    // routes it attached with it, rather than leaving routes aimed at parameters that are gone.
    const auto routesAt = [&](const std::string& id) {
        return std::count_if(engine.modulator().routes().begin(), engine.modulator().routes().end(),
                             [&](const params::ModRoute& r) {
                                 return r.target.starts_with(world::effectParameterPrefix(id));
                             });
    };
    CHECK(routesAt(cometId) == static_cast<long>(world::defaultEffectRoutes(cometId, world::EffectKind::Comet).size()));
    CHECK(history.undo(engine).ok()); // the move
    CHECK(history.undo(engine).ok()); // add comet
    CHECK(routesAt(cometId) == 0);
    CHECK(history.undo(engine).ok()); // add aurora
    CHECK(engine.effects().empty());
    CHECK(engine.modulator().routes().size() == routesAtStart);
    CHECK_FALSE(history.canUndo());
    CHECK(history.redo(engine).ok()); // add aurora again, routes and all
    CHECK(routesAt(*aurora) > 0);
}

TEST_CASE("a refused structural edit changes nothing and records nothing", "[effects][ui]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setEffects({}).has_value());
    ui::EditHistory history;
    // No type attaches to a light today; if one ever does, pick a target nothing supports instead.
    if (!world::effectKindsFor(world::EffectTarget::Light).empty()) {
        SKIP("a type now supports lights");
    }
    auto refused = ui::addEffectTo(engine, &history, world::EffectOwner::light("key"), world::EffectKind::Aurora);
    CHECK_FALSE(refused.has_value());
    CHECK(engine.effects().empty());
    CHECK_FALSE(history.canUndo());
}
