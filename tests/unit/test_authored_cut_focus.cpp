// ADR-947: `heroFocus` and `cameraTravel` read an authored cut.
//
// Until ADR-947 the only source of the shot spans those two activations gate on was the
// Auto-director's bake (Song mode, `installSequence`). A film cut by hand on the camera track --
// `cameraDirection.shots`, cameras with aim and follow nodes, which is how Glowmere Valley 3 is cut --
// gave them nothing, so GV2 multicam's sixteen Hero Pulses were deleted from GV3 as dead weight.
//
// Every engine-level case here carries a control arm: the same question asked of the director's
// spans alone (`Engine::shotSpans`, what the effects read before), which must answer "never".

#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/authored_cut.hpp"
#include "scene/composition.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/wave_effect.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

scene::CameraRig rig(scene::CameraId id, const char* name, const char* aim, const char* follow) {
    scene::CameraRig r;
    r.id = id;
    r.name = name;
    r.slug = scene::cameraSlug(name);
    r.aimNode = aim;
    r.followNode = follow;
    r.position = glm::vec3(0.0f, 20.0f, 60.0f);
    r.target = glm::vec3(0.0f);
    return r;
}

scene::CameraShot shot(scene::CameraId camera, double start, double end) {
    scene::CameraShot s;
    s.camera = camera;
    s.startSeconds = start;
    s.endSeconds = end;
    return s;
}

// Two cameras, one per hero -- one aims at `alpha`, one follows `beta` -- cut 0-6 s then 6-12 s.
scene::CameraDirection twoShotCut() {
    scene::CameraDirection d;
    d.ensureMainCamera();
    d.cameras.push_back(rig(2, "On Alpha", "alpha", ""));
    d.cameras.push_back(rig(3, "On Beta", "", "beta"));
    d.nextId = 4;
    d.shots.push_back(shot(2, 0.0, 6.0));
    d.shots.push_back(shot(3, 6.0, 12.0));
    return d;
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

world::HeroPoint hero(const char* name, glm::vec3 at) {
    world::HeroPoint h;
    h.name = name;
    h.assetId = "asset";
    h.position = at;
    h.height = 6.0f;
    h.radius = 2.0f;
    h.importance = 0.8f;
    h.preferredCameraDistance = 20.0f;
    h.activationRadius = 60.0f;
    return h;
}

world::EffectInstance pulseOn(const char* owner) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::GroundPulse, std::string("Hero Pulse ") + owner);
    e.owner = world::EffectOwner::entity(owner);
    e.wave.source.kind = world::SourceKind::Owner; // GV2 multicam's pulses: `"source": {"kind": "owner"}`
    return e;
}

// The engine GV3 is: two heroes, each with its own Hero Pulse (the shipped factory: Owner source,
// `heroFocus`), a World Travel Beam (`cameraTravel`), and the authored cut -- no director.
void authoredProject(app::Engine& engine) {
    engine.newComposition();
    REQUIRE(engine.loadAudio(*scoreWav()).has_value());
    REQUIRE(engine.composition()
                ->setHeroes({hero("alpha", glm::vec3(0.0f)), hero("beta", glm::vec3(80.0f, 0.0f, 0.0f))})
                .has_value());
    REQUIRE(engine
                .setEffects({pulseOn("alpha"), pulseOn("beta"),
                             world::makeEffect(world::EffectKind::TravelBeam, "Travel Beam")})
                .has_value());
    REQUIRE(engine.setCameraDirection(twoShotCut()).has_value());
}

void seekTo(app::Engine& engine, double seconds) {
    FixedStepClock clock(60.0);
    engine.seekSeconds(seconds);
    clock.restartAt(seconds); // the next tick is `seconds` itself, as an offline render's frame is
    engine.update(engine.tick(clock));
    REQUIRE(engine.timelineClock().seconds == Catch::Approx(seconds).margin(1e-9));
}

bool drawn(const app::Engine& engine, std::size_t i) {
    return engine.effectStatus()[i] == world::EffectStatus::Drawn;
}

// "Would it fire" asked of a list of spans the way the wave resolver asks: an entity-owned pulse
// fires for its own subject only.
bool pulseWindow(std::span<const world::ShotSpan> shots, double t, const char* subject) {
    world::Timing timing;
    return world::resolveActivationWindow(world::Activation::HeroFocus, timing, t, shots, false, subject).has_value();
}
bool travelWindow(std::span<const world::ShotSpan> shots, double t) {
    world::Timing timing;
    return world::resolveActivationWindow(world::Activation::CameraTravel, timing, t, shots).has_value();
}

std::vector<unsigned char> wavesBytes(const app::Engine& engine) {
    const world::WaveFrame& w = engine.scene().waves;
    std::vector<unsigned char> out(sizeof(w.count) + w.count * sizeof(world::WaveGpu));
    std::memcpy(out.data(), &w.count, sizeof(w.count));
    std::memcpy(out.data() + sizeof(w.count), w.effects, w.count * sizeof(world::WaveGpu));
    return out;
}

} // namespace

TEST_CASE("a shot's subject is its own 'Focuses on', else its camera's aim node, else its follow node",
          "[authoredcut][adr947]") {
    scene::CameraDirection d;
    d.ensureMainCamera();
    d.cameras.push_back(rig(2, "Aim", "a", "f"));
    d.cameras.push_back(rig(3, "Follow", "", "f"));
    d.cameras.push_back(rig(4, "Still", "", ""));
    d.nextId = 5;

    CHECK(scene::cameraSubject(d.cameras[1]) == "a"); // aim wins over follow: the Critic's order
    CHECK(scene::cameraSubject(d.cameras[2]) == "f");
    CHECK(scene::cameraSubject(d.cameras[3]).empty());
    scene::CameraRig main = d.cameras[0];
    main.aimNode = "a"; // the main camera is the legacy `camera/*` block, never about a hero
    CHECK(scene::cameraSubject(main).empty());

    scene::CameraShot s = shot(4, 0.0, 1.0);
    CHECK(scene::shotSubject(d, s).empty());
    s.subject = "spire";
    CHECK(scene::shotSubject(d, s) == "spire"); // a still camera framing a mushroom can say so
    s.camera = 2;
    CHECK(scene::shotSubject(d, s) == "spire"); // and the shot's word beats the camera's

    SECTION("'Focuses on' is saved with the shot, and only when set") {
        const nlohmann::json j = s.toJson();
        CHECK(j.value("subject", std::string()) == "spire");
        auto back = scene::CameraShot::fromJson(j);
        REQUIRE(back.has_value());
        CHECK(back->subject == "spire");
        CHECK_FALSE(shot(2, 0.0, 1.0).toJson().contains("subject")); // untouched files stay byte-identical
    }
}

TEST_CASE("the authored cut reads as holds and travels", "[authoredcut][adr947]") {
    SECTION("two shots on two heroes: two holds, and a travel at the cut toward the second") {
        const auto spans = scene::authoredShotSpans(twoShotCut());
        REQUIRE(spans.size() == 3);
        CHECK(spans[0].spotlight);
        CHECK_FALSE(spans[0].travel);
        CHECK(spans[0].subject == "alpha");
        CHECK(spans[0].start == 0.0);
        CHECK(spans[0].end == 6.0);
        CHECK(spans[1].travel);
        CHECK_FALSE(spans[1].spotlight);
        CHECK(spans[1].subject == "alpha");
        CHECK(spans[1].handoff == "beta");
        CHECK(spans[1].start == 6.0);
        CHECK(spans[1].end == 6.0 + scene::kCutTravelSeconds);
        CHECK(spans[2].spotlight);
        CHECK(spans[2].subject == "beta");
        CHECK(spans[2].start == 6.0);
        CHECK(spans[2].end == 12.0);
    }
    SECTION("the locator places a subject") {
        const auto spans = scene::authoredShotSpans(twoShotCut(), [](std::string_view n, glm::vec3& at, float& r) {
            at = n == "beta" ? glm::vec3(80.0f, 0.0f, 0.0f) : glm::vec3(1.0f);
            r = 3.0f;
            return true;
        });
        CHECK(spans[1].handoffPosition == glm::vec3(80.0f, 0.0f, 0.0f));
        CHECK(spans[2].subjectPosition == glm::vec3(80.0f, 0.0f, 0.0f));
        CHECK(spans[2].subjectRadius == 3.0f);
    }
    SECTION("a later overlapping shot takes the frame, as the director resolves it") {
        scene::CameraDirection d = twoShotCut();
        d.shots.push_back(shot(2, 8.0, 9.0)); // back to alpha for a second, inside beta's shot
        const auto spans = scene::authoredShotSpans(d);
        CHECK(pulseWindow(spans, 7.5, "beta"));
        CHECK(pulseWindow(spans, 8.5, "alpha"));
        CHECK_FALSE(pulseWindow(spans, 8.5, "beta"));
        CHECK(pulseWindow(spans, 9.5, "beta"));
        CHECK(travelWindow(spans, 8.2));   // the cut back to alpha travels
        CHECK(travelWindow(spans, 9.2));   // and so does the cut back to beta
    }
    SECTION("a cut to the same camera is not a camera change") {
        scene::CameraDirection d = twoShotCut();
        d.shots[1].camera = 2;
        const auto spans = scene::authoredShotSpans(d);
        CHECK_FALSE(travelWindow(spans, 6.5));
        CHECK(pulseWindow(spans, 6.5, "alpha")); // a new shot: its hold starts again at the cut
    }
    SECTION("a cut to a shot about nobody holds and travels nowhere") {
        scene::CameraDirection d = twoShotCut();
        d.cameras[2].followNode.clear();
        const auto spans = scene::authoredShotSpans(d);
        REQUIRE(spans.size() == 1);
        CHECK_FALSE(travelWindow(spans, 6.5));
    }
    SECTION("a blend longer than a cut's travel travels for the blend, never past its shot") {
        scene::CameraDirection d = twoShotCut();
        d.shots[1].transition = scene::ShotTransition::Blend;
        d.shots[1].blendSeconds = 4.0;
        auto spans = scene::authoredShotSpans(d);
        CHECK(spans[1].end == 10.0);
        d.shots[1].blendSeconds = 30.0;
        spans = scene::authoredShotSpans(d);
        CHECK(spans[1].end == 12.0);
    }
    SECTION("the default camera before the first shot is somebody's only when it aims at somebody") {
        scene::CameraDirection d = twoShotCut();
        for (auto& s : d.shots) {
            s.startSeconds += 3.0;
            s.endSeconds += 3.0;
        }
        CHECK_FALSE(pulseWindow(scene::authoredShotSpans(d), 1.0, "beta")); // the main camera: nobody
        CHECK(travelWindow(scene::authoredShotSpans(d), 3.5)); // main -> alpha is a cut to alpha all the same
        d.defaultCamera = 3;
        const auto spans = scene::authoredShotSpans(d);
        CHECK(pulseWindow(spans, 1.0, "beta"));
        CHECK(travelWindow(spans, 3.5));
        CHECK(spans.front().subject == "beta");
    }
    SECTION("no shots, no spans") {
        scene::CameraDirection d;
        d.ensureMainCamera();
        CHECK(scene::authoredShotSpans(d).empty());
    }
}

TEST_CASE("an authored cut fires each hero's pulse only in its own shot, and the travel beam at the cut",
          "[authoredcut][adr947][waves]") {
    if (!scoreWav()) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    authoredProject(engine);
    REQUIRE(engine.effects().size() == 3);
    const std::size_t alpha = 0;
    const std::size_t beta = 1;
    const std::size_t beam = 2;

    // Control arm: what the effects gated on before ADR-947. No director, so no spans, so nothing
    // could ever fire -- every CHECK below that expects Drawn fails on that input.
    REQUIRE(engine.shotSpans().empty());
    CHECK_FALSE(pulseWindow(engine.shotSpans(), 1.0, "alpha"));
    CHECK_FALSE(pulseWindow(engine.shotSpans(), 7.0, "beta"));
    CHECK_FALSE(travelWindow(engine.shotSpans(), 6.9));
    CHECK(engine.effectShotsFromAuthoredCut());

    // In alpha's shot, past the pulse's 0.35 s delay: alpha's ring, not beta's, no beam.
    seekTo(engine, 1.0);
    CHECK(drawn(engine, alpha));
    CHECK_FALSE(drawn(engine, beta));
    CHECK_FALSE(drawn(engine, beam));
    seekTo(engine, 4.4);
    CHECK(drawn(engine, alpha));
    CHECK_FALSE(drawn(engine, beta));

    // Just after the cut to beta: the beam sweeps toward beta, beta's ring starts, alpha's is gone.
    seekTo(engine, 6.9);
    CHECK(drawn(engine, beam));
    CHECK_FALSE(drawn(engine, alpha));
    seekTo(engine, 7.0);
    CHECK(drawn(engine, beta));
    CHECK_FALSE(drawn(engine, alpha));

    // Later in beta's shot: the travel is over, the hold is not.
    seekTo(engine, 10.4);
    CHECK_FALSE(drawn(engine, beam));
    CHECK(drawn(engine, beta));
    CHECK_FALSE(drawn(engine, alpha));

    // After the last shot nobody is held.
    seekTo(engine, 13.0);
    CHECK_FALSE(drawn(engine, alpha));
    CHECK_FALSE(drawn(engine, beta));

    SECTION("the travel beam points at the hero the cut hands off to") {
        const auto spans = engine.effectShots();
        world::EffectContext ctx;
        ctx.seconds = 6.9;
        ctx.cameraPosition = glm::vec3(40.0f, 20.0f, 60.0f);
        ctx.cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
        ctx.shots = spans;
        const std::vector<world::HeroPoint> heroes = engine.composition()->heroes();
        ctx.heroes = heroes;
        world::EffectInstance b = engine.effects()[beam];
        b.wave.propagation.direction = world::DirectionMode::SourceToTarget;
        const auto r = world::resolveWave(b, ctx);
        REQUIRE(r.has_value());
        const glm::vec3 toBeta = glm::normalize(glm::vec3(80.0f, 0.0f, 0.0f) - ctx.cameraPosition);
        CHECK(glm::dot(r->axis, toBeta) > 0.99f);
    }

    SECTION("an edit to the camera track is read on the next frame") {
        scene::CameraDirection d = engine.composition()->cameraDirection();
        d.shots[0].subject = "beta"; // "Focuses on: beta" on the first shot
        REQUIRE(engine.setCameraDirection(d).has_value());
        seekTo(engine, 1.0);
        CHECK(drawn(engine, beta));
        CHECK_FALSE(drawn(engine, alpha));
    }

    SECTION("a continuous take owns the frame, so the authored cut holds nobody") {
        engine.composition()->setContinuousTake(true);
        CHECK(engine.effectShots().empty());
        seekTo(engine, 1.0);
        CHECK_FALSE(drawn(engine, alpha));
    }
}

TEST_CASE("an authored cut's pulse lands on the same phase by seek, by play and by scrub",
          "[authoredcut][adr947][waves][determinism]") {
    if (!scoreWav()) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    constexpr int kFrames = 7 * 60 + 24; // 7.4 s: beta's shot, its ring on its first pass, the beam live
    const double at = kFrames / 60.0;

    app::Engine played(app::EngineMode::Offline);
    authoredProject(played);
    FixedStepClock clock(60.0);
    played.seekSeconds(0.0);
    clock.seek(0.0);
    for (int f = 0; f < kFrames; ++f) { // a seek to 0 then ticks: frame f lands at (f + 1) / 60
        played.update(played.tick(clock));
    }
    REQUIRE(played.timelineClock().seconds == Catch::Approx(at).margin(1e-9));
    const auto byPlay = wavesBytes(played);
    REQUIRE(played.scene().waves.count >= 2); // beta's ring and the beam: not a vacuous equality

    app::Engine sought(app::EngineMode::Offline);
    authoredProject(sought);
    seekTo(sought, at);
    CHECK(wavesBytes(sought) == byPlay);

    // A scrub: out past the end, back before the cut, then onto the frame.
    seekTo(sought, 20.0);
    seekTo(sought, 2.0);
    seekTo(sought, at);
    CHECK(wavesBytes(sought) == byPlay);

    // Control arm: one frame on, the ring has moved -- so equality above is about the phase.
    seekTo(sought, at + 1.0 / 60.0);
    CHECK(wavesBytes(sought) != byPlay);
}

TEST_CASE("Song mode's spans are what effects read, whatever the camera track says",
          "[authoredcut][adr947][director][glowmere]") {
    // The real thing: Glowmere Valley 2 - Multi-Camera, directed in Song mode, carrying the baked
    // focus schedule (`cameraShotSpans`, 45 spans) AND an authored camera track of its own (three
    // shots, and the Auto-director's camera as the default).
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path root(AVGEN_SOURCE_DIR);
    if (!std::filesystem::exists(root / "assets" / "aliens" / "alien-scout.glb")) {
        SKIP("Glowmere assets are not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(root / "examples" / "world" / "glowmere-valley-2-multicam.json").has_value());
    REQUIRE(engine.shotSpans().size() == 45);
    REQUIRE_FALSE(engine.composition()->cameraDirection().shots.empty());

    // The schedule effects read is the director's own list: the same storage, so the same spans.
    CHECK(engine.effectShots().data() == engine.shotSpans().data());
    CHECK(engine.effectShots().size() == engine.shotSpans().size());
    CHECK_FALSE(engine.effectShotsFromAuthoredCut());

    // Control arm: the film's camera track, read as an authored cut, is a DIFFERENT schedule -- so
    // the two checks above would fail if the authored cut ever took precedence over the director's.
    const auto authored = scene::authoredShotSpans(engine.composition()->cameraDirection());
    CHECK(authored.size() != engine.shotSpans().size());

    // The pulses fire exactly where the Song spans say: at 1 s into a hold, the held hero's own
    // Hero Pulse is drawn and a hero's that is not held is not.
    const std::vector<world::EffectInstance>& effects = engine.effects();
    const auto indexOf = [&](const std::string& owner) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < effects.size(); ++i) {
            if (effects[i].kind == world::EffectKind::GroundPulse && effects[i].owner.name == owner) {
                return i;
            }
        }
        return std::nullopt;
    };
    int checked = 0;
    for (const world::ShotSpan& span : engine.shotSpans()) {
        if (!span.spotlight || span.end - span.start < 3.0 || checked >= 4) {
            continue;
        }
        const auto held = indexOf(span.subject);
        if (!held) {
            continue;
        }
        seekTo(engine, span.start + 1.0);
        INFO("span " << span.start << " holds " << span.subject);
        CHECK(drawn(engine, *held));
        const auto other = indexOf(span.subject == "spire-cap" ? "bloom-cap" : "spire-cap");
        REQUIRE(other.has_value());
        CHECK_FALSE(drawn(engine, *other));
        ++checked;
    }
    CHECK(checked == 4);
#endif
}

// The measurement ADR-947 reports: which shots of a real project fire which Hero Pulses. Plays the
// project named by AVGEN_HEROFOCUS_PROJECT at 10 frames a second and prints, per camera-track shot,
// the pulses drawn in it, plus the travel spans. A probe, not a test: hidden, and it asserts nothing
// beyond the project loading.
TEST_CASE("probe: which shots of a project fire which Hero Pulses", "[.probe][adr947]") {
    const char* path = std::getenv("AVGEN_HEROFOCUS_PROJECT");
    if (path == nullptr) {
        SKIP("set AVGEN_HEROFOCUS_PROJECT");
    }
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(path);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    REQUIRE(engine.composition() != nullptr);
    const double duration = engine.durationSeconds();
    const auto& effects = engine.effects();
    std::printf("project %s: %.2f s, director spans %zu, effect spans %zu (%s)\n", path, duration,
                engine.shotSpans().size(), engine.effectShots().size(),
                engine.effectShotsFromAuthoredCut() ? "authored cut" : "director's schedule");
    for (const world::ShotSpan& s : engine.effectShots()) {
        if (s.travel) {
            std::printf("travel %7.2f-%7.2f  %s -> %s\n", s.start, s.end, s.subject.c_str(), s.handoff.c_str());
        }
    }
    const scene::CameraDirection direction = engine.composition()->cameraDirection();
    std::vector<std::vector<std::string>> firedIn(direction.shots.size());
    std::vector<double> drawnSeconds(effects.size(), 0.0);
    FixedStepClock clock(10.0);
    engine.seekSeconds(0.0);
    clock.restartAt(0.0);
    for (int f = 0;; ++f) {
        const FrameTime ft = engine.tick(clock);
        if (ft.renderTime >= duration) {
            break;
        }
        engine.update(ft);
        const double t = engine.timelineClock().seconds;
        for (std::size_t i = 0; i < effects.size(); ++i) {
            if (effects[i].kind != world::EffectKind::GroundPulse && effects[i].kind != world::EffectKind::TravelBeam) {
                continue;
            }
            if (engine.effectStatus()[i] != world::EffectStatus::Drawn) {
                continue;
            }
            drawnSeconds[i] += 0.1;
            const std::string who = effects[i].owner.isWorld() ? effects[i].name : effects[i].owner.name;
            for (std::size_t k = 0; k < direction.shots.size(); ++k) {
                if (direction.shots[k].contains(t) &&
                    std::find(firedIn[k].begin(), firedIn[k].end(), who) == firedIn[k].end()) {
                    firedIn[k].push_back(who);
                }
            }
        }
    }
    for (std::size_t k = 0; k < direction.shots.size(); ++k) {
        const scene::CameraShot& s = direction.shots[k];
        std::string list;
        for (const std::string& n : firedIn[k]) {
            list += (list.empty() ? "" : ", ") + n;
        }
        std::printf("shot %2zu %7.2f-%7.2f subject=%-12s fired=[%s]  %s\n", k + 1, s.startSeconds, s.endSeconds,
                    std::string(scene::shotSubject(direction, s)).c_str(), list.c_str(), s.label.c_str());
    }
    for (std::size_t i = 0; i < effects.size(); ++i) {
        if (effects[i].kind == world::EffectKind::GroundPulse || effects[i].kind == world::EffectKind::TravelBeam) {
            std::printf("effect %-28s drawn %6.1f s\n", effects[i].id.c_str(), drawnSeconds[i]);
        }
    }
}
