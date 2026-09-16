// The authored section timeline (ADR-247): building one from an analysis, editing it, persisting it,
// and projecting it into the cues a director consumes.
//
// Nothing here runs the detector. ADR-206's tests do that, and the rule they set holds here too:
// detection quality is *reported*, never asserted. What is asserted is the part with a right answer
// -- that an edit is recorded, that a boundary survives a JSON round trip bit for bit, and that a
// director's view of a section contains no way to find out what kind of section it is.

#include "analysis/structure.hpp"
#include "song/from_analysis.hpp"
#include "song/section_cue.hpp"
#include "song/section_timeline.hpp"
#include "song/shot_language.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace avgen;
using song::Section;
using song::SectionCue;
using song::SectionField;
using song::SectionCategory;
using song::SectionOrigin;
using song::SectionTimeline;
using song::SectionType;
using song::ShotIntent;
using song::ShotLanguage;

namespace {

Section made(const std::string& type, double start, double end) {
    Section s;
    s.type = type;
    s.startSeconds = start;
    s.endSeconds = end;
    s.labelConfidence = 0.7f;
    s.startConfidence = 0.6f;
    s.endConfidence = 0.5f;
    s.energy = 0.4f;
    s.density = 0.3f;
    return s;
}

// Six sections at times that are deliberately not whole numbers: a fixture whose boundaries are
// integers cannot catch a rounding bug, which is the lesson ADR-206's precision test recorded.
SectionTimeline fixture() {
    SectionTimeline t;
    t.durationSeconds = 180.372918;
    t.sections.push_back(made("intro", 0.0, 18.145231));
    t.sections.push_back(made("verse", 18.145231, 43.409117));
    t.sections.push_back(made("pre_chorus", 43.409117, 57.781004));
    t.sections.push_back(made("chorus", 57.781004, 83.226719));
    t.sections.push_back(made("verse", 83.226719, 148.117203));
    t.sections.push_back(made("chorus", 148.117203, 180.372918));
    t.renumber();
    return t;
}

analysis::SongSection detectedSection(double start, double end, analysis::SectionFunction f) {
    analysis::SongSection s;
    s.startSeconds = start;
    s.endSeconds = end;
    s.function = f;
    s.label = "the detector's guess";
    s.labelConfidence = 0.65f;
    s.startConfidence = 0.8f;
    s.endConfidence = 0.7f;
    s.energy = 0.42f;
    s.density = 0.31f;
    s.repetitionGroup = 2;
    return s;
}

} // namespace

// ---- the model ------------------------------------------------------------------------------------

TEST_CASE("A timeline is ordered, gapless and covering, or it says why not",
          "[song][timeline]") {
    const SectionTimeline t = fixture();
    REQUIRE(t.validate().has_value());
    CHECK(t.sections.size() == 6);

    SectionTimeline gapped = t;
    gapped.sections[2].startSeconds += 0.5; // daylight belonging to nothing
    CHECK(!gapped.validate().has_value());

    SectionTimeline inverted = t;
    inverted.sections[1].endSeconds = inverted.sections[1].startSeconds;
    CHECK(!inverted.validate().has_value());

    SectionTimeline untyped = t;
    untyped.sections[0].type.clear();
    CHECK(!untyped.validate().has_value());

    // An empty timeline is empty, not invalid -- a piece with no analysis yet is the ordinary state.
    CHECK(SectionTimeline{}.validate().has_value());
}

TEST_CASE("A section is found by the time it covers", "[song][timeline]") {
    const SectionTimeline t = fixture();
    REQUIRE(t.at(0.0) != nullptr);
    CHECK(t.at(0.0)->type == "intro");
    CHECK(t.at(18.145231)->type == "verse");      // a boundary belongs to the later section
    CHECK(t.at(18.145230)->type == "intro");
    CHECK(t.at(90.0)->type == "verse");
    // The last section owns its own end, so a query exactly at the duration answers.
    CHECK(t.at(180.372918) == &t.sections.back());
    CHECK(t.at(-1.0) == nullptr);
    CHECK(t.at(9999.0) == nullptr);
}

TEST_CASE("Repeats are occurrences of one type, not three types", "[song][timeline]") {
    const SectionTimeline t = fixture();
    const ShotLanguage language;
    CHECK(t.countOfType("verse") == 2);
    CHECK(t.sections[1].occurrence == 0);
    CHECK(t.sections[4].occurrence == 1);
    CHECK(t.sections[3].occurrence == 0); // the first chorus
    CHECK(t.sections[5].occurrence == 1);

    // Which is what makes "Verse", "Verse 2" free rather than two stored strings.
    CHECK(language.displayName(t.sections[1]) == "Verse");
    CHECK(language.displayName(t.sections[4]) == "Verse 2");
    CHECK(language.displayName(t.sections[5]) == "Chorus 2");

    // And a name a person typed wins over the composed one.
    Section named = t.sections[4];
    named.label = "The quiet verse";
    CHECK(language.displayName(named) == "The quiet verse");
}

TEST_CASE("Provenance is derived from what a person touched", "[song][timeline]") {
    Section s = made("verse", 0.0, 10.0);
    CHECK(s.origin() == SectionOrigin::Detected);
    CHECK(!s.spanIsFrozen());
    CHECK(song::labelConfidenceIsMeaningful(s));
    CHECK(song::boundaryConfidenceIsMeaningful(s));

    s.edited |= SectionField::Type;
    CHECK(s.origin() == SectionOrigin::Refined);
    // Retyping retires the detector's claim about the label -- it was a claim about the function
    // that became the type.
    CHECK(!song::labelConfidenceIsMeaningful(s));
    // But not its claim about where the section is. That is the whole point of per-field provenance.
    CHECK(song::boundaryConfidenceIsMeaningful(s));
    CHECK(!s.spanIsFrozen());

    s.edited |= SectionField::Start;
    CHECK(!song::boundaryConfidenceIsMeaningful(s));
    CHECK(s.spanIsFrozen());

    Section authored = made("ocean_ambience", 0.0, 10.0);
    authored.authored = true;
    CHECK(authored.origin() == SectionOrigin::Authored);
    CHECK(authored.spanIsFrozen());
    CHECK(!song::labelConfidenceIsMeaningful(authored));

    // The field names round-trip, which is how the mask gets into a project file.
    const auto names = song::sectionFieldNames(SectionField::Type | SectionField::ShotIntent);
    CHECK(names == std::vector<std::string>{"type", "shot-intent"});
    const auto back = song::sectionFieldsFromNames(names);
    REQUIRE(back.has_value());
    CHECK(*back == (SectionField::Type | SectionField::ShotIntent));
    CHECK(!song::sectionFieldsFromNames(std::vector<std::string>{"colour"}).has_value());
}

// ---- editing (the brief's section 8) ----------------------------------------------------------------

TEST_CASE("Moving a boundary keeps the time exactly and marks both sides", "[song][timeline][edit]") {
    SectionTimeline t = fixture();
    const double target = 20.918273645;
    const auto moved = moveBoundary(t, 1, target);
    REQUIRE(moved.has_value());
    // Bit for bit, not within a millisecond. A tolerance would pass against a version that quietly
    // rounded, which is precisely the bug worth catching.
    CHECK(*moved == target);
    CHECK(t.sections[0].endSeconds == target);
    CHECK(t.sections[1].startSeconds == target);
    CHECK(t.sections[0].isEdited(SectionField::End));
    CHECK(t.sections[1].isEdited(SectionField::Start));
    CHECK(t.sections[0].spanIsFrozen());
    CHECK(t.sections[1].spanIsFrozen());
    // The sections either side of those two are untouched -- per-field provenance does not spread.
    CHECK(t.sections[2].origin() == SectionOrigin::Detected);
    REQUIRE(t.validate().has_value());

    // It is clamped into the legal window rather than refused.
    const auto clamped = moveBoundary(t, 1, -500.0);
    REQUIRE(clamped.has_value());
    CHECK(*clamped == Catch::Approx(0.25));

    // The piece's own start and end are not boundaries.
    CHECK(!moveBoundary(t, 0, 5.0).has_value());
    CHECK(!moveBoundary(t, t.sections.size(), 5.0).has_value());
}

TEST_CASE("Splitting makes the later half a section that did not exist before",
          "[song][timeline][edit]") {
    SectionTimeline t = fixture();
    const std::size_t before = t.sections.size();
    const auto index = splitSection(t, 100.5);
    REQUIRE(index.has_value());
    CHECK(*index == 5);
    CHECK(t.sections.size() == before + 1);
    REQUIRE(t.validate().has_value());

    CHECK(t.sections[4].endSeconds == 100.5);
    CHECK(t.sections[5].startSeconds == 100.5);
    CHECK(t.sections[4].isEdited(SectionField::End));
    CHECK(t.sections[5].authored);
    CHECK(t.sections[5].origin() == SectionOrigin::Authored);
    CHECK(t.sections[5].type == "verse");
    // The detector's confidences were claims about a span that no longer exists.
    CHECK(t.sections[5].labelConfidence == 0.0f);
    CHECK(t.sections[5].repetitionGroup == -1);
    // The occurrence numbers are rebuilt, so the split verse is the third.
    CHECK(t.countOfType("verse") == 3);
    CHECK(t.sections[5].occurrence == 2);

    // Too close to either edge is refused rather than producing a sliver.
    CHECK(!splitSection(t, 100.5 + 0.0001).has_value());
    CHECK(!splitSection(t, 9999.0).has_value());
}

TEST_CASE("Merging and removing keep the timeline gapless", "[song][timeline][edit]") {
    SectionTimeline t = fixture();
    const double preChorusEnd = t.sections[2].endSeconds;
    REQUIRE(mergeSectionWithPrevious(t, 2));
    CHECK(t.sections.size() == 5);
    CHECK(t.sections[1].type == "verse");             // the survivor keeps its own identity
    CHECK(t.sections[1].endSeconds == preChorusEnd);  // and takes the absorbed section's end
    CHECK(t.sections[1].isEdited(SectionField::End));
    REQUIRE(t.validate().has_value());

    CHECK(!mergeSectionWithPrevious(t, 0)); // nothing to merge into

    SectionTimeline r = fixture();
    const double introStart = r.sections[0].startSeconds;
    REQUIRE(removeSection(r, 0));
    CHECK(r.sections.size() == 5);
    CHECK(r.sections[0].type == "verse");
    CHECK(r.sections[0].startSeconds == introStart); // the span went to the neighbour
    REQUIRE(r.validate().has_value());

    // A timeline of nothing is not a timeline.
    SectionTimeline one;
    one.durationSeconds = 10.0;
    one.sections.push_back(made("intro", 0.0, 10.0));
    CHECK(!removeSection(one, 0));
    CHECK(one.sections.size() == 1);
}

TEST_CASE("An authored section takes its span from what was there", "[song][timeline][edit]") {
    SectionTimeline t = fixture();
    const ShotLanguage language;
    // Landing inside one section, which must become two.
    const auto index = insertSection(t, "phrase", 90.0, 110.0, language);
    REQUIRE(index.has_value());
    REQUIRE(t.validate().has_value());
    const Section& made = t.sections[*index];
    CHECK(made.type == "phrase");
    CHECK(made.startSeconds == 90.0);
    CHECK(made.endSeconds == 110.0);
    CHECK(made.authored);
    CHECK(made.spanIsFrozen());
    // The verse it landed in is now two verses either side of it.
    CHECK(t.sections[*index - 1].type == "verse");
    CHECK(t.sections[*index - 1].endSeconds == 90.0);
    CHECK(t.sections[*index + 1].type == "verse");
    CHECK(t.sections[*index + 1].startSeconds == 110.0);

    // A type nobody defined, an inverted span and a sliver are all refused.
    CHECK(!insertSection(t, "not_a_type", 10.0, 20.0, language).has_value());
    CHECK(!insertSection(t, "phrase", 40.0, 30.0, language).has_value());
    CHECK(!insertSection(t, "phrase", 40.0, 40.05, language).has_value());
    CHECK(!insertSection(t, "phrase", 170.0, 500.0, language).has_value());
}

TEST_CASE("Changing the type moves the treatment with it, unless a person chose one",
          "[song][timeline][edit][override]") {
    SectionTimeline t = fixture();
    const ShotLanguage language;

    // Un-overridden: the treatment follows the type. No rule implements this -- the section stores
    // no intent at all, so it resolves through whatever type it currently has.
    CHECK(language.intentFor(t.sections[1]).id == "hero_coverage");
    REQUIRE(setSectionType(t, 1, "breakdown", language));
    CHECK(t.sections[1].isEdited(SectionField::Type));
    CHECK(language.intentFor(t.sections[1]).id == "intimate_restrained");
    CHECK(language.resolutionOf(t.sections[1]) == ShotLanguage::Resolution::TypeDefault);

    // Overridden: the person's choice outranks the type, and survives a later retype.
    REQUIRE(setSectionShotIntent(t, 1, "floating_unconventional", language));
    CHECK(t.sections[1].isEdited(SectionField::ShotIntent));
    CHECK(language.intentFor(t.sections[1]).id == "floating_unconventional");
    CHECK(language.resolutionOf(t.sections[1]) == ShotLanguage::Resolution::Override);
    REQUIRE(setSectionType(t, 1, "chorus", language));
    CHECK(language.intentFor(t.sections[1]).id == "floating_unconventional");

    // Dropping the override goes back to the type's default and un-marks the field.
    REQUIRE(clearSectionShotIntent(t, 1));
    CHECK(!t.sections[1].isEdited(SectionField::ShotIntent));
    CHECK(language.intentFor(t.sections[1]).id == "dynamic_hero_coverage");

    // The brief's own example: a person who wants Intro treated as an extreme close-up can have it,
    // because there is no semantic rule saying an intro is wide.
    REQUIRE(setSectionShotIntent(t, 0, "intimate_close_up", language));
    CHECK(language.intentFor(t.sections[0]).id == "intimate_close_up");

    // Things that do not exist are refused rather than stored.
    CHECK(!setSectionType(t, 0, "not_a_type", language));
    CHECK(!setSectionShotIntent(t, 0, "not_an_intent", language));
    CHECK(!setSectionType(t, 99, "verse", language));
}

TEST_CASE("Renaming back to nothing undoes the rename", "[song][timeline][edit]") {
    SectionTimeline t = fixture();
    const ShotLanguage language;
    REQUIRE(setSectionLabel(t, 3, "The big one"));
    CHECK(t.sections[3].isEdited(SectionField::Label));
    CHECK(language.displayName(t.sections[3]) == "The big one");

    REQUIRE(setSectionLabel(t, 3, ""));
    CHECK(!t.sections[3].isEdited(SectionField::Label));
    CHECK(t.sections[3].origin() == SectionOrigin::Detected);
    CHECK(language.displayName(t.sections[3]) == "Chorus");
}

// ---- persistence ------------------------------------------------------------------------------------

TEST_CASE("A timeline round-trips through JSON without losing a bit of any boundary",
          "[song][timeline][json]") {
    SectionTimeline t = fixture();
    const ShotLanguage language;
    REQUIRE(setSectionType(t, 4, "bridge", language));
    REQUIRE(setSectionLabel(t, 4, "The departure"));
    REQUIRE(setSectionShotIntent(t, 4, "floating_unconventional", language));
    REQUIRE(moveBoundary(t, 2, 44.918273645).has_value());

    const auto restored = song::sectionTimelineFromJson(song::sectionTimelineToJson(t));
    REQUIRE(restored.has_value());
    REQUIRE(restored->sections.size() == t.sections.size());
    CHECK(restored->durationSeconds == t.durationSeconds);
    for (std::size_t i = 0; i < t.sections.size(); ++i) {
        INFO("section " << i);
        const Section& a = t.sections[i];
        const Section& b = restored->sections[i];
        CHECK(b.startSeconds == a.startSeconds); // exactly
        CHECK(b.endSeconds == a.endSeconds);
        CHECK(b.type == a.type);
        CHECK(b.label == a.label);
        CHECK(b.shotIntent == a.shotIntent);
        CHECK(b.edited == a.edited);
        CHECK(b.authored == a.authored);
        CHECK(b.origin() == a.origin());
        CHECK(b.occurrence == a.occurrence); // rebuilt on load, not stored
    }
    CHECK(restored->sections[4].label == "The departure");
    CHECK(restored->sections[4].shotIntent == "floating_unconventional");
    CHECK(restored->sections[1].isEdited(SectionField::End));
    CHECK(restored->sections[2].isEdited(SectionField::Start));
}

TEST_CASE("A person's edit makes the detector's confidence meaningless, and unwritten",
          "[song][timeline][json]") {
    SectionTimeline t = fixture();
    const ShotLanguage language;
    REQUIRE(setSectionType(t, 1, "ambient", language));
    REQUIRE(moveBoundary(t, 3, 60.0).has_value());

    const nlohmann::json doc = song::sectionTimelineToJson(t);
    const auto& sections = doc.at("sections");
    // Untouched: the detector's record is written.
    CHECK(sections[0].contains("labelConfidence"));
    CHECK(sections[0].contains("startConfidence"));
    // Retyped: the label claim is retired, the boundary claim is not. Per-field provenance, visible
    // in the file.
    CHECK(!sections[1].contains("labelConfidence"));
    CHECK(sections[1].contains("startConfidence"));
    // Boundary moved: the boundary claim is retired.
    CHECK(!sections[3].contains("startConfidence"));
    CHECK(!sections[3].contains("endConfidence"));
    // `occurrence` is derived and is never written.
    CHECK(!sections[0].contains("occurrence"));
}

TEST_CASE("A malformed timeline is an error, not a crash", "[song][timeline][json]") {
    CHECK(!song::sectionTimelineFromJson(nlohmann::json::array()).has_value());
    // No sections at all is empty, not malformed.
    CHECK(song::sectionTimelineFromJson(nlohmann::json::object()).has_value());

    nlohmann::json bad;
    bad["sections"] = nlohmann::json::array();
    bad["sections"].push_back({{"start", 0.0}, {"end", 10.0}}); // no type
    CHECK(!song::sectionTimelineFromJson(bad).has_value());

    nlohmann::json overlapping;
    overlapping["duration"] = 20.0;
    overlapping["sections"] = nlohmann::json::array();
    overlapping["sections"].push_back({{"type", "intro"}, {"start", 0.0}, {"end", 12.0}});
    overlapping["sections"].push_back({{"type", "verse"}, {"start", 10.0}, {"end", 20.0}});
    CHECK(!song::sectionTimelineFromJson(overlapping).has_value());

    nlohmann::json unknownField;
    unknownField["sections"] = nlohmann::json::array();
    unknownField["sections"].push_back(
        {{"type", "intro"}, {"start", 0.0}, {"end", 10.0}, {"edited", {"colour"}}});
    CHECK(!song::sectionTimelineFromJson(unknownField).has_value());
}

// ---- from the analyzer --------------------------------------------------------------------------------

TEST_CASE("Every detected function maps to a defined section type", "[song][timeline][analysis]") {
    const ShotLanguage language;
    for (const analysis::SectionFunction f : analysis::allSectionFunctions()) {
        INFO("function " << analysis::sectionFunctionName(f));
        const auto type = song::sectionTypeForFunction(f);
        CHECK(!type.empty());
        REQUIRE(language.hasType(type));
        // And every one of them therefore lands on a real treatment.
        Section s;
        s.type = type;
        CHECK(language.intentFor(s).validate().has_value());
    }
    // "I do not know" becomes "an ordinary passage", once, here.
    CHECK(song::sectionTypeForFunction(analysis::SectionFunction::Other) ==
          song::neutralSectionTypeId());
    // "Final" is a property of position, not of the vocabulary.
    CHECK(song::sectionTypeForFunction(analysis::SectionFunction::FinalChorus) == "chorus");
    CHECK(song::sectionTypeForFunction(analysis::SectionFunction::Chorus) == "chorus");
}

TEST_CASE("An analysis becomes a complete first-pass treatment", "[song][timeline][analysis]") {
    analysis::SongStructure structure;
    structure.durationSeconds = 100.5;
    structure.sections.push_back(detectedSection(0.0, 18.25, analysis::SectionFunction::Intro));
    structure.sections.push_back(detectedSection(18.25, 43.75, analysis::SectionFunction::Verse));
    structure.sections.push_back(detectedSection(43.75, 70.5, analysis::SectionFunction::Chorus));
    structure.sections.push_back(detectedSection(70.5, 100.5, analysis::SectionFunction::FinalChorus));
    REQUIRE(structure.validate().has_value());

    const ShotLanguage language;
    const SectionTimeline t = song::timelineFromStructure(structure, language);
    REQUIRE(t.validate().has_value());
    REQUIRE(t.sections.size() == 4);
    CHECK(t.durationSeconds == structure.durationSeconds);

    // The brief's section 7: after analysis the person immediately has a usable sequence, and the
    // shot boundaries are the section boundaries, exactly.
    for (std::size_t i = 0; i < t.sections.size(); ++i) {
        INFO("section " << i);
        CHECK(t.sections[i].startSeconds == structure.sections[i].startSeconds);
        CHECK(t.sections[i].endSeconds == structure.sections[i].endSeconds);
        CHECK(t.sections[i].origin() == SectionOrigin::Detected);
        CHECK(!t.sections[i].shotIntent.has_value()); // resolves through its type
        CHECK(language.intentFor(t.sections[i]).validate().has_value());
        // Measurements are carried rather than recomputed: analysis is an offline job.
        CHECK(t.sections[i].energy == structure.sections[i].energy);
        CHECK(t.sections[i].density == structure.sections[i].density);
    }
    CHECK(language.intentFor(t.sections[0]).id == "atmospheric_establishing");
    CHECK(language.intentFor(t.sections[1]).id == "hero_coverage");
    CHECK(language.intentFor(t.sections[2]).id == "dynamic_hero_coverage");

    // The detector's own label is NOT a person's name for the passage. Copying it would mark every
    // section as renamed and freeze the whole timeline against the next re-analysis.
    for (const Section& s : t.sections) {
        CHECK(s.label.empty());
        CHECK(s.edited == SectionField::None);
    }
    // Both choruses are one type, so the last is the second occurrence.
    CHECK(t.countOfType("chorus") == 2);
    CHECK(t.sections[3].occurrence == 1);
    CHECK(language.displayName(t.sections[3]) == "Chorus 2");
}

// ---- the director's view ------------------------------------------------------------------------------

TEST_CASE("A cue carries a treatment and no way to find out what kind of section it is",
          "[song][cue]") {
    const SectionTimeline t = fixture();
    const ShotLanguage language;
    const auto cues = song::cueSheet(t, language);
    REQUIRE(cues.size() == t.sections.size());

    for (std::size_t i = 0; i < cues.size(); ++i) {
        INFO("cue " << i);
        const SectionCue& c = cues[i];
        CHECK(c.index == static_cast<int>(i));
        CHECK(c.startSeconds == t.sections[i].startSeconds); // exactly: shots align to sections
        CHECK(c.endSeconds == t.sections[i].endSeconds);
        
        CHECK(c.intent.validate().has_value());
        CHECK(!c.displayName.empty());
    }
    CHECK(cues[3].intent.id == "dynamic_hero_coverage");

    // "The last chorus is a different shot from the first", derived from position rather than from
    // a fourth enumerator somebody had to add.
    CHECK(!cues[1].finalOfKind); // verse 1
    CHECK(cues[4].finalOfKind);  // verse 2, the last verse
    CHECK(!cues[3].finalOfKind);
    CHECK(cues[5].finalOfKind);
    CHECK(cues[0].finalOfKind); // the only intro

    // Lookup, and the last cue owning its own end.
    CHECK(song::cueAt(cues, 60.0) == &cues[3]);
    CHECK(song::cueAt(cues, 180.372918) == &cues.back());
    CHECK(song::cueAt(cues, -1.0) == nullptr);
}

TEST_CASE("A cue's treatment travels across the section", "[song][cue]") {
    SectionTimeline t;
    t.durationSeconds = 40.0;
    t.sections.push_back(made("build", 0.0, 20.0));
    t.sections.push_back(made("pause", 20.0, 40.0));
    t.renumber();
    const ShotLanguage language;
    const auto cues = song::cueSheet(t, language);

    const SectionCue& build = cues[0];
    CHECK(build.progressAt(0.0) == Catch::Approx(0.0f));
    CHECK(build.progressAt(10.0) == Catch::Approx(0.5f));
    CHECK(build.progressAt(20.0) == Catch::Approx(1.0f));
    CHECK(build.progressAt(-5.0) == Catch::Approx(0.0f)); // clamped, not extrapolated
    CHECK(build.progressAt(999.0) == Catch::Approx(1.0f));

    // The brief's "Build -> progressively increase movement", with no rule about builds anywhere:
    // the type named a treatment, the treatment named a shape, and the shape did this.
    CHECK(build.intentAt(1.0).movement < build.intentAt(19.0).movement);
    CHECK(build.intentAt(19.0).movement == Catch::Approx(build.intent.movement).margin(0.02));

    // And "Pause -> hold or suspend", by the same route.
    CHECK(cues[1].intentAt(30.0).movement <= 0.1f);
    CHECK(cues[1].intentAt(30.0).cutFrequency <= 0.1f);
}

TEST_CASE("A custom section reaches the director exactly as a built-in one does",
          "[song][cue][custom]") {
    // The architectural claim, stated as a test: from the director's side, `ocean_ambience` and
    // `chorus` are the same kind of thing. The only observable difference is a display string.
    ShotLanguage language;
    REQUIRE(language
                .defineType(SectionType{"ocean_ambience", "Ocean Ambience",
                                        "Slow underwater environment passage",
                                        SectionCategory::Custom,
                                        "slow_environmental_exploration", false})
                .has_value());

    SectionTimeline t;
    t.durationSeconds = 60.0;
    t.sections.push_back(made("chorus", 0.0, 20.0));
    t.sections.push_back(made("ocean_ambience", 20.0, 40.0));
    t.sections.push_back(made("ocean_ambience", 40.0, 60.0));
    t.sections[1].authored = true;
    t.sections[2].authored = true;
    t.renumber();

    const auto cues = song::cueSheet(t, language);
    REQUIRE(cues.size() == 3);
    for (const SectionCue& c : cues) {
        
        CHECK(c.intent.validate().has_value());
    }
    CHECK(cues[1].intent.id == "slow_environmental_exploration");
    // Occurrence numbering, display naming and finalOfKind all work on a type the engine has never
    // heard of -- none of them consults the built-in list.
    CHECK(cues[1].occurrence == 0);
    CHECK(cues[2].occurrence == 1);
    CHECK(cues[2].displayName == "Ocean Ambience 2");
    CHECK(!cues[1].finalOfKind);
    CHECK(cues[2].finalOfKind);
}

TEST_CASE("A section naming a type nobody defined still gets a treatment", "[song][cue]") {
    // A project that removed a custom definition must not produce sections with no treatment and no
    // explanation. It falls back, and says that it fell back.
    SectionTimeline t;
    t.durationSeconds = 10.0;
    t.sections.push_back(made("a_type_that_went_away", 0.0, 10.0));
    t.renumber();
    const ShotLanguage language;
    CHECK(language.resolutionOf(t.sections[0]) == ShotLanguage::Resolution::MissingType);
    CHECK(language.intentFor(t.sections[0]).id == song::neutralShotIntentId());
    // And the name reads as the raw id rather than as nothing, because a wrong name is diagnosable.
    CHECK(language.displayName(t.sections[0]) == "a_type_that_went_away");

    const auto cues = song::cueSheet(t, language);
    REQUIRE(cues.size() == 1);
}

TEST_CASE("A cue sheet survives the language changing underneath it", "[song][cue]") {
    // `SectionCue::intent` is a value rather than a pointer into the registry, and this is why.
    // A director holds a cue sheet across frames; a person defines a section type while it is
    // playing; the registry's vector reallocates. With a pointer that is a use-after-free that ASan
    // catches and a release build reads as a treatment that mysteriously changed.
    ShotLanguage language;
    ShotIntent tidal = *song::builtInShotIntent("slow_environmental_exploration");
    tidal.id = "tidal_drift";
    tidal.name = "Tidal Drift";
    REQUIRE(language.defineIntent(tidal).has_value());
    REQUIRE(language
                .defineType(SectionType{"ocean_ambience", "Ocean Ambience", "",
                                        SectionCategory::Custom, "tidal_drift", false})
                .has_value());

    SectionTimeline t;
    t.durationSeconds = 20.0;
    t.sections.push_back(made("ocean_ambience", 0.0, 20.0));
    t.renumber();

    const auto cues = song::cueSheet(t, language);
    REQUIRE(cues.size() == 1);
    CHECK(cues[0].intent.id == "tidal_drift");

    // Enough definitions to force the registry's storage to move several times over.
    for (int i = 0; i < 64; ++i) {
        ShotIntent filler = tidal;
        filler.id = "filler_" + std::to_string(i);
        filler.name = "Filler";
        REQUIRE(language.defineIntent(filler).has_value());
    }

    // The cue still says what it said, and is still usable.
    CHECK(cues[0].intent.id == "tidal_drift");
    CHECK(cues[0].intent.name == "Tidal Drift");
    CHECK(cues[0].intent.validate().has_value());
    CHECK(cues[0].intentAt(10.0).validate().has_value());
    CHECK(cues[0].displayName == "Ocean Ambience");
}
