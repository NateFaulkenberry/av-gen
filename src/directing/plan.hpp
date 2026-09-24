#pragma once

// The Director Plan: a typed, versioned, engine-level description of cinematic intent (spec §11-§12,
// ADR-755).
//
// It is the one artefact between "what the person asked for" and "what the engine contains". A model,
// a script, a test or a person writes one; the resolver names its subjects and places its times; the
// validator checks it against the scene's capabilities; the compiler turns it into ordinary content
// (`seq::Sequence`, `scene::CameraDirection`, timeline keys, effect instances). The Plan is *not* that
// content and never becomes a second copy of it: it is the request, in engine vocabulary, plus the
// record of what it produced.
//
// ## Identity, and why a plan is kept
//
// Plans are saved in the project as the **provenance** of the content they produced (the owner's
// ruling, 2026-09-24), so "lower the camera and hold the peak longer" can revise the plan that made
// the shot rather than start again. Hence:
//
//   * `id` is stable for the life of the plan and readable ("rook-umbra"): a follow-up names it.
//   * `revision` counts accepted revisions from 1. A revision replaces the plan in the project; the
//     edit history holds the earlier ones (undo gives them back), the project does not.
//   * `produced` lists every piece of native content the last accepted revision created, by domain
//     and native id, with a fingerprint of that content as compiled. A later revision diffs against
//     it, and a fingerprint that no longer matches is content the person edited by hand -- which a
//     revision must surface, never silently overwrite.
//
// ## Items and keys
//
// Every item (shot, marker, performance, cue, retime) has a `key`, unique within the plan. Keys are
// how a revision matches old items to new, how a diff line is attributed, and how `produced` points
// back at intent. They are the plan's, not the engine's.
//
// ## Subjects
//
// Items name subjects by *alias*, declared once in `subjects`. The resolver fills each alias's
// canonical identity (kind + id) or reports it unknown or ambiguous, so "Umbra" is resolved -- or
// refused -- once, not per item.
//
// ## What is deliberately not here (v1)
//
//   * Constraints as a free-form list: every constraint v1 needs is a typed field (clearance on a
//     jump beat, `locked` on a shot). A generic constraint bag with no consumer is speculative.
//   * Global time warp: `retimes` are performance-local (spec §33), and a retime is not a scene clock.
//   * Anything vendor-specific: `provenance.source` is a free string, and nothing reads it.

#include "directing/issue.hpp"
#include "directing/time_ref.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::directing {

inline constexpr int kPlanSchemaVersion = 1;

// ---- vocabulary ---------------------------------------------------------------------------------

// Whether the plan's output renders reproducibly (spec §1.3; ADR-091's tiers).
enum class Tier : std::uint8_t {
    Baked,    // compiled to content that is a pure function of time: renders reproducibly
    Directed, // issued to characters as Director-tier actions: live, not reproducible
    Goal,     // biases autonomous characters: live and only approximate
};

enum class SubjectKind : std::uint8_t { Unresolved, Entity, Hero, Node, Camera, Effect, Parameter, World };

// Spec §15's initial camera vocabulary. Stored as the move, so the intent survives compilation.
enum class CameraMove : std::uint8_t {
    Chase, Follow, RiseOver, Pass, Orbit, Hold, Reveal, PushIn, PullOut, Wide, Close, TopDown, LowAngle,
};

enum class PerformanceMode : std::uint8_t { Scripted, Directed, Goal };

// What kind of native content a `produced` entry points at.
enum class ContentDomain : std::uint8_t {
    SequenceShot, SequenceMarker, SequenceActor, SequenceEvent, SequenceTrack,
    CameraRig, CameraShot, TimelineTrack, EffectInstance,
};

[[nodiscard]] const char* tierName(Tier v);
[[nodiscard]] std::optional<Tier> tierFromName(std::string_view n);
[[nodiscard]] const char* subjectKindName(SubjectKind v);
[[nodiscard]] std::optional<SubjectKind> subjectKindFromName(std::string_view n);
[[nodiscard]] const char* cameraMoveName(CameraMove v);
[[nodiscard]] std::optional<CameraMove> cameraMoveFromName(std::string_view n);
[[nodiscard]] const char* performanceModeName(PerformanceMode v);
[[nodiscard]] std::optional<PerformanceMode> performanceModeFromName(std::string_view n);
[[nodiscard]] const char* contentDomainName(ContentDomain v);
[[nodiscard]] std::optional<ContentDomain> contentDomainFromName(std::string_view n);
// Every name of each vocabulary, for schemas, prompts and error suggestions.
[[nodiscard]] std::vector<std::string> cameraMoveNames();
[[nodiscard]] std::vector<std::string> subjectKindNames();

// ---- the plan -----------------------------------------------------------------------------------

struct Subject {
    std::string alias;                          // how the plan's items name it: "rook", "umbra"
    std::string text;                           // as the request said it: "the Umbra hero mushroom"
    SubjectKind hint = SubjectKind::Unresolved; // the kind the request implies, when it implies one
    SubjectKind kind = SubjectKind::Unresolved; // filled by the resolver
    std::string id;                             // the canonical name, filled by the resolver
    friend bool operator==(const Subject&, const Subject&) = default;
};

struct CameraBeat {
    CameraMove move = CameraMove::Hold;
    std::string subject;             // alias; empty = the shot's subject
    std::optional<TimeRef> at;       // when the move begins; unset = the shot's start
    std::optional<float> heightMetres;
    std::optional<float> distanceMetres;
    std::optional<float> degrees;    // orbit
    std::string side;                // pass: "left" | "right" | ""
    friend bool operator==(const CameraBeat&, const CameraBeat&) = default;
};

// Compiles to a `seq::Shot` (the framing, in the sequence) AND, when it names or implies a camera, a
// `scene::CameraShot` (which camera is live) -- the two shot types of ADR-245, written together so
// they cannot disagree.
struct PlanShot {
    std::string key;
    std::string name;
    TimeRef start;
    double durationSeconds = 0.0;
    std::string subject;             // alias: what the shot is about
    std::vector<CameraBeat> camera;  // in order; "chase -> rise_over -> pass"
    std::string rig;                 // an existing camera by name, when the request named one
    std::string transition = "cut";  // seq::TransitionKind name
    bool locked = false;             // CameraShot::locked: an event camera may not take this moment
    friend bool operator==(const PlanShot&, const PlanShot&) = default;
};

struct PlanMarker {
    std::string key;
    std::string name;                // "rook.jump_peak"
    TimeRef at;
    friend bool operator==(const PlanMarker&, const PlanMarker&) = default;
};

struct PerformanceBeat {
    std::string action;              // semantic: "run_to", "jump", "backflip", "land", "run", "look_at"
    std::string target;              // alias, when the action is toward or over something
    std::optional<TimeRef> at;
    std::optional<double> seconds;   // how long, when the request said
    std::string emits;               // a plan event this beat raises: "rook.backflip_peak"
    std::optional<float> clearanceMetres; // over-the-target beats: how far above it to pass
    friend bool operator==(const PerformanceBeat&, const PerformanceBeat&) = default;
};

struct PlanPerformance {
    std::string key;
    std::string subject;             // alias
    PerformanceMode mode = PerformanceMode::Scripted;
    std::vector<PerformanceBeat> beats;
    friend bool operator==(const PlanPerformance&, const PlanPerformance&) = default;
};

// ADR-702's effect model: an instance is addressed by its scene-unique `id` when it is known, or by
// owner and type ("the Ground Pulse on umbra-cap") when it is not -- the resolver fills `id` from the
// latter once the effect list is available to it.
struct EffectRef {
    std::string id;                  // "umbra-cap-ground-pulse"; may be empty before resolution
    std::string owner;               // alias of the owning subject, or "world"
    std::string type;                // the effect type key, "groundPulse"
    friend bool operator==(const EffectRef&, const EffectRef&) = default;
};

// A change at a time: a parameter keyed, or an effect activated / a field moved.
struct PlanCue {
    std::string key;
    std::string parameter;           // a parameter path (the low-level escape hatch), or empty
    std::optional<EffectRef> effect; // or an effect
    std::string field;               // the effect's parameter leaf; empty = its activation
    std::optional<TimeRef> at;       // when it starts; or...
    std::string on;                  // ...a plan event it starts on ("rook.backflip_peak")
    std::optional<TimeRef> until;    // when it ends; unset with `holdSeconds` 0 = it stays
    double holdSeconds = 0.0;
    std::optional<float> value;
    double rampSeconds = 0.0;
    friend bool operator==(const PlanCue&, const PlanCue&) = default;
};

// Performance-local slow motion (spec §33): the performance's own keys and clip speeds stretched over
// the window. Not a scene clock; the rest of the world keeps normal time.
struct PlanRetime {
    std::string key;
    std::string performance;         // key of the performance it retimes
    TimeRef from;
    TimeRef until;
    double factor = 1.0;             // 0.35 = slower
    friend bool operator==(const PlanRetime&, const PlanRetime&) = default;
};

struct Provenance {
    std::string author;              // "person" | "assistant" | "script"
    std::string source;              // free text: a tool, a script path, a provider id
    std::string request;             // what was asked, verbatim
    friend bool operator==(const Provenance&, const Provenance&) = default;
};

struct ContentRef {
    std::string item;                // the plan key that produced it
    ContentDomain domain = ContentDomain::SequenceShot;
    std::string id;                  // the native id: shot name, marker name, actor id, rig slug...
    std::string fingerprint;         // of the content as compiled; a mismatch later = a hand edit
    friend bool operator==(const ContentRef&, const ContentRef&) = default;
};

struct Plan {
    int schemaVersion = kPlanSchemaVersion;
    std::string id;
    int revision = 1;
    std::string title;
    Provenance provenance;
    Tier tier = Tier::Baked;
    std::vector<Subject> subjects;
    std::vector<PlanShot> shots;
    std::vector<PlanMarker> markers;
    std::vector<PlanPerformance> performances;
    std::vector<PlanCue> cues;
    std::vector<PlanRetime> retimes;
    std::vector<ContentRef> produced;

    [[nodiscard]] const Subject* subject(std::string_view alias) const;
    [[nodiscard]] Subject* subject(std::string_view alias);

    // Canonical: the same plan always serialises to the same bytes (keys sorted, defaults omitted
    // where the reader restores them), so a plan diffs and fingerprints cleanly.
    [[nodiscard]] nlohmann::json toJson() const;
    friend bool operator==(const Plan&, const Plan&) = default;
};

struct PlanParse {
    std::optional<Plan> plan;  // present unless an error made the document unusable
    std::vector<Issue> issues; // every finding, warnings included (unknown fields)
};

// Reads a plan, checking its shape: types, required fields, known vocabulary, unique keys, declared
// aliases, positive durations. Semantics (does the subject exist, can it do that) are the resolver's
// and the validator's. A document from a newer schema is refused with SCHEMA_VERSION_UNSUPPORTED.
[[nodiscard]] PlanParse parsePlan(const nlohmann::json& document);

// The Director Plan contract, as a document a model or a person can read: every field, its type and
// meaning, and every vocabulary (camera moves, tiers, modes, subject kinds, content domains) taken from
// the same name tables the parser reads -- so the contract cannot drift from what is accepted.
[[nodiscard]] nlohmann::json planSchema();

// A readable id from a title, unique against `taken`: "Rook / Umbra!" -> "rook-umbra", then
// "rook-umbra-2". Never contains '/', so it can sit in a path.
[[nodiscard]] std::string mintPlanId(std::string_view title, const std::vector<std::string>& taken);

} // namespace avgen::directing
