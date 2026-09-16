// Re-analysis (ADR-247, the brief's section 18): what happens to a person's work when they press
// Analyze again.
//
// This is the file the feature lives or dies by, and it is written to fail if it regresses. "We kept
// your edits" and "we replaced the analysis" each pass half of any careless test, so every case here
// asserts **both** halves: something a person decided is still exactly where they put it, *and*
// something the analyzer guessed has been replaced by the new guess. A test that only checked the
// first would pass against an implementation that ignored the fresh detection entirely.
//
// Boundary times are compared bit for bit. A tolerance would pass against an implementation that
// recomputed a boundary "close enough", which is precisely the drift worth catching.

#include "song/reanalysis.hpp"
#include "song/section_timeline.hpp"
#include "song/shot_language.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace avgen;
using song::ReanalysisPolicy;
using song::ReanalysisReport;
using song::Section;
using song::SectionField;
using song::SectionOrigin;
using song::SectionTimeline;
using song::SectionType;
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

// What the detector produced the first time.
SectionTimeline original() {
    SectionTimeline t;
    t.durationSeconds = 160.0;
    t.sections.push_back(made("intro", 0.0, 20.0));
    t.sections.push_back(made("verse", 20.0, 50.0));
    t.sections.push_back(made("chorus", 50.0, 80.0));
    t.sections.push_back(made("verse", 80.0, 110.0));
    t.sections.push_back(made("bridge", 110.0, 130.0));
    t.sections.push_back(made("chorus", 130.0, 160.0));
    t.renumber();
    return t;
}

// What it produced the second time: completely different boundaries and completely different
// conclusions, so "kept everything" cannot be mistaken for "the detection happened to agree".
SectionTimeline fresh() {
    SectionTimeline t;
    t.durationSeconds = 160.0;
    t.sections.push_back(made("intro", 0.0, 14.5));
    t.sections.push_back(made("pre_chorus", 14.5, 46.25));
    t.sections.push_back(made("drop", 46.25, 92.75));
    t.sections.push_back(made("breakdown", 92.75, 133.5));
    t.sections.push_back(made("outro", 133.5, 160.0));
    t.renumber();
    return t;
}

bool hasSectionSpanning(const SectionTimeline& t, double start, double end) {
    for (const Section& s : t.sections) {
        if (s.startSeconds == start && s.endSeconds == end) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("A re-analysis that found nothing changes nothing", "[song][reanalysis]") {
    // Refusing to act on empty information is much better than emptying somebody's timeline because
    // the detector had a bad day.
    SectionTimeline current = original();
    const SectionTimeline before = current;
    const ReanalysisReport report = song::reanalyze(current, SectionTimeline{});
    CHECK(current == before);
    CHECK(!report.changedAnything());
    CHECK(report.summary() == "Re-analysis found nothing to do.");
}

TEST_CASE("Re-analysis of an untouched timeline simply replaces it", "[song][reanalysis]") {
    SectionTimeline current = original();
    const SectionTimeline replacement = fresh();
    const ReanalysisReport report = song::reanalyze(current, replacement);
    CHECK(current.sections.size() == replacement.sections.size());
    for (std::size_t i = 0; i < current.sections.size(); ++i) {
        CHECK(current.sections[i].type == replacement.sections[i].type);
        CHECK(current.sections[i].startSeconds == replacement.sections[i].startSeconds);
        CHECK(current.sections[i].endSeconds == replacement.sections[i].endSeconds);
    }
    CHECK(report.sectionsReplaced == 5);
    CHECK(report.spansFrozen == 0);
    CHECK(report.fieldsCarried == 0);
    CHECK(report.discarded == 0);
    REQUIRE(current.validate().has_value());
}

TEST_CASE("A moved boundary survives exactly, and an unmoved one does not",
          "[song][reanalysis]") {
    SectionTimeline current = original();
    const double moved = 47.918273645;
    REQUIRE(song::moveBoundary(current, 2, moved).has_value());
    // Both sections either side are now span-frozen, so both keep their exact times.
    const double verseStart = current.sections[1].startSeconds;
    const double chorusEnd = current.sections[2].endSeconds;

    const ReanalysisReport report = song::reanalyze(current, fresh());
    REQUIRE(current.validate().has_value());

    // Half one: the person's boundary is exactly where they put it. Bit for bit.
    CHECK(hasSectionSpanning(current, verseStart, moved));
    CHECK(hasSectionSpanning(current, moved, chorusEnd));
    CHECK(report.spansFrozen == 2);

    // Half two: material the analyzer had guessed on both sides really was replaced. The fresh
    // detection said "outro" from 133.5, and the old timeline said "chorus" from 130.
    CHECK(current.at(140.0) != nullptr);
    CHECK(current.at(140.0)->type == "outro");
    CHECK(report.sectionsReplaced > 0);
    CHECK(report.freshTrimmed + report.freshDropped > 0);
    CHECK(report.kept.size() == 2);
}

TEST_CASE("An authored section survives and the detection is cut around it",
          "[song][reanalysis]") {
    SectionTimeline current = original();
    ShotLanguage language;
    REQUIRE(language
                .defineType(SectionType{"ocean_ambience", "Ocean Ambience", "",
                                        song::SectionCategory::Custom,
                                        "slow_environmental_exploration", false})
                .has_value());
    // Right in the middle of what the fresh detection calls one long "drop".
    const auto index = song::insertSection(current, "ocean_ambience", 55.0, 75.0, language);
    REQUIRE(index.has_value());

    const ReanalysisReport report = song::reanalyze(current, fresh());
    REQUIRE(current.validate().has_value());

    CHECK(hasSectionSpanning(current, 55.0, 75.0));
    const Section* kept = current.at(60.0);
    REQUIRE(kept != nullptr);
    CHECK(kept->type == "ocean_ambience");
    CHECK(kept->authored);
    CHECK(kept->origin() == SectionOrigin::Authored);
    CHECK(report.spansFrozen == 1);

    // The fresh "drop" that straddled it became two pieces either side, rather than being dropped.
    CHECK(current.at(50.0) != nullptr);
    CHECK(current.at(50.0)->type == "drop");
    CHECK(current.at(80.0) != nullptr);
    CHECK(current.at(80.0)->type == "drop");
    CHECK(report.freshTrimmed >= 1);
}

TEST_CASE("A retyped section keeps its type and takes the better boundaries",
          "[song][reanalysis][override]") {
    // The improvement over ADR-215's per-section model, and the reason this model has per-field
    // provenance at all: renaming a passage is not a claim about where the passage starts.
    SectionTimeline current = original();
    ShotLanguage language;
    REQUIRE(language
                .defineType(SectionType{"ocean_ambience", "Ocean Ambience", "",
                                        song::SectionCategory::Custom,
                                        "slow_environmental_exploration", false})
                .has_value());
    // Verse 2 spans 80..110. Change what it is, and nothing else.
    REQUIRE(song::setSectionType(current, 3, "ocean_ambience", language));
    CHECK(!current.sections[3].spanIsFrozen());

    const ReanalysisReport report = song::reanalyze(current, fresh());
    REQUIRE(current.validate().has_value());

    // Half one: the type a person chose is still on the timeline.
    const Section* retyped = current.at(95.0);
    REQUIRE(retyped != nullptr);
    CHECK(retyped->type == "ocean_ambience");
    CHECK(retyped->isEdited(SectionField::Type));
    CHECK(retyped->origin() == SectionOrigin::Refined);
    CHECK(report.fieldsCarried == 1);
    CHECK(report.sectionsCarried == 1);

    // Half two: it is on the *fresh* boundaries, not the old ones. The fresh detection put a
    // boundary at 92.75 and the old one at 80, and 92.75 is what survived -- which is exactly what
    // a per-section freeze would have prevented.
    CHECK(retyped->startSeconds == 92.75);
    CHECK(retyped->endSeconds == 133.5);
    CHECK(!hasSectionSpanning(current, 80.0, 110.0));
    CHECK(report.spansFrozen == 0);

    // And the treatment follows the carried type, since nobody overrode it.
    CHECK(language.intentFor(*retyped).id == "slow_environmental_exploration");
}

TEST_CASE("The brief's own worked example", "[song][reanalysis][override]") {
    // "A person analyzes a song, gets sections, changes Verse 2 to Ocean Ambience, changes its shot,
    // moves a boundary, and presses Analyze again. The system must not casually destroy their work."
    SectionTimeline current = original();
    ShotLanguage language;
    REQUIRE(language
                .defineType(SectionType{"ocean_ambience", "Ocean Ambience", "",
                                        song::SectionCategory::Custom,
                                        "slow_environmental_exploration", false})
                .has_value());

    REQUIRE(song::setSectionType(current, 3, "ocean_ambience", language));         // retype
    REQUIRE(song::setSectionShotIntent(current, 3, "floating_unconventional",      // reshoot
                                       language));
    REQUIRE(song::setSectionLabel(current, 3, "Underwater"));                      // rename
    const double draggedTo = 18.918273645;
    REQUIRE(song::moveBoundary(current, 1, draggedTo).has_value());                // drag

    const ReanalysisReport report = song::reanalyze(current, fresh());
    REQUIRE(current.validate().has_value());

    // The dragged boundary is exactly where it was put.
    CHECK(hasSectionSpanning(current, 0.0, draggedTo));
    CHECK(current.sections[0].endSeconds == draggedTo);

    // The retyped section kept all three of its decisions.
    const Section* underwater = nullptr;
    for (const Section& s : current.sections) {
        if (s.type == "ocean_ambience") {
            underwater = &s;
        }
    }
    REQUIRE(underwater != nullptr);
    CHECK(underwater->label == "Underwater");
    REQUIRE(underwater->shotIntent.has_value());
    CHECK(*underwater->shotIntent == "floating_unconventional");
    CHECK(language.intentFor(*underwater).id == "floating_unconventional");
    CHECK(report.fieldsCarried == 3);
    CHECK(report.sectionsCarried == 1);
    CHECK(report.discarded == 0);

    // And the rest of the piece really was re-detected: the fresh outro is there and the old final
    // chorus is not.
    REQUIRE(current.at(150.0) != nullptr);
    CHECK(current.at(150.0)->type == "outro");
    CHECK(report.sectionsReplaced > 0);

    // The report is legible rather than a bare count.
    CHECK(report.summary().find("kept") != std::string::npos);
    CHECK(!report.kept.empty());

    // And the whole thing survives another round trip, which is what makes it repeatable.
    //
    // Compared field by field rather than with `==`, because a whole-Section comparison would also
    // compare the detector's confidences -- and those are deliberately *not* written once a person
    // has edited the field they were a claim about (ADR-215). An edited section coming back with a
    // zeroed confidence is the design, not a loss.
    const auto stored = song::sectionTimelineFromJson(song::sectionTimelineToJson(current));
    REQUIRE(stored.has_value());
    REQUIRE(stored->sections.size() == current.sections.size());
    CHECK(stored->durationSeconds == current.durationSeconds);
    for (std::size_t i = 0; i < current.sections.size(); ++i) {
        INFO("section " << i);
        const Section& a = current.sections[i];
        const Section& b = stored->sections[i];
        CHECK(b.startSeconds == a.startSeconds); // exactly
        CHECK(b.endSeconds == a.endSeconds);
        CHECK(b.type == a.type);
        CHECK(b.label == a.label);
        CHECK(b.shotIntent == a.shotIntent);
        CHECK(b.edited == a.edited);
        CHECK(b.authored == a.authored);
        CHECK(b.occurrence == a.occurrence);
    }
}

TEST_CASE("Two edited sections cannot both write to one fresh section", "[song][reanalysis]") {
    // A coarser fresh detection merges two old sections into one. Without a cap, both would carry
    // their fields onto it and the second would silently win -- data loss wearing the costume of a
    // successful merge.
    SectionTimeline current;
    current.durationSeconds = 100.0;
    current.sections.push_back(made("verse", 0.0, 40.0));
    current.sections.push_back(made("chorus", 40.0, 80.0));
    current.sections.push_back(made("outro", 80.0, 100.0));
    current.renumber();
    const ShotLanguage language;
    REQUIRE(song::setSectionType(current, 0, "ambient", language));
    REQUIRE(song::setSectionType(current, 1, "drone", language));

    SectionTimeline coarse;
    coarse.durationSeconds = 100.0;
    coarse.sections.push_back(made("phrase", 0.0, 80.0)); // one section where there were two
    coarse.sections.push_back(made("outro", 80.0, 100.0));
    coarse.renumber();

    const ReanalysisReport report = song::reanalyze(current, coarse);
    REQUIRE(current.validate().has_value());
    // Exactly one of the two edits landed, and the other is *reported* rather than vanishing.
    CHECK(report.sectionsCarried == 1);
    CHECK(report.fieldsCarried == 1);
    CHECK(report.discarded == 1);
    CHECK(report.summary().find("discarded") != std::string::npos);
    // The winner is the larger overlap, and the two sections overlap the merged one equally, so the
    // earlier index wins -- deterministic either way, which is what matters.
    CHECK(current.sections[0].type == "ambient");
}

TEST_CASE("An edit the fresh detection has nothing under is reported, not hidden",
          "[song][reanalysis]") {
    SectionTimeline current = original();
    const ShotLanguage language;
    REQUIRE(song::setSectionType(current, 5, "celebration", language)); // 130..160

    SectionTimeline shorter;
    shorter.durationSeconds = 100.0;
    shorter.sections.push_back(made("intro", 0.0, 50.0));
    shorter.sections.push_back(made("verse", 50.0, 100.0));
    shorter.renumber();

    const ReanalysisReport report = song::reanalyze(current, shorter);
    REQUIRE(current.validate().has_value());
    CHECK(report.discarded == 1);
    CHECK(report.fieldsCarried == 0);
}

TEST_CASE("Replace throws the timeline away, and says how much that cost",
          "[song][reanalysis]") {
    SectionTimeline current = original();
    const ShotLanguage language;
    REQUIRE(song::setSectionType(current, 1, "ambient", language));
    REQUIRE(song::moveBoundary(current, 4, 111.5).has_value());

    const SectionTimeline replacement = fresh();
    // The preview is the same call against a copy, so it cannot come to disagree with the action.
    const ReanalysisReport predicted =
        song::previewReanalysis(current, replacement, ReanalysisPolicy::Replace);
    const SectionTimeline untouched = current;
    CHECK(current == untouched); // a preview touches nothing

    const ReanalysisReport actual =
        song::reanalyze(current, replacement, ReanalysisPolicy::Replace);
    CHECK(actual.discarded == predicted.discarded);
    CHECK(actual.sectionsReplaced == predicted.sectionsReplaced);
    // Three sections carried an edit: the retyped one, and the two either side of the drag.
    CHECK(actual.discarded == 3);
    CHECK(actual.sectionsReplaced == 5);
    for (std::size_t i = 0; i < current.sections.size(); ++i) {
        CHECK(current.sections[i].type == replacement.sections[i].type);
        CHECK(current.sections[i].startSeconds == replacement.sections[i].startSeconds);
    }
    CHECK(std::string(song::reanalysisPolicyName(ReanalysisPolicy::Replace)) == "replace");
}

TEST_CASE("A preview predicts a merge exactly", "[song][reanalysis]") {
    SectionTimeline current = original();
    const ShotLanguage language;
    REQUIRE(song::setSectionType(current, 3, "ambient", language));
    REQUIRE(song::moveBoundary(current, 1, 21.5).has_value());

    const SectionTimeline replacement = fresh();
    const ReanalysisReport predicted = song::previewReanalysis(current, replacement);
    const SectionTimeline before = current;
    CHECK(current == before);

    const ReanalysisReport actual = song::reanalyze(current, replacement);
    CHECK(actual.sectionsReplaced == predicted.sectionsReplaced);
    CHECK(actual.spansFrozen == predicted.spansFrozen);
    CHECK(actual.fieldsCarried == predicted.fieldsCarried);
    CHECK(actual.discarded == predicted.discarded);
    CHECK(actual.kept == predicted.kept);
}

TEST_CASE("Re-analysis is idempotent and repeatable", "[song][reanalysis]") {
    // Pressing the button twice is safe: the second run has nothing left to change, because the
    // first run's result is already the fresh detection with the edits folded in.
    SectionTimeline current = original();
    const ShotLanguage language;
    REQUIRE(song::setSectionType(current, 3, "ambient", language));
    REQUIRE(song::moveBoundary(current, 1, 21.5).has_value());

    song::reanalyze(current, fresh());
    const SectionTimeline once = current;
    REQUIRE(once.validate().has_value());

    song::reanalyze(current, fresh());
    REQUIRE(current.validate().has_value());
    // The spans and the decisions are identical the second time round.
    REQUIRE(current.sections.size() == once.sections.size());
    for (std::size_t i = 0; i < current.sections.size(); ++i) {
        INFO("section " << i);
        CHECK(current.sections[i].startSeconds == once.sections[i].startSeconds);
        CHECK(current.sections[i].endSeconds == once.sections[i].endSeconds);
        CHECK(current.sections[i].type == once.sections[i].type);
        CHECK(current.sections[i].shotIntent == once.sections[i].shotIntent);
    }
}

TEST_CASE("Whatever a re-analysis does, the result is still a timeline", "[song][reanalysis]") {
    // The invariant that makes everything downstream safe, checked against every combination of
    // edits rather than against one lucky arrangement.
    const ShotLanguage language;
    for (int mask = 0; mask < 16; ++mask) {
        INFO("edit mask " << mask);
        SectionTimeline current = original();
        if ((mask & 1) != 0) {
            REQUIRE(song::setSectionType(current, 1, "ambient", language));
        }
        if ((mask & 2) != 0) {
            REQUIRE(song::moveBoundary(current, 2, 52.25).has_value());
        }
        if ((mask & 4) != 0) {
            REQUIRE(song::splitSection(current, 95.0).has_value());
        }
        if ((mask & 8) != 0) {
            REQUIRE(song::setSectionShotIntent(current, 0, "intimate_close_up", language));
        }
        song::reanalyze(current, fresh());
        REQUIRE(current.validate().has_value());
        CHECK(!current.sections.empty());
        CHECK(current.sections.front().startSeconds == Catch::Approx(0.0));
        CHECK(current.sections.back().endSeconds == Catch::Approx(160.0));
        // Occurrence numbering is rebuilt, never stale.
        SectionTimeline renumbered = current;
        renumbered.renumber();
        CHECK(renumbered == current);
    }
}
