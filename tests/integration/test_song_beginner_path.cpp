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
