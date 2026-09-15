// The four animated aliens (ADR-192, ADR-198): what their clips actually contain, how the player
// plays them, and whether the number a scene authors as a stride speed describes the clip it names.
//
// Three things are checked here that nothing checked before.
//
//   1. **A clip's playable range starts where its keys start.** Every clip in the alien pack has
//      its first key at 1/30 s, not at 0 -- the exporter wrote frame 1 rather than frame 0. The
//      player looped over [0, lastKey], so every cycle carried 33 ms of held first pose that no
//      animator authored, and every loop took 3.2% (walk) / 4.8% (run) longer than the animation.
//      The Mixamo character this engine was built against starts at 0, which is why it survived.
//
//   2. **A body's drawn facing must follow the body.** `EntityState::yaw` is an accumulator and the
//      node rotation parameter clamps at +/-360 degrees, so a character that had net-turned through
//      a revolution was drawn facing wherever the clamp left it while it walked somewhere else.
//
//   3. **An authored stride speed is a claim about a clip, and it has to be checked against the
//      clip.** `Gait::footSlip` (ADR-161) compares the behaviour's travel speed with
//      `GaitSettings::walkSpeed`; it cannot see that `walkSpeed` itself is wrong, because it is
//      both the expected value and half the actual one. The measurement here is independent: the
//      speed of the foot that is on the ground.
//
// The stride estimator. For an in-place cycle the planted foot is stationary in world space, so in
// the character's own frame it travels backwards at exactly the body's speed. The median backward
// speed of a toe while that toe is in the bottom fifth of its height range is that number, and it is
// stable: measured off the raw glTF accessors in Python at 1.5807 (Walking) and 3.81 (Running)
// model units per second, unchanged across contact thresholds from 10% to 30% of the toe's range.
// Those two literals are the independent expected values the estimator below is pinned against --
// they were not produced by any code in this repository.

#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "assets/gltf_loader.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/gait.hpp"
#include "entity/grounding.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/animation.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"
#include "signals/signal_bus.hpp"
#include "entity/navigation.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// ---- the cast ---------------------------------------------------------------------------------

struct Cast {
    const char* entity;
    const char* asset;
};
// Verified against examples/world/glowmere-valley-2.scene.json rather than assumed; see
// `the scene names the four aliens this file measures`.
constexpr std::array<Cast, 4> kCast{{{"rook", "alien-scout.glb"},
                                     {"tide", "alien-diver.glb"},
                                     {"sage", "alien-elder.glb"},
                                     {"ember", "alien-ranger.glb"}}};

// The activity -> clip map every one of the four declares. Only these states are exercised: this
// pass adds no animation types and invents no clips.
constexpr std::array<const char*, 5> kLocomotorClips{{"Idle", "Walking", "Running", "Idle_turn",
                                                      "Fall_loop"}};

fs::path alienDir() { return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens"; }
fs::path valleyScene() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.scene.json";
}
bool aliensPresent() { return fs::exists(alienDir() / "alien-scout.glb"); }

scene::Scene loadAlien(const char* file) {
    scene::Scene s;
    const auto summary = assets::loadGltf(alienDir() / file, s, {});
    REQUIRE(summary.has_value());
    REQUIRE(s.rigs.size() == 1);
    return s;
}

const scene::AnimationClip& clipNamed(const scene::SkinnedRig& rig, const char* name) {
    const int index = rig.findClip(name);
    REQUIRE(index >= 0);
    return rig.clips[static_cast<std::size_t>(index)];
}

// The first and last key times across every channel -- what the asset actually covers, read from
// the channels rather than from whatever the clip says its duration is.
struct KeySpan {
    float first = 0.0f;
    float last = 0.0f;
    [[nodiscard]] float period() const { return last - first; }
};
KeySpan keySpan(const scene::AnimationClip& clip) {
    KeySpan span{1e9f, -1e9f};
    for (const scene::AnimationChannel& c : clip.channels) {
        if (c.times.empty()) {
            continue;
        }
        span.first = std::min(span.first, c.times.front());
        span.last = std::max(span.last, c.times.back());
    }
    return span;
}

// ---- pose helpers -----------------------------------------------------------------------------

std::vector<glm::mat4> paletteAt(const scene::SkinnedRig& rig, const char* clipName, float clipTime) {
    scene::Pose pose;
    scene::setRestPose(rig.skeleton, pose);
    scene::sampleClip(clipNamed(rig, clipName), clipTime, pose);
    std::vector<glm::mat4> scratch;
    std::vector<glm::mat4> out;
    scene::skinningPalette(rig.skeleton, pose, scratch, out);
    return out;
}

// The pose the *player* produces at timeline second `now`, which is the thing that is drawn.
std::vector<glm::mat4> playedPalette(scene::SkinnedRig& rig, double now) {
    scene::Pose pose;
    scene::Pose scratch;
    rig.player.evaluate(rig.clips, rig.skeleton, now, pose, scratch);
    std::vector<glm::mat4> model;
    std::vector<glm::mat4> out;
    scene::skinningPalette(rig.skeleton, pose, model, out);
    return out;
}

float paletteDelta(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b) {
    REQUIRE(a.size() == b.size());
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                worst = std::max(worst, std::abs(a[i][c][r] - b[i][c][r]));
            }
        }
    }
    return worst;
}

bool finite(const std::vector<glm::mat4>& p) {
    for (const glm::mat4& m : p) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (!std::isfinite(m[c][r])) {
                    return false;
                }
            }
        }
    }
    return true;
}

// ---- the stride estimator ---------------------------------------------------------------------

// Model-space position of `joint` with `clip` sampled at `t`.
glm::vec3 jointAt(const scene::SkinnedRig& rig, const scene::AnimationClip& clip, int joint, float t,
                  scene::Pose& pose, std::vector<glm::mat4>& model) {
    scene::setRestPose(rig.skeleton, pose);
    scene::sampleClip(clip, t, pose);
    scene::poseToModel(rig.skeleton, pose, model);
    return glm::vec3(model[static_cast<std::size_t>(joint)][3]);
}

// The ground speed a clip implies, in model units per second: the median speed at which a toe
// travels backwards along the clip's forward axis while that toe is on the ground. Contact is "the
// bottom `contactFraction` of this toe's own height range over the cycle", which is a property of
// the take rather than a tuned number -- the answer is the same for any fraction from 0.10 to 0.30.
//
// Returns 0 when the rig has no toe joints, which is the honest answer for a skeleton this cannot
// be measured on rather than a number nobody should trust.
float clipStrideSpeed(const scene::SkinnedRig& rig, const char* clipName, float contactFraction = 0.2f) {
    const scene::AnimationClip& clip = clipNamed(rig, clipName);
    const KeySpan span = keySpan(clip);
    // Sampled on the take's own key grid: 30 fps, which is what the exporter wrote.
    std::vector<float> times;
    for (float t = span.first; t <= span.last + 1e-4f; t += 1.0f / 30.0f) {
        times.push_back(t);
    }
    scene::Pose pose;
    std::vector<glm::mat4> model;
    std::vector<float> speeds;
    for (const char* toe : {"toes_01.l", "toes_01.r"}) {
        const int joint = rig.skeleton.find(toe);
        if (joint < 0) {
            continue;
        }
        std::vector<glm::vec3> track;
        track.reserve(times.size());
        for (const float t : times) {
            track.push_back(jointAt(rig, clip, joint, t, pose, model));
        }
        float lo = track.front().y;
        float hi = track.front().y;
        for (const glm::vec3& p : track) {
            lo = std::min(lo, p.y);
            hi = std::max(hi, p.y);
        }
        const float threshold = lo + (hi - lo) * contactFraction;
        for (std::size_t k = 0; k + 1 < track.size(); ++k) {
            if (track[k].y > threshold || track[k + 1].y > threshold) {
                continue;
            }
            const float dt = times[k + 1] - times[k];
            const float v = -(track[k + 1].z - track[k].z) / dt;
            if (v > 0.0f) {
                speeds.push_back(v);
            }
        }
    }
    if (speeds.empty()) {
        return 0.0f;
    }
    std::sort(speeds.begin(), speeds.end());
    return speeds[speeds.size() / 2];
}

// ---- the scene file ---------------------------------------------------------------------------

// How far an entity's own secondary motion is authored to swing its drawn facing away from its
// heading, in radians. `liveliness`'s `sway` writes `MotionOffset::rotation.y` in degrees, and a
// scene may drive it further through a reaction; the drawn body is allowed to differ from the
// travelling body by exactly that and by nothing else.
float authoredYawWobble(const nlohmann::json& entity) {
    float sway = 0.0f;
    for (const nlohmann::json& b : entity.at("behaviors")) {
        if (b.value("kind", std::string()) == "liveliness") {
            sway += b.value("sway", 0.0f);
        }
    }
    if (entity.contains("reactions")) {
        for (const nlohmann::json& r : entity.at("reactions")) {
            if (r.value("target", std::string()) == "liveliness/sway") {
                sway += std::abs(r.value("depth", 0.0f));
            }
        }
    }
    return sway / 57.2957795f;
}

nlohmann::json sceneJson() {
    std::ifstream in(valleyScene());
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

const nlohmann::json* findNamed(const nlohmann::json& list, const char* name) {
    for (const nlohmann::json& item : list) {
        if (item.value("name", std::string()) == name) {
            return &item;
        }
    }
    return nullptr;
}

} // namespace

// =================================================================================================
// 1. Inventory: what the four aliens actually carry
// =================================================================================================

TEST_CASE("the scene names the four aliens this file measures", "[aliens][inventory]") {
    const nlohmann::json doc = sceneJson();
    for (const Cast& c : kCast) {
        INFO(c.entity);
        const nlohmann::json* node = findNamed(doc.at("nodes"), c.entity);
        REQUIRE(node != nullptr);
        CHECK(node->at("kind") == "gltf");
        const std::string asset = node->at("asset");
        CHECK(asset.find(c.asset) != std::string::npos);
        REQUIRE(findNamed(doc.at("entities"), c.entity) != nullptr);
    }
}

TEST_CASE("the four aliens carry one skeleton and the same clip list", "[aliens][inventory]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    std::vector<std::string> reference;
    for (const Cast& c : kCast) {
        INFO(c.asset);
        const scene::Scene s = loadAlien(c.asset);
        const scene::SkinnedRig& rig = s.rigs.front();
        CHECK(rig.skeleton.paletteSize() == 89);
        CHECK(rig.clips.size() == 26);
        CHECK(rig.skeleton.valid());
        // The joints locomotion is measured on exist on every variant.
        CHECK(rig.skeleton.find("root.x") >= 0);
        CHECK(rig.skeleton.find("toes_01.l") >= 0);
        CHECK(rig.skeleton.find("toes_01.r") >= 0);

        std::vector<std::string> names;
        for (const scene::AnimationClip& clip : rig.clips) {
            names.push_back(clip.name);
            INFO("clip " << clip.name);
            CHECK(clip.valid());
            // Every clip carries all three tracks for all 89 joints. A channel count that is not
            // 3 x 89 means the exporter dropped something.
            CHECK(clip.channels.size() == 89 * 3);
        }
        std::sort(names.begin(), names.end());
        if (reference.empty()) {
            reference = names;
        } else {
            CHECK(names == reference);
        }
    }
}

TEST_CASE("the alien clips are in-place: there is no root motion to extract", "[aliens][inventory]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    // ADR-161 and ADR-194 both claim this. It is the premise the whole code-driven locomotion path
    // rests on, so it is measured here rather than cited: if a clip ever gains root translation,
    // this is the test that says so.
    for (const Cast& c : kCast) {
        const scene::Scene s = loadAlien(c.asset);
        const scene::SkinnedRig& rig = s.rigs.front();
        const int root = rig.skeleton.find("root.x");
        REQUIRE(root >= 0);
        for (const char* name : {"Idle", "Walking", "Running", "Idle_turn", "Fall_loop"}) {
            INFO(c.asset << " / " << name);
            const scene::AnimationClip& clip = clipNamed(rig, name);
            const KeySpan span = keySpan(clip);
            scene::Pose pose;
            std::vector<glm::mat4> model;
            const glm::vec3 begin = jointAt(rig, clip, root, span.first, pose, model);
            const glm::vec3 end = jointAt(rig, clip, root, span.last, pose, model);
            const float net = glm::length(glm::vec2(end.x - begin.x, end.z - begin.z));
            INFO("net root xz displacement " << net);
            CHECK(net < 1e-4f);
        }
    }
}

// =================================================================================================
// 2. Clip playback: the range, the loop, and the invariants
// =================================================================================================

TEST_CASE("every alien clip starts at the same key, and the loops close on it", "[aliens][animation]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    const scene::Scene s = loadAlien("alien-scout.glb");
    const scene::SkinnedRig& rig = s.rigs.front();
    for (const scene::AnimationClip& clip : rig.clips) {
        INFO("clip " << clip.name);
        const KeySpan span = keySpan(clip);
        // Frame 1 at 30 fps. This is the whole of defect 1: the exporter wrote frame 1 rather than
        // frame 0, so the playable range does not begin at zero.
        CHECK(span.first == Approx(1.0f / 30.0f).margin(1e-5));
        CHECK(span.period() > 0.0f);
        CHECK(clip.duration == Approx(span.last).margin(1e-5));
    }

    // The locomotion loops close: the last key is the first key, which is what makes
    // `last - first` the cycle and not `last`.
    for (const char* name : {"Idle", "Walking", "Running", "Idle_turn", "Fall_loop"}) {
        INFO("clip " << name);
        const scene::AnimationClip& clip = clipNamed(rig, name);
        float worst = 0.0f;
        for (const scene::AnimationChannel& c : clip.channels) {
            REQUIRE(c.values.size() == c.times.size());
            worst = std::max(worst, glm::length(c.values.front() - c.values.back()));
        }
        // Walking, Running, Idle_turn and Fall_loop close *exactly* -- the exporter wrote the same
        // key at both ends. Idle closes to within half a millibit of a unit rather than exactly,
        // which is the float quantisation of a 60-key take and is invisible; 1e-3 admits that
        // without admitting a clip that does not loop at all.
        INFO("worst first-to-last channel difference " << worst);
        CHECK(worst < 1e-3f);
    }
}

TEST_CASE("a looping alien clip repeats on its own period", "[aliens][animation][loop]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    scene::Scene s = loadAlien("alien-scout.glb");
    scene::SkinnedRig& rig = s.rigs.front();
    for (const char* name : {"Walking", "Running", "Idle"}) {
        INFO("clip " << name);
        // The period comes from the asset's keys, not from anything the engine computed.
        const float period = keySpan(clipNamed(rig, name)).period();
        REQUIRE(rig.player.play(name, 0.0, 0.0f));
        for (const double t : {0.0, 0.137, 0.421, 0.688}) {
            INFO("t " << t);
            const std::vector<glm::mat4> a = playedPalette(rig, t);
            const std::vector<glm::mat4> b = playedPalette(rig, t + static_cast<double>(period));
            const std::vector<glm::mat4> c = playedPalette(rig, t + 3.0 * static_cast<double>(period));
            CHECK(paletteDelta(a, b) < 1e-4f);
            CHECK(paletteDelta(a, c) < 1e-4f);
        }
        // And the cycle is not something else: half a period away the pose is genuinely different,
        // so the test above cannot pass on a rig that never moves.
        const std::vector<glm::mat4> here = playedPalette(rig, 0.0);
        const std::vector<glm::mat4> half = playedPalette(rig, 0.5 * static_cast<double>(period));
        CHECK(paletteDelta(here, half) > 1e-2f);
    }
}

TEST_CASE("a looping alien clip never holds a pose it was not authored to hold",
          "[aliens][animation][loop]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    scene::Scene s = loadAlien("alien-scout.glb");
    scene::SkinnedRig& rig = s.rigs.front();
    // Sampled at the take's own 30 fps: every step should move to the next key, and none of them
    // should land on a stretch of held pose. Before the fix the engine looped over [0, lastKey],
    // so 1/30 s of every cycle replayed the first key and the character stood still for a frame in
    // the middle of its stride.
    for (const char* name : {"Walking", "Running"}) {
        INFO("clip " << name);
        const float period = keySpan(clipNamed(rig, name)).period();
        REQUIRE(rig.player.play(name, 0.0, 0.0f));
        const int steps = static_cast<int>(std::lround(period * 30.0f)) * 3;
        int frozen = 0;
        std::vector<glm::mat4> previous = playedPalette(rig, 0.0);
        for (int i = 1; i <= steps; ++i) {
            const double t = static_cast<double>(i) / 30.0;
            std::vector<glm::mat4> now = playedPalette(rig, t);
            CHECK(finite(now));
            if (paletteDelta(previous, now) < 1e-6f) {
                ++frozen;
            }
            previous = std::move(now);
        }
        INFO("frozen steps " << frozen << " of " << steps);
        CHECK(frozen == 0);
    }
}

TEST_CASE("sampling an alien clip is finite, normalised and deterministic", "[aliens][animation]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    const scene::Scene s = loadAlien("alien-elder.glb");
    const scene::SkinnedRig& rig = s.rigs.front();
    for (const char* name : kLocomotorClips) {
        INFO("clip " << name);
        const scene::AnimationClip& clip = clipNamed(rig, name);
        const KeySpan span = keySpan(clip);
        // Time zero, the first key, the last key, and well past the end: all valid poses, and the
        // two that name the same instant produce the same pose.
        const std::array<float, 6> probes{{0.0f, span.first, span.period() * 0.5f, span.last,
                                           span.last + 1.0f, -1.0f}};
        for (const float t : probes) {
            INFO("t " << t);
            scene::Pose pose;
            scene::setRestPose(rig.skeleton, pose);
            scene::sampleClip(clip, t, pose);
            REQUIRE(pose.size() == rig.skeleton.jointCount());
            for (const scene::Transform& x : pose.local) {
                CHECK(std::isfinite(x.position.x));
                CHECK(std::isfinite(x.position.y));
                CHECK(std::isfinite(x.position.z));
                CHECK(std::isfinite(x.scale.x));
                // glTF quaternions arrive as four floats; the sampler must hand back a unit one or
                // the joint matrix is a scale as well as a rotation.
                CHECK_THAT(glm::length(x.rotation), Catch::Matchers::WithinAbs(1.0, 1e-4));
            }
        }
        // Determinism: the same second twice is the same pose, bit for bit.
        const std::vector<glm::mat4> a = paletteAt(rig, name, span.first + span.period() * 0.37f);
        const std::vector<glm::mat4> b = paletteAt(rig, name, span.first + span.period() * 0.37f);
        CHECK(a == b);
        // Clamping past the end holds the last key rather than wrapping or collapsing.
        CHECK(paletteAt(rig, name, span.last) == paletteAt(rig, name, span.last + 5.0f));
        // And below the first key holds the first, rather than falling back to the rest pose.
        CHECK(paletteAt(rig, name, span.first) == paletteAt(rig, name, -3.0f));
    }
}

TEST_CASE("the alien skeletons and their inverse binds are sound", "[aliens][skeleton]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    for (const Cast& c : kCast) {
        INFO(c.asset);
        const scene::Scene s = loadAlien(c.asset);
        const scene::SkinnedRig& rig = s.rigs.front();
        const scene::Skeleton& sk = rig.skeleton;

        // Hierarchy: a parent always precedes its child, so one forward pass resolves model space.
        for (std::size_t i = 0; i < sk.joints.size(); ++i) {
            const int parent = sk.joints[i].parent;
            CHECK(parent < static_cast<int>(i));
            CHECK(parent >= -1);
        }
        REQUIRE(sk.palette.size() == sk.inverseBind.size());
        for (const std::uint32_t p : sk.palette) {
            CHECK(p < sk.joints.size());
        }

        // The inverse binds are the inverses of the bind-pose model matrices, so the rest pose must
        // skin to identity. This is the one assertion that catches an inverse bind applied to the
        // wrong joint, and nothing checked it before.
        scene::Pose rest;
        scene::setRestPose(sk, rest);
        std::vector<glm::mat4> scratch;
        std::vector<glm::mat4> palette;
        scene::skinningPalette(sk, rest, scratch, palette);
        REQUIRE(palette.size() == sk.paletteSize());
        float worst = 0.0f;
        for (const glm::mat4& m : palette) {
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    worst = std::max(worst, std::abs(m[col][row] - glm::mat4(1.0f)[col][row]));
                }
            }
        }
        INFO("worst rest-pose skinning deviation from identity " << worst);
        CHECK(worst < 1e-3f);

        // An unanimated channel keeps its rest value. `Idle` animates every joint, so this is
        // checked with a clip that carries one channel only.
        scene::AnimationClip sparse;
        sparse.name = "sparse";
        scene::AnimationChannel ch;
        ch.joint = 4;
        ch.path = scene::AnimationPath::Translation;
        ch.times = {0.0f, 1.0f};
        ch.values = {glm::vec4(0.0f, 5.0f, 0.0f, 0.0f), glm::vec4(0.0f, 5.0f, 0.0f, 0.0f)};
        sparse.duration = 1.0f;
        sparse.channels.push_back(ch);
        scene::Pose pose;
        scene::setRestPose(sk, pose);
        scene::sampleClip(sparse, 0.5f, pose);
        for (std::size_t j = 0; j < pose.size(); ++j) {
            if (j == 4) {
                continue;
            }
            CHECK(pose.local[j].position == rest.local[j].position);
            CHECK(pose.local[j].rotation == rest.local[j].rotation);
            CHECK(pose.local[j].scale == rest.local[j].scale);
        }
        CHECK(pose.local[4].position.y == Approx(5.0f));
    }
}

// =================================================================================================
// 3. Locomotion: the clip's own stride speed against the number the scene authors
// =================================================================================================

TEST_CASE("the stride estimator recovers the speed the clips were measured at",
          "[aliens][locomotion][stride]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    // Pinned against values derived outside this codebase, off the raw glTF accessors: Walking
    // 1.5807 and Running 3.81 model units per second, both stable from a 10% contact threshold to
    // a 30% one. Without this the estimator could agree with itself and prove nothing.
    for (const Cast& c : kCast) {
        INFO(c.asset);
        const scene::Scene s = loadAlien(c.asset);
        const scene::SkinnedRig& rig = s.rigs.front();
        const float walk = clipStrideSpeed(rig, "Walking");
        const float run = clipStrideSpeed(rig, "Running");
        INFO("walk " << walk << " run " << run);
        CHECK(walk == Approx(1.5807f).epsilon(0.03));
        CHECK(run == Approx(3.81f).epsilon(0.03));
        // The threshold is not a tuning knob: the answer is the same either side of it.
        CHECK(clipStrideSpeed(rig, "Walking", 0.10f) == Approx(walk).epsilon(0.03));
        CHECK(clipStrideSpeed(rig, "Walking", 0.30f) == Approx(walk).epsilon(0.03));
        // The run clip is a run: it covers more ground per second than the walk.
        CHECK(run > walk * 2.0f);
        // An idle has no stride at all, and the estimator must not invent one.
        CHECK(clipStrideSpeed(rig, "Idle") < walk * 0.05f);
    }
}

TEST_CASE("each alien's authored stride speed describes the clip it names",
          "[aliens][locomotion][stride]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    // This is the check ADR-161's foot-slip diagnostic cannot make. `footSlip` compares the
    // behaviour's travel speed with `GaitSettings::walkSpeed`; when the two agree it reports 1.0
    // whatever the clip is doing, so a walkSpeed that does not describe the clip is invisible to
    // it -- and ADR-198 set three of these four from the speed the character cruises at rather
    // than from the clip, which is how tide came to cover 3.2 m of ground per 5.7 m of stride
    // while every number the engine printed said the feet were fine.
    const nlohmann::json doc = sceneJson();
    for (const Cast& c : kCast) {
        INFO(c.entity);
        const nlohmann::json* node = findNamed(doc.at("nodes"), c.entity);
        const nlohmann::json* ent = findNamed(doc.at("entities"), c.entity);
        REQUIRE(node != nullptr);
        REQUIRE(ent != nullptr);
        const float scale = node->at("scale")[0].get<float>();
        REQUIRE(ent->contains("gait"));
        const nlohmann::json& gait = ent->at("gait");
        REQUIRE(gait.contains("walkSpeed"));
        REQUIRE(gait.contains("runSpeed"));
        const auto authoredWalk = gait.at("walkSpeed").get<float>();
        const auto authoredRun = gait.at("runSpeed").get<float>();

        const scene::Scene s = loadAlien(c.asset);
        const scene::SkinnedRig& rig = s.rigs.front();
        const float walk = clipStrideSpeed(rig, "Walking") * scale;
        const float run = clipStrideSpeed(rig, "Running") * scale;
        INFO("scale " << scale << "; authored walk " << authoredWalk << " vs clip " << walk
                      << "; authored run " << authoredRun << " vs clip " << run);
        // 5%: a twentieth of a stride is well inside what a viewer reads as a character adjusting
        // its pace, and well outside the 15-45% errors this found.
        CHECK(authoredWalk == Approx(walk).epsilon(0.05));
        CHECK(authoredRun == Approx(run).epsilon(0.05));
    }
}

TEST_CASE("rate matching covers each alien's whole travelling range", "[aliens][locomotion][gait]") {
    // With in-place clips and code-driven travel the feet are exact only while the playback rate is
    // free to be `speed / authoredSpeed`. A clamp that bites inside the band the body actually
    // travels in is sliding by another name, so the band and the clamp are checked against each
    // other rather than against a number somebody liked.
    const nlohmann::json doc = sceneJson();
    for (const Cast& c : kCast) {
        INFO(c.entity);
        const nlohmann::json* ent = findNamed(doc.at("entities"), c.entity);
        REQUIRE(ent != nullptr);
        const nlohmann::json& gait = ent->at("gait");
        entity::GaitSettings g;
        g.matchRate = gait.value("matchRate", false);
        g.walkSpeed = gait.at("walkSpeed").get<float>();
        g.runSpeed = gait.at("runSpeed").get<float>();
        g.rateMin = gait.at("rateMin").get<float>();
        g.rateMax = gait.at("rateMax").get<float>();
        g.moveExit = gait.at("moveExit").get<float>();
        g.runEnter = gait.at("runEnter").get<float>();
        g.runExit = gait.at("runExit").get<float>();
        REQUIRE(g.matchRate);

        // The walk band runs from the speed the gait stops calling it standing to the speed it
        // starts calling it running; the run band from there to the fastest the behaviour travels.
        const nlohmann::json* explore = nullptr;
        for (const nlohmann::json& b : ent->at("behaviors")) {
            if (b.value("kind", std::string()) == "explore") {
                explore = &b;
            }
        }
        REQUIRE(explore != nullptr);
        const auto topRun = explore->at("runSpeed").get<float>();

        for (const float v : {g.moveExit, (g.moveExit + g.runEnter) * 0.5f, g.runEnter}) {
            INFO("walk at " << v);
            CHECK(entity::Gait::footSlip(g, entity::Activity::Walk, v) == Approx(1.0f).epsilon(0.02));
        }
        for (const float v : {g.runExit, (g.runExit + topRun) * 0.5f, topRun}) {
            INFO("run at " << v);
            CHECK(entity::Gait::footSlip(g, entity::Activity::Run, v) == Approx(1.0f).epsilon(0.02));
        }
    }
}

// =================================================================================================
// 4. Transitions between the states that already exist
// =================================================================================================

TEST_CASE("every implemented gait transition blends without a snap or a bind pose",
          "[aliens][animation][transition]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    scene::Scene s = loadAlien("alien-ranger.glb");
    scene::SkinnedRig& rig = s.rigs.front();
    scene::Pose rest;
    scene::setRestPose(rig.skeleton, rest);
    std::vector<glm::mat4> scratch;
    std::vector<glm::mat4> restPalette;
    scene::skinningPalette(rig.skeleton, rest, scratch, restPalette);

    // Exactly the six the four aliens can perform: idle, walk and run are the gait machine's whole
    // locomotor vocabulary. Nothing new is added here.
    const std::array<std::pair<const char*, const char*>, 6> transitions{{{"Idle", "Walking"},
                                                                         {"Walking", "Idle"},
                                                                         {"Idle", "Running"},
                                                                         {"Walking", "Running"},
                                                                         {"Running", "Walking"},
                                                                         {"Running", "Idle"}}};
    constexpr float kBlend = 0.35f; // the scene's own blend
    for (const auto& [from, to] : transitions) {
        INFO(from << " -> " << to);
        REQUIRE(rig.player.play(from, 0.0, 0.0f));
        // Advance to a deterministic point inside the source cycle before changing.
        const double at = 0.4;
        const std::vector<glm::mat4> before = playedPalette(rig, at);
        REQUIRE(rig.player.play(to, at, kBlend));
        CHECK(std::string(rig.player.currentState()) == to);
        CHECK(std::string(rig.player.fadingState()) == from);
        // The blend is the one asked for, and it walks from 0 to 1 over exactly that long.
        CHECK(rig.player.blendWeight(at) == Approx(0.0f).margin(1e-5));
        CHECK(rig.player.blendWeight(at + kBlend * 0.5) == Approx(0.5f).margin(1e-3));
        CHECK(rig.player.blendWeight(at + kBlend) == Approx(1.0f).margin(1e-5));
        CHECK(rig.player.blending(at + kBlend * 0.5));
        CHECK_FALSE(rig.player.blending(at + kBlend + 1e-4));

        // Continuity, tested without a tolerance anybody had to choose. A pose that is a
        // continuous function of time moves half as far per step when the step is halved; a jump
        // discontinuity moves exactly as far however finely it is sampled. So: sample the blend at
        // two rates and compare.
        //
        // Deliberately *not* "the worst step is a small fraction of the whole transition", which
        // was the first version of this test and is worthless -- two poses a third of a second
        // apart can happen to land near each other, and then the criterion tightens to nothing for
        // no reason connected to the blend. And not "no bigger than the clips' own steps" either:
        // a cross-fade between two different poses legitimately moves faster than either clip does.
        const auto worstStepAt = [&](int samples) {
            std::vector<glm::mat4> previous = playedPalette(rig, at);
            float worst = 0.0f;
            for (int i = 1; i <= samples; ++i) {
                const double t = at + static_cast<double>(i) * (kBlend / samples);
                std::vector<glm::mat4> now = playedPalette(rig, t);
                REQUIRE(finite(now));
                // Never the bind pose: a cross-fade that drops to rest for a frame is the T-pose
                // flash, and it is the defect this whole family of tests exists for.
                CHECK(paletteDelta(now, restPalette) > 1e-3f);
                worst = std::max(worst, paletteDelta(previous, now));
                previous = std::move(now);
            }
            return worst;
        };
        const float coarse = worstStepAt(24);
        const float fine = worstStepAt(96);
        INFO("worst step at 24 samples " << coarse << ", at 96 " << fine << " (ratio "
                                         << (fine / std::max(coarse, 1e-9f)) << "; a continuous "
                                         << "blend gives about 0.25, a jump about 1)");
        REQUIRE(coarse > 0.0f);
        CHECK(fine < coarse * 0.5f);

        // Repeating the transition reproduces it exactly.
        REQUIRE(rig.player.play(from, 10.0, 0.0f));
        const std::vector<glm::mat4> firstRun = playedPalette(rig, 10.4);
        REQUIRE(rig.player.play(to, 10.4, kBlend));
        const std::vector<glm::mat4> firstMid = playedPalette(rig, 10.4 + kBlend * 0.5);
        REQUIRE(rig.player.play(from, 20.0, 0.0f));
        const std::vector<glm::mat4> secondRun = playedPalette(rig, 20.4);
        REQUIRE(rig.player.play(to, 20.4, kBlend));
        const std::vector<glm::mat4> secondMid = playedPalette(rig, 20.4 + kBlend * 0.5);
        CHECK(paletteDelta(firstRun, secondRun) < 1e-5f);
        CHECK(paletteDelta(firstMid, secondMid) < 1e-5f);
    }
}

TEST_CASE("a playback-rate change does not jump the clip", "[aliens][animation][rate]") {
    const auto sceneFile = fs::path(AVGEN_SOURCE_DIR) / "examples" / "characters" / "alien.scene.json";
    if (!fs::is_regular_file(sceneFile)) {
        SKIP("the character example is not present");
    }
    assets::AssetRegistry registry{sceneFile.parent_path()};
    auto loaded = scene::Composition::loadFile(sceneFile, registry);
    REQUIRE(loaded.has_value());
    scene::Composition& composition = **loaded;
    params::ParameterSet parameters;
    params::Modulator modulator;
    composition.attach(parameters, modulator);
    composition.setViewport(640, 360);

    FrameTime time;
    // Rate matching rewrites the playback rate every frame a body's speed changes, which is every
    // frame it is accelerating. The clip's local second must follow dt * rate; a change that
    // rebased on the state's entry second instead would jump by (elapsed * delta-rate), and after
    // ten seconds of walking that is most of a stride, sixty times a second.
    for (int i = 0; i <= 600; ++i) {
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / 60.0;
        REQUIRE(composition.setNodeAnimation("walk", "Walk", time.renderTime, 0.2f, 1.0f));
        composition.update(time);
    }
    const scene::Scene& s = composition.scene();
    scene::RigId rig = scene::kInvalidRig;
    for (const auto* node : {composition.findNode("walk")}) {
        REQUIRE(node != nullptr);
        REQUIRE_FALSE(node->rigs.empty());
        rig = node->rigs.front();
    }
    REQUIRE(rig < s.rigs.size());
    REQUIRE(std::string(s.rigs[rig].player.currentState()) == "Walk");

    float rate = 1.0f;
    float previous = s.rigs[rig].player.stateTime(10.0);
    for (int i = 601; i <= 660; ++i) {
        const double now = static_cast<double>(i) / 60.0;
        // The rate in force over the interval that just elapsed is the one set at its start; the
        // new one applies from `now` onwards. What must not happen is the clip jumping by
        // (elapsed * delta-rate), which is what a rate change rebased on the state's entry second
        // would do -- after ten seconds of walking, most of a stride, sixty times a second.
        const float expected = rate / 60.0f;
        rate += 0.01f; // a body accelerating: the rate moves every frame
        time.renderTime = now;
        time.deltaTime = 1.0 / 60.0;
        REQUIRE(composition.setNodeAnimation("walk", "Walk", now, 0.2f, rate));
        composition.update(time);
        const float current = s.rigs[rig].player.stateTime(now);
        INFO("frame " << i << " rate " << rate << " advanced " << (current - previous) << " expected "
                      << expected);
        CHECK(current - previous == Approx(expected).margin(1e-4));
        CHECK(current > previous); // and never backwards
        previous = current;
    }
}

// =================================================================================================
// 5. Facing: the drawn body follows the travelling body
// =================================================================================================

namespace {

entity::NodeBinding binding(const std::string& node, glm::vec3 anchor) {
    entity::NodeBinding b;
    b.node = node;
    b.exists = true;
    b.transformPrefix = "nodes/" + node + "/";
    b.anchor = anchor;
    return b;
}

void registerNode(params::ParameterSet& params, const std::string& node) {
    params.add(params::ParamDesc<glm::vec3>{.path = "nodes/" + node + "/position",
                                            .defaultValue = glm::vec3(0.0f),
                                            .hardMin = glm::vec3(-1e4f),
                                            .hardMax = glm::vec3(1e4f)});
    // The same hard range the composition registers (composition.cpp, registerNodeParameters):
    // a rotation is an orientation, and the parameter says so.
    params.add(params::ParamDesc<glm::vec3>{.path = "nodes/" + node + "/rotation",
                                            .defaultValue = glm::vec3(0.0f),
                                            .hardMin = glm::vec3(-360.0f),
                                            .hardMax = glm::vec3(360.0f)});
    params.add(params::ParamDesc<glm::vec3>{.path = "nodes/" + node + "/scale",
                                            .defaultValue = glm::vec3(1.0f),
                                            .hardMin = glm::vec3(0.001f),
                                            .hardMax = glm::vec3(100.0f)});
}

entity::BehaviorDesc behaviorDesc(const char* kind, nlohmann::json settings) {
    entity::BehaviorDesc d;
    d.kind = kind;
    d.name = kind;
    settings["kind"] = kind;
    d.settings = std::move(settings);
    return d;
}

} // namespace

TEST_CASE("a body drawn facing is its own facing, past a full revolution",
          "[aliens][entity][facing]") {
    // Reproduced from the report that the wanderer "looks like he is walking forwards while moving
    // backwards": `EntityState::yaw` is an accumulator, and it is written into a rotation parameter
    // whose hard range is +/-360 degrees. Once a character has net-turned through a revolution the
    // parameter clamps and the drawn facing stops following the body. Measured on the shipped
    // Glowmere wanderer at 31.77% of moving frames (see the [.probe] below); here it is reproduced
    // in ten seconds with nothing but a beacon going round.
    params::ParameterSet params;
    signals::SignalBus bus;
    entity::EntityWorld world;
    registerNode(params, "beacon");
    registerNode(params, "watcher");

    entity::EntityDesc beacon;
    beacon.name = "beacon";
    beacon.node = "beacon";
    beacon.seed = 1;
    // 200 deg/s around the watcher: the watcher's lookAt turns the same way for as long as it runs,
    // so the accumulated yaw passes 360 degrees in well under the probe's length.
    beacon.behaviors.push_back(behaviorDesc("orbit", {{"radius", 20.0}, {"rate", 200.0}}));

    entity::EntityDesc watcher;
    watcher.name = "watcher";
    watcher.node = "watcher";
    watcher.seed = 2;
    watcher.behaviors.push_back(behaviorDesc("lookAt", {{"target", "beacon"}, {"turnRate", 720.0}}));

    world.setEntities({beacon, watcher}, 7u);
    world.setBindings({binding("beacon", glm::vec3(0.0f)), binding("watcher", glm::vec3(0.0f))});
    world.registerParameters(params);
    world.bind(params);

    auto* rotation = params.findAs<glm::vec3>("nodes/watcher/rotation");
    REQUIRE(rotation != nullptr);
    const entity::Entity* who = nullptr;
    for (const auto& e : world.entities()) {
        if (e->name() == "watcher") {
            who = e.get();
        }
    }
    REQUIRE(who != nullptr);

    float worstGap = 0.0f;
    float turned = 0.0f;
    float lastYaw = 0.0f;
    for (int i = 0; i <= 600; ++i) {
        params.resetFinals();
        entity::EntityUpdate u;
        u.time = static_cast<double>(i) / 60.0;
        u.dt = i == 0 ? 0.0 : 1.0 / 60.0;
        u.frameIndex = static_cast<std::uint64_t>(i);
        u.bus = &bus;
        world.update(u, params);

        const float yaw = who->locomotion().yaw;
        if (i > 0) {
            float d = std::fmod(yaw - lastYaw + 3.14159265f, 6.2831853f);
            if (d < 0.0f) {
                d += 6.2831853f;
            }
            turned += std::abs(d - 3.14159265f);
        }
        lastYaw = yaw;

        // The drawn orientation, built exactly as the composition builds it.
        const glm::quat q = scene::quatFromEulerDegrees(rotation->value());
        const glm::vec3 fwd = q * glm::vec3(0.0f, 0.0f, 1.0f);
        const float drawn = std::atan2(fwd.x, fwd.z);
        float gap = std::fmod(std::abs(drawn - yaw), 6.2831853f);
        if (gap > 3.14159265f) {
            gap = 6.2831853f - gap;
        }
        worstGap = std::max(worstGap, gap);
    }
    // The probe established the state it claims to measure: the body really did turn more than a
    // revolution, so the clamp really was reachable (ADR-182).
    INFO("total turning " << turned << " rad");
    REQUIRE(turned > 6.2831853f);
    INFO("worst gap between drawn facing and body facing " << worstGap << " rad");
    CHECK(worstGap < 0.01f);
}

// =================================================================================================
// 6. Grounding
// =================================================================================================

TEST_CASE("an alien-sized body stays on the ground it crosses", "[aliens][entity][grounding]") {
    // Flat, a gentle slope and uneven ground, through the same `GroundFollower` the aliens use and
    // with *their* settings -- a 2.6 m footprint on a six-metre body, not the half-metre default the
    // existing grounding tests use. The contract is the header's own: never below everything the
    // footprint covers, never more than the band above the surface, and no vertical vibration the
    // terrain did not put there.
    world::Ecology ecology;
    entity::GroundSettings settings;
    settings.footprint = 2.6f;   // the four aliens' authored footprint
    settings.slopeAlign = 0.5f;  // and their authored lean
    constexpr double kStep = 1.0 / 60.0;
    constexpr float kSpeed = 5.7f; // the measured walk stride speed of a 3.6x alien

    struct Case {
        const char* name;
        std::vector<world::NoiseLayer> layers;
    };
    std::vector<Case> cases;
    cases.push_back({"flat", {}});
    cases.push_back({"gentle slope", {{.frequency = 0.0015f, .amplitude = 40.0f}}});
    cases.push_back({"uneven", {{.frequency = 0.008f, .amplitude = 24.0f},
                                {.frequency = 0.12f, .amplitude = 2.2f},
                                {.frequency = 1.6f, .amplitude = 0.35f}}});

    for (const Case& c : cases) {
        INFO(c.name);
        world::WorldMap map;
        map.size = glm::vec2(640.0f, 640.0f);
        map.layers = c.layers;
        map.prepare();
        world::ClearanceField clearance;
        clearance.map = &map;
        clearance.ecology = &ecology;
        clearance.cameraRadius = 2.4f;
        clearance.groundClearance = 0.0f;
        const entity::Navigator nav(&map, clearance);
        REQUIRE(nav.valid());

        entity::GroundFollower body;
        glm::vec2 p(-240.0f, -60.0f);
        const glm::vec2 heading(1.0f, 0.0f);
        const float yaw = std::atan2(heading.x, heading.y);
        float worstFloat = 0.0f;
        float worstSink = 0.0f;
        float span = 0.0f;
        float lowSeen = 1e9f;
        float highSeen = -1e9f;
        std::vector<float> heights;
        for (int i = 0; i < 2400; ++i) {
            const entity::GroundResult r = body.update(nav, p, yaw, kSpeed, kStep, settings);
            REQUIRE(std::isfinite(r.height));
            REQUIRE(std::isfinite(r.pitch));
            REQUIRE(std::isfinite(r.roll));
            CHECK(std::abs(r.pitch) <= settings.maxTilt + 1e-3f);
            CHECK(std::abs(r.roll) <= settings.maxTilt + 1e-3f);
            const float ground = nav.groundHeight(p);
            lowSeen = std::min(lowSeen, ground);
            highSeen = std::max(highSeen, ground);
            // Penetration is a statement about the body, not about a point: what is forbidden is
            // going below everything the footprint covers.
            float lowest = ground;
            for (int k = 0; k < 8; ++k) {
                const float a = static_cast<float>(k) * 0.7853982f;
                lowest = std::min(lowest, nav.groundHeight(p + glm::vec2(std::cos(a), std::sin(a)) *
                                                                   settings.footprint));
            }
            worstSink = std::max(worstSink, lowest - r.height);
            worstFloat = std::max(worstFloat, r.height - ground);
            heights.push_back(r.height);
            p += heading * kSpeed * static_cast<float>(kStep);
        }
        span = highSeen - lowSeen;
        INFO("terrain span " << span << " m; worst sink " << worstSink << ", worst float "
                             << worstFloat);
        CHECK(worstSink <= 1e-3f);
        CHECK(worstFloat <= settings.maxFloat + 0.15f);

        // Vertical jitter: the second difference of the followed height, which is the acceleration
        // a viewer reads as vibration. The followed path may never be jerkier than snapping
        // straight onto the surface would have been.
        double followedJerk = 0.0;
        double snappedJerk = 0.0;
        glm::vec2 q(-240.0f, -60.0f);
        std::vector<float> snapped;
        for (int i = 0; i < 2400; ++i) {
            snapped.push_back(nav.groundHeight(q));
            q += heading * kSpeed * static_cast<float>(kStep);
        }
        const auto rms = [&](const std::vector<float>& series) {
            double sum = 0.0;
            for (std::size_t i = 2; i < series.size(); ++i) {
                const double a = (series[i] - 2.0 * series[i - 1] + series[i - 2]) / (kStep * kStep);
                sum += a * a;
            }
            return std::sqrt(sum / static_cast<double>(series.size() - 2));
        };
        followedJerk = rms(heights);
        snappedJerk = rms(snapped);
        INFO("vertical acceleration " << followedJerk << " followed vs " << snappedJerk << " snapped");
        // Never jerkier than snapping straight onto the surface would have been -- or, where the
        // terrain is so smooth that snapping is already almost perfectly still, below a tenth of
        // gravity in absolute terms. On the gentle slope the follower comes out at 0.10 m/s^2
        // against snapping's 0.016: a 60 Hz frame of that is 28 micrometres on a six-metre body,
        // which is the one-pole filter converging rather than anything a viewer could see. Stating
        // it as a floor rather than raising the ratio keeps the assertion about visible jitter.
        CHECK(followedJerk <= std::max(snappedJerk, 1.0) + 1e-6);
    }
}

// =================================================================================================
// 7. Four aliens at once
// =================================================================================================

TEST_CASE("the four aliens animate independently in the scene they ship in",
          "[aliens][integration][glowmere2]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    const nlohmann::json doc = sceneJson();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(valleyScene()).has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    auto* cameraMode = engine.params().findAs<int>("camera/mode");
    auto* cameraPos = engine.params().findAs<glm::vec3>("camera/position");
    auto* cameraTarget = engine.params().findAs<glm::vec3>("camera/target");
    REQUIRE(cameraMode != nullptr);
    cameraMode->setBase(1);

    struct Watch {
        std::string name;
        const entity::Entity* who = nullptr;
        const scene::CompositionNode* node = nullptr;
        scene::RigId rig = scene::kInvalidRig;
        std::vector<std::string> statesSeen;
        std::vector<glm::mat4> lastPalette;
        double lastPaletteTime = -1.0;
        int posedSteps = 0;
        int frozenSteps = 0;
        int moving = 0;
        int backwards = 0;
        float worstBackwards = 0.0f;
        float worstYawGap = 0.0f;
        int slipOut = 0;
        int locomotor = 0;
        glm::vec3 lastPosition{0.0f};
        double travelled = 0.0;
    };
    std::vector<Watch> watch;
    for (const Cast& c : kCast) {
        Watch w;
        w.name = c.entity;
        for (const auto& e : comp->entityWorld().entities()) {
            if (e->name() == c.entity) {
                w.who = e.get();
            }
        }
        w.node = comp->findNode(c.entity);
        REQUIRE(w.who != nullptr);
        REQUIRE(w.node != nullptr);
        REQUIRE(w.node->rigs.size() == 1); // its own rig, not one shared with anybody
        w.rig = w.node->rigs.front();
        w.lastPosition = comp->nodeWorldTransform(*w.node).position;
        watch.push_back(std::move(w));
    }
    // Four entities, four rigs, no sharing. A shared rig is how one instance's state leaks into
    // another's pose.
    for (std::size_t i = 0; i < watch.size(); ++i) {
        for (std::size_t k = i + 1; k < watch.size(); ++k) {
            CHECK(watch[i].rig != watch[k].rig);
        }
    }

    // The rig is posed at least as far out as the entity keeps simulating, or a character walks
    // the gap between them in a frozen pose. The engine already warns about this; the scene has to
    // honour it.
    for (const Watch& w : watch) {
        INFO(w.name);
        const scene::SkinnedRig& rig = comp->scene().rigs[w.rig];
        const entity::EntityDesc& desc = w.who->desc();
        CHECK(rig.cullDistance >= desc.cullDistance);
    }

    FixedStepClock clock(60.0);
    constexpr int kSteps = 90 * 60;
    for (int i = 0; i < kSteps; ++i) {
        glm::vec3 centre(0.0f);
        for (const Watch& w : watch) {
            centre += comp->nodeWorldTransform(*w.node).position;
        }
        centre /= static_cast<float>(watch.size());
        cameraPos->setBase(centre + glm::vec3(0.0f, 45.0f, 90.0f));
        cameraTarget->setBase(centre);
        const FrameTime ft = engine.tick(clock);
        engine.update(ft);

        for (Watch& w : watch) {
            const scene::SkinnedRig& rig = comp->scene().rigs[w.rig];
            const entity::LocomotionState& loco = w.who->locomotion();
            const std::string state(rig.player.currentState());
            if (std::find(w.statesSeen.begin(), w.statesSeen.end(), state) == w.statesSeen.end()) {
                w.statesSeen.push_back(state);
            }
            const bool locomotor =
                loco.activity == entity::Activity::Walk || loco.activity == entity::Activity::Run;
            if (rig.paletteTime != w.lastPaletteTime) {
                if (w.lastPaletteTime >= 0.0 && locomotor) {
                    ++w.posedSteps;
                    if (rig.palette == w.lastPalette) {
                        ++w.frozenSteps;
                    }
                }
                w.lastPalette = rig.palette;
                w.lastPaletteTime = rig.paletteTime;
                REQUIRE(finite(rig.palette));
            }
            if (locomotor) {
                ++w.locomotor;
                const float slip = entity::Gait::footSlip(w.who->desc().gait, loco.activity, loco.speed);
                if (slip > 1.1f || slip < 1.0f / 1.1f) {
                    ++w.slipOut;
                }
            }
            const scene::Transform t = comp->nodeWorldTransform(*w.node);
            const glm::vec2 step(t.position.x - w.lastPosition.x, t.position.z - w.lastPosition.z);
            w.lastPosition = t.position;
            w.travelled += glm::length(step);
            const glm::vec3 fwd = t.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
            const float drawn = std::atan2(fwd.x, fwd.z);
            float gap = std::fmod(std::abs(drawn - loco.yaw), 6.2831853f);
            if (gap > 3.14159265f) {
                gap = 6.2831853f - gap;
            }
            w.worstYawGap = std::max(w.worstYawGap, gap);
            const float len = glm::length(step);
            if (len >= 1e-4f) {
                ++w.moving;
                const glm::vec2 facing(std::sin(drawn), std::cos(drawn));
                if (glm::dot(facing, step / len) < -0.2f) {
                    ++w.backwards;
                    w.worstBackwards = std::max(w.worstBackwards, len);
                }
            }
        }
    }

    for (const Watch& w : watch) {
        INFO(w.name << ": travelled " << w.travelled << " m, moving frames " << w.moving
                    << ", backwards " << w.backwards << " (worst " << w.worstBackwards
                    << " m), worst yaw gap " << w.worstYawGap << " rad, frozen pose steps "
                    << w.frozenSteps << "/" << w.posedSteps << ", slip outside 1.1x on "
                    << w.slipOut << "/" << w.locomotor << " locomotor frames");
        // It actually went somewhere, or everything below is vacuous.
        CHECK(w.travelled > 40.0);
        CHECK(w.moving > 400);
        // It used more than one state: a character stuck in a single clip would pass every other
        // assertion here.
        CHECK(w.statesSeen.size() >= 2);
        // The drawn facing follows the body, to within the secondary motion the scene authored on
        // top of it and nothing else. `liveliness`'s sway is a deliberate yaw wobble; the +/-360
        // degree clamp this test was written for is not, and it produced gaps up to 2.95 radians.
        const nlohmann::json* entJson = findNamed(doc.at("entities"), w.name.c_str());
        REQUIRE(entJson != nullptr);
        const float wobble = authoredYawWobble(*entJson) + 0.02f;
        INFO("authored yaw wobble " << wobble << " rad");
        CHECK(w.worstYawGap < wobble);
        CHECK(w.backwards * 200 < w.moving); // under half a percent
        // No frozen poses while walking.
        CHECK(w.posedSteps > 100);
        CHECK(w.frozenSteps * 100 < w.posedSteps);
        // And the feet match the ground for effectively the whole walk.
        CHECK(w.locomotor > 100);
        CHECK(w.slipOut * 100 < w.locomotor);
    }
}

// The long-form version of the facing measurement, on the scene the report was filed against.
// Tagged [.probe]: ten simulated minutes is seconds of wall clock but not something the default
// suite should pay for. Run it with `avgen_tests "[facing]"`.
TEST_CASE("probe: the four aliens travel the way they are drawn facing",
          "[.probe][aliens][facing]") {
    if (!aliensPresent()) {
        SKIP("assets/aliens is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(valleyScene()).has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    auto* cameraMode = engine.params().findAs<int>("camera/mode");
    auto* cameraPos = engine.params().findAs<glm::vec3>("camera/position");
    auto* cameraTarget = engine.params().findAs<glm::vec3>("camera/target");
    REQUIRE(cameraMode != nullptr);
    cameraMode->setBase(1);

    struct Row {
        std::string name;
        const entity::Entity* who = nullptr;
        const scene::CompositionNode* node = nullptr;
        glm::vec3 last{0.0f};
        int moving = 0;
        int backwards = 0;
        float worst = 0.0f;
    };
    std::vector<Row> rows;
    for (const Cast& c : kCast) {
        Row r;
        r.name = c.entity;
        for (const auto& e : comp->entityWorld().entities()) {
            if (e->name() == c.entity) {
                r.who = e.get();
            }
        }
        r.node = comp->findNode(c.entity);
        REQUIRE(r.who != nullptr);
        REQUIRE(r.node != nullptr);
        r.last = comp->nodeWorldTransform(*r.node).position;
        rows.push_back(std::move(r));
    }

    FixedStepClock clock(60.0);
    constexpr int kSteps = 10 * 60 * 60;
    for (int i = 0; i < kSteps; ++i) {
        glm::vec3 centre(0.0f);
        for (const Row& r : rows) {
            centre += comp->nodeWorldTransform(*r.node).position;
        }
        centre /= static_cast<float>(rows.size());
        cameraPos->setBase(centre + glm::vec3(0.0f, 45.0f, 90.0f));
        cameraTarget->setBase(centre);
        engine.update(engine.tick(clock));
        for (Row& r : rows) {
            const scene::Transform t = comp->nodeWorldTransform(*r.node);
            const glm::vec2 step(t.position.x - r.last.x, t.position.z - r.last.z);
            r.last = t.position;
            const float len = glm::length(step);
            if (len < 1e-4f) {
                continue;
            }
            ++r.moving;
            const glm::vec3 fwd = t.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
            const float drawn = std::atan2(fwd.x, fwd.z);
            const glm::vec2 facing(std::sin(drawn), std::cos(drawn));
            if (glm::dot(facing, step / len) < -0.2f) {
                ++r.backwards;
                r.worst = std::max(r.worst, len);
            }
        }
    }
    for (const Row& r : rows) {
        const double rate = r.moving > 0 ? 100.0 * r.backwards / r.moving : 0.0;
        INFO(r.name << ": moving " << r.moving << ", backwards " << r.backwards << " (" << rate
                    << "%), worst step " << r.worst << " m");
        REQUIRE(r.moving > 1000);
        CHECK(r.backwards * 200 < r.moving);
    }
}
