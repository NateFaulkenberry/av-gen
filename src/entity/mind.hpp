#pragma once

// The awareness layer (Phase D §10, §11, §21, §22, §25, §26, §29): what sits between the sense
// stage and the decider, and what the engine did not have.
//
// Phase D §78 draws the stack as
//
//     WORLD STATE + WORLD EVENTS -> PERCEPTION -> ATTENTION -> MEMORY / NOVELTY
//         -> GOALS / BEHAVIOURS -> CHARACTER INTENT -> NAVIGATION -> STEERING -> MOTION REQUEST
//
// and the audit (docs/design/autonomous-character-architecture.md) found every box built except the
// three in the middle. `GridPerception` answers "what does this body know"; `decision.hpp` answers
// "what will it do about it". Nothing answered **"what does it care about right now"** (attention),
// **"has it already seen that"** (memory, novelty), **"what kind of creature is it"** (personality)
// or **"what just happened"** (world events). This file is those four, and nothing else.
//
// **What it is not.**
//
//   * Not a second perception. It reads `Percept`s the sense stage already built (character_ai.hpp
//     §2 P4: nothing downstream reads the world's interest list directly).
//   * Not head look. Phase B's `chooseAttention` (entity/attention.hpp) owns *which target the body
//     is physically attending to* and its hysteresis; this computes the salience Phase B §22 says it
//     must be handed and "has no rule for producing". The split is D §11's: attention decides what
//     matters, a later procedural layer turns the head.
//   * Not a decider. A considerer reads what this produces through `DecisionContext`; nothing here
//     chooses an option or pushes an action (R3).
//
// **Determinism (character_ai.hpp §1).** Everything here is per-character state held by the
// deciding behaviour, bounded in size, cleared by `reset`, and reconstructed by `EntityWorld::seek`'s
// replay rather than persisted (D4). Time is the timeline second handed in (D1). Nothing draws from
// a stream (D2). Where two candidates tie, the lower `SubjectId` wins, so no answer depends on the
// order a list happened to be built in.
//
// **Allocation (§46).** Every container is sized once at `setSettings` and reused; the hot path is
// linear scans over at most a few dozen entries. No `std::string` is built per frame: tags and event
// names are interned to integers once, at load.

#include "core/error.hpp"
#include "entity/attention.hpp"
#include "entity/character_ai.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::entity {

// `ScoreFactor` -- one named contribution to a score -- is declared in `character_ai.hpp` beside
// `Option`, which carries them, so the normative header stays includable by this one.

// ---- identity ----------------------------------------------------------------------------------

// What a memory, an attention target or an option is *about*: one number, stable across frames.
//
// `Percept::source` is an index into one of two lists and the kind says which, so the pair is the
// identity. Packed rather than a struct because it has to be a `chooseAttention` id (which is a
// `uint64_t` for exactly this reason) and a hash key, and because 0 must mean "nothing".
using SubjectId = std::uint64_t;
inline constexpr SubjectId kNoSubject = 0;

[[nodiscard]] SubjectId bodySubject(std::size_t entityIndex);
[[nodiscard]] SubjectId pointSubject(std::size_t interestIndex);
[[nodiscard]] SubjectId eventSubject(std::uint64_t sequence);
[[nodiscard]] SubjectId subjectOf(const Percept& p);
// Which of the three a subject is, and its index in that list.
enum class SubjectKind : std::uint8_t { None, Body, Point, Event };
[[nodiscard]] SubjectKind subjectKind(SubjectId id);
[[nodiscard]] std::uint64_t subjectIndex(SubjectId id);

// ---- semantic tags (§25) -----------------------------------------------------------------------

// Words the world uses to describe itself -- "glowing", "mushroom", "ufo", "rock" -- interned once
// to a bit, so a percept can carry what the thing *is* as one integer and a filter is one AND.
//
// **Where the words come from** is ordinary data, never a branch on a scene name (§66): an
// entity's `tags` (which fields have filtered on since ADR-097), and each interest point's kind
// name ("glow", "water", "vista", "landmark", "character") so a considerer has one filtering
// mechanism rather than two.
//
// **Bounded at 64**, which is the width of the mask. A world that names more is told so, once, and
// the extra words carry no bit -- a filter naming one matches nothing, which is the honest answer.
class SemanticTags {
public:
    static constexpr std::size_t kCapacity = 64;

    // The bit for `name`, adding it if there is room. 0 when the table is full.
    std::uint64_t intern(std::string_view name);
    // The bit for `name`, or 0 when the world has never heard the word. Never allocates.
    [[nodiscard]] std::uint64_t bit(std::string_view name) const;
    [[nodiscard]] std::uint64_t mask(std::span<const std::string> names) const;
    // The first name set in `mask`, for a diagnostic. Empty when none is.
    [[nodiscard]] std::string_view first(std::uint64_t mask) const;
    [[nodiscard]] std::string describe(std::uint64_t mask) const;
    [[nodiscard]] std::size_t size() const { return names_.size(); }
    [[nodiscard]] bool overflowed() const { return overflowed_; }
    void clear() {
        names_.clear();
        overflowed_ = false;
    }

private:
    std::vector<std::string> names_;
    bool overflowed_ = false;
};

// ---- world events (§26, §27) -------------------------------------------------------------------

// Something that *happened*, as opposed to something that *is*. A percept is a fact about a thing
// that persists; an event is a fact about an instant, and a character that was not in earshot when
// it happened does not learn of it later.
//
// Events are world state, not alien state (§26: "events should not be tightly coupled to alien
// behaviour"): anything that can raise an `ActionEvent` with a name raises one of these, and any
// character may perceive it.
struct WorldEvent {
    // Monotonic within one run of the world, reset by `EntityWorld::reset`, and therefore the same
    // number on a replay as on a play. What a character remembers "I have heard this one" by.
    std::uint64_t sequence = 0;
    std::uint32_t type = 0;       // interned; `EntityWorld::eventName`
    glm::vec3 position{0.0f};     // where it happened (R1: the source's simulation position)
    float radius = 60.0f;         // metres it carries. A character further away does not perceive it.
    float magnitude = 1.0f;       // 0..1 how loud / bright / startling it was
    double time = 0.0;            // timeline seconds (D1)
    std::size_t source = static_cast<std::size_t>(-1); // the entity that raised it, when one did
};

// One event as one character perceived it: attenuated by distance and by how much this character
// cares about events at all (`Personality::eventSensitivity`).
struct PerceivedEvent {
    std::uint64_t sequence = 0;
    std::uint32_t type = 0;
    glm::vec3 position{0.0f};
    float intensity = 0.0f;       // 0..1 after attenuation and sensitivity
    double time = 0.0;            // when it happened, not when it was heard
    std::size_t source = static_cast<std::size_t>(-1);
};

// ---- personality (§29) -------------------------------------------------------------------------

// A small parameter layer, and deliberately not a model. Nine numbers in [0, 1], 0.5 neutral.
//
// **Neutral is 0.5, and 0.5 changes nothing.** Every place a trait enters a score it enters as
// `(0.5 + trait)^w` -- exactly 1 at 0.5 -- so a character with no personality block, and every
// scene written before this existed, scores identically to before. That is what lets the layer be
// added underneath the shipping cast without a single option moving.
struct Personality {
    float curiosity = 0.5f;
    float sociability = 0.5f;
    float caution = 0.5f;
    float aggression = 0.5f;
    float eventSensitivity = 0.5f;
    float attentionSpan = 0.5f;
    float wanderFrequency = 0.5f;
    float movementSpeed = 0.5f;
    float preferredDistance = 0.5f;

    static constexpr std::size_t kCount = 9;
    [[nodiscard]] float get(std::size_t i) const;
    void set(std::size_t i, float value);
    friend bool operator==(const Personality&, const Personality&) = default;
};
[[nodiscard]] std::span<const std::string_view> personalityTraitNames();
[[nodiscard]] std::optional<std::size_t> personalityTrait(std::string_view name);
[[nodiscard]] Result<Personality> personalityFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json personalityToJson(const Personality& p);

// How strongly each trait bends a score, as an author writes it on a considerer:
// `"traits": {"curiosity": 1.0, "caution": -0.5}`. A negative exponent is a trait that *reduces*
// the appetite, which is what caution does to curiosity.
struct TraitWeights {
    std::array<float, Personality::kCount> exponent{};
    bool any = false;
};
[[nodiscard]] TraitWeights traitWeightsFromJson(const nlohmann::json* settings, const char* key);
// `prod (0.5 + trait)^exponent`. Exactly 1 for a neutral personality or no weights.
[[nodiscard]] float traitFactor(const Personality& p, const TraitWeights& w);
// The single-trait form, for a considerer that bends two of its options by different traits.
[[nodiscard]] float traitFactor(float trait, float exponent);

// ---- memory and novelty (§21, §22) -------------------------------------------------------------

struct MemorySettings {
    std::uint16_t capacity = 24;      // things remembered; the least recently seen is forgotten
    // Seconds after an investigation for the thing to be fully novel again. The number that stops
    // "walk to mushroom, leave, walk back, repeat forever" (§21), and the one that lets it come
    // back eventually -- an alien that never looked at a mushroom twice would be as wrong as one
    // that could not stop.
    float recoverSeconds = 90.0f;
    // Seconds of attention after which a thing is worth 1/e of what it was, at a neutral
    // `attentionSpan`. **This is "loses interest"** (Success Demonstration step 11): a character
    // observing a mushroom is attending to it, attention accumulates, novelty falls, and the
    // option to keep observing it loses to something else on its own arithmetic.
    float habituationSeconds = 6.0f;
    // Seconds a target that could not be reached is left alone for. §59: "unbounded path retries"
    // is on the never-allow list; this is the bound.
    float failSeconds = 30.0f;
    std::uint16_t eventCapacity = 8;  // events remembered
    float eventSeconds = 10.0f;       // and for how long

    friend bool operator==(const MemorySettings&, const MemorySettings&) = default;
};

struct MemoryEntry {
    SubjectId id = kNoSubject;
    glm::vec3 position{0.0f};    // last known
    double firstNoticed = 0.0;
    double lastSeen = 0.0;
    // Seconds of attention, decaying back toward zero with time constant `recoverSeconds` while
    // the thing is not being attended to -- so a mushroom stared at until boring is interesting
    // again an hour later, without anything having to remember to reset it.
    float attended = 0.0f;
    double attendedAt = 0.0;
    double investigatedAt = -std::numeric_limits<double>::infinity();
    std::uint16_t investigations = 0;
    double failedAt = -std::numeric_limits<double>::infinity();
    std::uint16_t failures = 0;
};

// Lightweight, deterministic, bounded short-term memory. Not LLM memory (§21).
class ObjectMemory {
public:
    void setSettings(const MemorySettings& settings);
    [[nodiscard]] const MemorySettings& settings() const { return settings_; }

    // Record that the thing was perceived at `time`. Creates the entry if there is room, or
    // replaces the least recently seen one (ties: the lower id, so eviction is order-free).
    void noticed(SubjectId id, const glm::vec3& position, double time);
    // Seconds of attention paid to it this step, at `time`.
    void attend(SubjectId id, float seconds, double time);
    // `attended` as of `time`, with the decay applied.
    [[nodiscard]] float attendedAt(const MemoryEntry& e, double time) const;
    void investigated(SubjectId id, double time);
    void failed(SubjectId id, double time);

    // 0..1: how new this thing still is to this character. 1 for anything never met. `spanScale`
    // multiplies `habituationSeconds` (the personality's attention span).
    [[nodiscard]] float novelty(SubjectId id, double time, float spanScale = 1.0f) const;
    // True while a failed target is being left alone.
    [[nodiscard]] bool suppressed(SubjectId id, double time) const;
    [[nodiscard]] const MemoryEntry* find(SubjectId id) const;
    [[nodiscard]] std::span<const MemoryEntry> entries() const { return entries_; }

    // Events. `hear` is idempotent on `sequence`, so a replay that presents an event twice
    // remembers it once.
    void hear(const PerceivedEvent& event);
    [[nodiscard]] std::span<const PerceivedEvent> events() const { return events_; }
    void forgetEventsBefore(double time);
    [[nodiscard]] std::uint64_t lastEventSequence() const { return lastEvent_; }

    void reset();

private:
    MemoryEntry* findMutable(SubjectId id);
    MemorySettings settings_{};
    std::vector<MemoryEntry> entries_;
    std::vector<PerceivedEvent> events_;
    std::uint64_t lastEvent_ = 0;
};

// ---- attention (§10) ---------------------------------------------------------------------------

// The factors §10 lists, as contributions to one additive score, so "why is it looking at that"
// has an answer in the same shape "why is it doing that" does (§41): a list of named terms.
struct AttentionWeights {
    float salience = 1.0f;   // the sense stage's own ranking: taste times nearness
    float novelty = 0.8f;    // `ObjectMemory::novelty`
    float semantic = 1.0f;   // the strongest authored tag weight the thing carries
    float movement = 0.4f;   // how fast it is moving (bodies only), saturating at 3 m/s
    float sound = 1.2f;      // an event's perceived intensity
    float relevance = 0.6f;  // it is what the current behaviour is about
    friend bool operator==(const AttentionWeights&, const AttentionWeights&) = default;
};

struct AttentionModelSettings {
    AttentionWeights weights{};
    // "glowing": 1.5, "ufo": 3.0. Resolved to bits against the world's `SemanticTags` when used.
    std::vector<std::pair<std::string, float>> tagWeights;
    // Phase B's hysteresis, reused rather than re-invented (§10's "attention persistence" and
    // "attention decay" are its dwell, hold and refractory).
    AttentionSettings hysteresis{};
    // Whether the focus is published as the body's look target when nothing else has named one
    // this step. This is what makes "hears a sound, looks toward it, keeps walking" happen with no
    // behaviour asking for it (§10) -- and it never overrides an action's own look target, because
    // a character told to look at the mushroom looks at the mushroom.
    bool glance = true;
    friend bool operator==(const AttentionModelSettings&, const AttentionModelSettings&) = default;
};
[[nodiscard]] Result<AttentionModelSettings> attentionModelFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json attentionModelToJson(const AttentionModelSettings& s);
[[nodiscard]] Result<MemorySettings> memorySettingsFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json memorySettingsToJson(const MemorySettings& s);

// What a character is attending to, and why (§10's `AttentionTarget`).
struct AttentionFocus {
    SubjectId id = kNoSubject;
    glm::vec3 position{0.0f};
    float score = 0.0f;
    // The largest contributing factor's name, a static string: "novelty", "sound", "semantic"...
    std::string_view reason;
    // 0..1: how sure it is the thing is where it thinks. Freshness of the percept times its
    // visibility (1 when occlusion was not tested, `Percept::tested` says which).
    float confidence = 0.0f;
    double lastSeen = 0.0;
    bool isEvent = false;
    // Phase B's ease-in/out weight, and how long the target has been held.
    float weight = 0.0f;
    float dwell = 0.0f;
    [[nodiscard]] bool valid() const { return id != kNoSubject; }
};

// The attention model. One per character: it carries Phase B's `AttentionState` (the hysteresis
// memory) and nothing else between calls.
class AttentionModel {
public:
    void setSettings(const AttentionModelSettings& settings) { settings_ = settings; }
    [[nodiscard]] const AttentionModelSettings& settings() const { return settings_; }

    struct Inputs {
        std::span<const Percept> percepts;
        std::span<const PerceivedEvent> events;
        const ObjectMemory* memory = nullptr;
        const Personality* personality = nullptr;
        const SemanticTags* tags = nullptr;
        const EntityWorld* world = nullptr; // for a body's measured velocity (movement)
        SubjectId relevant = kNoSubject;    // what the current behaviour is about
        double time = 0.0;
        float eventSeconds = 10.0f;
    };
    // Scores every candidate and runs Phase B's hysteresis over them.
    AttentionFocus update(const Inputs& in);

    [[nodiscard]] const AttentionFocus& focus() const { return focus_; }
    // The winning candidate's factors, for the overlay and the "why" report.
    [[nodiscard]] std::span<const ScoreFactor> factors() const {
        return std::span<const ScoreFactor>(factors_.data(), factorCount_);
    }
    [[nodiscard]] std::size_t candidatesScored() const { return scored_; }
    void reset();

private:
    AttentionModelSettings settings_{};
    AttentionState state_{};
    AttentionFocus focus_{};
    std::vector<AttentionCandidate> candidates_;
    struct Detail {
        SubjectId id = kNoSubject;
        glm::vec3 position{0.0f};
        float confidence = 0.0f;
        double lastSeen = 0.0;
        bool isEvent = false;
        std::array<ScoreFactor, 6> factors{};
    };
    std::vector<Detail> details_;
    std::array<ScoreFactor, 6> factors_{};
    std::size_t factorCount_ = 0;
    std::size_t scored_ = 0;
    std::vector<std::pair<std::uint64_t, float>> resolvedTags_;
    std::size_t resolvedFor_ = static_cast<std::size_t>(-1);
};

// ---- what a considerer sees (DecisionContext::mind) ---------------------------------------------

// The awareness layer as one read-only view, handed to considerers through `DecisionContext::mind`.
// A bundle of pointers rather than copies: it is rebuilt every decision tick and must not allocate.
struct MindView {
    const Personality* personality = nullptr;
    const ObjectMemory* memory = nullptr;
    std::span<const PerceivedEvent> events;
    const AttentionFocus* attention = nullptr;
    const SemanticTags* tags = nullptr;
    // What the running option is about -- so a considerer can recognise its own errand and not
    // discount it for being the thing the body is currently habituating to.
    SubjectId committed = kNoSubject;
    // `habituationSeconds` scale from the personality's attention span.
    float spanScale = 1.0f;
    // The `InterestKind`s of the last few places this body finished an errand at, oldest first.
    std::span<const std::uint8_t> recentKinds;

    [[nodiscard]] float novelty(SubjectId id, double time) const {
        return memory != nullptr ? memory->novelty(id, time, spanScale) : 1.0f;
    }
    [[nodiscard]] bool suppressed(SubjectId id, double time) const {
        return memory != nullptr && memory->suppressed(id, time);
    }
};

// A subject's name, for a trace and an overlay: an entity's name, an interest point's name (or its
// kind and place, "glow@41,-8"), or an event's type and place ("bloom@12,3"). Allocates; call it on
// a decision, never per frame.
[[nodiscard]] std::string describeSubject(const EntityWorld& world, SubjectId id);
// "novelty +0.31, salience +0.18". Allocates, for the same reason and on the same occasions.
[[nodiscard]] std::string formatFactors(std::span<const ScoreFactor> factors);

// The neutral personality, for a view that has none.
[[nodiscard]] const Personality& neutralPersonality();

} // namespace avgen::entity
