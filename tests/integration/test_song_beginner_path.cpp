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
#include "song/from_analysis.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    const auto direction = app::directSongFromPlan(comp->heroes(), *plan, comp->cameraDirection(),
                                                   engine.autoDirector());
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
    const auto installed = app::installSongDirection(engine, *direction, engine.autoDirector());
    INFO((installed ? std::string() : installed.error().message));
    REQUIRE(installed.has_value());
    CHECK(*installed > 0);
#endif
}

// Song Mode's output is ordinary shots, and this is the test that says so.
//
// The defect it guards against is subtle because everything *looked* right: the camera moved, the
// film played, and the Shots lane was empty. The generated edit existed only as baked keyframes and
// a list in a panel -- nothing a person could select, drag, trim or replace. "It works" and "the
// artist can edit it" were two different claims and only the first was true.

#include "seq/sequence.hpp"

TEST_CASE("Song Mode creates real, editable sequencer shots", "[integration][song][beginner]") {
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

    // A hand-made shot, before the director runs. It stands in for a person's work.
    seq::Sequence piece = engine.sequence();
    auto detected = analysis::detectStructure(*engine.track());
    REQUIRE(detected.has_value());
    piece.structure = std::move(*detected);
    piece.sectionTimeline = song::timelineFromStructure(piece.structure, piece.shotLanguage);
    seq::Shot mine;
    mine.name = "my shot";
    mine.startSeconds = 0.0;
    mine.durationSeconds = 5.0;
    piece.shots.push_back(mine);
    REQUIRE(engine.setSequence(std::move(piece)).has_value());

    const auto plan = app::songPlanForEngine(engine);
    REQUIRE(plan.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    const auto direction = app::directSongFromPlan(comp->heroes(), *plan, comp->cameraDirection(),
                                                   engine.autoDirector());
    REQUIRE(direction.has_value());
    REQUIRE(app::installSongDirection(engine, *direction, engine.autoDirector()).has_value());

    const seq::Sequence& after = engine.sequence();

    // 1. The shots are ON THE SEQUENCER, which is the whole point. Before this change the lane was
    //    empty and the edit lived only as keyframes.
    INFO("shots on the sequencer: " << after.shots.size());
    REQUIRE(after.shots.size() > 1);

    // 2. They are ordinary shots: real spans, a camera, and the same type a person creates.
    std::size_t directed = 0;
    for (const seq::Shot& shot : after.shots) {
        INFO("shot '" << shot.name << "'");
        CHECK(shot.durationSeconds > 0.0);
        if (shot.origin == seq::Shot::Origin::Directed) {
            ++directed;
            // A directed shot carries a real camera move, not a placeholder to be resolved later.
            CHECK(shot.camera.kind == seq::CameraKind::Move);
        }
    }
    CHECK(directed > 1);

    // 3. **The artist's shot survived.** This is the promise that makes the rest usable: a
    //    re-direct replaces what the director made and leaves what a person made.
    const seq::Shot* kept = after.shotNamed("my shot");
    REQUIRE(kept != nullptr);
    CHECK(kept->origin == seq::Shot::Origin::Authored);
    CHECK(kept->durationSeconds == Catch::Approx(5.0));

    // 4. They edit like any other shot -- through the same functions the lane's gestures call.
    {
        seq::Sequence edited = after;
        const std::size_t before = edited.shots.size();
        std::size_t target = 0;
        for (std::size_t i = 0; i < edited.shots.size(); ++i) {
            if (edited.shots[i].origin == seq::Shot::Origin::Directed) {
                target = i;
                break;
            }
        }
        const double end = edited.shots[target].endSeconds();
        seq::trimShotEnd(edited.shots[target], end + 7.0);
        CHECK(edited.shots[target].endSeconds() == Catch::Approx(end + 7.0));
        REQUIRE(seq::duplicateShot(edited.shots, target).has_value());
        CHECK(edited.shots.size() == before + 1);
    }

    // 5. **Extending a generated shot does not move its section.** Sections are landmarks, not
    //    containers, and the two timelines are independent -- so this asserts the section is
    //    untouched by an edit that crosses it.
    {
        seq::Sequence edited = engine.sequence();
        REQUIRE_FALSE(edited.sectionTimeline.sections.empty());
        const double sectionEnd = edited.sectionTimeline.sections.front().endSeconds;
        std::size_t first = 0;
        for (std::size_t i = 0; i < edited.shots.size(); ++i) {
            if (edited.shots[i].origin == seq::Shot::Origin::Directed) {
                first = i;
                break;
            }
        }
        seq::trimShotEnd(edited.shots[first], sectionEnd + 10.0);
        CHECK(edited.shots[first].endSeconds() > sectionEnd);           // it crossed
        CHECK(edited.sectionTimeline.sections.front().endSeconds ==
              Catch::Approx(sectionEnd));                               // and the section did not move
    }

    // 6. They persist as ordinary shot data -- no regeneration on load.
    {
        const nlohmann::json doc = engine.sequence().toJson();
        const auto restored = seq::Sequence::fromJson(doc);
        REQUIRE(restored.has_value());
        CHECK(restored->shots.size() == after.shots.size());
        const seq::Shot* mineAgain = restored->shotNamed("my shot");
        REQUIRE(mineAgain != nullptr);
        CHECK(mineAgain->origin == seq::Shot::Origin::Authored);
        // And a directed shot comes back still marked directed, or the next re-direct would treat
        // the whole previous film as somebody's authored work and never replace any of it.
        const std::size_t directedAfter = static_cast<std::size_t>(
            std::count_if(restored->shots.begin(), restored->shots.end(), [](const seq::Shot& s) {
                return s.origin == seq::Shot::Origin::Directed;
            }));
        CHECK(directedAfter == directed);
    }
#endif
}
