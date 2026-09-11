// A sequence inside a running engine (ADR-089): that it installs, that it scrubs, that it survives
// a project save and a scene swap, and that the frame at t does not depend on how the playhead got
// there.
//
// This file exists because six features in this repository have been built, tested and wired into
// nothing, and because determinism under scrubbing is the property a music-video tool lives or dies
// by. Every check here goes through `app::Engine::update`, not through the model, so it is testing
// the path a render actually takes.

#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/composition.hpp"
#include "seq/sequence.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using Catch::Approx;

namespace {

struct Scratch {
    fs::path dir;
    fs::path previous;
    explicit Scratch(const char* name) {
        dir = testsupport::processTempDir() / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        previous = fs::current_path();
        fs::current_path(dir);
    }
    ~Scratch() {
        fs::current_path(previous);
        fs::remove_all(dir);
    }
};

// Two orb nodes standing in for two environments, and a third for the character. Orbs because this
// test is about *when* things happen, and an orb needs no asset file to exist.
fs::path writeStage(const fs::path& dir, const char* name) {
    const fs::path path = dir / name;
    std::ofstream out(path);
    out << R"({
        "format": "avgen-scene", "version": 1, "name": "stage",
        "camera": { "mode": 1, "position": [0, 4, 12], "target": [0, 1, 0] },
        "nodes": [
            { "name": "city-day", "kind": "orb" },
            { "name": "city-night", "kind": "orb", "position": [40, 0, 0] },
            { "name": "walker", "kind": "orb", "position": [0, 0, 0], "scale": [0.2, 0.2, 0.2] }
        ]})";
    return path;
}

seq::Sequence piece() {
    seq::Sequence s;
    s.name = "poc";
    s.scenes.push_back(seq::SceneSlot{.id = "day", .node = "city-day"});
    s.scenes.push_back(seq::SceneSlot{.id = "night", .node = "city-night"});

    seq::Shot wide;
    wide.name = "wide";
    wide.startSeconds = 0.0;
    wide.durationSeconds = 8.0;
    wide.scene = "day";
    wide.camera.kind = seq::CameraKind::Keys;
    wide.camera.keys.push_back(seq::CameraKey{.timeSeconds = 0.0, .position = {0.0f, 12.0f, 30.0f}});
    wide.camera.keys.push_back(seq::CameraKey{.timeSeconds = 8.0, .position = {0.0f, 6.0f, 14.0f}});
    s.shots.push_back(std::move(wide));

    seq::Shot night;
    night.name = "night";
    night.startSeconds = 8.0;
    night.durationSeconds = 8.0;
    night.scene = "night";
    night.in = seq::Transition{seq::TransitionKind::FadeIn, 0.8};
    night.camera.kind = seq::CameraKind::Keys;
    night.camera.keys.push_back(seq::CameraKey{.timeSeconds = 0.0, .position = {40.0f, 5.0f, 12.0f}});
    night.camera.keys.push_back(seq::CameraKey{.timeSeconds = 8.0, .position = {46.0f, 3.0f, 8.0f}});
    s.shots.push_back(std::move(night));

    seq::Actor hero;
    hero.id = "hero";
    hero.node = "walker";
    hero.keys.push_back(seq::ActorKey{.timeSeconds = 0.0, .position = {-8.0f, 0.0f, 0.0f}});
    hero.keys.push_back(seq::ActorKey{.timeSeconds = 8.0, .position = {8.0f, 0.0f, 0.0f}});
    hero.keys.push_back(seq::ActorKey{.timeSeconds = 16.0, .position = {8.0f, 0.0f, -12.0f}});
    s.actors.push_back(std::move(hero));

    seq::OverlayCue line;
    line.id = "line1";
    line.content = "THROUGH THE CITY";
    line.style = "lyric";
    line.startSeconds = 2.0;
    line.endSeconds = 6.0;
    line.anchor = glm::vec2(0.5f, 0.16f);
    line.preset = seq::OverlayPreset::FadeInOut;
    s.overlays.push_back(std::move(line));

    seq::OverlayCue border;
    border.id = "frame";
    border.kind = seq::OverlayKind::Shape;
    border.content = "rectangle";
    border.startSeconds = 0.0;
    border.endSeconds = 16.0;
    border.order = -1;
    border.anchor = glm::vec2(0.5f, 0.5f);
    border.color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    border.extra = nlohmann::json{{"size", nlohmann::json::array({1.7, 0.94})}, {"strokeWidth", 0.004}};
    s.overlays.push_back(std::move(border));
    return s;
}

// What the engine looks like at one second: every parameter the sequence writes, plus the layer
// state the compositor would draw. This is the "state" spec 33 says must not depend on the path.
struct Snapshot {
    std::vector<float> values;
    std::vector<std::uint32_t> drawnLayers;

    bool operator==(const Snapshot&) const = default;
};

Snapshot sampleAt(app::Engine& engine, double seconds, std::uint64_t frameIndex) {
    FixedStepClock clock(60.0, seconds);
    FrameTime time = clock.tick();
    time.frameIndex = frameIndex;
    engine.seekSeconds(seconds);
    engine.update(time);

    Snapshot out;
    for (const std::string& path : engine.sequenceTargets()) {
        const params::IParameter* p = engine.params().find(path);
        if (p == nullptr) {
            continue;
        }
        for (std::size_t c = 0; c < p->componentCount(); ++c) {
            out.values.push_back(p->finalComponent(c));
        }
    }
    const comp::CompositionFrame& frame = engine.layers().build(comp::Frame{1920, 1080}, seconds);
    out.drawnLayers.push_back(frame.drawnLayers);
    return out;
}

} // namespace

TEST_CASE("a sequence installs onto a real engine and reaches every system it names",
          "[integration][sequence]") {
    Scratch scratch("sequence_install");
    const fs::path stage = writeStage(scratch.dir, "stage.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(stage).has_value());
    REQUIRE(engine.composition() != nullptr);

    auto report = engine.setSequence(piece());
    REQUIRE(report.has_value());
    for (const std::string& warning : report->warnings) {
        INFO(warning);
    }
    CHECK(report->unresolved.empty());
    CHECK(report->trackCount > 0);
    CHECK(report->layersRealised == 2);

    // Every target the sequence claims is a parameter that exists and a track that is bound.
    CHECK_FALSE(engine.sequenceTargets().empty());
    for (const std::string& target : engine.sequenceTargets()) {
        INFO(target);
        CHECK(engine.params().find(target) != nullptr);
    }
    CHECK(engine.timeline().unboundTargets().empty());
    CHECK(engine.timeline().enabled);

    // The scene cut, the camera, the character and the lyric all reach the parameter set.
    FixedStepClock clock(60.0);
    FrameTime time = clock.tick();
    engine.update(time);
    const auto* dayVisible = engine.params().find("nodes/city-day/visible");
    const auto* nightVisible = engine.params().find("nodes/city-night/visible");
    REQUIRE(dayVisible != nullptr);
    REQUIRE(nightVisible != nullptr);
    CHECK(dayVisible->finalComponent(0) == Approx(1.0f));
    CHECK(nightVisible->finalComponent(0) == Approx(0.0f));

    // And the camera really moved, rather than the tracks evaluating into nowhere.
    engine.seekSeconds(10.0);
    FixedStepClock later(60.0, 10.0);
    engine.update(later.tick());
    CHECK(engine.params().find("nodes/city-day/visible")->finalComponent(0) == Approx(0.0f));
    CHECK(engine.params().find("nodes/city-night/visible")->finalComponent(0) == Approx(1.0f));
    CHECK(engine.scene().camera.position.x == Approx(40.0f).margin(8.0));
}

TEST_CASE("the same second is the same state however the playhead reached it",
          "[integration][sequence][determinism]") {
    Scratch scratch("sequence_scrub");
    const fs::path stage = writeStage(scratch.dir, "stage.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(stage).has_value());
    REQUIRE(engine.setSequence(piece()).has_value());

    // Played forward, one frame at a time, sampling as we pass.
    const std::vector<double> probes{3.0, 7.5, 9.25, 12.0, 15.5};
    std::vector<Snapshot> forwards;
    std::uint64_t frame = 0;
    for (double t = 0.0; t <= 16.0; t += 0.25, ++frame) {
        for (double probe : probes) {
            if (std::abs(t - probe) < 1e-9) {
                forwards.push_back(sampleAt(engine, t, frame));
            }
        }
    }
    REQUIRE(forwards.size() == probes.size());

    // Reached by jumping, in an order no playback would ever produce, in a *different* engine that
    // has never seen the earlier seconds at all. Spec 34's "10s -> 45s -> 3s -> 30s".
    app::Engine jumped(app::EngineMode::Offline);
    REQUIRE(jumped.loadFile(stage).has_value());
    REQUIRE(jumped.setSequence(piece()).has_value());
    std::vector<Snapshot> scrubbed(probes.size());
    const std::vector<std::size_t> order{3, 0, 4, 1, 2};
    std::uint64_t jumpFrame = 1000;
    for (std::size_t i : order) {
        scrubbed[i] = sampleAt(jumped, probes[i], jumpFrame++);
    }

    for (std::size_t i = 0; i < probes.size(); ++i) {
        INFO("t = " << probes[i]);
        CHECK(forwards[i] == scrubbed[i]);
    }
}

// The same sequence with events on it. Separate from `piece()` so the tests above keep measuring
// what they were written to measure.
seq::Sequence eventPiece() {
    seq::Sequence s = piece();

    // Baked tier: a parameter change on a shot edge, and a camera shake on a cue.
    seq::SequenceEvent fog;
    fog.id = "fog-up";
    fog.when = {.kind = seq::TriggerKind::ShotStart, .name = "night"};
    fog.what.kind = seq::EventActionKind::SetParameter;
    fog.what.target = "scene/fogDensity";
    fog.what.amount = glm::vec4(0.08f, 0.0f, 0.0f, 0.0f);
    fog.what.mode = params::TrackMode::Add;
    s.events.push_back(std::move(fog));

    s.markers.push_back(seq::Marker{10.0, "impact", seq::MarkerKind::Cue});
    seq::SequenceEvent shake;
    shake.id = "impact-shake";
    shake.when = {.kind = seq::TriggerKind::Cue, .name = "impact"};
    shake.what.kind = seq::EventActionKind::CameraShake;
    shake.what.amount = glm::vec4(0.35f, 11.0f, 0.0f, 0.0f);
    shake.what.seconds = 1.4;
    s.events.push_back(std::move(shake));

    // Scheduled tier: an imperative effect at a known time. Nothing consumes it here -- the point
    // is what the engine delivers, and when.
    seq::SequenceEvent act;
    act.id = "walk";
    act.when = {.kind = seq::TriggerKind::Time, .timeSeconds = 4.0};
    act.what.kind = seq::EventActionKind::EntityAction;
    act.what.target = "hero";
    act.what.value = "walkTo";
    act.what.argument = "door";
    s.events.push_back(std::move(act));
    seq::SequenceEvent act2 = s.events.back();
    act2.id = "stop";
    act2.when.timeSeconds = 11.0;
    act2.what.value = "idle";
    s.events.push_back(std::move(act2));
    return s;
}

TEST_CASE("a baked event is the same state however the playhead reached it",
          "[integration][sequence][events][determinism]") {
    Scratch scratch("sequence_events_scrub");
    const fs::path stage = writeStage(scratch.dir, "stage.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(stage).has_value());
    auto installed = engine.setSequence(eventPiece());
    REQUIRE(installed.has_value());
    // Two tiers, reported rather than inferred.
    CHECK(installed->events.baked.size() == 2);
    CHECK(installed->events.dispatches.size() == 2);
    CHECK(installed->events.live.empty());

    const std::vector<double> probes{3.0, 7.5, 9.25, 10.5, 12.0, 15.5};
    std::vector<Snapshot> forwards;
    std::uint64_t frame = 0;
    for (double t = 0.0; t <= 16.0; t += 0.25, ++frame) {
        for (double probe : probes) {
            if (std::abs(t - probe) < 1e-9) {
                forwards.push_back(sampleAt(engine, t, frame));
            }
        }
    }
    REQUIRE(forwards.size() == probes.size());

    app::Engine jumped(app::EngineMode::Offline);
    REQUIRE(jumped.loadFile(stage).has_value());
    REQUIRE(jumped.setSequence(eventPiece()).has_value());
    std::vector<Snapshot> scrubbed(probes.size());
    const std::vector<std::size_t> order{4, 1, 5, 0, 3, 2};
    std::uint64_t jumpFrame = 2000;
    for (std::size_t i : order) {
        scrubbed[i] = sampleAt(jumped, probes[i], jumpFrame++);
    }
    for (std::size_t i = 0; i < probes.size(); ++i) {
        INFO("t = " << probes[i]);
        CHECK(forwards[i] == scrubbed[i]);
    }

    // ...and the events actually did something, so the equality above is not two identical
    // sequences of zeros. 10.5 s is inside the shake's decay; 15.5 s is past it.
    const auto shakeAmplitude = [&](double t) {
        sampleAt(jumped, t, jumpFrame++);
        const params::IParameter* p = jumped.params().find("camera/shake/amplitude");
        REQUIRE(p != nullptr);
        return p->finalComponent(0);
    };
    CHECK(shakeAmplitude(3.0) == Approx(0.0f));
    CHECK(shakeAmplitude(10.5) == Approx(0.35f));
    const params::IParameter* fogParam = jumped.params().find("scene/fogDensity");
    REQUIRE(fogParam != nullptr);
    sampleAt(jumped, 3.0, jumpFrame++);
    const float fogBefore = fogParam->finalComponent(0);
    sampleAt(jumped, 12.0, jumpFrame++);
    CHECK(fogParam->finalComponent(0) == Approx(fogBefore + 0.08f));
}

TEST_CASE("the engine delivers a scheduled event once forward and restores it on a seek",
          "[integration][sequence][events]") {
    Scratch scratch("sequence_events_dispatch");
    const fs::path stage = writeStage(scratch.dir, "stage.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(stage).has_value());
    REQUIRE(engine.setSequence(eventPiece()).has_value());

    // Played forward without a seek between frames, which is what `sampleAt` cannot do.
    FixedStepClock clock(30.0, 0.0);
    int walkTo = 0;
    int idle = 0;
    int restored = 0;
    for (int i = 0; i < 480; ++i) { // sixteen seconds at thirty frames
        FrameTime time = clock.tick();
        time.frameIndex = static_cast<std::uint64_t>(i);
        engine.update(time);
        for (const seq::FiredEvent& f : engine.firedEvents()) {
            const seq::SequenceEvent& e = engine.sequence().events[f.eventIndex];
            walkTo += e.what.value == "walkTo" ? 1 : 0;
            idle += e.what.value == "idle" ? 1 : 0;
            restored += f.restored ? 1 : 0;
        }
    }
    CHECK(walkTo == 1);
    CHECK(idle == 1);
    CHECK(restored == 0);

    // A seek does not replay both; it restores the one that is standing at the new playhead.
    engine.seekSeconds(6.0);
    FixedStepClock after(30.0, 6.0);
    FrameTime time = after.tick();
    time.frameIndex = 1000;
    engine.update(time);
    REQUIRE(engine.firedEvents().size() == 1);
    CHECK(engine.sequence().events[engine.firedEvents()[0].eventIndex].what.value == "walkTo");
    CHECK(engine.firedEvents()[0].restored);
    // ...and it does not keep re-delivering it on every subsequent frame.
    FrameTime next = after.tick();
    next.frameIndex = 1001;
    engine.update(next);
    CHECK(engine.firedEvents().empty());
}

TEST_CASE("a sequence survives a project save and load", "[integration][sequence][project]") {
    Scratch scratch("sequence_project");
    const fs::path stage = writeStage(scratch.dir, "stage.json");
    const fs::path file = "session.json";

    std::string saved;
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadFile(stage).has_value());
        REQUIRE(engine.setSequence(piece()).has_value());
        REQUIRE(engine.saveProject(file).has_value());
        std::ifstream in(file);
        saved.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK(saved.find("\"sequence\"") != std::string::npos);
    // The layers the sequence made are derived, and saving them as well would restore a second copy
    // under an id the next install will not reuse.
    CHECK(saved.find("seq:line1") == std::string::npos);
    // Nor are its baked tracks saved: the load re-bakes them, and two tracks writing camera/position
    // is whichever one the timeline applies second rather than a blend. (camera/position still
    // appears under "parameters" as an ordinary base value; it is the *track* that must not.)
    {
        const nlohmann::json doc = nlohmann::json::parse(saved);
        CHECK_FALSE(doc.contains("timeline"));
    }

    app::Engine loaded(app::EngineMode::Offline);
    REQUIRE(loaded.loadProject(file).has_value());
    REQUIRE(loaded.hasSequence());
    CHECK(loaded.sequence().shots.size() == 2);
    CHECK(loaded.sequence().actors.size() == 1);
    CHECK(loaded.sequence().overlays.size() == 2);
    // Installed, not merely carried: the layers are back and the tracks are bound.
    CHECK(loaded.layers().size() == 2);
    CHECK(loaded.sequenceReport().unresolved.empty());
    CHECK(loaded.timeline().unboundTargets().empty());
    CHECK_FALSE(loaded.sequenceTargets().empty());
    // Exactly one track per target: a load that both restored the bake and re-baked it would have
    // two of everything, and the camera would follow whichever applied last.
    for (const std::string& target : loaded.sequenceTargets()) {
        int count = 0;
        for (const params::Track& track : loaded.timeline().tracks()) {
            count += track.target == target ? 1 : 0;
        }
        INFO(target);
        CHECK(count >= 1);
        CHECK(count <= 3); // position and scale may be keyed per component
    }
    CHECK(loaded.timeline().tracks().size() == loaded.sequenceReport().trackCount);

    FixedStepClock clock(60.0, 10.0);
    loaded.seekSeconds(10.0);
    loaded.update(clock.tick());
    CHECK(loaded.params().find("nodes/city-night/visible")->finalComponent(0) == Approx(1.0f));
}

TEST_CASE("a project with no sequence loads exactly as it always did", "[integration][sequence][project]") {
    Scratch scratch("sequence_legacy");
    const fs::path file = "legacy.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.saveProject(file).has_value());
    }
    nlohmann::json doc;
    {
        std::ifstream in(file);
        doc = nlohmann::json::parse(in);
    }
    REQUIRE_FALSE(doc.contains("sequence"));

    app::Engine loaded(app::EngineMode::Offline);
    REQUIRE(loaded.loadProject(file).has_value());
    CHECK_FALSE(loaded.hasSequence());
    CHECK(loaded.sequenceTargets().empty());
    CHECK(loaded.projectWarnings().empty());
}

TEST_CASE("swapping the scene re-installs the sequence instead of leaving it bound to nothing",
          "[integration][sequence]") {
    Scratch scratch("sequence_swap");
    const fs::path first = writeStage(scratch.dir, "one.json");
    const fs::path second = writeStage(scratch.dir, "two.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(first).has_value());
    REQUIRE(engine.setSequence(piece()).has_value());
    const std::size_t targets = engine.sequenceTargets().size();
    REQUIRE(targets > 0);

    // A scene swap clears the parameter set. Without a re-install every track the sequence baked
    // would still be there, still correct, and bound to nothing -- which is exactly how this
    // project has lost a feature before.
    REQUIRE(engine.loadFile(second).has_value());
    CHECK(engine.sequenceTargets().size() == targets);
    CHECK(engine.timeline().unboundTargets().empty());
    CHECK(engine.layers().size() == 2);
    for (const std::string& target : engine.sequenceTargets()) {
        INFO(target);
        CHECK(engine.params().find(target) != nullptr);
    }

    FixedStepClock clock(60.0, 10.0);
    engine.seekSeconds(10.0);
    engine.update(clock.tick());
    CHECK(engine.params().find("nodes/city-night/visible")->finalComponent(0) == Approx(1.0f));
}

TEST_CASE("editing a sequence and re-installing does not accumulate tracks or layers",
          "[integration][sequence]") {
    Scratch scratch("sequence_edit");
    const fs::path stage = writeStage(scratch.dir, "stage.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(stage).has_value());
    REQUIRE(engine.setSequence(piece()).has_value());
    const std::size_t tracks = engine.timeline().tracks().size();
    const std::size_t layers = engine.layers().size();

    for (int i = 0; i < 4; ++i) {
        engine.sequence().shots[1].startSeconds = 8.0 + 0.25 * i;
        engine.sequence().shots[0].durationSeconds = 8.0 + 0.25 * i;
        REQUIRE(engine.installSequence().has_value());
        CHECK(engine.timeline().tracks().size() == tracks);
        CHECK(engine.layers().size() == layers);
        CHECK(engine.timeline().unboundTargets().empty());
    }

    engine.clearSequence();
    CHECK(engine.timeline().tracks().empty());
    CHECK(engine.layers().empty());
    CHECK(engine.sequenceTargets().empty());
    CHECK_FALSE(engine.hasSequence());
}

// Hidden (`[.]`): it needs `assets/audio/night-shift.wav`, which is generated rather than committed
// (`python3 tools/make_city_score.py assets/audio/night-shift.wav`). Run it with
// `avgen_tests "[poc]"` after regenerating the score.
//
// What it pins is that the proof-of-concept's *song* is analysable: the sequencer's "Sections"
// button folds a track into labelled sections, and a score with no energy contrast folds into one
// section and gives an author nothing to cut to. The score was written to be sequenced, and this is
// where that claim is checked rather than asserted.
TEST_CASE("the proof-of-concept score folds into sections the editor can cut to", "[.][poc]") {
    const fs::path wav = fs::path(AVGEN_SOURCE_DIR) / "assets" / "audio" / "night-shift.wav";
    if (!fs::exists(wav)) {
        WARN("assets/audio/night-shift.wav is absent; run tools/make_city_score.py");
        return;
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.track() != nullptr);
    CHECK(engine.durationSeconds() == Approx(105.0).margin(0.1));

    const auto& beats = engine.track()->beats();
    WARN(fmt::format("tempo {:.1f} bpm (confidence {:.2f}), {} beat(s)", beats.tempoBpm,
                     beats.confidence, beats.beatTimes.size()));
    // 96 BPM over 105 s is 168 beats; the tracker may find the half- or double-time grid, which is
    // still a grid an author can snap to.
    CHECK(beats.beatTimes.size() > 60);

    auto structure = app::structureOfTrack(*engine.track(), engine.phraseBars(), engine.sectionPhrases());
    REQUIRE(structure.has_value());
    for (const signals::StructureSection& s : structure->sections) {
        WARN(fmt::format("{:7.2f}s {:7.2f}s  {:<12} intensity {:.2f}", s.startSeconds,
                         s.durationSeconds, signals::musicalSectionName(s.kind), s.intensity));
    }
    // More than one section, or the fold has told the author nothing.
    CHECK(structure->sections.size() >= 3);
}
