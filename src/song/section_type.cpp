#include "song/section_type.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace avgen::song {
namespace {
using nlohmann::json;

constexpr std::array kCategories{SectionCategory::Structural, SectionCategory::Energy,
                                 SectionCategory::Texture, SectionCategory::Cinematic,
                                 SectionCategory::Custom};
constexpr std::array kCategoryNames{"structural", "energy", "texture", "cinematic", "custom"};
static_assert(kCategories.size() == kCategoryNames.size());

[[nodiscard]] SectionType make(const char* id, const char* name, SectionCategory category,
                               const char* intent, const char* description) {
    SectionType t;
    t.id = id;
    t.name = name;
    t.description = description;
    t.category = category;
    t.defaultShotIntent = intent;
    t.builtIn = true;
    return t;
}

// The brief's four lists, implemented as written.
//
// Near-synonyms are all present -- Build and Build-Up, Rise and Riser, Peak and Climax, Swell and
// Crescendo -- because a person reaching for the word "Riser" should find it rather than be told the
// correct word is "Rise". They share a default treatment, and sharing one is the whole of what being
// a synonym costs here.
//
// Every default is the brief's own where the brief states one. The rest are chosen to be defensible
// rather than correct, and every one of them is overridable on any section.
const std::vector<SectionType>& typeLibrary() {
    static const std::vector<SectionType> kLibrary = [] {
        const auto St = SectionCategory::Structural;
        const auto En = SectionCategory::Energy;
        const auto Tx = SectionCategory::Texture;
        const auto Ci = SectionCategory::Cinematic;
        std::vector<SectionType> v;
        v.reserve(60);

        // ---- structural: the shape of a song ------------------------------------------------
        v.push_back(make("intro", "Intro", St, "atmospheric_establishing",
                         "Before the piece has committed to anything."));
        v.push_back(make("verse", "Verse", St, "hero_coverage",
                         "The narrative body: words carrying the story forward."));
        v.push_back(make("pre_chorus", "Pre-Chorus", St, "building_tension",
                         "The run-up that makes the chorus land."));
        v.push_back(make("chorus", "Chorus", St, "dynamic_hero_coverage",
                         "The recurring payoff -- the part people remember."));
        v.push_back(make("post_chorus", "Post-Chorus", St, "dynamic_alternate_coverage",
                         "The tail of the payoff, often wordless."));
        v.push_back(make("refrain", "Refrain", St, "hero_performance",
                         "A repeated line that is not a full chorus."));
        v.push_back(make("bridge", "Bridge", St, "visual_departure",
                         "The departure: new material, usually two thirds in."));
        v.push_back(make("middle_eight", "Middle 8", St, "visual_departure",
                         "A short contrasting passage in the middle of the piece."));
        v.push_back(make("outro", "Outro", St, "slow_pullback",
                         "The wind-down after the last payoff."));
        v.push_back(make("interlude", "Interlude", St, "environmental_performance_exploration",
                         "A passage between two others, belonging to neither."));
        v.push_back(make("instrumental", "Instrumental", St,
                         "environmental_performance_exploration",
                         "A passage with the voice out of it."));
        v.push_back(make("hook", "Hook", St, "dynamic_hero_coverage",
                         "The short, insistent figure the piece is built around."));
        v.push_back(make("tag", "Tag", St, "slow_pullback",
                         "A brief repeated ending fragment."));
        v.push_back(make("phrase", "Phrase", St, "steady_coverage",
                         "An ordinary passage -- what a section becomes when nothing else fits."));

        // ---- energy and arrangement --------------------------------------------------------
        v.push_back(make("build", "Build", En, "increasing_movement",
                         "Going somewhere: the arrangement accumulating."));
        v.push_back(make("build_up", "Build-Up", En, "increasing_movement",
                         "A build, by its other name."));
        v.push_back(make("rise", "Rise", En, "rising_reveal",
                         "Pitch or intensity climbing toward a boundary."));
        v.push_back(make("riser", "Riser", En, "rising_reveal",
                         "A rise used as a transition device."));
        v.push_back(make("drop", "Drop", En, "dramatic_reveal",
                         "The payoff a build was for."));
        v.push_back(make("impact", "Impact", En, "immediate_dramatic_framing",
                         "A single decisive hit."));
        v.push_back(make("peak", "Peak", En, "large_scale_dynamic_coverage",
                         "The loudest, fullest part of the piece."));
        v.push_back(make("climax", "Climax", En, "large_scale_dynamic_coverage",
                         "The peak, as the point the piece was heading for."));
        v.push_back(make("release", "Release", En, "slow_pullback",
                         "Tension letting go."));
        v.push_back(make("transition", "Transition", En, "hard_transition",
                         "A passage whose job is to get from one place to another."));
        v.push_back(make("pause", "Pause", En, "suspended_locked_off",
                         "Everything stops, briefly."));
        v.push_back(make("stop", "Stop", En, "suspended_locked_off",
                         "A full stop, held."));
        v.push_back(make("break", "Break", En, "intimate_restrained",
                         "The music stops holding still: quieter, resolving into nothing yet."));
        v.push_back(make("breakdown", "Breakdown", En, "intimate_restrained",
                         "Stripped back to very little."));
        v.push_back(make("swell", "Swell", En, "increasing_movement",
                         "A gradual rise and fall in level."));
        v.push_back(make("crescendo", "Crescendo", En, "increasing_movement",
                         "Getting louder, deliberately, over time."));
        v.push_back(make("decrescendo", "Decrescendo", En, "slow_pullback",
                         "Getting quieter, deliberately, over time."));

        // ---- texture and musical character -------------------------------------------------
        v.push_back(make("ambient", "Ambient", Tx, "slow_environmental_exploration",
                         "Texture without pulse."));
        v.push_back(make("atmospheric", "Atmospheric", Tx, "slow_environmental_exploration",
                         "Mood carried by space rather than by notes."));
        v.push_back(make("drone", "Drone", Tx, "calm_stillness",
                         "A sustained tone that does not develop."));
        v.push_back(make("soundscape", "Soundscape", Tx, "slow_environmental_exploration",
                         "A place, made of sound."));
        v.push_back(make("groove", "Groove", Tx, "groove_coverage",
                         "The pocket: rhythm as the subject."));
        v.push_back(make("beat", "Beat", Tx, "groove_coverage",
                         "Drums carrying the passage on their own."));
        v.push_back(make("percussion", "Percussion", Tx, "groove_coverage",
                         "A percussive passage, tuned or not."));
        v.push_back(make("vocal", "Vocal", Tx, "vocal_focus",
                         "The voice is the passage."));
        v.push_back(make("vocal_break", "Vocal Break", Tx, "intimate_close_up",
                         "The voice alone, with the arrangement out."));
        v.push_back(make("solo", "Solo", Tx, "hero_performance",
                         "One player, foregrounded."));
        v.push_back(make("acapella", "Acapella", Tx, "intimate_close_up",
                         "Voices with nothing behind them."));
        v.push_back(make("spoken_word", "Spoken Word", Tx, "vocal_focus",
                         "Spoken rather than sung."));
        v.push_back(make("rap", "Rap", Tx, "vocal_focus",
                         "Rhythmic delivery as the lead."));
        v.push_back(make("ad_lib", "Ad-Lib", Tx, "vocal_focus",
                         "Improvised vocal decoration."));

        // ---- visual and cinematic: not produced by the analyzer, entirely valid to author ----
        v.push_back(make("dream_sequence", "Dream Sequence", Ci, "floating_unconventional",
                         "A passage that does not obey the film's own rules."));
        v.push_back(make("montage", "Montage", Ci, "rapid_multi_shot",
                         "Many short shots standing in for elapsed time."));
        v.push_back(make("exploration", "Exploration", Ci, "free_roaming_environment",
                         "Going and looking, with nothing particular to find."));
        v.push_back(make("reflection", "Reflection", Ci, "intimate_restrained",
                         "The film pausing to consider what just happened."));
        v.push_back(make("suspense", "Suspense", Ci, "held_tension",
                         "Something is about to happen and has not."));
        v.push_back(make("calm", "Calm", Ci, "calm_stillness",
                         "Nothing is wrong and nothing is hurrying."));
        v.push_back(make("tension", "Tension", Ci, "held_tension",
                         "Something is wrong and has not resolved."));
        v.push_back(make("celebration", "Celebration", Ci, "dynamic_multi_subject",
                         "Everyone, at once, pleased about it."));
        v.push_back(make("finale", "Finale", Ci, "large_scale_dynamic_coverage",
                         "The last big thing the piece does."));
        v.push_back(make("scene_change", "Scene Change", Ci, "hard_transition",
                         "The film is somewhere else now."));
        v.push_back(make("character_introduction", "Character Introduction", Ci,
                         "character_introduction", "The film meets somebody."));
        v.push_back(make("character_reveal", "Character Reveal", Ci, "dramatic_reveal",
                         "The film finds out who somebody is."));
        v.push_back(make("establishing", "Establishing", Ci, "atmospheric_establishing",
                         "Where we are, before what happens here."));
        v.push_back(make("action", "Action", Ci, "action_coverage",
                         "Things happening fast enough to keep up with."));
        v.push_back(make("credits", "Credits", Ci, "detached_observation",
                         "After the piece, and knowingly so."));
        return v;
    }();
    return kLibrary;
}
} // namespace

const char* sectionCategoryName(SectionCategory c) {
    const auto i = static_cast<std::size_t>(c);
    return i < kCategoryNames.size() ? kCategoryNames[i] : "custom";
}

std::optional<SectionCategory> sectionCategoryFromName(std::string_view name) {
    for (std::size_t i = 0; i < kCategoryNames.size(); ++i) {
        if (name == kCategoryNames[i]) {
            return kCategories[i];
        }
    }
    return std::nullopt;
}

std::span<const SectionCategory> allSectionCategories() { return kCategories; }

Result<void> SectionType::validate() const {
    if (auto ok = validateId(id); !ok) {
        return ok;
    }
    if (name.empty()) {
        return fail("section type '{}' has no name", id);
    }
    if (auto ok = validateId(defaultShotIntent); !ok) {
        return fail("section type '{}': its default shot intent is not a usable id: {}", id,
                    ok.error().message);
    }
    return {};
}

nlohmann::json sectionTypeToJson(const SectionType& type) {
    return json{{"id", type.id},
                {"name", type.name},
                {"description", type.description},
                {"category", sectionCategoryName(type.category)},
                {"defaultShotIntent", type.defaultShotIntent}};
}

Result<SectionType> sectionTypeFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("a section type must be an object");
    }
    const auto readString = [&j](const char* key) {
        const auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    SectionType out;
    out.id = readString("id");
    out.name = readString("name");
    out.description = readString("description");
    out.defaultShotIntent = readString("defaultShotIntent");
    if (const auto it = j.find("category"); it != j.end() && it->is_string()) {
        const auto parsed = sectionCategoryFromName(it->get<std::string>());
        if (!parsed) {
            return fail("section type '{}': unknown category '{}'", out.id, it->get<std::string>());
        }
        out.category = *parsed;
    }
    // A read type is always custom; the built-ins are code and are never written.
    out.builtIn = false;
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

std::span<const SectionType> builtInSectionTypes() { return typeLibrary(); }

const SectionType* builtInSectionType(std::string_view id) {
    for (const SectionType& t : typeLibrary()) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

SectionTypeId neutralSectionTypeId() { return "phrase"; }

} // namespace avgen::song
