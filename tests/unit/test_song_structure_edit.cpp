// The editable song structure (ADR-215): persistence, the re-analysis policy, the edits, and the
// relationship between the two section vocabularies.
//
// Nothing here runs the detector. ADR-206's tests do that, and they already established the rule
// this file obeys as well: detection quality on real music is *reported*, never asserted. What is
// asserted here is the thing that has a right answer -- that a boundary a person moved survives a
// re-analysis and a boundary the analyser guessed does not, which is a data-loss requirement and
// deserves a test that would fail if it regressed.

#include "analysis/structure.hpp"
#include "app/cinematic.hpp"
#include "seq/sequence.hpp"
#include "seq/song_structure.hpp"
#include "signals/musical_events.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <set>
#include <string>

using namespace avgen;
using analysis::SectionFunction;
using analysis::SectionOrigin;
using analysis::SongSection;
using analysis::SongStructure;
using signals::MusicalSection;

namespace {

SongSection made(double start, double end, SectionFunction f,
                 SectionOrigin origin = SectionOrigin::Detected) {
    SongSection s;
    s.startSeconds = start;
    s.endSeconds = end;
    s.function = f;
    s.origin = origin;
    s.labelConfidence = 0.7f;
    s.startConfidence = 0.6f;
    s.endConfidence = 0.5f;
    s.energy = 0.4f;
    s.density = 0.3f;
    return s;
}

// Four sections, all detected, at times that are not whole numbers -- because a fixture whose
// boundaries are integers cannot catch a rounding bug, which is exactly the lesson ADR-206's
// precision test recorded.
SongStructure detected() {
    SongStructure s;
    s.durationSeconds = 120.372918;
    s.tempoBpm = 122.6f;
    s.tempoConfidence = 0.8f;
    s.sections.push_back(made(0.0, 30.145231, SectionFunction::Intro));
    s.sections.push_back(made(30.145231, 62.409117, SectionFunction::Verse));
    s.sections.push_back(made(62.409117, 95.781004, SectionFunction::Chorus));
    s.sections.push_back(made(95.781004, 120.372918, SectionFunction::Outro));
    return s;
}

} // namespace

// ---- persistence ---------------------------------------------------------------------------------

TEST_CASE("A song structure round-trips through JSON without losing a bit of any boundary",
          "[seq][structure]") {
    const SongStructure original = detected();
    const auto parsed = seq::songStructureFromJson(seq::songStructureToJson(original));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->sections.size() == original.sections.size());
    for (std::size_t i = 0; i < original.sections.size(); ++i) {
        // Bit for bit, not "within a millisecond". A boundary is a beat time and the whole point of
        // storing it as a double is that it comes back as the same double; a comparison with a
        // tolerance would pass against a serialiser that quietly rounded to three decimals.
        CHECK(parsed->sections[i].startSeconds == original.sections[i].startSeconds);
        CHECK(parsed->sections[i].endSeconds == original.sections[i].endSeconds);
        CHECK(parsed->sections[i].function == original.sections[i].function);
        CHECK(parsed->sections[i].origin == original.sections[i].origin);
    }
    CHECK(parsed->durationSeconds == original.durationSeconds);
}

TEST_CASE("A refined section's label and origin survive the project file", "[seq][structure]") {
    SongStructure s = detected();
    CHECK(seq::setSectionLabel(s, 2, "the big one"));
    const auto parsed = seq::songStructureFromJson(seq::songStructureToJson(s));
    REQUIRE(parsed.has_value());
    CHECK(parsed->sections[2].label == "the big one");
    CHECK(parsed->sections[2].origin == SectionOrigin::Refined);
    CHECK(seq::sectionDisplayName(parsed->sections[2]) == "the big one");
}

TEST_CASE("A sequence carries its structure through its own JSON", "[seq][structure]") {
    seq::Sequence piece;
    piece.name = "test";
    piece.structure = detected();
    CHECK(seq::moveBoundary(piece.structure, 2, 63.5).has_value());
    piece.refreshSectionMarkers();

    const auto reloaded = seq::Sequence::fromJson(piece.toJson());
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->structure.sections.size() == 4);
    CHECK(reloaded->structure.sections[2].startSeconds == 63.5);
    CHECK(reloaded->structure.sections[2].origin == SectionOrigin::Refined);
    // The markers are derived from the structure, so a reload rebuilds the same ones.
    CHECK(reloaded->markerTimes(seq::MarkerKind::Section).size() == 4);
}

// ---- the re-analysis policy (the brief's section 17) ---------------------------------------------

TEST_CASE("Re-analysis replaces what the analyser guessed and keeps what a person decided",
          "[seq][structure][reanalysis]") {
    SongStructure current = detected();
    // A person drags the boundary that ends the chorus from 1:35.781004 to 1:37.100000.
    //
    // *That* boundary rather than the one before it, because provenance is per section rather than
    // per boundary: dragging one boundary marks the two sections it separates, which protects their
    // outer boundaries as well. So the boundary this test expects to be replaced has to be one with
    // a detected section on both sides -- which is the boundary at 30.145231.
    const auto moved = seq::moveBoundary(current, 3, 97.1);
    REQUIRE(moved.has_value());
    REQUIRE(*moved == 97.1);
    REQUIRE(current.sections[2].origin == SectionOrigin::Refined);
    REQUIRE(current.sections[3].origin == SectionOrigin::Refined);
    REQUIRE(current.sections[0].origin == SectionOrigin::Detected);
    REQUIRE(current.sections[1].origin == SectionOrigin::Detected);
    const double detectedBoundaryBefore = current.sections[0].endSeconds;

    // The detector now has a different opinion about everything.
    SongStructure fresh;
    fresh.durationSeconds = current.durationSeconds;
    fresh.tempoBpm = 123.4f;
    fresh.sections.push_back(made(0.0, 17.2, SectionFunction::Intro));
    fresh.sections.push_back(made(17.2, 51.9, SectionFunction::Verse));
    fresh.sections.push_back(made(51.9, 88.3, SectionFunction::Drop));
    fresh.sections.push_back(made(88.3, 120.372918, SectionFunction::Outro));

    const seq::ReanalysisReport report = seq::reanalyse(current, fresh);

    CHECK(current.validate().has_value());
    // The refined boundary is still exactly where the person put it.
    const SongSection* chorus = nullptr;
    for (const SongSection& s : current.sections) {
        if (s.origin == SectionOrigin::Refined && s.function == SectionFunction::Chorus) {
            chorus = &s;
        }
    }
    REQUIRE(chorus != nullptr);
    CHECK(chorus->startSeconds == 62.409117);
    CHECK(chorus->endSeconds == 97.1);

    // ...and the detected one is gone: nothing in the merged structure ends where the analyser's
    // first pass said the intro ended.
    for (const SongSection& s : current.sections) {
        if (s.origin != SectionOrigin::Detected) {
            continue;
        }
        CHECK(s.endSeconds != detectedBoundaryBefore);
    }
    // The fresh intro boundary at 17.2 did survive, which is the other half of the same claim.
    bool sawFreshBoundary = false;
    for (const SongSection& s : current.sections) {
        sawFreshBoundary = sawFreshBoundary || s.endSeconds == 17.2;
    }
    CHECK(sawFreshBoundary);

    CHECK(report.kept() == 2); // the two sections the dragged boundary separates
    CHECK(report.refinedKept == 2);
    CHECK(report.authoredKept == 0);
    CHECK(report.keptSpans.size() == 2);
    CHECK(report.summary().find("kept 2") != std::string::npos);
    CHECK(current.tempoBpm == 123.4f); // the measurements are the fresh pass's
}

TEST_CASE("Re-analysis keeps an authored section and stays gapless around it",
          "[seq][structure][reanalysis]") {
    SongStructure current = detected();
    const auto added = seq::splitSection(current, 40.0);
    REQUIRE(added.has_value());
    REQUIRE(current.sections[*added].origin == SectionOrigin::Authored);
    REQUIRE(seq::setSectionLabel(current, *added, "the bit I like"));
    const double authoredStart = current.sections[*added].startSeconds;
    const double authoredEnd = current.sections[*added].endSeconds;

    SongStructure fresh;
    fresh.durationSeconds = current.durationSeconds;
    fresh.sections.push_back(made(0.0, 60.0, SectionFunction::Intro));
    fresh.sections.push_back(made(60.0, 120.372918, SectionFunction::Chorus));

    const seq::ReanalysisReport report = seq::reanalyse(current, fresh);
    CHECK(current.validate().has_value());
    CHECK(report.authoredKept == 1);
    CHECK(report.refinedKept == 1); // the left half of the split, whose end moved

    bool found = false;
    for (const SongSection& s : current.sections) {
        if (s.label != "the bit I like") {
            continue;
        }
        found = true;
        CHECK(s.startSeconds == authoredStart);
        CHECK(s.endSeconds == authoredEnd);
        CHECK(s.origin == SectionOrigin::Authored);
    }
    CHECK(found);
    // A fresh section that straddled the authored one was cut around it rather than dropped, so the
    // piece is still covered from nothing to its end.
    CHECK(current.sections.front().startSeconds == 0.0);
    CHECK(current.sections.back().endSeconds == 120.372918);
    CHECK(report.detectedTrimmed > 0);
}

TEST_CASE("A re-analysis that found nothing changes nothing", "[seq][structure][reanalysis]") {
    SongStructure current = detected();
    const SongStructure before = current;
    const seq::ReanalysisReport report = seq::reanalyse(current, SongStructure{});
    CHECK(report.detectedReplaced == 0);
    CHECK(report.kept() == 0);
    REQUIRE(current.sections.size() == before.sections.size());
    for (std::size_t i = 0; i < before.sections.size(); ++i) {
        CHECK(current.sections[i].startSeconds == before.sections[i].startSeconds);
        CHECK(current.sections[i].function == before.sections[i].function);
    }
}

TEST_CASE("Re-analysis of an untouched structure simply replaces it",
          "[seq][structure][reanalysis]") {
    SongStructure current = detected();
    SongStructure fresh;
    fresh.durationSeconds = 120.372918;
    fresh.sections.push_back(made(0.0, 44.4, SectionFunction::Intro));
    fresh.sections.push_back(made(44.4, 120.372918, SectionFunction::Chorus));

    const seq::ReanalysisReport report = seq::reanalyse(current, fresh);
    CHECK(report.kept() == 0);
    CHECK(report.detectedReplaced == 2);
    REQUIRE(current.sections.size() == 2);
    CHECK(current.sections[0].endSeconds == 44.4);
    CHECK(current.validate().has_value());
}

// ---- editing ---------------------------------------------------------------------------------

TEST_CASE("Every edit records who made it", "[seq][structure][edit]") {
    SECTION("moving a boundary marks both of the sections it separates") {
        SongStructure s = detected();
        CHECK(seq::moveBoundary(s, 1, 25.0).has_value());
        CHECK(s.sections[0].origin == SectionOrigin::Refined);
        CHECK(s.sections[1].origin == SectionOrigin::Refined);
        CHECK(s.sections[2].origin == SectionOrigin::Detected);
        CHECK(s.sections[0].endSeconds == s.sections[1].startSeconds);
        CHECK(s.validate().has_value());
    }
    SECTION("the track's own start and end are not boundaries") {
        SongStructure s = detected();
        CHECK_FALSE(seq::moveBoundary(s, 0, 5.0).has_value());
        CHECK_FALSE(seq::moveBoundary(s, s.sections.size(), 5.0).has_value());
    }
    SECTION("a boundary cannot be dragged past its neighbours") {
        SongStructure s = detected();
        const auto clamped = seq::moveBoundary(s, 1, -400.0, 0.25);
        REQUIRE(clamped.has_value());
        CHECK(*clamped == 0.25);
        CHECK(s.validate().has_value());
    }
    SECTION("retyping a section refines it") {
        SongStructure s = detected();
        CHECK(seq::setSectionFunction(s, 1, SectionFunction::Bridge));
        CHECK(s.sections[1].origin == SectionOrigin::Refined);
        CHECK_FALSE(seq::setSectionFunction(s, 1, SectionFunction::Bridge)); // no change, no edit
    }
    SECTION("deleting a section gives its time to a neighbour") {
        SongStructure s = detected();
        const double end = s.sections[1].endSeconds;
        CHECK(seq::removeSection(s, 1));
        CHECK(s.sections.size() == 3);
        CHECK(s.sections[0].endSeconds == end);
        CHECK(s.sections[0].origin == SectionOrigin::Refined);
        CHECK(s.validate().has_value());
    }
    SECTION("the last section cannot be deleted") {
        SongStructure s;
        s.sections.push_back(made(0.0, 10.0, SectionFunction::Other));
        CHECK_FALSE(seq::removeSection(s, 0));
    }
}

TEST_CASE("A person's edit makes the detector's confidence meaningless", "[seq][structure][edit]") {
    SongStructure s = detected();
    CHECK(seq::confidenceIsMeaningful(s.sections[1]));
    CHECK(seq::moveBoundary(s, 1, 25.0).has_value());
    CHECK_FALSE(seq::confidenceIsMeaningful(s.sections[1]));
    // The number is not destroyed -- that would be a second kind of data loss -- it is simply no
    // longer a claim about anything, and the JSON does not carry it.
    CHECK(s.sections[1].startConfidence == 0.6f);
    const nlohmann::json j = seq::songStructureToJson(s);
    CHECK_FALSE(j["sections"][1].contains("startConfidence"));
    CHECK(j["sections"][3].contains("startConfidence")); // still detected
}

// ---- the two vocabularies ----------------------------------------------------------------------

TEST_CASE("Every section function maps to a director section", "[seq][structure][vocabulary]") {
    // The whole of `SectionFunction`, listed so that a new one added to ADR-206's enum without a
    // director meaning shows up here rather than silently becoming whatever the fallback is.
    const SectionFunction all[] = {
        SectionFunction::Intro,       SectionFunction::Verse,       SectionFunction::PreChorus,
        SectionFunction::Build,       SectionFunction::Chorus,      SectionFunction::Drop,
        SectionFunction::Break,       SectionFunction::Bridge,      SectionFunction::Instrumental,
        SectionFunction::Breakdown,   SectionFunction::FinalChorus, SectionFunction::Outro,
        SectionFunction::Other};
    std::set<MusicalSection> images;
    for (const SectionFunction f : all) {
        images.insert(seq::sectionKindFor(f));
    }
    // All thirteen land on distinct director kinds. `Other` is the interesting one: it maps onto
    // `Phrase`, which nothing else maps onto, so "I do not know" stays distinguishable from every
    // musical claim rather than being folded into the nearest one.
    CHECK(images.size() == 13);
    CHECK(seq::sectionKindFor(SectionFunction::Other) == MusicalSection::Phrase);
    CHECK(seq::sectionKindFor(SectionFunction::Chorus) == MusicalSection::Chorus);
    CHECK(seq::sectionKindFor(SectionFunction::Breakdown) == MusicalSection::Breakdown);
}

TEST_CASE("The last payoff late in a piece becomes the final one", "[seq][structure][vocabulary]") {
    SongStructure s;
    s.durationSeconds = 100.0;
    s.sections.push_back(made(0.0, 20.0, SectionFunction::Intro));
    s.sections.push_back(made(20.0, 40.0, SectionFunction::Chorus));
    s.sections.push_back(made(40.0, 60.0, SectionFunction::Verse));
    s.sections.push_back(made(60.0, 90.0, SectionFunction::Chorus));
    s.sections.push_back(made(90.0, 100.0, SectionFunction::Outro));

    const auto kinds = seq::sectionKindsFor(s);
    REQUIRE(kinds.size() == 5);
    CHECK(kinds[1] == MusicalSection::Chorus);      // the first one is not the last one
    CHECK(kinds[3] == MusicalSection::FinalChorus); // ...and this one starts after 55% of the piece

    SECTION("a payoff that is the last but is early is not promoted") {
        SongStructure early;
        early.durationSeconds = 100.0;
        early.sections.push_back(made(0.0, 30.0, SectionFunction::Chorus));
        early.sections.push_back(made(30.0, 100.0, SectionFunction::Outro));
        CHECK(seq::sectionKindsFor(early)[0] == MusicalSection::Chorus);
    }
}

TEST_CASE("An edited structure reads as a director structure", "[seq][structure][vocabulary]") {
    SongStructure s = detected();
    const signals::MusicalStructure director = seq::toMusicalStructure(s);
    REQUIRE(director.sections.size() == s.sections.size());
    for (std::size_t i = 0; i < s.sections.size(); ++i) {
        CHECK(director.sections[i].startSeconds == s.sections[i].startSeconds);
        // `endSeconds()` on the director's section is `start + duration` and therefore need not
        // reproduce the authored end in the last bit -- see the note in `toMusicalStructure`. The
        // *start* is copied verbatim, and that is the value a section trigger fires on and the one
        // the project stores, so it is the one asserted exactly.
        CHECK(director.sections[i].endSeconds() == Catch::Approx(s.sections[i].endSeconds).epsilon(0).margin(1e-9));
        CHECK(director.sections[i].intensity == s.sections[i].energy);
    }
    // The chorus here opens at 51.8% of the piece, which is short of the 55% that makes a payoff
    // *the* payoff -- so it stays a Chorus. The promotion case has a test of its own above.
    CHECK(director.sections[2].kind == MusicalSection::Chorus);
}

// ---- the director vocabulary, and the four switches that read it ---------------------------------

TEST_CASE("Every MusicalSection has a name, and the names round-trip", "[cinematic][vocabulary]") {
    std::set<std::string> names;
    for (const MusicalSection s : signals::allMusicalSections()) {
        const std::string name = signals::musicalSectionName(s);
        CHECK_FALSE(name.empty());
        CHECK(names.insert(name).second); // distinct: two kinds sharing a name is one kind
        const auto back = signals::musicalSectionFromName(name);
        REQUIRE(back.has_value());
        CHECK(*back == s);
    }
    CHECK(names.size() == 15);
}

TEST_CASE("Every MusicalSection is answered deliberately by all four direction switches",
          "[cinematic][vocabulary]") {
    // `isDropSection` and `mayBeSplit` are exhaustive switches with no `default`, so an unhandled
    // enumerator is a -Wswitch warning at the point of the bug rather than a runtime surprise. The
    // other two cannot be: `Establish` and an emphasis of `0` are legitimate answers *and* the
    // values a fall-through would produce. So the kinds that hold them are pinned here, which is
    // what makes a new enumerator falling through visible.
    std::set<MusicalSection> establishing;
    std::set<MusicalSection> unemphasised;
    for (const MusicalSection s : signals::allMusicalSections()) {
        if (app::shotKindForSection(s) == app::ShotKind::Establish) {
            establishing.insert(s);
        }
        if (app::emphasisForSection(s, 1.0f) == 0.0f) {
            unemphasised.insert(s);
        }
    }
    CHECK(establishing == std::set<MusicalSection>{MusicalSection::Intro, MusicalSection::Outro});
    CHECK(unemphasised == std::set<MusicalSection>{
                              MusicalSection::Intro, MusicalSection::Build, MusicalSection::Phrase,
                              MusicalSection::Verse, MusicalSection::Breakdown,
                              MusicalSection::PreChorus, MusicalSection::Break,
                              MusicalSection::Bridge, MusicalSection::Instrumental,
                              MusicalSection::Outro});

    for (const MusicalSection s : signals::allMusicalSections()) {
        INFO(signals::musicalSectionName(s));
        // The three invariants the four switches have to agree about, which is the property a
        // fall-through breaks even when its value happens to be plausible.
        if (app::isDropSectionKind(s)) {
            // A payoff opens its own shot, is never cut into, and carries the hero.
            CHECK_FALSE(app::mayBeSplitSection(s));
            CHECK(app::emphasisForSection(s, 1.0f) >= 0.8f);
            const app::ShotKind kind = app::shotKindForSection(s);
            CHECK((kind == app::ShotKind::HeroReveal || kind == app::ShotKind::Reveal));
        }
        if (app::mayBeSplitSection(s)) {
            // A passage is the world going past, and the world going past is not about one object.
            CHECK(app::emphasisForSection(s, 1.0f) == 0.0f);
        }
        // Emphasis is a fraction and rises with intensity, for every kind.
        CHECK(app::emphasisForSection(s, 0.0f) >= 0.0f);
        CHECK(app::emphasisForSection(s, 1.0f) <= 1.0f);
        CHECK(app::emphasisForSection(s, 1.0f) >= app::emphasisForSection(s, 0.0f));
    }
}
