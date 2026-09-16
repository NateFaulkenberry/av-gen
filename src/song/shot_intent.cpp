#include "song/shot_intent.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace avgen::song {
namespace {
using nlohmann::json;

constexpr std::size_t kMaxIdLength = 64;

// How far an arc travels. A rising treatment starts at this fraction of its stated values and
// arrives at them; a falling one does the reverse. Not zero, because a build whose camera is
// completely still at its first frame reads as a mistake rather than as restraint.
constexpr float kArcFloor = 0.35f;
// A burst lands at full value and settles to this. A drop that stayed at full intensity for its
// whole section would have nothing left to be a drop against.
constexpr float kBurstSustain = 0.5f;
constexpr float kBurstDecay = 0.22f; // in units of section progress
// What `Suspended` pins movement and cutting down to, whatever the dials say. A suspended treatment
// that travelled would not be suspended.
constexpr float kSuspendedCeiling = 0.1f;

constexpr std::array kSubjectFocuses{SubjectFocus::Hero, SubjectFocus::Ensemble,
                                     SubjectFocus::Environment, SubjectFocus::Mixed};
constexpr std::array kSubjectFocusNames{"hero", "ensemble", "environment", "mixed"};
static_assert(kSubjectFocuses.size() == kSubjectFocusNames.size());

constexpr std::array kFramings{Framing::ExtremeClose, Framing::Close, Framing::Medium, Framing::Wide,
                               Framing::VeryWide};
constexpr std::array kFramingNames{"extreme-close", "close", "medium", "wide", "very-wide"};
static_assert(kFramings.size() == kFramingNames.size());

constexpr std::array kArcs{Arc::Steady, Arc::Rising, Arc::Falling, Arc::Suspended, Arc::Burst};
constexpr std::array kArcNames{"steady", "rising", "falling", "suspended", "burst"};
static_assert(kArcs.size() == kArcNames.size());

[[nodiscard]] float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// Tolerant readers, in the shape `seq/song_structure.cpp` established: a missing key is the default
// and a key of the wrong type is *also* the default rather than an exception. Exceptions are for
// programming errors here (`core/error.hpp`), and a hand-edited project file is not one.
[[nodiscard]] float readFloat(const json& j, const char* key, float fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<float>() : fallback;
}

[[nodiscard]] int readInt(const json& j, const char* key, int fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

[[nodiscard]] std::string readString(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

[[nodiscard]] bool inUnitRange(float v) { return v >= 0.0f && v <= 1.0f; }

// A dial's value once `arc` has carried it `p` of the way through the section.
[[nodiscard]] float arcScale(Arc arc, float p) {
    switch (arc) {
    case Arc::Steady:
    case Arc::Suspended:
        return 1.0f;
    case Arc::Rising:
        return kArcFloor + (1.0f - kArcFloor) * p;
    case Arc::Falling:
        return 1.0f - (1.0f - kArcFloor) * p;
    case Arc::Burst:
        return kBurstSustain + (1.0f - kBurstSustain) * std::exp(-p / kBurstDecay);
    }
    return 1.0f;
}

// ---- the built-in library -----------------------------------------------------------------------
//
// One row per treatment. The brief's table of defaults, plus the handful its type list implies but
// its table does not name. Read the columns as: focus, focusStrength, framing tightest..widest,
// movement, energy, variation, cutFrequency, visualDensity, cameras fewest..most, arc.
//
// `most = 0` means "as many cameras as there are". Every number is a preference and every one of them
// is defensible rather than correct; they are the opinionated starting points the brief asked for.
[[nodiscard]] ShotIntent make(const char* id, const char* name, const char* description,
                              SubjectFocus focus, float focusStrength, Framing tight, Framing wide,
                              float movement, float energy, float variation, float cutFrequency,
                              float visualDensity, int fewest, int most, Arc arc) {
    ShotIntent i;
    i.id = id;
    i.name = name;
    i.description = description;
    i.focus = focus;
    i.focusStrength = focusStrength;
    i.framing = FramingRange{tight, wide};
    i.movement = movement;
    i.energy = energy;
    i.variation = variation;
    i.cutFrequency = cutFrequency;
    i.visualDensity = visualDensity;
    i.cameras = CameraCount{fewest, most};
    i.arc = arc;
    i.builtIn = true;
    return i;
}

const std::vector<ShotIntent>& intentLibrary() {
    static const std::vector<ShotIntent> kLibrary = [] {
        using F = Framing;
        const auto Hero = SubjectFocus::Hero;
        const auto Ens = SubjectFocus::Ensemble;
        const auto Env = SubjectFocus::Environment;
        const auto Mix = SubjectFocus::Mixed;
        std::vector<ShotIntent> v;
        v.reserve(28);
        // --- the brief's table, in its order ---------------------------------------------------
        v.push_back(make("atmospheric_establishing", "Atmospheric Establishing",
                         "Hold wide and let the place read before anything happens in it.",
                         Env, 0.30f, F::Wide, F::VeryWide, 0.25f, 0.25f, 0.30f, 0.15f, 0.40f, 1, 2,
                         Arc::Steady));
        v.push_back(make("hero_coverage", "Hero / Character Coverage",
                         "Ordinary, legible coverage of the subject, with room around them.",
                         Hero, 0.70f, F::Medium, F::Wide, 0.35f, 0.45f, 0.40f, 0.30f, 0.50f, 1, 2,
                         Arc::Steady));
        v.push_back(make("building_tension", "Building Tension",
                         "Close in and speed up across the passage; arrive somewhere tighter.",
                         Hero, 0.60f, F::Close, F::Medium, 0.55f, 0.60f, 0.50f, 0.50f, 0.55f, 1, 3,
                         Arc::Rising));
        v.push_back(make("dynamic_hero_coverage", "Dynamic Hero Coverage",
                         "The payoff: several angles on the subject, moving, cut hard.",
                         Hero, 0.80f, F::Close, F::Wide, 0.75f, 0.90f, 0.80f, 0.80f, 0.70f, 2, 0,
                         Arc::Steady));
        v.push_back(make("dynamic_alternate_coverage", "Dynamic Alternate Coverage",
                         "The same energy from somewhere else -- deliberately not the last angle.",
                         Ens, 0.50f, F::Medium, F::Wide, 0.70f, 0.80f, 0.90f, 0.75f, 0.70f, 2, 0,
                         Arc::Steady));
        v.push_back(make("visual_departure", "Visual Departure",
                         "Break the film's own habits: a look it has not used yet.",
                         Env, 0.35f, F::Close, F::VeryWide, 0.60f, 0.50f, 0.95f, 0.45f, 0.60f, 1, 3,
                         Arc::Steady));
        v.push_back(make("slow_pullback", "Slow Pullback / Resolution",
                         "Widen and slow until the piece has somewhere to stop.",
                         Env, 0.30f, F::Wide, F::VeryWide, 0.35f, 0.20f, 0.25f, 0.10f, 0.45f, 1, 2,
                         Arc::Falling));
        v.push_back(make("increasing_movement", "Increasing Movement",
                         "Start settled and end travelling; the movement is the build.",
                         Mix, 0.50f, F::Medium, F::Wide, 0.60f, 0.65f, 0.55f, 0.55f, 0.55f, 1, 3,
                         Arc::Rising));
        v.push_back(make("rising_reveal", "Rising / Reveal",
                         "Climb, opening out, so the boundary has something to land on.",
                         Mix, 0.45f, F::Medium, F::VeryWide, 0.70f, 0.70f, 0.50f, 0.40f, 0.60f, 1, 2,
                         Arc::Rising));
        v.push_back(make("dramatic_reveal", "Dramatic Reveal / Impact",
                         "Land hard on the first frame and let it settle.",
                         Hero, 0.75f, F::Close, F::VeryWide, 0.85f, 1.00f, 0.70f, 0.70f, 0.80f, 2, 0,
                         Arc::Burst));
        v.push_back(make("intimate_restrained", "Intimate / Restrained",
                         "Close, still, and held: the shot a loud passage could not hold.",
                         Hero, 0.80f, F::ExtremeClose, F::Medium, 0.18f, 0.20f, 0.25f, 0.15f, 0.30f,
                         1, 1, Arc::Steady));
        v.push_back(make("slow_environmental_exploration", "Slow Environmental Exploration",
                         "Drift through the world slowly, with nothing in particular to find.",
                         Env, 0.25f, F::Medium, F::VeryWide, 0.30f, 0.20f, 0.40f, 0.12f, 0.45f, 1, 2,
                         Arc::Steady));
        v.push_back(make("environmental_performance_exploration",
                         "Environmental / Performance Exploration",
                         "The world and whoever is in it, roughly equally, without hurry.",
                         Mix, 0.45f, F::Medium, F::Wide, 0.50f, 0.50f, 0.65f, 0.40f, 0.55f, 1, 3,
                         Arc::Steady));
        v.push_back(make("hero_performance", "Hero Performance",
                         "Stay on the subject and let the performance carry the passage.",
                         Hero, 0.90f, F::Close, F::Medium, 0.45f, 0.70f, 0.45f, 0.40f, 0.45f, 1, 2,
                         Arc::Steady));
        v.push_back(make("intimate_close_up", "Intimate Close-Up",
                         "One face, one frame, almost no movement.",
                         Hero, 0.95f, F::ExtremeClose, F::Close, 0.15f, 0.35f, 0.20f, 0.12f, 0.25f,
                         1, 1, Arc::Steady));
        v.push_back(make("large_scale_dynamic_coverage", "Large-Scale Dynamic Coverage",
                         "Everything at once, from everywhere available: the widest the film gets.",
                         Ens, 0.50f, F::Medium, F::VeryWide, 0.85f, 1.00f, 0.90f, 0.85f, 0.90f, 3, 0,
                         Arc::Steady));
        v.push_back(make("immediate_dramatic_framing", "Immediate Dramatic Framing",
                         "A hard, close, unmistakable frame on the instant it arrives.",
                         Hero, 0.85f, F::ExtremeClose, F::Medium, 0.50f, 1.00f, 0.50f, 0.90f, 0.60f,
                         1, 3, Arc::Burst));
        v.push_back(make("suspended_locked_off", "Suspended / Locked-Off",
                         "Stop. One frame, no movement, no cut, for as long as it lasts.",
                         Mix, 0.50f, F::Medium, F::Wide, 0.00f, 0.10f, 0.05f, 0.00f, 0.40f, 1, 1,
                         Arc::Suspended));
        v.push_back(make("rapid_multi_shot", "Rapid Multi-Shot Coverage",
                         "As many different frames as the passage will take.",
                         Mix, 0.35f, F::Close, F::VeryWide, 0.60f, 0.80f, 1.00f, 1.00f, 0.70f, 2, 0,
                         Arc::Steady));
        v.push_back(make("floating_unconventional", "Floating / Unconventional Exploration",
                         "Move in ways the rest of the film does not: unanchored, slightly wrong.",
                         Env, 0.20f, F::Close, F::VeryWide, 0.55f, 0.35f, 0.85f, 0.30f, 0.50f, 1, 3,
                         Arc::Steady));
        v.push_back(make("free_roaming_environment", "Free-Roaming Environment",
                         "Go where the world is interesting and do not report back.",
                         Env, 0.15f, F::Medium, F::VeryWide, 0.60f, 0.45f, 0.70f, 0.35f, 0.50f, 1, 2,
                         Arc::Steady));
        v.push_back(make("dynamic_multi_subject", "Dynamic Multi-Subject Coverage",
                         "Several subjects, moving, none of them owning the passage.",
                         Ens, 0.45f, F::Close, F::Wide, 0.70f, 0.85f, 0.85f, 0.75f, 0.80f, 2, 0,
                         Arc::Steady));
        // --- the ones the type list needs and the table does not name ---------------------------
        v.push_back(make("steady_coverage", "Steady Coverage",
                         "Legible, unremarkable, wrong for nothing: what an unlabelled passage gets.",
                         Mix, 0.50f, F::Medium, F::Wide, 0.40f, 0.50f, 0.50f, 0.40f, 0.50f, 1, 2,
                         Arc::Steady));
        v.push_back(make("hard_transition", "Hard Transition",
                         "Leave where the film was and arrive somewhere else, visibly.",
                         Mix, 0.40f, F::Close, F::Wide, 0.50f, 0.60f, 0.80f, 0.85f, 0.55f, 1, 3,
                         Arc::Burst));
        v.push_back(make("held_tension", "Held Tension",
                         "Still and close, tightening, with the cut withheld.",
                         Hero, 0.70f, F::Close, F::Medium, 0.25f, 0.40f, 0.30f, 0.20f, 0.45f, 1, 2,
                         Arc::Rising));
        v.push_back(make("calm_stillness", "Calm / Stillness",
                         "Almost nothing moves, and that is the point.",
                         Env, 0.25f, F::Medium, F::Wide, 0.20f, 0.12f, 0.20f, 0.08f, 0.35f, 1, 1,
                         Arc::Steady));
        v.push_back(make("character_introduction", "Character Introduction",
                         "Find the subject and close on them: the film has not met them yet.",
                         Hero, 0.85f, F::Close, F::Medium, 0.40f, 0.50f, 0.35f, 0.30f, 0.40f, 1, 2,
                         Arc::Rising));
        v.push_back(make("groove_coverage", "Groove Coverage",
                         "Cut on the pulse; keep moving; nothing dramatic.",
                         Mix, 0.50f, F::Medium, F::Wide, 0.55f, 0.65f, 0.60f, 0.60f, 0.60f, 2, 0,
                         Arc::Steady));
        v.push_back(make("vocal_focus", "Vocal Focus",
                         "Whoever is speaking or singing, framed so the words carry.",
                         Hero, 0.90f, F::Close, F::Medium, 0.30f, 0.50f, 0.35f, 0.30f, 0.35f, 1, 2,
                         Arc::Steady));
        v.push_back(make("action_coverage", "Action Coverage",
                         "Fast, close, following: the camera keeps up or it loses it.",
                         Ens, 0.60f, F::Close, F::Wide, 0.90f, 0.90f, 0.80f, 0.90f, 0.80f, 2, 0,
                         Arc::Steady));
        v.push_back(make("detached_observation", "Detached Observation",
                         "Wide, still, uninvolved: the film watching rather than participating.",
                         Env, 0.20f, F::Wide, F::VeryWide, 0.25f, 0.20f, 0.30f, 0.20f, 0.40f, 1, 1,
                         Arc::Steady));
        return v;
    }();
    return kLibrary;
}
} // namespace

// ---- ids -----------------------------------------------------------------------------------------

Result<void> validateId(std::string_view id) {
    if (id.empty()) {
        return fail("an id may not be empty");
    }
    if (id.size() > kMaxIdLength) {
        return fail("id '{}' is longer than {} characters", id, kMaxIdLength);
    }
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return fail("id '{}' may only contain lowercase letters, digits and underscore", id);
        }
    }
    if (id.front() == '_' || id.back() == '_') {
        return fail("id '{}' may not begin or end with an underscore", id);
    }
    return {};
}

std::optional<std::string> makeId(std::string_view displayName) {
    std::string out;
    out.reserve(displayName.size());
    bool pendingSeparator = false;
    for (const char c : displayName) {
        char lowered = c;
        if (lowered >= 'A' && lowered <= 'Z') {
            lowered = static_cast<char>(lowered - 'A' + 'a');
        }
        const bool keep = (lowered >= 'a' && lowered <= 'z') || (lowered >= '0' && lowered <= '9');
        if (keep) {
            if (pendingSeparator && !out.empty()) {
                out.push_back('_');
            }
            pendingSeparator = false;
            out.push_back(lowered);
        } else {
            pendingSeparator = true;
        }
        if (out.size() >= kMaxIdLength) {
            break;
        }
    }
    if (out.empty()) {
        return std::nullopt;
    }
    return out;
}

// ---- the small vocabularies ----------------------------------------------------------------------

const char* subjectFocusName(SubjectFocus f) {
    const auto i = static_cast<std::size_t>(f);
    return i < kSubjectFocusNames.size() ? kSubjectFocusNames[i] : "mixed";
}

std::optional<SubjectFocus> subjectFocusFromName(std::string_view name) {
    for (std::size_t i = 0; i < kSubjectFocusNames.size(); ++i) {
        if (name == kSubjectFocusNames[i]) {
            return kSubjectFocuses[i];
        }
    }
    return std::nullopt;
}

std::span<const SubjectFocus> allSubjectFocuses() { return kSubjectFocuses; }

const char* framingName(Framing f) {
    const auto i = static_cast<std::size_t>(f);
    return i < kFramingNames.size() ? kFramingNames[i] : "medium";
}

std::optional<Framing> framingFromName(std::string_view name) {
    for (std::size_t i = 0; i < kFramingNames.size(); ++i) {
        if (name == kFramingNames[i]) {
            return kFramings[i];
        }
    }
    return std::nullopt;
}

std::span<const Framing> allFramings() { return kFramings; }

const char* arcName(Arc a) {
    const auto i = static_cast<std::size_t>(a);
    return i < kArcNames.size() ? kArcNames[i] : "steady";
}

std::optional<Arc> arcFromName(std::string_view name) {
    for (std::size_t i = 0; i < kArcNames.size(); ++i) {
        if (name == kArcNames[i]) {
            return kArcs[i];
        }
    }
    return std::nullopt;
}

std::span<const Arc> allArcs() { return kArcs; }

Framing FramingRange::clamp(Framing f) const {
    if (f < tightest) {
        return tightest;
    }
    if (f > widest) {
        return widest;
    }
    return f;
}

Framing FramingRange::middle() const {
    const auto lo = static_cast<int>(tightest);
    const auto hi = static_cast<int>(widest);
    // Rounds wider: a band of Close..Medium reads Medium. A frame that is slightly too wide is a
    // legible shot and one that is slightly too tight is a mistake.
    return static_cast<Framing>((lo + hi + 1) / 2);
}

int CameraCount::resolve(int available) const {
    // As many as the treatment asks for, capped by what there is, and never fewer than one. A
    // treatment wanting three cameras in a scene with one gets one: an intent is a preference, and a
    // director that refused to shoot because the scene was under-equipped would be useless.
    const int room = std::max(available, 1);
    const int want = unbounded() ? room : std::max(most, std::max(fewest, 1));
    return std::clamp(want, 1, room);
}

// ---- the intent -----------------------------------------------------------------------------------

Result<void> ShotIntent::validate() const {
    if (auto ok = validateId(id); !ok) {
        return ok;
    }
    if (name.empty()) {
        return fail("shot intent '{}' has no name", id);
    }
    if (framing.tightest > framing.widest) {
        return fail("shot intent '{}': framing {} is tighter than {}", id,
                    framingName(framing.widest), framingName(framing.tightest));
    }
    if (cameras.fewest < 1) {
        return fail("shot intent '{}': a treatment needs at least one camera, not {}", id,
                    cameras.fewest);
    }
    if (!cameras.unbounded() && cameras.most < cameras.fewest) {
        return fail("shot intent '{}': at most {} cameras but at least {}", id, cameras.most,
                    cameras.fewest);
    }
    const std::array<std::pair<const char*, float>, 6> dials{
        {{"focusStrength", focusStrength},
         {"movement", movement},
         {"energy", energy},
         {"variation", variation},
         {"cutFrequency", cutFrequency},
         {"visualDensity", visualDensity}}};
    for (const auto& [label, value] : dials) {
        if (!inUnitRange(value)) {
            // Refused rather than clamped (ADR-225): a clamped 1.7 is indistinguishable from an
            // authored 1.0 for the rest of the project's life.
            return fail("shot intent '{}': {} is {}, which is outside 0..1", id, label, value);
        }
    }
    return {};
}

ShotIntent ShotIntent::atProgress(float progress) const {
    ShotIntent out = *this;
    if (arc == Arc::Steady) {
        return out;
    }
    const float p = clamp01(progress);
    if (arc == Arc::Suspended) {
        out.movement = std::min(out.movement, kSuspendedCeiling);
        out.cutFrequency = std::min(out.cutFrequency, kSuspendedCeiling);
        return out;
    }
    const float scale = arcScale(arc, p);
    out.movement = clamp01(movement * scale);
    out.energy = clamp01(energy * scale);
    out.variation = clamp01(variation * scale);
    out.cutFrequency = clamp01(cutFrequency * scale);
    return out;
}

// ---- persistence ------------------------------------------------------------------------------------

json shotIntentToJson(const ShotIntent& intent) {
    return json{{"id", intent.id},
                {"name", intent.name},
                {"description", intent.description},
                {"focus", subjectFocusName(intent.focus)},
                {"focusStrength", intent.focusStrength},
                {"framing",
                 json{{"tightest", framingName(intent.framing.tightest)},
                      {"widest", framingName(intent.framing.widest)}}},
                {"movement", intent.movement},
                {"energy", intent.energy},
                {"variation", intent.variation},
                {"cutFrequency", intent.cutFrequency},
                {"visualDensity", intent.visualDensity},
                {"cameras", json{{"fewest", intent.cameras.fewest}, {"most", intent.cameras.most}}},
                {"arc", arcName(intent.arc)}};
}

Result<ShotIntent> shotIntentFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("a shot intent must be an object");
    }
    ShotIntent out;
    out.id = readString(j, "id");
    out.name = readString(j, "name");
    out.description = readString(j, "description");
    // An unknown enum name is a hard failure rather than a silent default. A project naming a focus
    // this build does not have was written by a newer engine, and quietly reading it as "mixed"
    // would lose the distinction without saying so.
    if (const auto it = j.find("focus"); it != j.end() && it->is_string()) {
        const auto parsed = subjectFocusFromName(it->get<std::string>());
        if (!parsed) {
            return fail("shot intent '{}': unknown subject focus '{}'", out.id,
                        it->get<std::string>());
        }
        out.focus = *parsed;
    }
    out.focusStrength = readFloat(j, "focusStrength", 0.5f);
    if (const auto fr = j.find("framing"); fr != j.end() && fr->is_object()) {
        if (const auto it = fr->find("tightest"); it != fr->end() && it->is_string()) {
            const auto parsed = framingFromName(it->get<std::string>());
            if (!parsed) {
                return fail("shot intent '{}': unknown framing '{}'", out.id,
                            it->get<std::string>());
            }
            out.framing.tightest = *parsed;
        }
        if (const auto it = fr->find("widest"); it != fr->end() && it->is_string()) {
            const auto parsed = framingFromName(it->get<std::string>());
            if (!parsed) {
                return fail("shot intent '{}': unknown framing '{}'", out.id,
                            it->get<std::string>());
            }
            out.framing.widest = *parsed;
        }
    }
    out.movement = readFloat(j, "movement", 0.5f);
    out.energy = readFloat(j, "energy", 0.5f);
    out.variation = readFloat(j, "variation", 0.5f);
    out.cutFrequency = readFloat(j, "cutFrequency", 0.5f);
    out.visualDensity = readFloat(j, "visualDensity", 0.5f);
    if (const auto cams = j.find("cameras"); cams != j.end() && cams->is_object()) {
        out.cameras.fewest = readInt(*cams, "fewest", 1);
        out.cameras.most = readInt(*cams, "most", 1);
    }
    if (const auto it = j.find("arc"); it != j.end() && it->is_string()) {
        const auto parsed = arcFromName(it->get<std::string>());
        if (!parsed) {
            return fail("shot intent '{}': unknown arc '{}'", out.id, it->get<std::string>());
        }
        out.arc = *parsed;
    }
    // A read intent is always custom: the built-ins are code and are never written.
    out.builtIn = false;
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

// ---- the library --------------------------------------------------------------------------------

std::span<const ShotIntent> builtInShotIntents() { return intentLibrary(); }

const ShotIntent* builtInShotIntent(std::string_view id) {
    for (const ShotIntent& i : intentLibrary()) {
        if (i.id == id) {
            return &i;
        }
    }
    return nullptr;
}

ShotIntentId neutralShotIntentId() { return "steady_coverage"; }

} // namespace avgen::song
