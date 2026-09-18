// The beginner workflow, end to end: import, analyze, Song Mode, shots on cameras.
//
// **This is the test the Song Director audit called the highest-value missing item, and the reason
// is a defect this repository actually shipped.** `songPlanFromCues` was written, correct, unit
// tested -- and called by nothing for its entire life, because every test covered a *link* and none
// covered the *chain*. Each piece passing says nothing about whether the pieces are connected.
//
// The second thing it guards is architectural rather than mechanical: the chain must complete with
// NO performer rules. Performer actions (`seq::SectionPerformanceSet`) direct the cast; the Song
// Director directs cameras; neither is a prerequisite for the other. That claim is easy to state,
// easy to believe, and easy to break with one convenience lookup -- so it is asserted rather than
// documented.

#include "analysis/analysis_track.hpp"
#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "scene/composition.hpp"
#include "seq/sequence.hpp"
#include "world/effects.hpp"
#include "song/from_analysis.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <set>

using namespace avgen;

namespace {
std::filesystem::path glowmereProject() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" /
           "glowmere-valley-2-multicam.json";
}
} // namespace

TEST_CASE("import, analyze, Song Mode: a first film with no performer rules",
          "[integration][song][beginner]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(glowmereProject())) {
        SKIP("the Glowmere multi-camera demo is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(glowmereProject());
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    if (engine.track() == nullptr) {
        SKIP("the project's audio is not available here");
    }

    // ---- step 1: the song is analyzed, as pressing Analyze does -----------------------------
    //
    // Through the real analyzer on the real track, not a fixture: the point is that what the button
    // produces is what the director can use.
    seq::Sequence piece = engine.sequence();
    auto detected = analysis::detectStructure(*engine.track());
    INFO((detected ? std::string() : detected.error().message));
    REQUIRE(detected.has_value());
    piece.structure = std::move(*detected);
    REQUIRE_FALSE(piece.structure.sections.empty());

    // ---- step 2: sections become a film -------------------------------------------------------
    piece.sectionTimeline = song::timelineFromStructure(piece.structure, piece.shotLanguage);
    REQUIRE_FALSE(piece.sectionTimeline.sections.empty());

    // **No performer rules.** This is the architectural assertion, and it is deliberately made
    // before anything downstream runs: everything after this point must work with the table empty.
    piece.sectionPerformance.entries.clear();
    REQUIRE(piece.sectionPerformance.empty());
    REQUIRE(engine.setSequence(std::move(piece)).has_value());

    // ---- step 3: the plan, from the production function -------------------------------------
    const auto plan = app::songPlanForEngine(engine);
    INFO((plan ? std::string() : plan.error().message));
    REQUIRE(plan.has_value());
    REQUIRE_FALSE(plan->sections.empty());
    // Every section arrived with a treatment. A section whose intent id is empty would be one the
    // shot language failed to resolve -- the film would run and that section would be undirected.
    for (const app::SongPlanSection& section : plan->sections) {
        INFO("section '" << section.label << "'");
        CHECK_FALSE(section.intent.id.empty());
        CHECK(section.endSeconds > section.startSeconds);
    }

    // ---- step 4: the director turns the plan into shots --------------------------------------
    //
    // **The autonomy is stated, not inherited.** This used to pass `engine.autoDirector()` straight
    // through, which is whatever freedom the demo project happened to be saved with -- and the
    // owner saved it as `locked` in commit 5e3637c. Locked is one shot on one camera by definition,
    // so the "more than one camera was used" assertion below became a claim about a JSON field
    // rather than about the director, and failed. What this test is for is that the director can
    // cut a film from an analyzed song, so it says which freedom it is asking for.
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    app::AutoDirectorSettings settings = engine.autoDirector();
    settings.autonomy = app::Autonomy::Expressive;
    const auto direction =
        app::directSongFromPlan(comp->heroes(), *plan, comp->cameraDirection(), settings);
    INFO((direction ? std::string() : direction.error().message));
    REQUIRE(direction.has_value());

    // ---- step 5: what came out is playable ---------------------------------------------------
    INFO("shots " << direction->shots.size() << ", decisions " << direction->decisions.size());
    REQUIRE_FALSE(direction->shots.empty());
    REQUIRE_FALSE(direction->sequence.shots.empty());

    // The shots cover the piece in order and do not overlap: a camera track that jumps backwards or
    // leaves a hole is not a film, and neither failure shows up in a count.
    for (std::size_t i = 0; i < direction->shots.size(); ++i) {
        const scene::CameraShot& shot = direction->shots[i];
        INFO("shot " << i << " " << shot.startSeconds << ".." << shot.endSeconds);
        CHECK(shot.endSeconds > shot.startSeconds);
        if (i > 0) {
            CHECK(shot.startSeconds >= direction->shots[i - 1].endSeconds - 1e-6);
        }
    }

    // More than one camera was actually used. The demo declares three; a director that quietly fell
    // back to the main camera for everything would satisfy every assertion above.
    std::set<scene::CameraId> used;
    for (const scene::CameraShot& shot : direction->shots) {
        used.insert(shot.camera);
    }
    INFO("distinct cameras used: " << used.size());
    CHECK(used.size() > 1);

    // ---- step 6: and it installs, which is what playback consumes ----------------------------
    const auto installed = app::installSongDirection(engine, *direction, settings);
    INFO((installed ? std::string() : installed.error().message));
    REQUIRE(installed.has_value());
    CHECK(*installed > 0);
#endif
}

// **Song Mode leaves the Shots lane alone, and this is the test that says so.**
//
// The contract changed, and the reversal is worth recording rather than just deleting a test. This
// file briefly asserted the opposite: that Song Mode writes ordinary `seq::Shot`s into the
// sequencer, on the argument that a generated edit a person cannot select or drag is not an edit.
//
// That argument is right about an *edited sequence* and wrong about Song Mode, which is a live
// directing mode -- it decides coverage from the song's structure and re-deciding is how it is used.
// In practice it put forty-three shots a person did not author into their lane on every run, refused
// to install at all if any hand-made shot happened to overlap the first generated one
// (`Sequence::validate` refuses overlaps outright), and had to be cleared by hand to get back to an
// empty timeline.
//
// So Song Mode does what the other two director modes do: bake the framing onto the timeline and
// leave the sequencer to the person. What that also restores is `setShotSpans` -- the flattened cut
// that world effects gate on, which Song Mode had silently lost by not calling `installSequence`.

#include "seq/sequence.hpp"

TEST_CASE("Song Mode bakes a film without touching the Shots lane", "[integration][song][beginner]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(glowmereProject())) {
        SKIP("the Glowmere multi-camera demo is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(glowmereProject()).has_value());
    if (engine.track() == nullptr) {
        SKIP("the project's audio is not available here");
    }

    // A hand-made shot, before the director runs. It stands in for a person's work, and it is what
    // must still be there afterwards -- untouched, and the only thing in the lane.
    seq::Sequence piece = engine.sequence();
    auto detected = analysis::detectStructure(*engine.track());
    REQUIRE(detected.has_value());
    piece.structure = std::move(*detected);
    piece.sectionTimeline = song::timelineFromStructure(piece.structure, piece.shotLanguage);
    // Cleared first: the project on disk may carry shots from an earlier run, and this test is about
    // what the director does to a lane rather than about what was in it.
    piece.shots.clear();
    seq::Shot mine;
    mine.name = "my shot";
    mine.startSeconds = 0.0;
    mine.durationSeconds = 5.0;
    piece.shots.push_back(mine);
    auto set = engine.setSequence(std::move(piece));
    INFO((set ? std::string() : set.error().message));
    REQUIRE(set.has_value());

    const auto plan = app::songPlanForEngine(engine);
    REQUIRE(plan.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    // Stated rather than inherited, for the reason given at the first test's step 4: the demo is
    // saved Locked, and Locked is one shot on one camera, which is not the thing under test here.
    app::AutoDirectorSettings settings = engine.autoDirector();
    settings.autonomy = app::Autonomy::Expressive;
    const auto direction =
        app::directSongFromPlan(comp->heroes(), *plan, comp->cameraDirection(), settings);
    REQUIRE(direction.has_value());
    REQUIRE(direction->sequence.shots.size() > 1); // the director really did decide a film
    REQUIRE(app::installSongDirection(engine, *direction, settings).has_value());

    const seq::Sequence& after = engine.sequence();

    // 1. **The lane is exactly what the person left there.** Not "mostly", not "plus the generated
    //    ones marked so you can tell" -- the same one shot.
    INFO("shots on the sequencer after directing: " << after.shots.size());
    REQUIRE(after.shots.size() == 1);
    CHECK(after.shots.front().name == "my shot");
    CHECK(after.shots.front().durationSeconds == Catch::Approx(5.0));

    // 2. And the film exists anyway, which is the half that makes (1) acceptable rather than a
    //    regression: the camera track carries the cut.
    const scene::CameraDirection& cameras = comp->cameraDirection();
    const std::size_t directed = static_cast<std::size_t>(
        std::count_if(cameras.shots.begin(), cameras.shots.end(), [](const scene::CameraShot& s) {
            return s.origin == scene::CameraShot::Origin::Directed;
        }));
    INFO("directed camera cuts: " << directed);
    CHECK(directed > 1);

    // 3. **The cut is published to world effects.** This is the thing Song Mode had lost by not
    //    going through `installSequence`, and the World Effects panel reported it accurately as
    //    "No directed camera: effects gated on the cut cannot fire". A shot span list is what a
    //    camera-gated effect activates against.
    INFO("shot spans for world effects: " << engine.shotSpans().size());
    CHECK_FALSE(engine.shotSpans().empty());
#endif
}

// Does a camera-travel world effect have anything to fire on in Song Mode?
//
// Reported as a possible regression: "the camera travel beam seems not to work in Song Mode -- it
// may work but it never fires (there could be a reason for this that I don't understand that is not
// a bug)". Both halves of that hunch turned out to be right, and they are different things.
//
// **There was a real bug, and it is fixed.** Song Mode had stopped calling `installSequence`, so
// `setShotSpans` was never reached and `engine.shotSpans()` was empty.
// `resolveActivationWindow` walks that list looking for a span with `travel` set, finds nothing, and
// the effect correctly never activates -- on any song, always.
//
// **And there is a second reason, which is not a bug.** `Sequence::shotSpans` sets `travel` only for
// a `Transition` shot or one that hands its aim on -- the camera in transit *between* subjects.
// `shotKindForIntent` emits `Transition` only for an intent in the middle hero-emphasis band
// (0.35..0.70) that is also moving and close. Glowmere's sections all resolve to
// `dynamic_hero_coverage`, which is high-emphasis, so every shot holds or approaches one subject and
// none of them travel between two. The camera moves -- an approach from 12.6 to 6.8 radii is not a
// still camera -- but it is not *travelling*, which is what the effect is gated on.
//
// So the two tests below are a measurement and its control: what this song actually produces, and
// proof that the mechanism fires when a song asks it to.

TEST_CASE("Song Mode publishes spans, and this song's are all holds",
          "[integration][song][effects]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(glowmereProject())) {
        SKIP("the Glowmere multi-camera demo is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(glowmereProject()).has_value());
    if (engine.track() == nullptr) {
        SKIP("the project's audio is not available here");
    }
    const auto plan = app::songPlanForEngine(engine);
    REQUIRE(plan.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    const auto direction = app::directSongFromPlan(comp->heroes(), *plan, comp->cameraDirection(),
                                                   engine.autoDirector());
    REQUIRE(direction.has_value());
    REQUIRE(app::installSongDirection(engine, *direction, engine.autoDirector()).has_value());

    // The half that was broken: the spans reach the engine at all.
    const auto& spans = engine.shotSpans();
    REQUIRE_FALSE(spans.empty());

    const std::size_t travelling = static_cast<std::size_t>(
        std::count_if(spans.begin(), spans.end(), [](const world::ShotSpan& s) { return s.travel; }));
    const std::size_t holding = static_cast<std::size_t>(
        std::count_if(spans.begin(), spans.end(), [](const world::ShotSpan& s) { return s.spotlight; }));
    INFO(spans.size() << " span(s): " << travelling << " travelling, " << holding << " holding");

    // The half that is not broken: on THIS song every shot is about somebody, so a camera-travel
    // effect has nothing to fire on and is correctly inert. Asserted rather than merely noted,
    // because if this ever changes the explanation above stops being true and should be revisited.
    CHECK(holding > 0);
    CHECK(travelling == 0);
#endif
}

// The control (ADR-182). Without it the test above is "the effect never fires and here is a story
// about why" -- which is indistinguishable from a mechanism that is simply broken.
TEST_CASE("A travelling intent does produce a span the beam fires on",
          "[integration][song][effects]") {
    // An intent in the middle hero-emphasis band, moving and close: the one combination
    // `shotKindForIntent` answers with `Transition`.
    app::ShotIntentProfile travelling;
    travelling.id = "a passage between two things";
    travelling.heroEmphasis = 0.5f;  // neither the world's shot nor the subject's
    travelling.movement = 0.8f;      // moving
    travelling.distance = 0.3f;      // close
    CHECK(app::shotKindForIntent(travelling) == app::ShotKind::Transition);

    // And the two neighbouring bands do not, which is what says the band above is the reason rather
    // than a coincidence.
    app::ShotIntentProfile hero = travelling;
    hero.heroEmphasis = 0.85f; // what Glowmere's sections actually resolve to
    CHECK(app::shotKindForIntent(hero) == app::ShotKind::Approach);
    app::ShotIntentProfile world = travelling;
    world.heroEmphasis = 0.2f;
    CHECK(app::shotKindForIntent(world) == app::ShotKind::Drift);

    // A `Transition` shot that knows where it came from marks its span as travelling, which is the
    // link between the intent and the effect.
    app::Sequence film;
    app::Shot shot;
    shot.kind = app::ShotKind::Transition;
    shot.startSeconds = 0.0;
    shot.durationSeconds = 4.0;
    shot.subject.name = "from";
    shot.subject.radius = 2.0f;
    app::FocalTarget to;
    to.name = "to";
    to.radius = 2.0f;
    shot.handoff = to;
    film.shots.push_back(shot);

    const std::vector<world::ShotSpan> spans = film.shotSpans();
    REQUIRE(spans.size() == 1);
    CHECK(spans.front().travel);

    world::WorldEffect beam = world::cameraTravelBeam();
    CHECK(world::resolveActivationWindow(beam.activation, beam.timing, 2.0, spans).has_value());
    // And it does not fire where there is no travel, or the check above would pass on anything.
    app::Sequence held;
    app::Shot still = shot;
    still.kind = app::ShotKind::Track;
    still.handoff.reset();
    held.shots.push_back(still);
    const std::vector<world::ShotSpan> holds = held.shotSpans();
    REQUIRE(holds.size() == 1);
    CHECK_FALSE(holds.front().travel);
    CHECK_FALSE(world::resolveActivationWindow(beam.activation, beam.timing, 2.0, holds).has_value());
}

// Editing the sections edits the film, and Song Mode follows.
//
// Reported as: drag Verse 2 to start at twenty seconds and Song Mode still lists it where it was;
// split a Chorus in two and Song Mode still sees one section. Both were the same cause --
// `songPlanForEngine` consulted `engine.songPlan()` *before* the section timeline, so any project
// carrying an authored plan had a frozen snapshot shadowing the thing being edited.
// `glowmere-valley-2-multicam` was carrying a ten-section one.
//
// ADR-247 had the principle already: a timeline is what a person decided, a structure is what a
// detector reported, and the decision outranks the report. What it had not had to say is that a
// *saved* decision does not outrank a *live* one.

TEST_CASE("Song Mode's sections follow the sequencer's", "[integration][song][sections]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(glowmereProject())) {
        SKIP("the Glowmere multi-camera demo is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(glowmereProject()).has_value());
    if (engine.track() == nullptr) {
        SKIP("the project's audio is not available here");
    }

    seq::Sequence piece = engine.sequence();
    if (piece.sectionTimeline.sections.empty()) {
        auto detected = analysis::detectStructure(*engine.track());
        REQUIRE(detected.has_value());
        piece.structure = std::move(*detected);
        piece.sectionTimeline = song::timelineFromStructure(piece.structure, piece.shotLanguage);
        REQUIRE(engine.setSequence(piece).has_value());
    }
    REQUIRE(engine.sequence().sectionTimeline.sections.size() > 2);

    const auto before = app::songPlanForEngine(engine);
    REQUIRE(before.has_value());
    const std::size_t sectionsBefore = engine.sequence().sectionTimeline.sections.size();
    REQUIRE(before->sections.size() == sectionsBefore);

    // ---- 1. moving a boundary moves it in Song Mode too ----------------------------------------
    {
        seq::Sequence edited = engine.sequence();
        const double was = edited.sectionTimeline.sections[1].startSeconds;
        // **A quarter of the section's own length, not a fixed 7.5 s.** `moveBoundary` clamps so
        // neither neighbour falls under the minimum, so a fixed distance is a bet on the fixture
        // staying roughly as long as it was. It did not: the owner split the opening of
        // `glowmere-valley-2-multicam` into seven pieces of about 3.7 s each, the 7.5 s move
        // clamped to 3.44 s, and this test started failing on a data edit rather than on a code
        // change. A fraction of the span cannot clamp, so it measures what it meant to measure.
        const double now = was + 0.25 * edited.sectionTimeline.sections[1].durationSeconds();
        REQUIRE(song::moveBoundary(edited.sectionTimeline, 1, now));
        edited.refreshSectionMarkers();
        REQUIRE(engine.setSequence(std::move(edited)).has_value());

        const auto after = app::songPlanForEngine(engine);
        REQUIRE(after.has_value());
        REQUIRE(after->sections.size() > 1);
        INFO("boundary was " << was << "s, moved to " << now << "s, plan says "
                             << after->sections[1].startSeconds << "s");
        CHECK(after->sections[1].startSeconds == Catch::Approx(now).margin(0.05));
        // The section before it ends where the next one starts -- the structure is gapless, and a
        // plan that disagreed with the timeline about that would direct across a hole.
        CHECK(after->sections[0].endSeconds == Catch::Approx(now).margin(0.05));
    }

    // ---- 2. splitting a section adds one to Song Mode -------------------------------------------
    {
        seq::Sequence edited = engine.sequence();
        const std::size_t was = edited.sectionTimeline.sections.size();
        // The longest section, for the same reason: a fixture whose second section happens to be a
        // quarter of a second long cannot be split, and refusing to split it is `splitSection`
        // being right rather than this test finding something.
        const auto& all = edited.sectionTimeline.sections;
        const auto longest = std::max_element(
            all.begin(), all.end(), [](const song::Section& a, const song::Section& b) {
                return a.durationSeconds() < b.durationSeconds();
            });
        REQUIRE(longest != all.end());
        const double at = (longest->startSeconds + longest->endSeconds) * 0.5;
        REQUIRE(song::splitSection(edited.sectionTimeline, at, 0.25));
        edited.refreshSectionMarkers();
        REQUIRE(engine.setSequence(std::move(edited)).has_value());
        REQUIRE(engine.sequence().sectionTimeline.sections.size() == was + 1);

        const auto after = app::songPlanForEngine(engine);
        REQUIRE(after.has_value());
        INFO("timeline has " << engine.sequence().sectionTimeline.sections.size()
                             << " section(s), the plan has " << after->sections.size());
        CHECK(after->sections.size() == was + 1);
    }

    // ---- 3. and the plan is still directable, which is what makes 1 and 2 worth anything --------
    {
        const auto plan = app::songPlanForEngine(engine);
        REQUIRE(plan.has_value());
        CHECK(plan->validate().has_value());
        scene::Composition* comp = engine.composition();
        REQUIRE(comp != nullptr);
        const auto direction = app::directSongFromPlan(comp->heroes(), *plan, comp->cameraDirection(),
                                                       engine.autoDirector());
        INFO((direction ? std::string() : direction.error().message));
        REQUIRE(direction.has_value());
    }
#endif
}
