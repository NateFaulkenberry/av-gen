#include "ui/effects_panel_logic.hpp"

#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "ui/edit_history.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_stack.hpp"

#include <algorithm>
#include <array>
#include <memory>

namespace avgen::ui {

std::vector<AddEffectGroup> addEffectMenu(world::EffectTarget target) {
    std::vector<AddEffectGroup> groups;
    for (const world::EffectKind kind : world::effectKindsFor(target)) {
        const world::EffectSchema* schema = world::effectSchema(kind);
        if (schema == nullptr) {
            continue;
        }
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&](const AddEffectGroup& g) { return g.category == schema->category; });
        if (it == groups.end()) {
            AddEffectGroup group;
            group.category = schema->category;
            group.name = world::effectCategoryName(schema->category);
            groups.push_back(std::move(group));
            it = std::prev(groups.end());
        }
        it->entries.push_back(AddEffectEntry{kind, schema->displayName, schema->addTip});
    }
    return groups;
}

namespace {

bool startsWith(const char* text, std::string_view prefix) {
    return text != nullptr && std::string_view(text).starts_with(prefix);
}

} // namespace

CardRows effectCardRows(const world::EffectSchema& schema, const world::EffectInstance& effect) {
    CardRows rows;
    for (const world::EffectField& field : schema.fields) {
        switch (field.page) {
        case world::FieldPage::Main: rows.main.push_back(&field); break;
        case world::FieldPage::Advanced: rows.advanced.push_back(&field); break;
        case world::FieldPage::Hidden: break;
        }
    }
    const bool groundLit = schema.groundGlow && effect.ground.mode != world::GroundGlow::Off;
    for (const world::EffectField& field : world::sharedEffectFields()) {
        if (!world::sharedFieldApplies(schema, field) || field.page == world::FieldPage::Hidden) {
            continue;
        }
        // Filed by where the row's value lives, which is what the row declares about itself, so a
        // shared row added later lands somewhere without this function learning its leaf.
        if (startsWith(field.jsonPath, "timing/")) {
            if (startsWith(field.jsonPath, "timing/window") && effect.activation != world::Activation::Window) {
                continue;
            }
            rows.timing.push_back(&field);
        } else if (startsWith(field.jsonPath, "ground/")) {
            if (!groundLit) {
                continue;
            }
            (field.page == world::FieldPage::Main ? rows.groundMain : rows.groundAdvanced).push_back(&field);
        } else if (startsWith(field.jsonPath, "flow/")) {
            rows.flow.push_back(&field);
        } else {
            rows.advanced.push_back(&field);
        }
    }
    return rows;
}

bool effectHasFlowPicker(const world::EffectSchema& schema) {
    for (const world::EffectField& field : world::sharedEffectFields()) {
        if (startsWith(field.jsonPath, "flow/") && world::sharedFieldApplies(schema, field)) {
            return true;
        }
    }
    return false;
}

StatusBadge effectStatusBadge(world::EffectStatus status) {
    switch (status) {
    case world::EffectStatus::Disabled:
        return {"off", "Switched off.", BadgeSeverity::Muted};
    case world::EffectStatus::Dormant:
        return {"dormant", "On, but outside its activation window right now.", BadgeSeverity::Muted};
    case world::EffectStatus::Drawn:
        return {"drawn", "Contributing to this frame.", BadgeSeverity::Ok};
    case world::EffectStatus::Dropped:
        return {"not drawn",
                "Not drawn: its render stage's GPU capacity is full. Effects above it in the same "
                "stage took every slot; disable one of them, or this one.",
                BadgeSeverity::Warning};
    case world::EffectStatus::Partial:
        return {"partial",
                "Drawn, but not all of it: one of its parts did not fit its budget, or its owner "
                "cannot give it what it needs. The line below says which.",
                BadgeSeverity::Warning};
    case world::EffectStatus::Orphaned:
        return {"orphaned",
                "Not drawn: its owner is not in this scene (deleted or renamed). Remove it, or "
                "restore the owner.",
                BadgeSeverity::Warning};
    }
    return {"dormant", "", BadgeSeverity::Muted};
}

StackPosition effectStackPosition(std::span<const world::EffectInstance> effects, std::string_view id) {
    StackPosition out;
    const std::size_t at = world::findEffect(effects, id);
    if (at == effects.size()) {
        return out;
    }
    const std::vector<std::size_t> stack = world::effectsOf(effects, effects[at].owner);
    const auto it = std::find(stack.begin(), stack.end(), at);
    out.count = stack.size();
    out.index = static_cast<std::size_t>(std::distance(stack.begin(), it));
    out.canMoveUp = out.index > 0;
    out.canMoveDown = out.index + 1 < out.count;
    return out;
}

bool resetEffectParameters(world::EffectInstance& effect) {
    const world::EffectSchema* schema = world::effectSchema(effect.kind);
    if (schema == nullptr || schema->factory == nullptr) {
        return false;
    }
    const world::EffectInstance fresh = world::makeEffect(effect.kind, effect.name);
    for (const world::EffectField& field : schema->fields) {
        switch (field.type) {
        case world::FieldType::Float:
        case world::FieldType::Choice:
            world::setFieldFloat(field, *schema, effect, world::fieldFloat(field, *schema, fresh));
            break;
        case world::FieldType::Color:
            world::setFieldColor(field, *schema, effect, world::fieldColor(field, *schema, fresh));
            break;
        case world::FieldType::Bool:
            world::setFieldBool(field, *schema, effect, world::fieldBool(field, *schema, fresh));
            break;
        }
    }
    return true;
}

namespace {

constexpr std::array<world::SourceKind, 6> kEndpointKinds{
    world::SourceKind::World,  world::SourceKind::Node,      world::SourceKind::Hero,
    world::SourceKind::Camera, world::SourceKind::FocusHero, world::SourceKind::Owner,
};

constexpr const char* kActivationLabels[] = {"always", "authored window", "while the camera travels",
                                             "while a hero is in focus"};

} // namespace

std::span<const world::SourceKind> endpointKinds() { return kEndpointKinds; }

const char* endpointKindLabel(world::SourceKind kind) {
    switch (kind) {
    case world::SourceKind::World: return "world position";
    case world::SourceKind::Node: return "scene node";
    case world::SourceKind::Hero: return "hero";
    case world::SourceKind::Camera: return "active camera";
    case world::SourceKind::FocusHero: return "the hero in focus";
    case world::SourceKind::Owner: return "what it is attached to";
    }
    return "world position";
}

bool endpointNeedsName(world::SourceKind kind) {
    return kind == world::SourceKind::Node || kind == world::SourceKind::Hero;
}

bool endpointNeedsPosition(world::SourceKind kind) { return kind == world::SourceKind::World; }

std::span<const char* const> activationLabels() { return kActivationLabels; }

bool activationNeedsCut(world::Activation activation) {
    return activation == world::Activation::CameraTravel || activation == world::Activation::HeroFocus;
}

std::string beatResponseSource() { return "beat.pulse"; }

std::string beatResponseTarget(std::string_view effectId, const world::EffectSchema& schema) {
    if (schema.beatLeaf == nullptr || schema.beatLeaf[0] == '\0') {
        return {};
    }
    return world::effectParameterPrefix(effectId) + schema.beatLeaf;
}

float beatResponseDepth(float amount, float softRange) {
    return std::clamp(amount, 0.0f, 1.0f) * std::max(softRange, 0.0f);
}

Result<void> commitEffectEdit(app::Engine& engine, EditHistory* history, std::string label,
                              const EffectEdit& edit) {
    std::vector<world::EffectInstance> before = engine.capturedEffects();
    if (auto ok = engine.editEffects(edit); !ok) {
        return ok;
    }
    if (history != nullptr) {
        EditCommand command(std::move(label));
        command.effects = std::make_unique<EffectChange>();
        command.effects->before = std::move(before);
        command.effects->after = engine.capturedEffects();
        history->push(std::move(command));
    }
    return {};
}

Result<std::string> addEffectTo(app::Engine& engine, EditHistory* history, const world::EffectOwner& owner,
                                world::EffectKind kind) {
    const world::EffectSchema* schema = world::effectSchema(kind);
    std::vector<world::EffectInstance> before = engine.capturedEffects();
    std::vector<params::ModRoute> routesBefore = engine.modulator().routes();
    std::string id;
    auto ok = engine.editEffects([&](std::vector<world::EffectInstance>& list) -> Result<void> {
        auto added = world::addEffect(list, owner, kind);
        if (!added) {
            return std::unexpected(added.error());
        }
        id = *added;
        return {};
    });
    if (!ok) {
        return std::unexpected(ok.error());
    }
    engine.addDefaultEffectRoutes(id);
    // One undo step for the whole gesture: the effect AND the routes it came with, so undoing the
    // add does not leave routes aimed at parameters that are gone.
    if (history != nullptr) {
        EditCommand command(std::string("Add ") + (schema != nullptr ? schema->displayName : "effect"));
        command.effects = std::make_unique<EffectChange>();
        command.effects->before = std::move(before);
        command.effects->after = engine.capturedEffects();
        command.effects->routesTouched = true;
        command.effects->routesBefore = std::move(routesBefore);
        command.effects->routesAfter = engine.modulator().routes();
        history->push(std::move(command));
    }
    return id;
}

const std::vector<world::EffectInstance>& authoredEffects(const app::Engine& engine) {
    if (const scene::Composition* comp = engine.composition(); comp != nullptr) {
        return comp->effects();
    }
    return engine.effects();
}

} // namespace avgen::ui
