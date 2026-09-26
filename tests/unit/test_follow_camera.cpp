// ADR-911 and ADR-912: the follow camera's filtered subject reference, and cuts.
//
// The reference is a pure function of the subject's transform history (HIST), so the claims worth
// testing are about that function and about the plumbing that feeds it:
//
//   * the kernel is causal, finite, normalised and delays by exactly T, and the lead cancels a steady
//     walk's delay (the arithmetic);
//   * the vertical constant takes out the stride bob, and `followGround` follows a descent the
//     vertical constant alone lags (the two knobs the audit's s19 needed);
//   * the heading is the yaw alone, and unwraps across the seam;
//   * the soft floor never lets the eye under the surface and puts no step in its velocity;
//   * through a real composition: every knob reaches the picture, a scrub lands on the pose a play
//     does (ADR-267's 0.000022 m), and a 30 fps play draws the same camera as a 60 fps one;
//   * a cut to another camera changes `Scene::camera.cutSerial` and holds the skinned rigs, while a
//     blend, a new shot on the same camera and a steady frame do not; a keyed jump in the timeline
//     is found by the engine.
//
// Every case has a control arm that fails without the thing it tests.

#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/timeline.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"
#include "scene/follow_reference.hpp"
#include "signals/signal_bus.hpp"
#include "world/effects/history_bank.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <unistd.h>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
namespace fs = std::filesystem;

namespace {

constexpr double kStep = 1.0 / 60.0;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
// ADR-267: what a play and a seek of the same simulation agree to.
constexpr float kSeekExact = 0.000022f;

// ---- a hand-filled history ------------------------------------------------------------------------

using PositionAt = std::function<glm::vec3(double)>;
using RotationAt = std::function<glm::quat(double)>;

glm::quat identity() { return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); }

// One ring for "subject", sampled at `rate` from 0 up to, not including, `now`; the head is the
// subject at `now`. What a 60 Hz play leaves in HIST when a frame's camera is evaluated.
struct Trail {
    world::HistoryBank bank;
    world::HistorySample head;
    double now = 0.0;
    Trail(const PositionAt& position, double now_, double rate = 60.0, const RotationAt& rotation = nullptr)
        : now(now_) {
        const world::HistorySubscription subs[] = {{"subject", 16.0f}};
        REQUIRE(bank.subscribe(subs));
        for (long long k = 0;; ++k) {
            const double t = static_cast<double>(k) / rate;
            if (t >= now - 1e-9) {
                break;
            }
            bank.record(0, t, position(t), rotation ? rotation(t) : identity(), glm::vec3(1.0f));
        }
        head.t = now;
        head.position = position(now);
        head.rotation = rotation ? rotation(now) : identity();
    }
    [[nodiscard]] scene::SubjectTrail trail() const { return scene::SubjectTrail(&bank, 0, now, head); }
};

class Plane final : public scene::FollowGround {
public:
    explicit Plane(float slopeX) : slope_(slopeX) {}
    [[nodiscard]] float heightAt(glm::vec2 xz) const override { return slope_ * xz.x; }

private:
    float slope_;
};

double halfRange(const std::vector<double>& v) {
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    return 0.5 * (*hi - *lo);
}

float bob(double t) { return 0.18f * static_cast<float>(std::sin(kTwoPi * 1.03 * t)); }

// ---- a composition with a camera on something ------------------------------------------------------

fs::path fixtureDir() {
    const fs::path dir = fs::temp_directory_path() / ("avgen_follow_" + std::to_string(getpid()));
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::string& text) { std::ofstream(path) << text; }

// A scene with a node the test moves by hand ("subject"), optional terrain, and one authored rig on
// it that has the frame for the whole piece.
std::string scriptedScene(const nlohmann::json& rig, bool terrain) {
    nlohmann::json doc = nlohmann::json::parse(R"({ "format": "avgen-scene", "version": 1, "name": "follow",
      "camera": { "mode": 1, "position": [0, 5, 20], "target": [0, 1, 0], "fov": 50 },
      "nodes": [ { "kind": "orb", "name": "subject", "position": [0, 1, 0] } ] })");
    if (terrain) {
        doc["nodes"].push_back(nlohmann::json::parse(R"({ "name": "ground", "kind": "terrain",
          "world": { "name": "small", "size": [160, 160] },
          "terrain": { "chunkSize": 40.0, "resolution": 8, "lodLevels": 4, "lodDistance": 50.0, "viewDistance": 400.0 } })"));
    }
    nlohmann::json camera = rig;
    camera["id"] = 2;
    camera["name"] = "Follow";
    camera["slug"] = "follow";
    camera["placement"] = "free";
    camera["position"] = {0, 5, 20};
    camera["target"] = {0, 1, 0};
    camera["fov"] = 40.0;
    doc["cameraDirection"] = {{"cameras", nlohmann::json::array({{{"id", 1}, {"name", "Main"}}, camera})},
                              {"shots", nlohmann::json::array()},
                              {"default", 2},
                              {"nextId", 3}};
    return doc.dump(2);
}

// The HIST harness of test_history_bank, with the camera's needs subscribed the way the engine does.
struct Stage {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;
    world::HistoryBank bank;
    // A subject the test moves as a pure function of time, set on the node's parameter bases.
    // `spin` is Euler degrees, the node's own convention (`quatFromEulerDegrees`: Rz Ry Rx).
    PositionAt script;
    std::function<glm::vec3(double)> spin;

    Stage(const fs::path& dir, const std::string& file) : registry(dir) {
        auto loaded = scene::Composition::loadFile(file, registry);
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        for (const char* name : {"audio.bass", "audio.mid", "audio.rms", "audio.treble"}) {
            bus.declare(name, 0.0f, 1.0f);
        }
        bus.declare("audio.onset", 0.0f, 1.0f, true);
        comp->attach(params, modulator);
        comp->setViewport(640, 360);
        comp->scene().detailLimits.entityDistanceCull = false;
        std::vector<world::HistorySubscription> needs;
        comp->appendCameraHistoryNeeds(needs);
        if (!needs.empty()) {
            REQUIRE(bank.subscribe(needs));
        }
        comp->setHistoryBank(&bank);
    }
    void place(double t) {
        if (script) {
            params::IParameter* p = params.find("nodes/subject/position");
            REQUIRE(p != nullptr);
            const glm::vec3 v = script(t);
            p->setBaseComponent(0, v.x);
            p->setBaseComponent(1, v.y);
            p->setBaseComponent(2, v.z);
        }
        if (spin) {
            params::IParameter* r = params.find("nodes/subject/rotation");
            REQUIRE(r != nullptr);
            const glm::vec3 euler = spin(t);
            r->setBaseComponent(0, euler.x);
            r->setBaseComponent(1, euler.y);
            r->setBaseComponent(2, euler.z);
        }
    }
    void tickAt(double t, double dt, std::uint64_t frame) {
        FrameTime time;
        time.renderTime = t;
        time.deltaTime = dt;
        time.frameIndex = frame;
        place(t);
        params.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
        comp->recordHistory(bank, time.renderTime); // what Engine::update does after the controller
    }
    // Frames 0..last at `fps`, the application's convention (frame 0 at t = 0 with no delta).
    void play(double seconds, double fps, const std::function<void(double)>& each = nullptr, long long from = 0) {
        const auto last = static_cast<long long>(std::llround(seconds * fps));
        for (long long f = from; f <= last; ++f) {
            tickAt(static_cast<double>(f) / fps, f == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(f));
            if (each) {
                each(static_cast<double>(f) / fps);
            }
        }
    }
    void seekTo(double seconds) {
        comp->seekWithDirector(seconds, params,
                               entity::SeekBudget{.maxSeconds = 90.0,
                                                  .maxBodySteps = entity::SeekBudget::kEditorBodySteps,
                                                  .mode = entity::SeekMode::Checkpointed},
                               1.0 / 60.0);
    }
    [[nodiscard]] const scene::Camera& camera() const { return comp->scene().camera; }
};

} // namespace

// ---- the kernel -----------------------------------------------------------------------------------

TEST_CASE("the follow kernel is causal, finite, normalised and delays by exactly T", "[camera][follow][adr911]") {
    for (const double T : {0.1, 0.3, 0.8, 1.5}) {
        INFO("T = " << T);
        const double reach = scene::followKernelReach(T);
        CHECK_THAT(reach, WithinAbs(4.0 * T, kStep)); // cut at 8 / w = 4 T, on HIST's grid
        const auto last = static_cast<std::size_t>(std::llround(reach / kStep));
        double sum = 0.0;
        double mean = 0.0;
        bool nonNegative = true;
        for (std::size_t k = 0; k <= last; ++k) {
            const double w = scene::followKernelWeight(T, k);
            nonNegative = nonNegative && w >= 0.0;
            sum += w;
            mean += w * static_cast<double>(k) * kStep;
        }
        CHECK(nonNegative);
        CHECK(scene::followKernelWeight(T, last + 1) == 0.0); // finite: nothing past the cut
        // What the cut leaves out is e^-8 (1 + 8) = 0.3% of the weight; the reference renormalises.
        CHECK(sum > 0.99);
        CHECK(sum <= 1.0 + 1e-12);
        CHECK_THAT(mean / sum, WithinRel(T, 0.02)); // the mean delay is T
    }
    // 0 is the raw subject, and a vanishing constant approaches it rather than a one-step delay.
    CHECK(scene::followKernelWeight(0.0, 0) == 1.0);
    CHECK(scene::followKernelWeight(0.0, 1) == 0.0);
    CHECK(scene::followKernelWeight(1e-4, 0) > 0.999);
}

TEST_CASE("a steady walk: the reference trails by the kernel's delay, and a lead of 1 cancels it",
          "[camera][follow][adr911]") {
    const PositionAt walk = [](double t) { return glm::vec3(3.0f * static_cast<float>(t), 1.0f, -2.0f); };
    const Trail h(walk, 6.0);
    const scene::SubjectTrail trail = h.trail();

    scene::FollowFilter f;
    f.horizontalSeconds = 0.3;
    const scene::FollowReference lagged = scene::followReference(trail, 6.0, f, nullptr);
    // About T behind: 0.9 m at 3 m/s (the truncation shortens the delay by about 1%).
    CHECK_THAT(lagged.position.x, WithinAbs(3.0 * (6.0 - 0.3), 0.03));
    CHECK(lagged.position.x < 17.2f);

    f.lead = 1.0f;
    const scene::FollowReference led = scene::followReference(trail, 6.0, f, nullptr);
    CHECK_THAT(led.position.x, WithinAbs(18.0, 1e-3)); // exactly back on the subject
    CHECK_THAT(led.position.z, WithinAbs(-2.0, 1e-5));
    CHECK_THAT(led.position.y, WithinAbs(1.0, 1e-5)); // no vertical kernel asked for: the raw height

    f.lead = 0.5f;
    const scene::FollowReference half = scene::followReference(trail, 6.0, f, nullptr);
    CHECK_THAT(half.position.x, WithinAbs(0.5 * (led.position.x + lagged.position.x), 1e-3));

    // Control: no knob is the subject itself, bit for bit.
    const scene::FollowReference raw = scene::followReference(trail, 6.0, scene::FollowFilter{}, nullptr);
    CHECK(raw.position == walk(6.0));
}

TEST_CASE("the vertical constant takes out the stride bob, and the horizontal one does not touch it",
          "[camera][follow][adr911]") {
    // The audit's s03: an 18 cm bob at 1.03 Hz on a body walking at 3 m/s.
    const PositionAt walker = [](double t) {
        return glm::vec3(3.0f * static_cast<float>(t), 1.0f + bob(t), 0.0f);
    };
    const Trail h(walker, 12.0);
    const scene::SubjectTrail trail = h.trail();
    const auto heightSwing = [&](const scene::FollowFilter& f) {
        std::vector<double> ys;
        for (double t = 8.0; t <= 12.0; t += kStep) {
            ys.push_back(scene::followReference(trail, t, f, nullptr).position.y);
        }
        return halfRange(ys);
    };
    scene::FollowFilter raw;
    scene::FollowFilter vertical;
    vertical.verticalSeconds = 0.8;
    scene::FollowFilter horizontal;
    horizontal.horizontalSeconds = 0.3;
    horizontal.lead = 1.0f;
    const double swingRaw = heightSwing(raw);
    const double swingVertical = heightSwing(vertical);
    const double swingHorizontal = heightSwing(horizontal);
    INFO("bob swing: raw " << swingRaw << " m, vertical 0.8 s " << swingVertical << " m, horizontal only "
                           << swingHorizontal << " m");
    CHECK(swingRaw > 0.17);
    // A critically damped kernel passes 1 / (1 + (w / w0)^2) of a sine: 13% of a 1.03 Hz bob at 0.8 s.
    CHECK(swingVertical < 0.035);
    CHECK(swingVertical > 0.01);
    CHECK(swingHorizontal > 0.17); // control: the horizontal knob does not reach the height
}

TEST_CASE("followGround follows a descent that the vertical constant alone lags", "[camera][follow][adr911]") {
    // The audit's s19: 3 m/s down a 33% slope, with the bob. The vertical constant alone put the
    // subject a third of the frame low; the ground term is what it needed.
    const Plane slope(-0.33f);
    const PositionAt walker = [](double t) {
        const float x = 3.0f * static_cast<float>(t);
        return glm::vec3(x, (-0.33f * x) + 1.0f + bob(t), 0.0f);
    };
    const Trail h(walker, 10.0);
    const scene::SubjectTrail trail = h.trail();
    scene::FollowFilter f;
    f.horizontalSeconds = 0.3;
    f.verticalSeconds = 0.8;
    f.lead = 1.0f;
    scene::FollowFilter grounded = f;
    grounded.ground = true;
    double worstPlain = 0.0;
    double worstGround = 0.0;
    for (double t = 6.0; t <= 10.0; t += kStep) {
        // Where a camera should hold its height: the stance over the ground under the subject.
        const double truth = (-0.33 * 3.0 * t) + 1.0;
        worstPlain = std::max(worstPlain, std::abs(scene::followReference(trail, t, f, &slope).position.y - truth));
        worstGround =
            std::max(worstGround, std::abs(scene::followReference(trail, t, grounded, &slope).position.y - truth));
    }
    INFO("worst height error: vertical constant alone " << worstPlain << " m, with the ground " << worstGround << " m");
    // 0.8 s of a 0.99 m/s descent, against the horizontal constant's 0.3 s of it plus the bob's 2 cm.
    CHECK(worstPlain > 0.7);
    CHECK(worstGround < 0.34);

    // On level ground the term is exactly nothing, so it cannot disturb a flat walk.
    const Plane level(0.0f);
    const PositionAt flat = [](double t) { return glm::vec3(3.0f * static_cast<float>(t), 1.0f + bob(t), 0.0f); };
    const Trail l(flat, 10.0);
    const scene::SubjectTrail levelTrail = l.trail();
    CHECK(scene::followReference(levelTrail, 9.0, f, &level).position ==
          scene::followReference(levelTrail, 9.0, grounded, &level).position);
    // And with no ground to read -- a scene with no terrain -- it is the vertical constant alone.
    CHECK(scene::followReference(trail, 9.0, grounded, nullptr).position ==
          scene::followReference(trail, 9.0, f, nullptr).position);
}

TEST_CASE("the heading is the yaw alone, filtered, and unwraps across the seam", "[camera][follow][adr911]") {
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::quat yaw30 = glm::angleAxis(glm::radians(30.0f), up);
    // Sway, a nod and a slope tilt on top: the offset used to swing through all three.
    const glm::quat tilted = yaw30 * glm::angleAxis(glm::radians(12.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
                             glm::angleAxis(glm::radians(-5.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK_THAT(scene::headingOf(yaw30), WithinAbs(glm::radians(30.0f), 1e-5));
    CHECK_THAT(scene::headingOf(tilted), WithinAbs(glm::radians(30.0f), 1e-5));

    // A body spinning at 1 rad/s through +/-180 degrees.
    const auto yawAt = [](double t) { return std::numbers::pi - 0.3 + t; };
    const RotationAt rotation = [&](double t) { return glm::angleAxis(static_cast<float>(yawAt(t)), up); };
    const PositionAt still = [](double) { return glm::vec3(0.0f); };
    const Trail h(still, 3.0, 60.0, rotation);
    const scene::SubjectTrail trail = h.trail();
    scene::FollowFilter f;
    f.headingSeconds = 0.5;
    const auto wrap = [](double a) { return std::remainder(a, kTwoPi); };
    double previous = scene::followReference(trail, 1.0, f, nullptr).heading;
    double worstStep = 0.0;
    for (double t = 1.0 + kStep; t <= 3.0; t += kStep) {
        const double heading = scene::followReference(trail, t, f, nullptr).heading;
        worstStep = std::max(worstStep, std::abs(wrap(heading - previous)));
        previous = heading;
    }
    // Steady turning: the filtered heading trails by the kernel's delay and turns at the body's rate.
    const double expected = yawAt(3.0) - 0.5;
    CHECK(std::abs(wrap(previous - expected)) < 0.02);
    CHECK(worstStep < 1.5 * kStep); // 1 rad/s is 0.017 rad a frame: no jump anywhere across the seam
    // Control: averaging the wrapped angles themselves, as a naive filter would, lands on the far side
    // of the circle while the kernel straddles the seam.
    double naive = 0.0;
    double weights = 0.0;
    const double at = 0.3 + 0.2; // the seam is at t = 0.3; the kernel reaches back across it
    for (std::size_t k = 0; k <= static_cast<std::size_t>(std::llround(scene::followKernelReach(0.5) / kStep)); ++k) {
        const double w = scene::followKernelWeight(0.5, k);
        naive += w * static_cast<double>(scene::headingOf(rotation(at - (static_cast<double>(k) * kStep))));
        weights += w;
    }
    naive /= weights;
    const double filtered = scene::followReference(trail, at, f, nullptr).heading;
    INFO("filtered " << filtered << " rad against a naive average of wrapped angles " << naive);
    CHECK(std::abs(wrap(filtered - naive)) > 1.0);
}

TEST_CASE("a trail reads the history before now and the subject at now, and holds its ends", "[camera][follow][adr911]") {
    const PositionAt line = [](double t) { return glm::vec3(static_cast<float>(t), 0.0f, 0.0f); };
    Trail h(line, 2.0);
    // The sample a seek replay records at its landing instant, here with a value the head disagrees
    // with: a play has not recorded it yet when its camera is evaluated, so the trail must not read it.
    h.bank.record(0, 2.0, glm::vec3(99.0f), identity(), glm::vec3(1.0f));
    const scene::SubjectTrail trail = h.trail();
    REQUIRE(trail.hasHistory());
    CHECK(trail.at(2.0).position.x == 2.0f);
    CHECK_THAT(trail.at(2.0 - (0.5 * kStep)).position.x, WithinAbs(2.0 - (0.5 * kStep), 1e-5));
    CHECK(trail.at(-5.0).position.x == 0.0f); // before the film: where the subject was first seen
    CHECK(trail.at(7.0).position.x == 2.0f);  // past now: the head
    // No history at all -- no bank, or no ring -- is the head throughout.
    const scene::SubjectTrail bare(nullptr, 0, 2.0, h.head);
    CHECK_FALSE(bare.hasHistory());
    CHECK(bare.at(1.0).position.x == 2.0f);
    const scene::SubjectTrail noRing(&h.bank, 5, 2.0, h.head);
    CHECK_FALSE(noRing.hasHistory());
}

TEST_CASE("the clearance floor is soft: never under the surface, and no step in the eye's velocity",
          "[camera][follow][clearance][adr911]") {
    const float floor = 10.0f;
    bool neverUnder = true;
    for (int i = 0; i <= 1000; ++i) {
        const float y = 5.0f + (0.01f * static_cast<float>(i));
        neverUnder = neverUnder && scene::softFloor(y, floor) >= floor;
    }
    CHECK(neverUnder);
    CHECK(scene::softFloor(11.0f, floor) - 11.0f < 0.005f); // a metre clear: within 5 mm of the free eye
    CHECK_THAT(scene::softFloor(floor, floor), WithinAbs(floor + (0.25 * std::log(2.0)), 1e-5));

    // The audit's s20: the eye leaving the floor at 1.44 m/s, sampled at 60 fps, the crossing on a frame.
    std::vector<double> soft;
    std::vector<double> hard;
    for (int i = 0; i < 180; ++i) {
        const double y = (floor - 1.44) + (1.44 * static_cast<double>(i) / 60.0);
        soft.push_back(scene::softFloor(static_cast<float>(y), floor));
        hard.push_back(std::max(y, static_cast<double>(floor)));
    }
    const auto worstVelocityStep = [](const std::vector<double>& v) {
        double worst = 0.0;
        for (std::size_t i = 1; i + 1 < v.size(); ++i) {
            worst = std::max(worst, std::abs(((v[i + 1] - v[i]) - (v[i] - v[i - 1])) * 60.0));
        }
        return worst;
    };
    INFO("largest change of vertical velocity in one frame: max " << worstVelocityStep(hard) << " m/s, softplus "
                                                                   << worstVelocityStep(soft) << " m/s");
    CHECK(worstVelocityStep(hard) > 1.4); // the `max` it replaced: 0 to 1.44 m/s in one frame
    // The softplus: at most v^2 / (4 w) per second, 0.035 m/s a frame.
    CHECK(worstVelocityStep(soft) < 0.04);
}

TEST_CASE("a track jumps at a step or a cut's ramp, not along a dense fast move", "[timeline][cut][adr912]") {
    constexpr double kRamp = 2.0 * params::kCutRampSeconds;
    const params::KeyValue a{0.0f, 2.0f, 10.0f, 0.0f};
    const params::KeyValue b{50.0f, 20.0f, -30.0f, 0.0f};
    params::Track cut; // the sequence bake's cut: hold, a millisecond's ramp, hold
    cut.addKey({.time = 0.0, .value = a});
    cut.addKey({.time = 2.0 - params::kCutRampSeconds, .value = a});
    cut.addKey({.time = 2.0, .value = b});
    cut.addKey({.time = 4.0, .value = {52.0f, 20.0f, -30.0f, 0.0f}});
    CHECK(cut.jumpsWithin(2.0 - kStep, 2.0, kRamp));        // the frame that lands on the new shot
    CHECK_FALSE(cut.jumpsWithin(1.0, 1.0 + kStep, kRamp));  // inside the outgoing shot
    CHECK_FALSE(cut.jumpsWithin(2.0, 2.0 + kStep, kRamp));  // the frame after the cut
    CHECK_FALSE(cut.jumpsWithin(3.0, 3.0 + kStep, kRamp));  // inside the incoming shot, which moves

    params::Track zero; // two keys at one instant, which a JSON load keeps (the cinematic bake writes these)
    zero.keys = {{.time = 0.0, .value = a}, {.time = 3.0, .value = a}, {.time = 3.0, .value = b}, {.time = 5.0, .value = b}};
    CHECK(zero.jumpsWithin(3.0 - kStep, 3.0, kRamp));

    params::Track step;
    step.addKey({.time = 0.0, .value = a, .interp = params::KeyInterp::Step});
    step.addKey({.time = 1.5, .value = b});
    CHECK(step.jumpsWithin(1.5 - kStep, 1.5, kRamp));
    params::Track still; // a step to the value it already holds is no jump
    still.addKey({.time = 0.0, .value = a, .interp = params::KeyInterp::Step});
    still.addKey({.time = 1.5, .value = a});
    CHECK_FALSE(still.jumpsWithin(1.5 - kStep, 1.5, kRamp));

    // A fast move keyed densely -- a millisecond apart, as a short baked shot can be -- is motion:
    // every segment moves as fast as its neighbours. A cut in the middle of it is still found.
    params::Track dense;
    for (int i = 0; i <= 600; ++i) {
        const float x = i <= 300 ? 0.05f * static_cast<float>(i) : 500.0f + (0.05f * static_cast<float>(i));
        dense.addKey({.time = static_cast<double>(i) * 1e-3, .value = {x, 0.0f, 0.0f, 0.0f}});
    }
    CHECK_FALSE(dense.jumpsWithin(0.1, 0.1 + kStep, kRamp));
    CHECK_FALSE(dense.jumpsWithin(0.5, 0.5 + kStep, kRamp));
    CHECK(dense.jumpsWithin(0.3, 0.3 + kStep, kRamp));
    // A beat-based or looped track answers false rather than guessing.
    params::Track beats = cut;
    beats.timeBase = params::TimeBase::Beats;
    CHECK_FALSE(beats.jumpsWithin(2.0 - kStep, 2.0, kRamp));
}

TEST_CASE("the subject reference's knobs round-trip, and nonsense is refused", "[camera][follow][adr911]") {
    scene::CameraDirection d;
    d.ensureMainCamera();
    scene::CameraRig rig;
    rig.name = "Chase";
    rig.followNode = "subject";
    rig.aimNode = "subject";
    rig.followLocal = true;
    rig.followLagSeconds = 0.1;
    rig.followSmoothSeconds = 0.3;
    rig.followVerticalSmoothSeconds = 0.8;
    rig.followLead = 1.0f;
    rig.followGround = true;
    rig.followHeadingSmoothSeconds = 1.2;
    d.addCamera(rig);
    // An aim-only rig: the knobs filter whatever the rig reads, so they are written for it too.
    scene::CameraRig watcher;
    watcher.name = "Watcher";
    watcher.aimNode = "subject";
    watcher.followSmoothSeconds = 0.4;
    watcher.followVerticalSmoothSeconds = 0.6;
    d.addCamera(watcher);
    const nlohmann::json doc = d.toJson();
    CHECK(doc["cameras"][1]["followSmoothSeconds"] == 0.3);
    CHECK(doc["cameras"][1]["followGround"] == true);
    CHECK(doc["cameras"][1]["followHeadingSmoothSeconds"] == 1.2);
    CHECK(doc["cameras"][2]["followVerticalSmoothSeconds"] == 0.6);
    auto back = scene::CameraDirection::fromJson(doc);
    REQUIRE(back.has_value());
    CHECK(*back == d);

    // Written only when set: a follow rig with none of them serialises exactly as it did before.
    scene::CameraDirection plain;
    plain.ensureMainCamera();
    scene::CameraRig p;
    p.name = "Plain";
    p.followNode = "subject";
    plain.addCamera(p);
    const nlohmann::json plainRig = plain.toJson()["cameras"][1];
    for (const char* key : {"followSmoothSeconds", "followVerticalSmoothSeconds", "followLead", "followGround",
                            "followHeadingSmoothSeconds", "followLagSeconds"}) {
        INFO(key);
        CHECK_FALSE(plainRig.contains(key));
    }

    // How deep a rig reads: the lag, four of its longest constant, and two taps.
    const scene::CameraRig& chase = d.cameras[1];
    CHECK_THAT(chase.subjectHistorySeconds(), WithinAbs(0.1 + (4.0 * 1.2) + (2.0 * kStep), kStep));
    CHECK(chase.readsSubjectHistory());
    CHECK_FALSE(p.readsSubjectHistory());

    const auto refused = [&](const std::function<void(scene::CameraRig&)>& edit) {
        scene::CameraDirection bad = d;
        edit(bad.cameras[1]);
        return !bad.validate().has_value();
    };
    CHECK(refused([](scene::CameraRig& r) { r.followLead = 1.5f; }));
    CHECK(refused([](scene::CameraRig& r) { r.followLead = -0.1f; }));
    CHECK(refused([](scene::CameraRig& r) { r.followVerticalSmoothSeconds = -0.2; }));
    CHECK(refused([](scene::CameraRig& r) { r.followLagSeconds = -0.5; }));
    CHECK(refused([](scene::CameraRig& r) { r.followHeadingSmoothSeconds = 5.0; })); // 20 s of HIST: more than it keeps
    CHECK_FALSE(refused([](scene::CameraRig& r) { r.followSmoothSeconds = 2.0; }));   // 8 s is fine
}

// ---- through a composition -------------------------------------------------------------------------

TEST_CASE("every knob of the subject reference reaches the picture", "[camera][follow][adr911]") {
    const fs::path dir = fixtureDir();
    const auto stageWith = [&](const nlohmann::json& rig, const char* name, bool terrain = false) {
        writeFile(dir / name, scriptedScene(rig, terrain));
        return std::make_unique<Stage>(dir, name);
    };
    const nlohmann::json base = {{"followNode", "subject"}, {"followOffset", {0, 2, 6}},
                                 {"aimNode", "subject"},    {"aimOffset", {0, 1, 0}}};
    // A body walking along +x at 3 m/s with the stride bob.
    const PositionAt walker = [](double t) {
        return glm::vec3(3.0f * static_cast<float>(t), 1.0f + bob(t), 0.0f);
    };
    const auto run = [&](Stage& stage, double seconds) {
        stage.script = walker;
        stage.play(seconds, 60.0);
    };

    SECTION("followSmoothSeconds and followLead: the eye trails a walk by T, and a lead puts it back") {
        nlohmann::json smooth = base;
        smooth["followSmoothSeconds"] = 0.3;
        auto s = stageWith(smooth, "smooth.json");
        run(*s, 5.0);
        const float behind = walker(5.0).x - s->camera().position.x;
        smooth["followLead"] = 1.0;
        auto l = stageWith(smooth, "lead.json");
        run(*l, 5.0);
        auto raw = stageWith(base, "raw.json");
        run(*raw, 5.0);
        INFO("eye behind the subject: raw " << walker(5.0).x - raw->camera().position.x << " m, T 0.3 s " << behind
                                            << " m, with lead " << walker(5.0).x - l->camera().position.x << " m");
        CHECK(raw->camera().position.x == walker(5.0).x); // control: the raw node, as every rig was
        CHECK_THAT(behind, WithinAbs(0.9, 0.03));
        CHECK_THAT(l->camera().position.x, WithinAbs(walker(5.0).x, 2e-3));
    }
    SECTION("followVerticalSmoothSeconds: the stride bob leaves the eye") {
        std::vector<double> rawY;
        std::vector<double> smoothY;
        auto raw = stageWith(base, "raw.json");
        raw->script = walker;
        raw->play(8.0, 60.0, [&](double t) {
            if (t >= 4.0) {
                rawY.push_back(raw->camera().position.y);
            }
        });
        nlohmann::json v = base;
        v["followVerticalSmoothSeconds"] = 0.8;
        auto s = stageWith(v, "vertical.json");
        s->script = walker;
        s->play(8.0, 60.0, [&](double t) {
            if (t >= 4.0) {
                smoothY.push_back(s->camera().position.y);
            }
        });
        INFO("eye height swing: raw " << halfRange(rawY) << " m, 0.8 s " << halfRange(smoothY) << " m");
        CHECK(halfRange(rawY) > 0.17);
        CHECK(halfRange(smoothY) < 0.035);
    }
    SECTION("followLagSeconds reads HIST: the eye stands where the subject was") {
        nlohmann::json lag = base;
        lag["followLagSeconds"] = 0.5;
        auto s = stageWith(lag, "lag.json");
        run(*s, 5.0);
        CHECK_THAT(s->camera().position.x, WithinAbs(walker(4.5).x, 1e-3));
        CHECK_THAT(s->camera().target.x, WithinAbs(walker(5.0).x, 1e-4)); // the aim is not lagged
    }
    SECTION("followHeadingSmoothSeconds: followLocal swings round after a turn instead of snapping") {
        nlohmann::json local = base;
        local["followOffset"] = {0, 2, -4};
        local["followLocal"] = true;
        // A quarter turn at 2 s, with a 10 degree lean the offset used to follow.
        const std::function<glm::vec3(double)> turn = [](double t) {
            return glm::vec3(10.0f, t < 2.0 ? 0.0f : 90.0f, 0.0f);
        };
        const PositionAt stand = [](double) { return glm::vec3(0.0f, 1.0f, 0.0f); };
        const auto offsetAngle = [](const Stage& s) {
            const glm::vec3 d = s.camera().position - glm::vec3(0.0f, 1.0f, 0.0f);
            return std::atan2(-d.x, -d.z); // "behind" is -Z in the subject's frame
        };
        auto snap = stageWith(local, "snap.json");
        snap->script = stand;
        snap->spin = turn;
        snap->play(2.2, 60.0);
        local["followHeadingSmoothSeconds"] = 1.0;
        auto swing = stageWith(local, "swing.json");
        swing->script = stand;
        swing->spin = turn;
        swing->play(2.2, 60.0);
        INFO("0.2 s after a quarter turn the offset has turned " << glm::degrees(offsetAngle(*snap)) << " degrees raw, "
                                                                  << glm::degrees(offsetAngle(*swing)) << " filtered");
        CHECK_THAT(offsetAngle(*snap), WithinAbs(glm::radians(90.0f), 1e-3));
        CHECK(offsetAngle(*swing) > glm::radians(1.0f));
        CHECK(offsetAngle(*swing) < glm::radians(30.0f));
        // The lean is not the camera's: the offset stays level at the height it was authored at.
        CHECK_THAT(snap->camera().position.y, WithinAbs(3.0f, 1e-4));
        swing->play(8.0, 60.0, nullptr, 133); // on from 2.2 s: four constants later it has come round
        CHECK_THAT(offsetAngle(*swing), WithinAbs(glm::radians(90.0f), glm::radians(1.0f)));
    }
    SECTION("followGround and the soft floor reach the eye over real terrain") {
        auto probe = stageWith(base, "terrain.json", true);
        probe->tickAt(0.0, 0.0, 0);
        const world::TerrainQuery ground = probe->comp->terrainQuery();
        REQUIRE(ground.valid());
        // A walk across the generated terrain, standing on it.
        const PositionAt hiker = [&](double t) {
            const glm::vec2 xz(-40.0f + (3.0f * static_cast<float>(t)), 10.0f);
            return glm::vec3(xz.x, ground.heightAt(xz) + 1.0f + bob(t), xz.y);
        };
        double relief = 0.0;
        for (double t = 0.0; t <= 20.0; t += 0.5) {
            relief = std::max(relief, std::abs(static_cast<double>(hiker(t).y - hiker(0.0).y)));
        }
        REQUIRE(relief > 1.0); // the walk climbs or descends, or this proves nothing

        nlohmann::json g = base;
        g["followSmoothSeconds"] = 0.3;
        g["followVerticalSmoothSeconds"] = 0.8;
        g["followLead"] = 1.0;
        std::vector<double> plainError;
        std::vector<double> groundError;
        auto plain = stageWith(g, "plain.json", true);
        plain->script = hiker;
        plain->play(20.0, 60.0, [&](double t) {
            if (t >= 5.0) {
                plainError.push_back(std::abs(plain->camera().position.y - (hiker(t).y - bob(t) + 2.0f)));
            }
        });
        g["followGround"] = true;
        auto grounded = stageWith(g, "grounded.json", true);
        grounded->script = hiker;
        grounded->play(20.0, 60.0, [&](double t) {
            if (t >= 5.0) {
                groundError.push_back(std::abs(grounded->camera().position.y - (hiker(t).y - bob(t) + 2.0f)));
            }
        });
        const double worstPlain = *std::max_element(plainError.begin(), plainError.end());
        const double worstGround = *std::max_element(groundError.begin(), groundError.end());
        INFO("worst eye height error over the terrain: vertical constant alone " << worstPlain << " m, with the ground "
                                                                                 << worstGround << " m");
        CHECK(worstGround < worstPlain);
        CHECK(std::abs(worstGround - worstPlain) > 0.01); // it reached the eye

        // The soft floor. An offset chosen so the free eye stands exactly on the floor at 6 s: a
        // `max` would put it there, the softplus a quarter-metre's ln 2 above -- which is how this
        // case knows which of the two ran.
        const glm::vec3 at6 = hiker(6.0);
        const float floorAt6 = ground.surfaceAt(glm::vec2(at6.x, at6.z + 6.0f)) + 1.0f;
        nlohmann::json low = base;
        low["followOffset"] = {0.0, static_cast<double>(floorAt6 - at6.y), 6.0};
        low["followClearance"] = 1.0;
        auto floored = stageWith(low, "floored.json", true);
        floored->script = hiker;
        bool neverUnder = true;
        floored->play(6.0, 60.0, [&](double) {
            const glm::vec3 eye = floored->camera().position;
            neverUnder = neverUnder && eye.y >= ground.surfaceAt(glm::vec2(eye.x, eye.z)) + 1.0f - 1e-4f;
        });
        CHECK(neverUnder);
        CHECK_THAT(floored->camera().position.y, WithinAbs(floorAt6 + (0.25 * std::log(2.0)), 2e-3));
    }
    fs::remove_all(dir);
}

TEST_CASE("a follow camera read from HIST lands on the same pose played and scrubbed",
          "[camera][follow][seek][determinism][adr911]") {
    // A subject the SIMULATION moves (an orbit integrator with a drift on top), so the scrub has to
    // replay it -- and the camera's history with it -- rather than recompute a function of time.
    const fs::path dir = fixtureDir();
    nlohmann::json doc = nlohmann::json::parse(R"({ "format": "avgen-scene", "version": 1, "name": "chase",
      "camera": { "mode": 1, "position": [0, 5, 30], "target": [0, 1, 0], "fov": 50 },
      "nodes": [ { "kind": "orb", "name": "craft", "position": [0, 6, 0] } ],
      "entities": [ { "name": "craft", "node": "craft", "seed": 7,
                      "behaviors": [ { "kind": "orbit", "radius": 15.0, "rate": 20.0, "authority": "simulation" },
                                     { "kind": "drift", "radius": 1.5, "rate": 0.4 } ] } ] })");
    const nlohmann::json rig = {{"id", 2},
                                {"name", "Chase"},
                                {"slug", "chase"},
                                {"placement", "free"},
                                {"position", {0, 5, 30}},
                                {"target", {0, 1, 0}},
                                {"fov", 45.0},
                                {"followNode", "craft"},
                                {"followOffset", {0, 2, -6}},
                                {"followLocal", true},
                                {"followLagSeconds", 0.3},
                                {"followSmoothSeconds", 0.4},
                                {"followVerticalSmoothSeconds", 0.8},
                                {"followLead", 0.5},
                                {"followHeadingSmoothSeconds", 0.8},
                                {"aimNode", "craft"},
                                {"aimOffset", {0, 1, 0}}};
    doc["cameraDirection"] = {{"cameras", nlohmann::json::array({{{"id", 1}, {"name", "Main"}}, rig})},
                              {"shots", nlohmann::json::array()},
                              {"default", 2},
                              {"nextId", 3}};
    writeFile(dir / "chase.json", doc.dump(2));
    constexpr double kLate = 12.5;
    const auto landing = static_cast<std::uint64_t>(std::llround(kLate * 60.0));

    Stage played(dir, "chase.json");
    REQUIRE(played.bank.find("craft") < played.bank.ringCount()); // the rig subscribed its subject
    CHECK(played.bank.ringSeconds(played.bank.find("craft")) >= 0.3f + (4.0f * 0.8f));
    played.play(kLate, 60.0);
    const scene::Camera atPlay = played.camera();

    Stage scrubbed(dir, "chase.json");
    scrubbed.seekTo(kLate);
    REQUIRE(scrubbed.comp->entityWorld().lastSeekWork().exact);
    scrubbed.tickAt(kLate, 0.0, landing); // a render's first frame after its seek
    const scene::Camera atSeek = scrubbed.camera();
    INFO("eye " << glm::length(atSeek.position - atPlay.position) << " m, target "
                << glm::length(atSeek.target - atPlay.target) << " m apart");
    CHECK(glm::length(atSeek.position - atPlay.position) <= kSeekExact);
    CHECK(glm::length(atSeek.target - atPlay.target) <= kSeekExact);
    // And every frame after it.
    float worst = 0.0f;
    for (std::uint64_t f = landing + 1; f <= landing + 60; ++f) {
        const double t = static_cast<double>(f) / 60.0;
        played.tickAt(t, 1.0 / 60.0, f);
        scrubbed.tickAt(t, 1.0 / 60.0, f);
        worst = std::max({worst, glm::length(played.camera().position - scrubbed.camera().position),
                          glm::length(played.camera().target - scrubbed.camera().target)});
    }
    CHECK(worst <= kSeekExact);

    // Control 1: the reference is doing something. The raw node plus the offset is centimetres away.
    const scene::CompositionNode* craft = played.comp->findNode("craft");
    REQUIRE(craft != nullptr);
    const glm::vec3 rawTarget = played.comp->nodeWorldTransform(*craft).position + glm::vec3(0.0f, 1.0f, 0.0f);
    CHECK(glm::length(played.camera().target - rawTarget) > 0.05f);
    // Control 2: a scrub that did not replay the history lands somewhere else -- the old FollowTrail's
    // "un-lagged after a seek", which is exactly what this ADR removes.
    Stage forgetful(dir, "chase.json");
    forgetful.comp->setHistoryBank(nullptr);
    forgetful.seekTo(kLate);
    forgetful.comp->setHistoryBank(&forgetful.bank);
    forgetful.tickAt(kLate, 0.0, landing);
    CHECK(glm::length(forgetful.camera().position - atPlay.position) > 0.01f);
    fs::remove_all(dir);
}

TEST_CASE("a 30 fps play and a 60 fps play draw the same follow camera", "[camera][follow][framerate][adr911]") {
    // The subject is a pure function of time, so the only thing that differs between the two plays
    // is how often HIST is sampled -- which is the camera's business, not the simulation's.
    const fs::path dir = fixtureDir();
    const nlohmann::json rig = {{"followNode", "subject"},     {"followOffset", {-5, 2.4, 4}},
                                {"aimNode", "subject"},        {"aimOffset", {0, 1.9, 0}},
                                {"followSmoothSeconds", 0.3},  {"followVerticalSmoothSeconds", 0.8},
                                {"followLead", 1.0}};
    writeFile(dir / "rates.json", scriptedScene(rig, false));
    const PositionAt walker = [](double t) {
        const auto s = static_cast<float>(t);
        return glm::vec3(3.0f * s, 1.0f + bob(t), 2.0f * std::sin(0.4f * s));
    };
    std::vector<glm::vec3> eye60;
    std::vector<glm::vec3> eye30;
    std::vector<double> bob60;
    Stage at60(dir, "rates.json");
    at60.script = walker;
    at60.play(6.0, 60.0, [&](double t) {
        eye60.push_back(at60.camera().position);
        if (t >= 3.0) {
            bob60.push_back(at60.camera().position.y);
        }
    });
    Stage at30(dir, "rates.json");
    at30.script = walker;
    at30.play(6.0, 30.0, [&](double) { eye30.push_back(at30.camera().position); });
    REQUIRE(eye60.size() == 361);
    REQUIRE(eye30.size() == 181);
    float worst = 0.0f;
    float oneFrameLate = 0.0f;
    for (std::size_t i = 60; i < eye30.size(); ++i) { // from 2 s: past the kernel's reach from the start
        worst = std::max(worst, glm::length(eye30[i] - eye60[2 * i]));
        oneFrameLate = std::max(oneFrameLate, glm::length(eye30[i] - eye60[(2 * i) - 1]));
    }
    INFO("worst eye difference 30 vs 60 fps " << worst << " m; against the 60 fps camera one frame late "
                                              << oneFrameLate << " m");
    CHECK(worst < 0.003f);
    // Control: 3 mm is tight enough to see one 60 fps frame of delay (5 cm at this speed).
    CHECK(oneFrameLate > 0.03f);
    // And the filter is doing its job at both rates: the 18 cm bob is a couple of centimetres.
    CHECK(halfRange(bob60) < 0.035);
    fs::remove_all(dir);
}

TEST_CASE("a cut to another camera changes the cut serial; a blend, a same-camera shot and a steady frame do not",
          "[camera][cut][adr912]") {
    const fs::path dir = fixtureDir();
    nlohmann::json doc = nlohmann::json::parse(scriptedScene({{"position", {0, 5, 20}}}, false));
    doc["cameraDirection"]["cameras"].push_back({{"id", 3},
                                                 {"name", "Other"},
                                                 {"slug", "other"},
                                                 {"placement", "free"},
                                                 {"position", {30, 8, -10}},
                                                 {"target", {0, 1, 0}},
                                                 {"fov", 35.0}});
    doc["cameraDirection"]["nextId"] = 4;
    doc["cameraDirection"]["default"] = 1;
    doc["cameraDirection"]["shots"] = nlohmann::json::array(
        {{{"camera", 2}, {"start", 0.0}, {"end", 2.0}, {"transition", "cut"}},
         {{"camera", 3}, {"start", 2.0}, {"end", 4.0}, {"transition", "cut"}},
         {{"camera", 3}, {"start", 4.0}, {"end", 6.0}, {"transition", "cut"}},                   // same camera
         {{"camera", 2}, {"start", 6.0}, {"end", 8.0}, {"transition", "blend"}, {"blend", 1.0}}}); // a blend
    writeFile(dir / "cuts.json", doc.dump(2));
    Stage stage(dir, "cuts.json");
    std::vector<double> cuts;
    std::uint32_t serial = 0;
    bool first = true;
    stage.play(9.0, 60.0, [&](double t) {
        const std::uint32_t now = stage.camera().cutSerial;
        CHECK(now == stage.comp->cameraCutSerial()); // published every frame
        if (!first && now != serial) {
            cuts.push_back(t);
        }
        first = false;
        serial = now;
    });
    // At 2 s (camera 2 to 3, a cut) and at 8 s (the shots end: back to the default camera, a cut).
    // Not at 4 s (a new shot on the same camera) and not across the blend from 6 s.
    REQUIRE(cuts.size() == 2);
    CHECK_THAT(cuts[0], WithinAbs(2.0, 1e-9));
    CHECK_THAT(cuts[1], WithinAbs(8.0, 1e-9));

    // A cut's frame draws no joint motion either: the skinned rigs are held on their current pose.
    scene::SkinnedRig rig;
    rig.palette = {glm::mat4(2.0f)};
    rig.previousPalette = {glm::mat4(1.0f)};
    stage.comp->scene().rigs.push_back(rig);
    const std::uint64_t version = stage.comp->scene().rigs.back().paletteVersion;
    stage.comp->markCameraCut(); // the engine's entry for a keyed jump
    CHECK(stage.camera().cutSerial == serial + 1);
    CHECK(stage.comp->scene().rigs.back().previousPalette == stage.comp->scene().rigs.back().palette);
    CHECK(stage.comp->scene().rigs.back().paletteVersion != version);
    stage.comp->markCameraCut(); // the same frame again: one cut, not two
    CHECK(stage.camera().cutSerial == serial + 1);
    fs::remove_all(dir);
}

TEST_CASE("the engine records the follow camera's subject in HIST and finds a keyed cut",
          "[camera][cut][follow][integration][adr911][adr912]") {
    const fs::path dir = fixtureDir();
    const nlohmann::json smoothed = {{"followNode", "subject"}, {"followOffset", {0, 2, 6}},
                                     {"followSmoothSeconds", 0.3}, {"followVerticalSmoothSeconds", 0.8}};
    writeFile(dir / "engine.json", scriptedScene(smoothed, false));
    writeFile(dir / "plainrig.json", scriptedScene({{"followNode", "subject"}, {"followOffset", {0, 2, 6}}}, false));

    SECTION("a smoothed rig's subject is subscribed as deep as the rig reads; a plain rig's is not") {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadFile(dir / "engine.json").has_value());
        const world::HistoryBank& bank = engine.historyBank();
        const std::size_t ring = bank.find("subject");
        REQUIRE(ring < bank.ringCount());
        CHECK(bank.ringSeconds(ring) >= static_cast<float>(4.0 * 0.8));
        app::Engine plain(app::EngineMode::Offline);
        REQUIRE(plain.loadFile(dir / "plainrig.json").has_value());
        CHECK(plain.historyBank().find("subject") >= plain.historyBank().ringCount());
    }
    SECTION("a millisecond ramp in the main camera's track is a cut; a dense fast move is not") {
        writeFile(dir / "main.json", R"({ "format": "avgen-scene", "version": 1, "name": "stage",
            "camera": { "mode": 1, "position": [0, 2, 10], "target": [0, 1, 0], "fov": 50 },
            "nodes": [ { "name": "orb", "kind": "orb" } ] })");
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadFile(dir / "main.json").has_value());
        params::Track track;
        track.target = "camera/position";
        track.addKey({.time = 0.0, .value = {0.0f, 2.0f, 10.0f}});
        // A fast dolly keyed a millisecond apart from 0.5 s to 0.8 s: motion, not a cut.
        for (int i = 0; i <= 300; ++i) {
            track.addKey({.time = 0.5 + (static_cast<double>(i) * 1e-3),
                          .value = {0.1f * static_cast<float>(i), 2.0f, 10.0f}});
        }
        // The sequence bake's cut at 2 s.
        track.addKey({.time = 2.0 - params::kCutRampSeconds, .value = {30.0f, 2.0f, 10.0f}});
        track.addKey({.time = 2.0, .value = {-40.0f, 12.0f, -30.0f}});
        track.addKey({.time = 3.0, .value = {-41.0f, 12.0f, -30.0f}});
        engine.timeline().addTrack(track);
        engine.rebind();
        FixedStepClock clock(60.0);
        std::vector<double> cuts;
        std::uint32_t serial = 0;
        bool firstFrame = true;
        for (int f = 0; f <= 180; ++f) {
            const FrameTime time = engine.tick(clock);
            engine.update(time);
            const std::uint32_t now = engine.composition()->scene().camera.cutSerial;
            if (!firstFrame && now != serial) {
                cuts.push_back(time.renderTime);
            }
            firstFrame = false;
            serial = now;
        }
        REQUIRE(cuts.size() == 1);
        CHECK_THAT(cuts[0], WithinAbs(2.0, 1e-9));
        // And the picture did jump there, which is what the serial is a statement about.
        CHECK_THAT(engine.composition()->scene().camera.position.x, WithinAbs(-41.0f, 1e-3f));
    }
    fs::remove_all(dir);
}
