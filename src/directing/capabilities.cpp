#include "directing/capabilities.hpp"

#include "entity/airborne.hpp"
#include "entity/behavior.hpp"
#include "entity/entity.hpp"
#include "entity/locomotion.hpp"
#include "scene/animation.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"
#include "seq/events.hpp"
#include "seq/sequence.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_kind.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_timing.hpp"

#include <algorithm>
#include <optional>
#include <set>

namespace avgen::directing {
namespace {

// Every value of an enum with a name table, found by asking the table rather than by listing the
// values here: a value whose name reads back as itself is a real one. A value added to the enum
// and its table appears in the registry with no edit to this file, which is the point.
//
// **Only for tables whose name function is total** -- the `seq::` ones, which search a table of
// pairs and fall back to its first entry for a value they do not know. A name function that indexes
// an array (`scene::shotTransitionName`) must be walked through its own `all...()` list instead;
// probing it out of range reads past the table, which is how this was found.
template <typename E, typename Name, typename From>
std::vector<std::string> enumerate(Name name, From from) {
    std::vector<std::string> out;
    for (int i = 0; i < 256; ++i) {
        const auto value = static_cast<E>(i);
        const char* text = name(value);
        if (text == nullptr) {
            continue;
        }
        if (const std::optional<E> back = from(text); back && *back == value) {
            out.emplace_back(text);
        }
    }
    return out;
}

ActivityKind kindOf(std::string_view activity) {
    entity::Activity known{};
    if (!entity::activityFromName(activity, known)) {
        return ActivityKind::Custom;
    }
    // A switch, so a new `entity::Activity` is a compile warning here rather than a silent Custom.
    switch (known) {
    case entity::Activity::Idle:
    case entity::Activity::Walk:
    case entity::Activity::Run:
    case entity::Activity::Turn: return ActivityKind::Ground;
    case entity::Activity::Observe:
    case entity::Activity::React: return ActivityKind::Action;
    case entity::Activity::Jump:
    case entity::Activity::Fall:
    case entity::Activity::Land: return ActivityKind::Airborne;
    }
    return ActivityKind::Custom;
}

// ADR-822: a character's jump is one data block, read by `explore`'s autonomous hops and by this
// card alike, so the two cannot disagree about what the body can do. The numbers are the entity's
// own; when it authors no block they are `entity::JumpSettings`' defaults, and the card says so.
JumpEnvelope jumpOf(const entity::EntityDesc& desc) {
    const entity::JumpSettings defaults;
    const entity::JumpSettings& j = desc.jump;
    JumpEnvelope out;
    out.apex = j.apex;
    out.maxApex = j.apexLimit();
    out.gravity = j.gravity;
    out.maxDistance = j.maxDistance;
    out.landSeconds = j.landSeconds;
    const bool authored = j.apex != defaults.apex || j.maxApex != defaults.maxApex || j.gravity != defaults.gravity ||
                          j.maxDistance != defaults.maxDistance || j.landSeconds != defaults.landSeconds ||
                          j.maxSeconds != defaults.maxSeconds;
    out.source = authored ? "jump" : "default";
    return out;
}

CharacterCard cardFor(const entity::EntityDesc& desc, const scene::Composition& composition) {
    CharacterCard card;
    card.subject = desc.name;
    card.node = desc.node.empty() ? desc.name : desc.node;
    card.tags = desc.tags;
    card.affordances = desc.capabilities;
    card.walkSpeed = desc.gait.walkSpeed;
    card.runSpeed = desc.gait.runSpeed;
    card.jump = jumpOf(desc);
    for (const entity::BehaviorDesc& behavior : desc.behaviors) {
        if (behavior.kind != "decide" || !behavior.settings.is_object()) {
            continue;
        }
        if (const auto it = behavior.settings.find("considerers"); it != behavior.settings.end() && it->is_array()) {
            for (const nlohmann::json& c : *it) {
                card.goalSlot = card.goalSlot || (c.is_object() && c.value("kind", std::string()) == "goal");
            }
        }
    }

    // The node's loaded rigs: what clips actually exist, not what the description hopes for.
    std::vector<const scene::SkinnedRig*> rigs;
    if (const scene::CompositionNode* node = composition.findNode(card.node); node != nullptr) {
        const auto& all = composition.scene().rigs;
        for (const scene::RigId id : node->rigs) {
            if (id < all.size()) {
                rigs.push_back(&all[id]);
            }
        }
    }
    card.rigLoaded = !rigs.empty();
    const scene::ClipSemanticsTable* semantics = composition.clipSemanticsFor(card.node);

    std::set<std::string> mappedClips;
    for (const auto& [activity, stateName] : desc.clips) {
        ActivityCapability cap;
        cap.activity = activity;
        cap.kind = kindOf(activity);
        cap.clip = stateName;
        for (const scene::SkinnedRig* rig : rigs) {
            if (const scene::AnimationState* state = rig->player.findState(stateName);
                state != nullptr && state->clip < rig->clips.size()) {
                cap.available = true;
                cap.seconds = rig->clips[state->clip].length();
                cap.loops = state->loop;
                cap.clip = rig->clips[state->clip].name;
                if (semantics != nullptr) {
                    const scene::ClipSemantics* measured = semantics->find(stateName);
                    measured = measured != nullptr ? measured : semantics->find(cap.clip);
                    if (measured != nullptr) {
                        cap.semantics = *measured;
                        cap.loops = measured->loops;
                    }
                }
                break;
            }
        }
        mappedClips.insert(cap.clip);
        card.activities.push_back(std::move(cap));
    }
    for (const scene::SkinnedRig* rig : rigs) {
        for (const scene::AnimationClip& clip : rig->clips) {
            if (!mappedClips.contains(clip.name)) {
                card.unmappedClips.push_back(clip.name);
                mappedClips.insert(clip.name);
            }
        }
    }
    return card;
}

} // namespace

const char* activityKindName(ActivityKind kind) {
    switch (kind) {
    case ActivityKind::Ground: return "ground";
    case ActivityKind::Action: return "action";
    case ActivityKind::Airborne: return "airborne";
    case ActivityKind::Custom: return "custom";
    }
    return "custom";
}

const ActivityCapability* CharacterCard::activity(std::string_view name) const {
    const auto it = std::find_if(activities.begin(), activities.end(),
                                 [&](const ActivityCapability& a) { return a.activity == name; });
    return it == activities.end() ? nullptr : &*it;
}

bool CharacterCard::can(std::string_view activityName) const {
    const ActivityCapability* a = activity(activityName);
    return a != nullptr && a->available;
}

std::vector<std::string> CharacterCard::available(ActivityKind kind) const {
    std::vector<std::string> out;
    for (const ActivityCapability& a : activities) {
        if (a.kind == kind && a.available) {
            out.push_back(a.activity);
        }
    }
    return out;
}

nlohmann::json CharacterCard::toJson() const {
    nlohmann::json activitiesJson = nlohmann::json::array();
    for (const ActivityCapability& a : activities) {
        activitiesJson.push_back({{"activity", a.activity},
                                  {"kind", activityKindName(a.kind)},
                                  {"available", a.available},
                                  {"seconds", a.seconds},
                                  {"loops", a.loops},
                                  {"clip", a.clip}});
        if (a.semantics) {
            nlohmann::json events = nlohmann::json::array();
            for (const scene::ClipEvent& e : a.semantics->events) {
                events.push_back({{"name", e.name}, {"seconds", e.seconds}});
            }
            activitiesJson.back()["events"] = std::move(events);
            activitiesJson.back()["ground"] = scene::clipGroundName(a.semantics->ground);
        }
    }
    return {{"subject", subject},
            {"node", node},
            {"tags", tags},
            {"activities", std::move(activitiesJson)},
            {"locomotion", {{"walkSpeed", walkSpeed}, {"runSpeed", runSpeed}}},
            {"jump",
             {{"apex", jump.apex}, {"maxApex", jump.maxApex}, {"gravity", jump.gravity}, {"maxDistance", jump.maxDistance},
              {"landSeconds", jump.landSeconds}, {"source", jump.source}}},
            {"affordances", affordances},
            {"unmappedClips", unmappedClips},
            {"goalSlot", goalSlot},
            {"rigLoaded", rigLoaded}};
}

nlohmann::json CameraCatalog::toJson() const {
    nlohmann::json rigsJson = nlohmann::json::array();
    for (const CameraRigCapability& r : rigs) {
        rigsJson.push_back({{"name", r.name},
                            {"slug", r.slug},
                            {"placement", r.placement},
                            {"aimNode", r.aimNode},
                            {"followNode", r.followNode}});
    }
    return {{"rigs", std::move(rigsJson)},
            {"presets", presets},
            {"behaviors", behaviors},
            {"shotCameraKinds", shotCameraKinds},
            {"sequenceTransitions", sequenceTransitions},
            {"cutTransitions", cutTransitions}};
}

nlohmann::json EventCatalog::toJson() const {
    const auto list = [](const std::vector<EventKindCapability>& kinds) {
        nlohmann::json out = nlohmann::json::array();
        for (const EventKindCapability& k : kinds) {
            out.push_back({{"name", k.name}, {"deterministic", k.deterministic}});
        }
        return out;
    };
    return {{"triggers", list(triggers)}, {"actions", list(actions)}};
}

CapabilityRegistry CapabilityRegistry::fromComposition(const scene::Composition& composition) {
    CapabilityRegistry out;
    for (const entity::EntityDesc& desc : composition.entities()) {
        out.characters_.push_back(cardFor(desc, composition));
    }

    for (const scene::CameraRig& rig : composition.cameraDirection().cameras) {
        out.cameras_.rigs.push_back(CameraRigCapability{rig.name, rig.slug,
                                                        scene::cameraPlacementName(rig.placement),
                                                        rig.aimNode, rig.followNode});
    }
    for (const seq::CameraPreset preset : seq::allCameraPresets()) {
        out.cameras_.presets.emplace_back(seq::cameraPresetName(preset));
    }
    out.cameras_.behaviors = enumerate<seq::CameraBehaviorKind>(seq::cameraBehaviorName, seq::cameraBehaviorFromName);
    out.cameras_.shotCameraKinds = enumerate<seq::CameraKind>(seq::cameraKindName, seq::cameraKindFromName);
    out.cameras_.sequenceTransitions =
        enumerate<seq::TransitionKind>(seq::transitionKindName, seq::transitionKindFromName);
    for (const scene::ShotTransition transition : scene::allShotTransitions()) {
        out.cameras_.cutTransitions.emplace_back(scene::shotTransitionName(transition));
    }

    for (const std::string& name : enumerate<seq::TriggerKind>(seq::triggerKindName, seq::triggerKindFromName)) {
        out.events_.triggers.push_back(
            EventKindCapability{name, seq::triggerIsScheduled(*seq::triggerKindFromName(name))});
    }
    for (const std::string& name :
         enumerate<seq::EventActionKind>(seq::eventActionKindName, seq::eventActionKindFromName)) {
        out.events_.actions.push_back(
            EventKindCapability{name, seq::actionIsBaked(*seq::eventActionKindFromName(name))});
    }
    // Effects (ADR-702): the registry's types and the scene's instances.
    for (const world::EffectSchema* schema : world::effectSchemas()) {
        if (schema == nullptr) {
            continue;
        }
        EffectTypeCapability t;
        t.type = schema->key;
        t.displayName = schema->displayName;
        t.category = world::effectCategoryName(schema->category);
        t.stage = world::renderStageName(schema->stage);
        for (std::size_t k = 0; k < world::kEffectTargetCount; ++k) {
            const auto target = static_cast<world::EffectTarget>(k);
            if ((schema->targets & world::targetBit(target)) != 0) {
                t.owners.emplace_back(world::effectTargetName(target));
            }
        }
        for (const world::EffectField& field : schema->fields) {
            if (field.page != world::FieldPage::Hidden) {
                t.fields.emplace_back(field.leaf);
            }
        }
        out.effects_.types.push_back(std::move(t));
    }
    for (const world::EffectInstance& e : composition.effects()) {
        out.effects_.instances.push_back(EffectInstanceCapability{
            e.id, world::effectKindName(e.kind), e.name, world::effectTargetName(e.owner.kind), e.owner.name,
            world::activationName(e.activation), e.enabled});
    }
    return out;
}

CapabilityRegistry CapabilityRegistry::withCharacters(std::vector<CharacterCard> characters) {
    CapabilityRegistry out;
    out.characters_ = std::move(characters);
    return out;
}

const EffectTypeCapability* EffectCatalog::type(std::string_view key) const {
    const auto it = std::find_if(types.begin(), types.end(), [&](const EffectTypeCapability& t) { return t.type == key; });
    return it == types.end() ? nullptr : &*it;
}

nlohmann::json EffectCatalog::toJson() const {
    nlohmann::json typesJson = nlohmann::json::array();
    for (const EffectTypeCapability& t : types) {
        typesJson.push_back({{"type", t.type}, {"displayName", t.displayName}, {"category", t.category},
                             {"stage", t.stage}, {"owners", t.owners}, {"fields", t.fields}});
    }
    nlohmann::json instancesJson = nlohmann::json::array();
    for (const EffectInstanceCapability& i : instances) {
        instancesJson.push_back({{"id", i.id}, {"type", i.type}, {"name", i.name}, {"ownerKind", i.ownerKind},
                                 {"owner", i.owner}, {"activation", i.activation}, {"enabled", i.enabled}});
    }
    return {{"types", std::move(typesJson)}, {"instances", std::move(instancesJson)}};
}

const CharacterCard* CapabilityRegistry::character(std::string_view subject) const {
    const auto it = std::find_if(characters_.begin(), characters_.end(),
                                 [&](const CharacterCard& c) { return c.subject == subject; });
    return it == characters_.end() ? nullptr : &*it;
}

nlohmann::json CapabilityRegistry::toJson() const {
    nlohmann::json chars = nlohmann::json::array();
    for (const CharacterCard& c : characters_) {
        chars.push_back(c.toJson());
    }
    return {{"characters", std::move(chars)}, {"cameras", cameras_.toJson()}, {"events", events_.toJson()},
            {"effects", effects_.toJson()}};
}

} // namespace avgen::directing
