// Taking the camera back parks the director's cut; it never destroys it (ADR-582).
//
// The owner's report, 21 Sep: "the world effect 'hero pulse' does not display or animate in the
// rendered scene as it does during playback." The cause was a deliberate hand-back of the camera:
// `releaseDirectedCamera` erased the director's camera tracks, its aim-follow table, its directed
// camera shots and -- the part that silenced the pulse -- the shot spans a HeroFocus or CameraTravel
// world effect gates on. The next save wrote the loss (45 spans, 39 aim-follow entries, ~122 KB of
// camera tracks), and an offline render loads the project, so every exported frame had no pulse.
//
// The ruling (ADR-582, overruling ADR-207's "stop firing" and ADR-249's delete-on-release): who is
// steering the camera and what the film's focus schedule is are two different things. Taking the
// camera back stops the director steering. It does not touch the schedule, and it keeps the cut
// parked -- in memory and in the saved project -- so "Resume director" puts back exactly what was
// there, without a re-bake (which would re-photograph the hero anchors, ADR-344, and come back
// different).

#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "core/time.hpp"
#include "params/timeline.hpp"
#include "scene/composition.hpp"
#include "ui/ui_logic.hpp"
#include "world/wave_effect.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

using namespace avgen;

namespace {

world::HeroPoint hero(const char* name, glm::vec3 position, float height, float radius,
                      float importance) {
    world::HeroPoint h;
    h.name = name;
    h.assetId = "asset";
    h.position = position;
    h.height = height;
    h.radius = radius;
    h.importance = importance;
    h.preferredCameraDistance = std::max(height * 3.0f, 18.0f);
    h.activationRadius = h.preferredCameraDistance * 3.0f;
    return h;
}

std::vector<world::HeroPoint> threeHeroes() {
    return {hero("elder", glm::vec3(0.0f, 0.0f, -40.0f), 20.0f, 4.0f, 0.95f),
            hero("spire", glm::vec3(60.0f, 0.0f, 20.0f), 12.0f, 2.0f, 0.72f),
            hero("bloom", glm::vec3(-45.0f, 0.0f, 30.0f), 3.0f, 1.5f, 0.48f)};
}

std::optional<std::filesystem::path> scoreWav() {
#ifdef AVGEN_SOURCE_DIR
    const std::filesystem::path wav =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "audio" / "glowmere-valley.wav";
    if (std::filesystem::exists(wav)) {
        return wav;
    }
#endif
    return std::nullopt;
}

// A directed project with the two effects the owner's project carries: the Hero Pulse gated on
// `heroFocus` and the Camera Travel Beam gated on `cameraTravel`. Both are the shipped factories, so
// this is the same gating code the multicam film runs.
void directedWithEffects(app::Engine& engine) {
    engine.newComposition();
    REQUIRE(engine.loadAudio(*scoreWav()).has_value());
    REQUIRE(engine.composition()->setHeroes(threeHeroes()).has_value());
    REQUIRE(engine
                .setWorldEffects({world::heroGroundPulse("Hero Pulse"),
                                  world::cameraTravelBeam("Camera Travel Beam")})
                .has_value());
    REQUIRE(app::directEngine(engine, engine.composition()->heroes(), {}).has_value());
}

// How many world effects the engine packs for the GPU at `seconds`: the number the renderer draws.
// Driven through `update` exactly as a frame is, rather than by calling the resolver by hand, so
// what is measured is what reaches the scene the renderer reads.
std::uint32_t activeEffectsAt(app::Engine& engine, double seconds) {
    FixedStepClock clock(60.0);
    engine.seekSeconds(seconds);
    clock.seek(seconds);
    engine.update(engine.tick(clock));
    return engine.scene().worldEffects.count;
}

// A moment the cut is *holding* a hero -- spotlit and not travelling -- far enough into a long
// enough span that the pulse is past its delay and fade-in and before its fade-out.
std::optional<double> spotlightMoment(std::span<const world::ShotSpan> spans) {
    for (const world::ShotSpan& s : spans) {
        if (s.spotlight && !s.travel && s.end - s.start > 4.0) {
            return s.start + 1.5;
        }
    }
    return std::nullopt;
}

std::optional<double> travelMoment(std::span<const world::ShotSpan> spans) {
    for (const world::ShotSpan& s : spans) {
        if (s.travel && s.end - s.start > 2.5) {
            return s.start + 0.9;
        }
    }
    return std::nullopt;
}

std::filesystem::path scratch(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "avgen-parked-cut";
    std::filesystem::create_directories(dir);
    return dir / name;
}

nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream in(path);
    return nlohmann::json::parse(in);
}

} // namespace

// The owner's scenario, end to end: direct, take the camera back deliberately, save, reload the way
// an offline render does -- and the pulse still lands on the spotlit hero.
TEST_CASE("taking the camera back keeps the focus schedule through a save and a render's reload",
          "[director][camera][parked][adr582]") {
    if (!scoreWav()) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    directedWithEffects(engine);
    app::DirectorState state;
    app::noteDirected(engine, state);

    const std::vector<world::ShotSpan> spans(engine.shotSpans().begin(), engine.shotSpans().end());
    const std::size_t aimFollow = engine.composition()->aimFollow().size();
    INFO("baked " << spans.size() << " span(s), " << aimFollow << " aim-follow entries");
    const auto spot = spotlightMoment(spans);
    const auto travel = travelMoment(spans);
    // The premise. Every assertion below is about an effect surviving; a cut with no spotlight
    // or no travel would pass them all by never having fired in the first place.
    REQUIRE(spot.has_value());
    REQUIRE(travel.has_value());
    const std::uint32_t pulseLive = activeEffectsAt(engine, *spot);
    const std::uint32_t beamLive = activeEffectsAt(engine, *travel);
    REQUIRE(pulseLive >= 1);
    REQUIRE(beamLive >= 1);

    // The deliberate hand-back: the menu item, the Free Camera action and the Cameras panel's
    // unlock all end in this one call.
    static_cast<void>(app::releaseDirectedCamera(engine, state));
    CHECK_FALSE(engine.timeline().isAutomated("camera/position"));   // the director stopped steering
    CHECK_FALSE(state.directed);

    // ...and the film's focus schedule did not go with it. The owner flying a free camera still
    // sees the pulse land on the spotlit hero, at the right time.
    CHECK(engine.shotSpans().size() == spans.size());
    CHECK(activeEffectsAt(engine, *spot) == pulseLive);
    CHECK(activeEffectsAt(engine, *travel) == beamLive);

    const std::filesystem::path project = scratch("owner-scenario.json");
    REQUIRE(engine.saveProject(project).has_value());
    {
        const nlohmann::json doc = readJson(project);
        REQUIRE(doc.contains("cameraShotSpans"));
        CHECK(doc["cameraShotSpans"].size() == spans.size());
    }

    // What an offline render does: a fresh engine, the project file, nothing else.
    app::Engine render(app::EngineMode::Offline);
    auto loaded = render.loadProject(project);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    CHECK(render.shotSpans().size() == spans.size());
    CHECK_FALSE(render.timeline().isAutomated("camera/position"));   // still parked, not steering
    CHECK(activeEffectsAt(render, *spot) == pulseLive);
    CHECK(activeEffectsAt(render, *travel) == beamLive);
    std::filesystem::remove(project);
}

namespace {

// A span in a form Catch can compare and print. `ShotSpan` has no `operator==`, and a field-wise
// comparison written here would silently skip a field added later; this lists every one.
nlohmann::json spansJson(std::span<const world::ShotSpan> spans) {
    nlohmann::json out = nlohmann::json::array();
    for (const world::ShotSpan& s : spans) {
        out.push_back({s.start, s.end, s.travel, s.spotlight, s.emphasis, s.subject,
                       {s.subjectPosition.x, s.subjectPosition.y, s.subjectPosition.z},
                       s.subjectRadius, s.handoff,
                       {s.handoffPosition.x, s.handoffPosition.y, s.handoffPosition.z}});
    }
    return out;
}

// The director's camera tracks as the timeline writes them, bound (so each writes its parameter's
// own component count -- an unbound track writes up to its last non-zero component instead).
nlohmann::json cameraTracksJson(app::Engine& engine) {
    params::Timeline only;
    const auto owned = app::directedCameraTargets();
    for (const params::Track& t : engine.timeline().tracks()) {
        if (std::find(owned.begin(), owned.end(), t.target) != owned.end()) {
            only.addTrack(t);
        }
    }
    REQUIRE(only.bind(engine.params()).has_value());
    return only.toJson()["tracks"];
}

std::set<std::string> topLevelKeys(const nlohmann::json& doc) {
    std::set<std::string> keys;
    for (const auto& [key, value] : doc.items()) {
        keys.insert(key);
    }
    return keys;
}

// One directed shot on the main camera, as Song Mode writes them, so the parked cut carries all
// three of its parts.
void addDirectedShot(app::Engine& engine) {
    scene::CameraDirection direction = engine.composition()->cameraDirection();
    direction.ensureMainCamera();
    scene::CameraShot shot;
    shot.camera = scene::kMainCamera;
    shot.startSeconds = 3.0;
    shot.endSeconds = 11.5;
    shot.label = "directed";
    shot.origin = scene::CameraShot::Origin::Directed;
    direction.shots.push_back(shot);
    REQUIRE(engine.setCameraDirection(std::move(direction)).has_value());
}

} // namespace

TEST_CASE("resume puts back exactly the cut that was parked", "[director][camera][parked][adr582]") {
    if (!scoreWav()) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    directedWithEffects(engine);
    addDirectedShot(engine);
    app::DirectorState state;
    app::noteDirected(engine, state);

    const nlohmann::json tracks = cameraTracksJson(engine);
    const nlohmann::json timeline = engine.timeline().toJson();
    const std::vector<scene::AimFollow> follow = engine.composition()->aimFollow();
    const nlohmann::json spans = spansJson(engine.shotSpans());
    const std::vector<scene::CameraShot> shots = engine.composition()->cameraDirection().shots;
    const std::filesystem::path path = scratch("resume.json");
    const nlohmann::json document = engine.projectDocument(path);
    REQUIRE(!tracks.empty());
    REQUIRE(!follow.empty());

    const std::size_t parked = app::releaseDirectedCamera(engine, state);
    CHECK(parked == tracks.size());
    REQUIRE(engine.directorParked());
    CHECK(engine.parkedCut().tracks.size() == tracks.size());
    CHECK(engine.parkedCut().aimFollow == follow);
    CHECK(engine.parkedCut().cameraShots.size() == 1);
    CHECK(engine.composition()->aimFollow().empty());
    CHECK(app::directedCameraBakeSize(engine) == 0);

    // A second hand-back while parked changes nothing: the parked cut is never overwritten.
    CHECK(app::releaseDirectedCamera(engine, state) == 0);
    CHECK(engine.parkedCut().tracks.size() == tracks.size());

    SECTION("in the same session") {
        auto resumed = app::resumeDirectedCamera(engine, state);
        INFO((resumed ? std::string() : resumed.error().message));
        REQUIRE(resumed.has_value());
        CHECK(*resumed == tracks.size());
        CHECK_FALSE(engine.directorParked());
        CHECK(state.directed);
        CHECK(engine.timeline().isAutomated("camera/position"));
        // Byte for byte: the tracks, the whole timeline, the follow table, the spans, the shots --
        // and the project document a save would write.
        CHECK(cameraTracksJson(engine) == tracks);
        CHECK(engine.timeline().toJson() == timeline);
        CHECK(engine.composition()->aimFollow() == follow);
        CHECK(spansJson(engine.shotSpans()) == spans);
        CHECK(engine.composition()->cameraDirection().shots == shots);
        CHECK(engine.projectDocument(path) == document);
    }

    SECTION("in the next session, from the saved project") {
        REQUIRE(engine.saveProject(path).has_value());
        app::Engine next(app::EngineMode::Offline);
        auto loaded = next.loadProject(path);
        INFO((loaded ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        REQUIRE(next.directorParked());
        CHECK_FALSE(next.timeline().isAutomated("camera/position"));
        CHECK(next.composition()->aimFollow().empty());
        CHECK(next.parkedCut().aimFollow == follow);
        app::DirectorState fresh;
        auto resumed = app::resumeDirectedCamera(next, fresh);
        INFO((resumed ? std::string() : resumed.error().message));
        REQUIRE(resumed.has_value());
        CHECK(cameraTracksJson(next) == tracks);
        CHECK(next.composition()->aimFollow() == follow);
        CHECK(spansJson(next.shotSpans()) == spans);
        CHECK(next.composition()->cameraDirection().shots == shots);
        CHECK(fresh.directed);
    }
    std::filesystem::remove(path);
}

TEST_CASE("resume refuses rather than overwrite camera keys made while parked",
          "[director][camera][parked][adr582]") {
    if (!scoreWav()) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    directedWithEffects(engine);
    app::DirectorState state;
    app::noteDirected(engine, state);
    static_cast<void>(app::releaseDirectedCamera(engine, state));
    const std::size_t parked = engine.parkedCut().tracks.size();

    // Somebody keys the camera by hand while the director is set aside.
    params::Track mine;
    mine.target = "camera/position";
    mine.addKey(params::Key{0.0, {1.0f, 2.0f, 3.0f, 0.0f}});
    mine.addKey(params::Key{5.0, {4.0f, 5.0f, 6.0f, 0.0f}});
    engine.timeline().addTrack(std::move(mine));

    auto resumed = app::resumeDirectedCamera(engine, state);
    REQUIRE_FALSE(resumed.has_value());
    CHECK(resumed.error().message.find("camera/position") != std::string::npos);
    // Nothing moved: their key is still there and the parked cut is still whole.
    CHECK(engine.timeline().tracks().size() == 1);
    CHECK(engine.parkedCut().tracks.size() == parked);
    // And a hand-back now does not park their key over the director's cut.
    CHECK(app::releaseDirectedCamera(engine, state) == 0);
    CHECK(engine.parkedCut().tracks.size() == parked);
    CHECK(engine.timeline().tracks().size() == 1);
}

// The owner's words: the effect must behave in the rendered scene as it does during playback. So
// the live engine with a parked director and a fresh engine that loaded its save -- which is all an
// offline render is -- must pack the same effects, lane for lane, at every sampled second.
TEST_CASE("a parked director's effects render exactly as they play", "[director][camera][parked][adr582]") {
    if (!scoreWav()) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine live(app::EngineMode::Offline);
    live.newComposition();
    REQUIRE(live.loadAudio(*scoreWav()).has_value());
    REQUIRE(live.composition()->setHeroes(threeHeroes()).has_value());
    // The pulse alone: its source is the spotlit hero and its direction is authored, so no lane
    // depends on where the (differently driven) camera happens to be.
    REQUIRE(live.setWorldEffects({world::heroGroundPulse("Hero Pulse")}).has_value());
    REQUIRE(app::directEngine(live, live.composition()->heroes(), {}).has_value());
    app::DirectorState state;
    app::noteDirected(live, state);
    static_cast<void>(app::releaseDirectedCamera(live, state));
    REQUIRE(live.directorParked());

    const std::filesystem::path path = scratch("parity.json");
    REQUIRE(live.saveProject(path).has_value());
    app::Engine render(app::EngineMode::Offline);
    REQUIRE(render.loadProject(path).has_value());

    double end = 0.0;
    for (const world::ShotSpan& s : live.shotSpans()) {
        end = std::max(end, s.end);
    }
    REQUIRE(end > 10.0);
    std::size_t sampled = 0;
    std::size_t active = 0;
    for (double t = 0.0; t < end; t += 0.5) {
        const std::uint32_t a = activeEffectsAt(live, t);
        const std::uint32_t b = activeEffectsAt(render, t);
        INFO("t = " << t);
        REQUIRE(a == b);
        for (std::uint32_t i = 0; i < a; ++i) {
            CHECK(std::memcmp(&live.scene().worldEffects.effects[i],
                              &render.scene().worldEffects.effects[i],
                              sizeof(world::WorldEffectGpu)) == 0);
        }
        ++sampled;
        active += a > 0 ? 1 : 0;
    }
    INFO(active << " of " << sampled << " sampled seconds had the pulse active");
    // The premise: a comparison of two engines that both draw nothing proves nothing.
    CHECK(active > 0);
    std::filesystem::remove(path);
}

// The Tree of Life case: a project with 0 camera tracks, 0 follow entries and 0 spans. Handing the
// camera back must cost nothing and write nothing new.
TEST_CASE("releasing a project that was never directed is a no-op", "[director][camera][parked][adr582]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    const std::filesystem::path path = scratch("never-directed.json");
    const nlohmann::json before = engine.projectDocument(path);

    app::DirectorState state;
    CHECK(app::releaseDirectedCamera(engine, state) == 0);
    CHECK_FALSE(engine.directorParked());
    CHECK(engine.shotSpans().empty());
    CHECK(app::directedCameraBakeSize(engine) == 0);
    CHECK_FALSE(app::resumeDirectedCamera(engine, state).has_value());

    const nlohmann::json after = engine.projectDocument(path);
    CHECK(topLevelKeys(after) == topLevelKeys(before));
    CHECK_FALSE(after.contains("parkedDirector"));
    CHECK_FALSE(after.contains("cameraShotSpans"));
    CHECK(after == before);
}

// testing.md #31 / ADR-618: a key the reader reads and the writer drops is lost on the second save,
// so every new key goes load -> save -> reload, at both of the places a project serialises its
// scene: inlined (`assets.scene.inline`) and by reference to a scene file.
TEST_CASE("a parked cut survives load, save and reload with the scene inline or by reference",
          "[director][camera][parked][adr582][persistence]") {
    if (!scoreWav()) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    const bool byReference = GENERATE(false, true);
    INFO((byReference ? "scene by reference" : "scene inline"));
    app::Engine engine(app::EngineMode::Offline);
    directedWithEffects(engine);
    if (byReference) {
        const std::filesystem::path scene = scratch("parked.scene.json");
        REQUIRE(engine.saveComposition(scene).has_value());
        REQUIRE(engine.loadComposition(scene).has_value());
        REQUIRE(!engine.compositionPath().empty());
        // The director's tracks and spans survive a scene swap already; direct again so the cut
        // matches this composition's heroes exactly.
        REQUIRE(app::directEngine(engine, engine.composition()->heroes(), {}).has_value());
    }
    addDirectedShot(engine);
    app::DirectorState state;
    app::noteDirected(engine, state);
    static_cast<void>(app::releaseDirectedCamera(engine, state));
    REQUIRE(engine.directorParked());
    REQUIRE(!engine.parkedCut().cameraShots.empty());

    const std::filesystem::path first = scratch(byReference ? "rt-ref-1.json" : "rt-inline-1.json");
    const std::filesystem::path second = scratch(byReference ? "rt-ref-2.json" : "rt-inline-2.json");
    REQUIRE(engine.saveProject(first).has_value());
    const nlohmann::json a = readJson(first);
    REQUIRE(a.contains("parkedDirector"));
    REQUIRE(a.contains("cameraShotSpans"));
    CHECK_FALSE(a.contains("cameraAimFollow"));   // it is parked, not live
    CHECK(a["parkedDirector"]["tracks"].size() == engine.parkedCut().tracks.size());
    CHECK(a["parkedDirector"]["aimFollow"].size() == engine.parkedCut().aimFollow.size());
    CHECK(a["parkedDirector"]["cameraShots"].size() == 1);

    app::Engine reopened(app::EngineMode::Offline);
    auto loaded = reopened.loadProject(first);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    REQUIRE(reopened.saveProject(second).has_value());
    const nlohmann::json b = readJson(second);

    // Keys first, sizes second: an app save once dropped two whole top-level keys while growing.
    CHECK(topLevelKeys(b) == topLevelKeys(a));
    CHECK(b["parkedDirector"] == a["parkedDirector"]);
    CHECK(b["cameraShotSpans"] == a["cameraShotSpans"]);

    app::Engine third(app::EngineMode::Offline);
    REQUIRE(third.loadProject(second).has_value());
    CHECK(third.parkedCut().aimFollow == engine.parkedCut().aimFollow);
    CHECK(third.parkedCut().cameraShots == engine.parkedCut().cameraShots);
    CHECK(third.parkedCut().tracks.size() == engine.parkedCut().tracks.size());
    CHECK(spansJson(third.shotSpans()) == spansJson(engine.shotSpans()));
    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

// ADR-441, no shims: a project written before ADR-582 has no `parkedDirector` block, and must open
// and behave exactly as it did -- directed if its tracks are there, nothing parked either way.
TEST_CASE("a project saved before parking existed opens as it always did", "[director][camera][parked][adr582]") {
    if (!scoreWav()) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    directedWithEffects(engine);
    const std::filesystem::path path = scratch("pre-adr582.json");
    REQUIRE(engine.saveProject(path).has_value());
    const nlohmann::json doc = readJson(path);
    // A steering director's save is exactly the shape of an old file: nothing parked to write.
    REQUIRE_FALSE(doc.contains("parkedDirector"));

    app::Engine old(app::EngineMode::Offline);
    REQUIRE(old.loadProject(path).has_value());
    CHECK_FALSE(old.directorParked());
    CHECK(app::cameraLooksDirected(old));
    CHECK(old.composition()->aimFollow() == engine.composition()->aimFollow());
    CHECK(spansJson(old.shotSpans()) == spansJson(engine.shotSpans()));

    // And loading it into an engine that had something parked does not inherit that.
    app::Engine busy(app::EngineMode::Offline);
    directedWithEffects(busy);
    app::DirectorState state;
    static_cast<void>(app::releaseDirectedCamera(busy, state));
    REQUIRE(busy.directorParked());
    REQUIRE(busy.loadProject(path).has_value());
    CHECK_FALSE(busy.directorParked());
    std::filesystem::remove(path);
}

// Rule 4 of the ruling: only an explicit re-bake or an explicit discard may replace or delete a cut.
TEST_CASE("only a re-bake or a discard replaces or deletes a parked cut", "[director][camera][parked][adr582]") {
    if (!scoreWav()) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    directedWithEffects(engine);
    app::DirectorState state;
    app::noteDirected(engine, state);
    static_cast<void>(app::releaseDirectedCamera(engine, state));
    REQUIRE(engine.directorParked());

    SECTION("a heroes change does not touch a parked cut") {
        std::vector<world::HeroPoint> fewer = engine.composition()->heroes();
        fewer.pop_back();
        REQUIRE(engine.composition()->setHeroes(fewer).has_value());
        auto redirected = app::refreshDirection(engine, state);
        REQUIRE(redirected.has_value());
        CHECK(*redirected == app::Redirect::Nothing);
        CHECK(engine.directorParked());
    }
    SECTION("an assistant rollback's composition swap keeps both halves") {
        const std::size_t parked = engine.parkedCut().aimFollow.size();
        const nlohmann::json spans = spansJson(engine.shotSpans());
        REQUIRE(engine.setCompositionJson(engine.composition()->toJson()).has_value());
        CHECK(engine.parkedCut().aimFollow.size() == parked);
        CHECK(spansJson(engine.shotSpans()) == spans);
    }
    SECTION("an explicit re-bake replaces it") {
        REQUIRE(app::directEngine(engine, engine.composition()->heroes(), {}).has_value());
        CHECK_FALSE(engine.directorParked());
        CHECK(engine.timeline().isAutomated("camera/position"));
    }
    SECTION("an explicit discard deletes it, spans and all") {
        CHECK(app::discardDirectorsCut(engine, state) > 0);
        CHECK_FALSE(engine.directorParked());
        CHECK(engine.shotSpans().empty());
        const nlohmann::json doc = engine.projectDocument(scratch("discarded.json"));
        CHECK_FALSE(doc.contains("parkedDirector"));
        CHECK_FALSE(doc.contains("cameraShotSpans"));
    }
}

// The aim-follow table is camera automation saved beside the timeline, not scene content. An
// assistant rollback rebuilds the composition from its own document, which never held the table --
// so a steering cut lost its follow on every rollback, and the next save wrote the loss.
TEST_CASE("a composition swap keeps a steering cut's aim-follow table", "[director][camera][adr582]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    REQUIRE(engine.composition()->setHeroes(threeHeroes()).has_value());
    const std::vector<scene::AimFollow> follow{scene::AimFollow{.startSeconds = 1.0,
                                                                .endSeconds = 6.0,
                                                                .hero = "elder",
                                                                .heroAtCut = glm::vec3(1.0f, 2.0f, 3.0f)}};
    engine.composition()->setAimFollow(follow);
    REQUIRE(engine.setCompositionJson(engine.composition()->toJson()).has_value());
    CHECK(engine.composition()->aimFollow() == follow);
}

// A different scene is not a changed cast: opening a directed project in a session that was
// directing another one must not re-cut the project it just opened.
TEST_CASE("opening a directed project does not re-cut it", "[director][camera][redirect][adr582]") {
    if (!scoreWav()) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    const std::filesystem::path path = scratch("opened.json");
    {
        app::Engine writer(app::EngineMode::Offline);
        directedWithEffects(writer);
        REQUIRE(writer.saveProject(path).has_value());
    }
    app::Engine session(app::EngineMode::Offline);
    directedWithEffects(session);
    app::DirectorState state;
    app::noteDirected(session, state);

    REQUIRE(session.loadProject(path).has_value());
    // The state still holds the previous composition's counters. Make them disagree with the new
    // one's, the way two unrelated compositions' counters may -- which used to read as a changed
    // cast and re-cut the project that had just been opened.
    state.heroRevision = session.composition()->heroRevision() + 1;
    const nlohmann::json loaded = session.timeline().toJson();
    auto redirected = app::refreshDirection(session, state);
    REQUIRE(redirected.has_value());
    CHECK(*redirected == app::Redirect::Nothing);
    CHECK(session.timeline().toJson() == loaded);
    CHECK(state.directed);
    std::filesystem::remove(path);
}
