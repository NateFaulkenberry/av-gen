#pragma once

// What the scene can do, generated from the engine's own data (ADR-754).
//
// The Director must never plan a capability that does not exist: "have Rook backflip over Umbra"
// has to come back as CAPABILITY_UNAVAILABLE naming what Rook *can* do, before anything is compiled.
// That needs a description of the scene's capabilities, and the one thing this codebase has already
// learned about such descriptions is that a hand-written one goes stale (`capability.list` marked
// domains unavailable while listing tools in them; ADR-617, "documentation that cannot rot").
//
// So nothing here is a list somebody maintains. Every entry is read from the structure that
// actually decides the behaviour:
//
//   characters   `entity::EntityDesc` (activity -> clip map, gait speeds, tags, capabilities, the
//                `explore` behaviour's jump settings) joined to the node's LOADED rig (which clips
//                exist, how long they are, whether their state loops)
//   cameras      the camera collection's rigs, and `seq::`/`scene::` camera enums, enumerated from
//                their own name tables
//   events       `seq::TriggerKind` and `seq::EventActionKind`, with the tier each belongs to taken
//                from `triggerIsScheduled` / `actionIsBaked` -- the predicates the engine itself uses
//
//   effects      `world::effectSchemas()` (types, the owners each may attach to, their fields) and the
//                composition's effect list (ADR-702's instances, by id and owner)
//
// Semantic activity names ("run", "jump", "land") are the interface; clip names are carried as
// information and never as the thing a plan asks for (spec §10).

#include <nlohmann/json.hpp>

#include "scene/clip_semantics.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {
class Composition;
}

namespace avgen::directing {

// How an activity relates to the ground and to time, from the engine's `entity::Activity`
// (ADR-194's airborne three included). `Custom` is an activity name the entity maps a clip to that
// the engine's enum does not know.
enum class ActivityKind : std::uint8_t { Ground, Action, Airborne, Custom };
[[nodiscard]] const char* activityKindName(ActivityKind kind);

struct ActivityCapability {
    std::string activity;     // the semantic name: "run", "jump"
    ActivityKind kind = ActivityKind::Custom;
    std::string clip;         // the asset clip it maps to, for information
    bool available = false;   // the clip exists on the character's loaded rig
    float seconds = 0.0f;     // the clip's playable length, 0 when unavailable
    // Whether the clip is a loop, as MEASURED (ADR-821: its end joins its start), not as the rig's
    // default state happens to play it -- both of the scout's jump clips measure as loops, which is
    // why a compiled stunt says `playback: once` rather than trusting this. Falls back to the rig
    // state's flag when the rig has no semantics.
    bool loops = true;
    // What the clip is, measured (ADR-821): flight, peak, touchdown, foot plants. Absent when the
    // clip is unavailable or the rig has no skeleton to measure. A jump's clip cue is fitted to its
    // arc from these (`seq::jumpClipCue`).
    std::optional<scene::ClipSemantics> semantics;
};

// The body's jump envelope, for a planned (baked) jump. A scripted performance computes its own arc
// with `Airborne`'s arithmetic from these numbers, so a character with no autonomous jump can still
// be given one -- but only up to what the numbers allow, which is what the validator checks.
struct JumpEnvelope {
    float apex = 0.0f;        // metres above take-off: the hop it does unprompted
    float maxApex = 0.0f;     // the highest it CAN leap (ADR-822 `apexLimit()`): what a plan may ask
    float gravity = 0.0f;     // m/s^2
    float maxDistance = 0.0f; // metres
    float landSeconds = 0.0f; // the recovery after touchdown
    // "jump" when the entity authors a `jump` block (ADR-822), "default" when it authors none and
    // `entity::JumpSettings` defaults apply. Said out loud because the defaults are not a property
    // anybody chose for this character.
    std::string source;
};

struct CharacterCard {
    std::string subject; // the entity's name, which is how everything else addresses it
    std::string node;    // the composition node it drives
    std::vector<std::string> tags;
    std::vector<ActivityCapability> activities;
    float walkSpeed = 0.0f; // m/s the walk clip was authored at
    float runSpeed = 0.0f;
    JumpEnvelope jump;
    // Phase D affordances ("can_inspect"), as the entity declares them.
    std::vector<std::string> affordances;
    // Clips the rig has that no activity maps to. Not a plan's vocabulary -- they have no semantic
    // name -- but the honest answer to "could this character do X if somebody mapped it".
    std::vector<std::string> unmappedClips;
    // True when the node has a loaded rig. False means the card came from the description alone and
    // no clip is `available`, which the validator must treat as "cannot perform", not "unknown".
    bool rigLoaded = false;
    // ADR-828 / ADR-763: the character's decider has a `goal` considerer, the slot a goal-mode
    // performance fills. Without one a `CharacterGoal` event would be given to nothing.
    bool goalSlot = false;

    // The activity by semantic name, or null.
    [[nodiscard]] const ActivityCapability* activity(std::string_view name) const;
    // Mapped AND present on the rig.
    [[nodiscard]] bool can(std::string_view activityName) const;
    // Every available activity of one kind, by name, in card order ("jump", "fall", "land").
    [[nodiscard]] std::vector<std::string> available(ActivityKind kind) const;
    [[nodiscard]] nlohmann::json toJson() const;
};

struct CameraRigCapability {
    std::string name;
    std::string slug;
    std::string placement;
    std::string aimNode;
    std::string followNode;
};

struct CameraCatalog {
    std::vector<CameraRigCapability> rigs;   // the scene's cameras, main camera first
    std::vector<std::string> presets;        // `seq::CameraPreset`
    std::vector<std::string> behaviors;      // `seq::CameraBehaviorKind`
    std::vector<std::string> shotCameraKinds;// `seq::CameraKind`
    std::vector<std::string> sequenceTransitions; // `seq::TransitionKind`
    std::vector<std::string> cutTransitions;      // `scene::ShotTransition`
    [[nodiscard]] nlohmann::json toJson() const;
};

struct EventKindCapability {
    std::string name;
    // Trigger: known before the piece runs (`triggerIsScheduled`). Action: bakes to keys
    // (`actionIsBaked`). A rendered result may depend only on scheduled triggers and baked actions
    // unless something replays the rest (ADR-091, spec §1.3).
    bool deterministic = false;
};

struct EventCatalog {
    std::vector<EventKindCapability> triggers;
    std::vector<EventKindCapability> actions;
    [[nodiscard]] nlohmann::json toJson() const;
};

// The effect vocabulary (ADR-702): every type the registry declares, and every instance the scene
// has. Read from `world::effectSchemas()` and the composition's one effect list -- never listed.
struct EffectTypeCapability {
    std::string type;                 // the serialised key: "groundPulse"
    std::string displayName;          // "Ground Pulse"
    std::string category;
    std::string stage;                // where in the frame it is drawn
    std::vector<std::string> owners;  // the owner kinds it may attach to: "world", "entity", ...
    std::vector<std::string> fields;  // parameter leaves under fx/<id>/ ("intensity", ...)
};
struct EffectInstanceCapability {
    std::string id;                   // "umbra-cap-hero-pulse": the fx/<id>/ prefix
    std::string type;
    std::string name;
    std::string ownerKind;            // "world" | "entity" | "camera" | "light"
    std::string owner;                // empty for the world
    std::string activation;           // "always" | "window" | "cameraTravel" | "heroFocus"
    bool enabled = true;
};
struct EffectCatalog {
    std::vector<EffectTypeCapability> types;
    std::vector<EffectInstanceCapability> instances;
    [[nodiscard]] const EffectTypeCapability* type(std::string_view key) const;
    [[nodiscard]] nlohmann::json toJson() const;
};

class CapabilityRegistry {
public:
    // Reads everything from the composition as it is now (its entities, its loaded rigs, its
    // cameras) and the engine's enums. Pure: the same composition gives the same registry.
    [[nodiscard]] static CapabilityRegistry fromComposition(const scene::Composition& composition);
    // A registry holding exactly these cards (and the engine's camera and event vocabularies): for a
    // host that describes characters without a composition, and for tests that need a character the
    // asset set does not have.
    [[nodiscard]] static CapabilityRegistry withCharacters(std::vector<CharacterCard> characters);

    [[nodiscard]] const std::vector<CharacterCard>& characters() const { return characters_; }
    [[nodiscard]] const CharacterCard* character(std::string_view subject) const;
    [[nodiscard]] const CameraCatalog& cameras() const { return cameras_; }
    [[nodiscard]] const EventCatalog& events() const { return events_; }
    [[nodiscard]] const EffectCatalog& effects() const { return effects_; }
    [[nodiscard]] nlohmann::json toJson() const;

private:
    std::vector<CharacterCard> characters_;
    CameraCatalog cameras_;
    EventCatalog events_;
    EffectCatalog effects_;
};

} // namespace avgen::directing
