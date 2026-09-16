// The shot language (ADR-247): the built-in section types, the built-in shot intents, the registry
// that holds both, and the custom definitions a person adds to it.
//
// The claim this file is written to break is the architectural one: that a custom section type is
// **not** a second-class citizen bolted on beside Verse and Chorus. So the built-in assertions are
// factored into helpers and run a second time against three types that did not exist when the engine
// was compiled. If anything anywhere ever special-cases the structural vocabulary, the custom half
// of this file fails and the built-in half does not.

#include "song/section_timeline.hpp"
#include "song/section_type.hpp"
#include "song/shot_intent.hpp"
#include "song/shot_language.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
using song::Arc;
using song::CameraCount;
using song::Framing;
using song::FramingRange;
using song::Section;
using song::SectionCategory;
using song::SectionType;
using song::ShotIntent;
using song::ShotLanguage;
using song::SubjectFocus;

namespace {

// The identifiers that go in project files. Written out in full and compared as a set, because the
// entire point of a stable identifier is that renaming one is a breaking change -- and a test that
// only counted them would pass a rename. A new entry is a one-line addition here; a *changed* entry
// is a failure, which is exactly the asymmetry wanted.
const std::vector<std::string> kBuiltInIntentIds{
    "atmospheric_establishing", "hero_coverage", "building_tension", "dynamic_hero_coverage",
    "dynamic_alternate_coverage", "visual_departure", "slow_pullback", "increasing_movement",
    "rising_reveal", "dramatic_reveal", "intimate_restrained", "slow_environmental_exploration",
    "environmental_performance_exploration", "hero_performance", "intimate_close_up",
    "large_scale_dynamic_coverage", "immediate_dramatic_framing", "suspended_locked_off",
    "rapid_multi_shot", "floating_unconventional", "free_roaming_environment",
    "dynamic_multi_subject", "steady_coverage", "hard_transition", "held_tension", "calm_stillness",
    "character_introduction", "groove_coverage", "vocal_focus", "action_coverage",
    "detached_observation"};

const std::vector<std::string> kBuiltInTypeIds{
    "intro", "verse", "pre_chorus", "chorus", "post_chorus", "refrain", "bridge", "middle_eight",
    "outro", "interlude", "instrumental", "hook", "tag", "phrase", "build", "build_up", "rise",
    "riser", "drop", "impact", "peak", "climax", "release", "transition", "pause", "stop", "break",
    "breakdown", "swell", "crescendo", "decrescendo", "ambient", "atmospheric", "drone",
    "soundscape", "groove", "beat", "percussion", "vocal", "vocal_break", "solo", "acapella",
    "spoken_word", "rap", "ad_lib", "dream_sequence", "montage", "exploration", "reflection",
    "suspense", "calm", "tension", "celebration", "finale", "scene_change", "character_introduction",
    "character_reveal", "establishing", "action", "credits"};

Section sectionOf(const std::string& type, double start = 0.0, double end = 10.0) {
    Section s;
    s.type = type;
    s.startSeconds = start;
    s.endSeconds = end;
    return s;
}

// Everything a usable section type must satisfy. Run against built-ins and against custom types
// alike -- which is the whole architectural claim, made executable.
void assertTypeIsUsable(const ShotLanguage& language, const std::string& id) {
    INFO("section type '" << id << "'");
    const SectionType* type = language.type(id);
    REQUIRE(type != nullptr);
    CHECK(!type->name.empty());
    CHECK(song::validateId(type->id).has_value());

    // Its default treatment must exist and be well formed.
    const ShotIntent* intent = language.intent(type->defaultShotIntent);
    REQUIRE(intent != nullptr);
    CHECK(intent->validate().has_value());

    // And a section of this type must resolve to exactly that treatment, through the public path
    // everything downstream uses.
    const Section s = sectionOf(id);
    CHECK(language.intentFor(s).id == type->defaultShotIntent);
    CHECK(language.resolutionOf(s) == ShotLanguage::Resolution::TypeDefault);
    CHECK(!language.displayName(s).empty());
}

} // namespace

// ---- the built-in vocabularies ------------------------------------------------------------------

TEST_CASE("Every built-in section type has a valid default shot intent", "[song][language]") {
    const ShotLanguage language;
    REQUIRE(song::builtInSectionTypes().size() == kBuiltInTypeIds.size());
    for (const SectionType& type : song::builtInSectionTypes()) {
        assertTypeIsUsable(language, type.id);
    }
}

TEST_CASE("Every built-in shot intent validates", "[song][language]") {
    REQUIRE(song::builtInShotIntents().size() == kBuiltInIntentIds.size());
    for (const ShotIntent& intent : song::builtInShotIntents()) {
        INFO("shot intent '" << intent.id << "'");
        CHECK(intent.validate().has_value());
        CHECK(intent.builtIn);
        CHECK(!intent.name.empty());
        CHECK(!intent.description.empty());
    }
}

TEST_CASE("The built-in identifiers are frozen", "[song][language]") {
    // These strings are in saved project files. Renaming one silently repoints every section that
    // used it, which is why this is a literal list rather than a count.
    std::set<std::string> intents;
    for (const ShotIntent& i : song::builtInShotIntents()) {
        CHECK(intents.insert(i.id).second); // and they are unique
    }
    CHECK(intents == std::set<std::string>(kBuiltInIntentIds.begin(), kBuiltInIntentIds.end()));

    std::set<std::string> types;
    for (const SectionType& t : song::builtInSectionTypes()) {
        CHECK(types.insert(t.id).second);
    }
    CHECK(types == std::set<std::string>(kBuiltInTypeIds.begin(), kBuiltInTypeIds.end()));

    // The neutral fallbacks are named, and they are among the built-ins rather than invented.
    CHECK(song::builtInShotIntent(song::neutralShotIntentId()) != nullptr);
    CHECK(song::builtInSectionType(song::neutralSectionTypeId()) != nullptr);
}

TEST_CASE("The brief's stated default treatments are the ones shipped", "[song][language]") {
    // The table in the brief's section 5, asserted rather than trusted to a code review.
    const ShotLanguage language;
    const std::vector<std::pair<std::string, std::string>> expected{
        {"intro", "atmospheric_establishing"},
        {"verse", "hero_coverage"},
        {"pre_chorus", "building_tension"},
        {"chorus", "dynamic_hero_coverage"},
        {"post_chorus", "dynamic_alternate_coverage"},
        {"bridge", "visual_departure"},
        {"outro", "slow_pullback"},
        {"build", "increasing_movement"},
        {"riser", "rising_reveal"},
        {"drop", "dramatic_reveal"},
        {"breakdown", "intimate_restrained"},
        {"ambient", "slow_environmental_exploration"},
        {"instrumental", "environmental_performance_exploration"},
        {"solo", "hero_performance"},
        {"acapella", "intimate_close_up"},
        {"peak", "large_scale_dynamic_coverage"},
        {"impact", "immediate_dramatic_framing"},
        {"pause", "suspended_locked_off"},
        {"montage", "rapid_multi_shot"},
        {"dream_sequence", "floating_unconventional"},
        {"exploration", "free_roaming_environment"},
        {"celebration", "dynamic_multi_subject"}};
    for (const auto& [type, intent] : expected) {
        INFO(type << " -> " << intent);
        REQUIRE(language.type(type) != nullptr);
        CHECK(language.type(type)->defaultShotIntent == intent);
    }
}

TEST_CASE("Every section category is populated, and Custom is not used by a built-in",
          "[song][language]") {
    const ShotLanguage language;
    for (const SectionCategory c : song::allSectionCategories()) {
        INFO("category " << song::sectionCategoryName(c));
        const auto in = language.typesInCategory(c);
        if (c == SectionCategory::Custom) {
            // Nothing shipped is filed as Custom: that word is reserved for a person's own filing.
            CHECK(in.empty());
        } else {
            CHECK(!in.empty());
        }
    }
}

TEST_CASE("The small vocabularies round-trip through their names", "[song][language]") {
    for (const SubjectFocus f : song::allSubjectFocuses()) {
        CHECK(song::subjectFocusFromName(song::subjectFocusName(f)) == f);
    }
    for (const Framing f : song::allFramings()) {
        CHECK(song::framingFromName(song::framingName(f)) == f);
    }
    for (const Arc a : song::allArcs()) {
        CHECK(song::arcFromName(song::arcName(a)) == a);
    }
    for (const SectionCategory c : song::allSectionCategories()) {
        CHECK(song::sectionCategoryFromName(song::sectionCategoryName(c)) == c);
    }
    CHECK(!song::framingFromName("enormous").has_value());
    CHECK(!song::arcFromName("wobbly").has_value());
}

// ---- identifiers ---------------------------------------------------------------------------------

TEST_CASE("An identifier that could not survive a project file is refused", "[song][language]") {
    CHECK(song::validateId("ocean_ambience").has_value());
    CHECK(song::validateId("verse2").has_value());
    CHECK(!song::validateId("").has_value());
    CHECK(!song::validateId("Ocean Ambience").has_value()); // spaces and capitals
    CHECK(!song::validateId("ocean-ambience").has_value()); // hyphens
    CHECK(!song::validateId("_ocean").has_value());
    CHECK(!song::validateId("ocean_").has_value());
    CHECK(!song::validateId(std::string(65, 'a')).has_value());
}

TEST_CASE("A display name becomes an identifier", "[song][language]") {
    CHECK(song::makeId("Ocean Ambience") == "ocean_ambience");
    CHECK(song::makeId("Middle 8") == "middle_8");
    CHECK(song::makeId("  Character Reveal!  ") == "character_reveal");
    CHECK(song::makeId("already_an_id") == "already_an_id");
    // Idempotent, which is what lets a UI slugify on every keystroke.
    CHECK(song::makeId(*song::makeId("Dream Sequence")) == song::makeId("Dream Sequence"));
    // A name with nothing usable in it is nothing, not a crash and not an empty id.
    CHECK(!song::makeId("???").has_value());
}

// ---- the dials -----------------------------------------------------------------------------------

TEST_CASE("An out-of-range dial is refused rather than clamped", "[song][language]") {
    // ADR-225's third rule. A clamped 1.7 is indistinguishable from an authored 1.0 for the rest of
    // the project's life, so the value never gets in.
    ShotIntent i = *song::builtInShotIntent("hero_performance");
    i.movement = 1.7f;
    const auto refused = i.validate();
    REQUIRE(!refused.has_value());
    CHECK(refused.error().message.find("movement") != std::string::npos);

    i = *song::builtInShotIntent("hero_performance");
    i.framing = FramingRange{Framing::Wide, Framing::Close}; // inverted
    CHECK(!i.validate().has_value());

    i = *song::builtInShotIntent("hero_performance");
    i.cameras = CameraCount{0, 1};
    CHECK(!i.validate().has_value());
}

TEST_CASE("A framing range narrows a request without inverting it", "[song][language]") {
    const FramingRange band{Framing::Close, Framing::Wide};
    CHECK(band.contains(Framing::Medium));
    CHECK(!band.contains(Framing::VeryWide));
    CHECK(band.clamp(Framing::ExtremeClose) == Framing::Close);
    CHECK(band.clamp(Framing::VeryWide) == Framing::Wide);
    CHECK(band.clamp(Framing::Medium) == Framing::Medium);
    // Rounds wider: a frame slightly too wide is a legible shot and one slightly too tight is a
    // mistake.
    CHECK(FramingRange{Framing::Close, Framing::Medium}.middle() == Framing::Medium);
}

TEST_CASE("A camera count is a preference, not a demand", "[song][language]") {
    // An intent wanting three cameras in a scene with one gets one. A director that refused to shoot
    // because the scene was under-equipped would be useless.
    CHECK(CameraCount{3, 0}.resolve(1) == 1);
    CHECK(CameraCount{3, 0}.resolve(6) == 6); // unbounded takes everything available
    CHECK(CameraCount{1, 1}.resolve(6) == 1);
    CHECK(CameraCount{2, 4}.resolve(6) == 4);
    CHECK(CameraCount{2, 4}.resolve(3) == 3);
    CHECK(CameraCount{1, 1}.resolve(0) == 1); // never zero
}

TEST_CASE("An arc travels the dials across the section and nothing else", "[song][language]") {
    const ShotIntent rising = *song::builtInShotIntent("increasing_movement");
    REQUIRE(rising.arc == Arc::Rising);
    const ShotIntent atStart = rising.atProgress(0.0f);
    const ShotIntent atEnd = rising.atProgress(1.0f);
    CHECK(atStart.movement < atEnd.movement);
    CHECK(atStart.energy < atEnd.energy);
    CHECK(atStart.cutFrequency < atEnd.cutFrequency);
    // It arrives at what it says, and starts somewhere short of it -- not at nothing, because a
    // build whose camera is completely still on its first frame reads as a mistake.
    CHECK(atEnd.movement == Catch::Approx(rising.movement));
    CHECK(atStart.movement > 0.0f);
    // What the passage *is* does not travel.
    CHECK(atStart.framing == rising.framing);
    CHECK(atStart.focus == rising.focus);
    CHECK(atStart.visualDensity == Catch::Approx(rising.visualDensity));
    CHECK(atStart.cameras == rising.cameras);

    const ShotIntent falling = *song::builtInShotIntent("slow_pullback");
    REQUIRE(falling.arc == Arc::Falling);
    CHECK(falling.atProgress(0.0f).movement > falling.atProgress(1.0f).movement);

    const ShotIntent burst = *song::builtInShotIntent("dramatic_reveal");
    REQUIRE(burst.arc == Arc::Burst);
    CHECK(burst.atProgress(0.0f).energy > burst.atProgress(0.5f).energy);
    CHECK(burst.atProgress(0.0f).energy == Catch::Approx(burst.energy).margin(1e-5));

    // Steady is an exact copy, which is what makes `atProgress` safe to call unconditionally.
    const ShotIntent steady = *song::builtInShotIntent("hero_performance");
    REQUIRE(steady.arc == Arc::Steady);
    CHECK(steady.atProgress(0.3f) == steady);

    // Suspended pins movement and cutting down whatever the dials say, so the word means something
    // even on a custom intent that set them high.
    ShotIntent odd = *song::builtInShotIntent("suspended_locked_off");
    odd.movement = 0.9f;
    odd.cutFrequency = 0.9f;
    CHECK(odd.atProgress(0.5f).movement <= 0.1f);
    CHECK(odd.atProgress(0.5f).cutFrequency <= 0.1f);

    // Progress outside 0..1 is clamped rather than extrapolated: a director sampling slightly past
    // a boundary must not get a negative movement.
    CHECK(rising.atProgress(-3.0f).movement == Catch::Approx(rising.atProgress(0.0f).movement));
    CHECK(rising.atProgress(9.0f).movement == Catch::Approx(rising.atProgress(1.0f).movement));
    CHECK(rising.atProgress(9.0f).validate().has_value());
}

// ---- custom definitions: the architectural claim --------------------------------------------------

TEST_CASE("A custom section type behaves exactly like a built-in one", "[song][language][custom]") {
    ShotLanguage language;
    REQUIRE(!language.customized());

    // Three types that did not exist when the engine was compiled -- the brief's own examples.
    // One reuses a built-in treatment, one defines its own.
    ShotIntent tidal;
    tidal.id = "tidal_drift";
    tidal.name = "Tidal Drift";
    tidal.description = "Very slow lateral movement, as if carried.";
    tidal.focus = SubjectFocus::Environment;
    tidal.focusStrength = 0.1f;
    tidal.framing = FramingRange{Framing::Medium, Framing::VeryWide};
    tidal.movement = 0.2f;
    tidal.energy = 0.15f;
    tidal.variation = 0.3f;
    tidal.cutFrequency = 0.05f;
    tidal.visualDensity = 0.4f;
    tidal.cameras = CameraCount{1, 2};
    tidal.arc = Arc::Steady;
    REQUIRE(language.defineIntent(tidal).has_value());

    REQUIRE(language
                .defineType(SectionType{"ocean_ambience", "Ocean Ambience",
                                        "Slow underwater environment passage",
                                        SectionCategory::Custom, "tidal_drift", false})
                .has_value());
    REQUIRE(language
                .defineType(SectionType{"dream_state", "Dream State", "Not quite real",
                                        SectionCategory::Cinematic, "floating_unconventional",
                                        false})
                .has_value());
    REQUIRE(language
                .defineType(SectionType{"the_reveal", "The Reveal", "Who they actually are",
                                        SectionCategory::Custom, "dramatic_reveal", false})
                .has_value());
    CHECK(language.customized());

    // The identical assertions the built-ins are held to. Nothing in the engine may distinguish.
    assertTypeIsUsable(language, "ocean_ambience");
    assertTypeIsUsable(language, "dream_state");
    assertTypeIsUsable(language, "the_reveal");

    // And they appear in the picker alongside the built-ins.
    const auto all = language.types();
    CHECK(all.size() == song::builtInSectionTypes().size() + 3);
    CHECK(std::any_of(all.begin(), all.end(),
                      [](const SectionType* t) { return t->id == "ocean_ambience"; }));
    const auto custom = language.typesInCategory(SectionCategory::Custom);
    CHECK(custom.size() == 2); // ocean_ambience and the_reveal
}

TEST_CASE("A type whose default treatment does not exist is refused", "[song][language][custom]") {
    ShotLanguage language;
    // The failure this prevents is a section with no treatment and no error, which looks exactly
    // like the feature not working.
    const auto refused = language.defineType(SectionType{
        "ocean_ambience", "Ocean Ambience", "", SectionCategory::Custom, "tidal_drift", false});
    REQUIRE(!refused.has_value());
    CHECK(refused.error().message.find("tidal_drift") != std::string::npos);
    CHECK(!language.hasType("ocean_ambience"));
    CHECK(!language.customized());

    // A malformed id is refused too, before anything else is looked at.
    CHECK(!language
               .defineType(SectionType{"Ocean Ambience", "Ocean Ambience", "",
                                       SectionCategory::Custom, "steady_coverage", false})
               .has_value());
}

TEST_CASE("A custom definition shadows a built-in, reversibly", "[song][language][custom]") {
    ShotLanguage language;
    REQUIRE(language.type("intro")->defaultShotIntent == "atmospheric_establishing");

    // The type-level half of "the user must be able to override them": every Intro in this piece,
    // not eleven separate choices.
    REQUIRE(language
                .defineType(SectionType{"intro", "Intro", "This film opens tight",
                                        SectionCategory::Structural, "intimate_close_up", false})
                .has_value());
    CHECK(language.type("intro")->defaultShotIntent == "intimate_close_up");
    CHECK(language.intentFor(sectionOf("intro")).id == "intimate_close_up");
    // It appears once, in the built-in's position, so a picker does not reshuffle.
    CHECK(language.types().size() == song::builtInSectionTypes().size());

    CHECK(language.removeCustomType("intro"));
    CHECK(language.type("intro")->defaultShotIntent == "atmospheric_establishing");
    CHECK(!language.customized());
}

TEST_CASE("A treatment a type still depends on cannot be removed", "[song][language][custom]") {
    ShotLanguage language;
    ShotIntent tidal = *song::builtInShotIntent("slow_environmental_exploration");
    tidal.id = "tidal_drift";
    tidal.name = "Tidal Drift";
    REQUIRE(language.defineIntent(tidal).has_value());
    REQUIRE(language
                .defineType(SectionType{"ocean_ambience", "Ocean Ambience", "",
                                        SectionCategory::Custom, "tidal_drift", false})
                .has_value());

    const auto refused = language.removeCustomIntent("tidal_drift");
    REQUIRE(!refused.has_value());
    CHECK(refused.error().message.find("ocean_ambience") != std::string::npos);

    // Once nothing depends on it, it goes.
    CHECK(language.removeCustomType("ocean_ambience"));
    CHECK(language.removeCustomIntent("tidal_drift").has_value());
    CHECK(!language.customized());
}

// ---- persistence ----------------------------------------------------------------------------------

TEST_CASE("A language with no custom definitions writes nothing", "[song][language][json]") {
    // Built-ins are code. Writing them would mean a project saved today could resurrect a definition
    // the engine has since improved -- and it would put ninety objects in every project file that
    // never defined one.
    const ShotLanguage language;
    CHECK(language.toJson().empty());
    CHECK(!language.customized());
}

TEST_CASE("Custom definitions survive the project file", "[song][language][json]") {
    ShotLanguage language;
    ShotIntent tidal;
    tidal.id = "tidal_drift";
    tidal.name = "Tidal Drift";
    tidal.description = "Carried, rather than driven.";
    tidal.focus = SubjectFocus::Environment;
    tidal.focusStrength = 0.125f;
    tidal.framing = FramingRange{Framing::Medium, Framing::VeryWide};
    tidal.movement = 0.1875f;
    tidal.energy = 0.25f;
    tidal.variation = 0.375f;
    tidal.cutFrequency = 0.0625f;
    tidal.visualDensity = 0.5f;
    tidal.cameras = CameraCount{1, 0};
    tidal.arc = Arc::Falling;
    REQUIRE(language.defineIntent(tidal).has_value());
    REQUIRE(language
                .defineType(SectionType{"ocean_ambience", "Ocean Ambience",
                                        "Slow underwater environment passage",
                                        SectionCategory::Custom, "tidal_drift", false})
                .has_value());

    const auto restored = ShotLanguage::fromJson(language.toJson());
    REQUIRE(restored.has_value());
    CHECK(*restored == language);

    const ShotIntent* back = restored->intent("tidal_drift");
    REQUIRE(back != nullptr);
    CHECK(back->name == "Tidal Drift");
    CHECK(back->focus == SubjectFocus::Environment);
    CHECK(back->arc == Arc::Falling);
    CHECK(back->framing == tidal.framing);
    CHECK(back->cameras == tidal.cameras);
    CHECK(back->cameras.unbounded());
    CHECK(back->focusStrength == tidal.focusStrength); // exactly: these are not measurements
    CHECK(back->cutFrequency == tidal.cutFrequency);
    CHECK(!back->builtIn);

    const SectionType* type = restored->type("ocean_ambience");
    REQUIRE(type != nullptr);
    CHECK(type->name == "Ocean Ambience");
    CHECK(type->category == SectionCategory::Custom);
    CHECK(type->defaultShotIntent == "tidal_drift");
    CHECK(!type->builtIn);
    // And it resolves through the public path, after a round trip, exactly as before one.
    CHECK(restored->intentFor(sectionOf("ocean_ambience")).id == "tidal_drift");
}

TEST_CASE("A project naming something this build does not have says so", "[song][language][json]") {
    // Unknown enum names are a hard failure rather than a silent default: a project written by a
    // newer engine is not the same thing as a project that chose "mixed".
    nlohmann::json doc;
    doc["intents"] = nlohmann::json::array();
    doc["intents"].push_back({{"id", "odd"}, {"name", "Odd"}, {"arc", "corkscrew"}});
    const auto refused = ShotLanguage::fromJson(doc);
    REQUIRE(!refused.has_value());
    CHECK(refused.error().message.find("corkscrew") != std::string::npos);

    // A type pointing at an intent the file did not define is refused on load, not silently kept.
    nlohmann::json orphan;
    orphan["types"] = nlohmann::json::array();
    orphan["types"].push_back({{"id", "ocean_ambience"},
                               {"name", "Ocean Ambience"},
                               {"category", "custom"},
                               {"defaultShotIntent", "tidal_drift"}});
    CHECK(!ShotLanguage::fromJson(orphan).has_value());

    // Wrong container types are errors, not crashes.
    CHECK(!ShotLanguage::fromJson(nlohmann::json::array()).has_value());
    CHECK(ShotLanguage::fromJson(nlohmann::json::object()).has_value());
}
