// Skeletal animation (ADR-086): the rig maths, clip sampling in all three glTF interpolation
// modes, the cross-fading player's determinism, the pose-rate policy, and the glTF import.

#include "assets/asset_registry.hpp"
#include "assets/gltf_loader.hpp"
#include "scene/composition.hpp"
#include "scene/animation.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

// Three joints in a chain, each one metre further along +X than its parent.
scene::Skeleton chain() {
    scene::Skeleton s;
    s.name = "chain";
    for (int i = 0; i < 3; ++i) {
        scene::Joint j;
        j.name = "j" + std::to_string(i);
        j.parent = i - 1;
        j.rest.position = glm::vec3(i == 0 ? 0.0f : 1.0f, 0.0f, 0.0f);
        s.joints.push_back(j);
    }
    s.palette = {0, 1, 2};
    // Bind pose model matrices are translations of 0, 1 and 2 along +X, so the inverse binds are
    // the negatives of those.
    for (int i = 0; i < 3; ++i) {
        s.inverseBind.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(-static_cast<float>(i), 0.0f, 0.0f)));
    }
    return s;
}

scene::AnimationClip oneChannel(scene::Interpolation mode, std::vector<float> times,
                                std::vector<glm::vec4> values) {
    scene::AnimationChannel c;
    c.joint = 1;
    c.path = scene::AnimationPath::Translation;
    c.interpolation = mode;
    c.times = std::move(times);
    c.values = std::move(values);
    scene::AnimationClip clip;
    clip.name = "clip";
    clip.duration = c.times.back();
    clip.channels.push_back(std::move(c));
    return clip;
}

std::filesystem::path alienPath() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "imported" / "alien.gltf";
}

} // namespace

TEST_CASE("a joint chain resolves to model space and a palette", "[scene][skeleton]") {
    const scene::Skeleton s = chain();
    REQUIRE(s.valid());
    scene::Pose pose = scene::restPose(s);
    std::vector<glm::mat4> model;
    scene::poseToModel(s, pose, model);
    REQUIRE(model.size() == 3);
    CHECK(glm::vec3(model[2][3]).x == Approx(2.0f));

    std::vector<glm::mat4> palette;
    scene::jointPalette(s, model, palette);
    // The rest pose is the bind pose, so every palette entry is the identity.
    for (const glm::mat4& m : palette) {
        CHECK(glm::vec3(m[3]).x == Approx(0.0f));
        CHECK(m[0][0] == Approx(1.0f));
    }

    // Rotating the root a quarter turn about +Z carries the tip to (0, 2, 0).
    pose.local[0].rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    scene::skinningPalette(s, pose, model, palette);
    const glm::vec4 tip = palette[2] * glm::vec4(2.0f, 0.0f, 0.0f, 1.0f);
    CHECK(tip.x == Approx(0.0f).margin(1e-5));
    CHECK(tip.y == Approx(2.0f).margin(1e-5));
}

TEST_CASE("a skeleton with a forward reference is rejected", "[scene][skeleton]") {
    scene::Skeleton s = chain();
    s.joints[0].parent = 2; // a root that claims its own grandchild as a parent
    CHECK_FALSE(s.valid());
}

TEST_CASE("blending takes the short arc", "[scene][skeleton]") {
    scene::Transform a;
    scene::Transform b;
    a.rotation = glm::angleAxis(glm::radians(170.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    b.rotation = glm::angleAxis(glm::radians(-170.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    // 170 to -170 is 20 degrees the short way and 340 the long way. Halfway must be near 180.
    const scene::Transform mid = scene::blendTransform(a, b, 0.5f);
    const glm::vec3 forward = mid.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    CHECK(forward.z == Approx(-1.0f).margin(1e-3));

    scene::Pose pa;
    scene::Pose pb;
    pa.local = {a};
    pb.local = {b};
    scene::Pose out;
    scene::blendPose(pa, pb, 0.0f, out);
    CHECK(glm::dot(out.local[0].rotation, a.rotation) == Approx(1.0f).margin(1e-5));
    scene::blendPose(pa, pb, 1.0f, out);
    CHECK(std::abs(glm::dot(out.local[0].rotation, b.rotation)) == Approx(1.0f).margin(1e-5));
}

TEST_CASE("clip sampling honours each glTF interpolation mode", "[scene][animation]") {
    scene::Skeleton s = chain();
    scene::Pose pose = scene::restPose(s);

    SECTION("linear") {
        const auto clip = oneChannel(scene::Interpolation::Linear, {0.0f, 1.0f},
                                     {glm::vec4(0.0f), glm::vec4(10.0f, 0.0f, 0.0f, 0.0f)});
        scene::sampleClip(clip, 0.25f, pose);
        CHECK(pose.local[1].position.x == Approx(2.5f));
    }
    SECTION("step holds the key until the next one") {
        const auto clip = oneChannel(scene::Interpolation::Step, {0.0f, 1.0f},
                                     {glm::vec4(0.0f), glm::vec4(10.0f, 0.0f, 0.0f, 0.0f)});
        scene::sampleClip(clip, 0.99f, pose);
        CHECK(pose.local[1].position.x == Approx(0.0f));
        scene::sampleClip(clip, 1.0f, pose);
        CHECK(pose.local[1].position.x == Approx(10.0f));
    }
    SECTION("cubic spline uses the tangents") {
        // Two keys, values 0 and 1, both tangents zero: the Hermite basis reduces to smoothstep,
        // so the midpoint is 0.5 and the curve is flat at both ends.
        const auto clip = oneChannel(scene::Interpolation::CubicSpline, {0.0f, 1.0f},
                                     {glm::vec4(0.0f), glm::vec4(0.0f), glm::vec4(0.0f), glm::vec4(0.0f),
                                      glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), glm::vec4(0.0f)});
        scene::sampleClip(clip, 0.5f, pose);
        CHECK(pose.local[1].position.x == Approx(0.5f));
        scene::sampleClip(clip, 0.25f, pose);
        CHECK(pose.local[1].position.x == Approx(0.15625f)); // 3t^2 - 2t^3 at t = 0.25
    }
    SECTION("a channel leaves joints it does not name alone") {
        const auto clip = oneChannel(scene::Interpolation::Linear, {0.0f, 1.0f},
                                     {glm::vec4(0.0f), glm::vec4(10.0f, 0.0f, 0.0f, 0.0f)});
        scene::sampleClip(clip, 0.5f, pose);
        CHECK(pose.local[2].position.x == Approx(1.0f)); // still its rest transform
    }
}

TEST_CASE("wrapTime folds a looping clip", "[scene][animation]") {
    CHECK(scene::wrapTime(0.25f, 1.0f) == Approx(0.25f));
    CHECK(scene::wrapTime(3.25f, 1.0f) == Approx(0.25f));
    CHECK(scene::wrapTime(-0.25f, 1.0f) == Approx(0.75f));
    CHECK(scene::wrapTime(5.0f, 0.0f) == Approx(0.0f));
}

namespace {

// Two states over two clips: "a" holds joint 1 at x = 0, "b" moves it to x = 10.
scene::SkinnedRig twoStateRig() {
    scene::SkinnedRig rig;
    rig.name = "rig";
    rig.skeleton = chain();
    rig.clips.push_back(oneChannel(scene::Interpolation::Linear, {0.0f, 1.0f},
                                   {glm::vec4(0.0f), glm::vec4(0.0f)}));
    rig.clips.back().name = "a";
    rig.clips.push_back(oneChannel(scene::Interpolation::Linear, {0.0f, 1.0f},
                                   {glm::vec4(10.0f, 0.0f, 0.0f, 0.0f), glm::vec4(10.0f, 0.0f, 0.0f, 0.0f)}));
    rig.clips.back().name = "b";
    rig.addDefaultStates(0.5f);
    return rig;
}

} // namespace

TEST_CASE("the player cross-fades instead of snapping", "[scene][animation]") {
    scene::SkinnedRig rig = twoStateRig();
    scene::Pose pose;
    scene::Pose scratch;

    REQUIRE(rig.player.currentState() == "a");
    rig.player.evaluate(rig.clips, rig.skeleton, 0.0, pose, scratch);
    CHECK(pose.local[1].position.x == Approx(0.0f));

    REQUIRE(rig.player.play("b", 10.0));
    CHECK(rig.player.fadingState() == "a");
    // Halfway through the half-second blend the joint is halfway between the two clips: no snap.
    rig.player.evaluate(rig.clips, rig.skeleton, 10.25, pose, scratch);
    CHECK(rig.player.blendWeight(10.25) == Approx(0.5f));
    CHECK(pose.local[1].position.x == Approx(5.0f));
    rig.player.evaluate(rig.clips, rig.skeleton, 10.5, pose, scratch);
    CHECK(pose.local[1].position.x == Approx(10.0f));
    CHECK_FALSE(rig.player.blending(10.5));
}

TEST_CASE("asking for the state already playing does not restart it", "[scene][animation]") {
    scene::SkinnedRig rig = twoStateRig();
    REQUIRE(rig.player.play("b", 4.0));
    const float before = rig.player.stateTime(rig.clips, 5.0);
    for (int i = 0; i < 10; ++i) { // a behaviour calling play() every frame
        REQUIRE(rig.player.play("b", 4.5 + 0.05 * i));
    }
    CHECK(rig.player.stateTime(rig.clips, 5.0) == Approx(before));
    CHECK_FALSE(rig.player.play("nope", 6.0));
    CHECK(rig.player.currentState() == "b");
}

TEST_CASE("the pose is a pure function of the timeline, not of the frame rate", "[scene][animation]") {
    // The same decisions made at the same timeline seconds, then sampled from two different frame
    // sequences: a wobbling live one and a fixed offline one. The pose at t = 11.0 must be
    // identical bit for bit, which is what makes an offline render match the window.
    const auto poseAt = [](const std::vector<double>& frames, double at) {
        scene::SkinnedRig rig = twoStateRig();
        scene::Pose pose;
        scene::Pose scratch;
        for (const double t : frames) {
            if (t >= 10.0) {
                rig.player.play("b", 10.0); // the decision is timestamped, not "this frame"
            }
            rig.player.evaluate(rig.clips, rig.skeleton, t, pose, scratch);
        }
        rig.player.evaluate(rig.clips, rig.skeleton, at, pose, scratch);
        return pose.local[1].position.x;
    };
    std::vector<double> live;
    for (int i = 0; i < 400; ++i) {
        live.push_back(9.0 + 0.0173 * i); // an uneven, wall-clock-like cadence
    }
    std::vector<double> offline;
    for (int i = 0; i < 100; ++i) {
        offline.push_back(9.0 + i / 24.0);
    }
    const float a = poseAt(live, 10.2);
    const float b = poseAt(offline, 10.2);
    CHECK(a == b); // exact: no accumulated delta anywhere in the path
}

TEST_CASE("the pose rate is quantised onto the timeline", "[scene][animation]") {
    // A 20 Hz rig samples at multiples of 0.05 s whatever the frame rate, so two frame sequences
    // that straddle the same grid cell get the same matrices.
    CHECK(scene::SkinnedRig::sampleTime(1.234, 20.0f) == Approx(1.20));
    CHECK(scene::SkinnedRig::sampleTime(1.249, 20.0f) == Approx(1.20));
    CHECK(scene::SkinnedRig::sampleTime(1.251, 20.0f) == Approx(1.25));
    CHECK(scene::SkinnedRig::sampleTime(1.234, 0.0f) == Approx(1.234));

    scene::SkinnedRig rig = twoStateRig();
    CHECK(rig.rateFor(1.0f) == Approx(0.0f));    // near: every frame
    CHECK(rig.rateFor(50.0f) == Approx(20.0f));  // mid: the far rate
    CHECK(rig.rateFor(500.0f) < 0.0f);           // beyond the cull distance: not posed
}

TEST_CASE("a rate-limited rig reports no motion between poses", "[scene][animation]") {
    scene::SkinnedRig rig = twoStateRig();
    rig.player.play("b", 0.0, 0.0f);
    REQUIRE(rig.evaluate(0.0, 20.0f));
    const std::uint64_t version = rig.paletteVersion;
    CHECK_FALSE(rig.evaluate(0.03, 20.0f)); // same 20 Hz cell
    CHECK(rig.paletteVersion == version);
    // previousPalette tracks what the rig was drawn with, so a still frame writes zero velocity.
    REQUIRE(rig.previousPalette.size() == rig.palette.size());
    for (std::size_t i = 0; i < rig.palette.size(); ++i) {
        CHECK(rig.previousPalette[i] == rig.palette[i]);
    }
    CHECK(rig.evaluate(0.06, 20.0f)); // the next cell
}

TEST_CASE("updateRigs skips rigs nothing visible refers to", "[scene][animation]") {
    scene::Scene s;
    s.camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    s.rigs.push_back(twoStateRig());
    const scene::MeshId mesh = s.addMesh([] {
        scene::MeshData m;
        m.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}}, {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
        m.indices = {0, 1, 2};
        return m;
    }());
    scene::Entity& e = s.addEntity("alien", mesh);
    e.rig = 0;

    FrameTime time;
    time.renderTime = 1.0;
    auto stats = scene::updateRigs(s, time);
    CHECK(stats.rigs == 1);
    CHECK(stats.posed == 1);

    e.visible = false;
    time.renderTime = 2.0;
    stats = scene::updateRigs(s, time);
    CHECK(stats.posed == 0);
    CHECK(stats.culled == 1);

    e.visible = true;
    e.transform.position = glm::vec3(0.0f, 0.0f, -1000.0f); // past the cull distance
    time.renderTime = 3.0;
    stats = scene::updateRigs(s, time);
    CHECK(stats.culled == 1);
}

TEST_CASE("camera culling does not freeze an authored-visible rig", "[scene][animation]") {
    scene::Scene s;
    s.camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    s.rigs.push_back(twoStateRig());
    const scene::MeshId mesh = s.addMesh([] {
        scene::MeshData m;
        m.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}},
                      {{1, 0, 0}, {0, 0, 1}, {1, 0}},
                      {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
        m.indices = {0, 1, 2};
        return m;
    }());
    scene::Entity& e = s.addEntity("alien", mesh);
    e.rig = 0;
    e.cameraCulled = true;

    FrameTime time;
    time.renderTime = 1.0;
    auto stats = scene::updateRigs(s, time);
    CHECK(stats.posed == 1);
    CHECK(stats.culled == 0);
    const std::uint64_t posedVersion = s.rigs[0].paletteVersion;

    e.cameraCulled = false;
    time.renderTime = 1.1;
    stats = scene::updateRigs(s, time);
    CHECK(stats.posed == 1);
    CHECK(s.rigs[0].paletteVersion > posedVersion);
}

// ---- the imported asset --------------------------------------------------------------------

TEST_CASE("the alien imports as a rig with its three clips", "[assets][gltf][skeleton]") {
    if (!std::filesystem::is_regular_file(alienPath())) {
        SKIP("assets/imported/alien.gltf is not present");
    }
    scene::Scene s;
    const auto summary = assets::loadGltf(alienPath(), s);
    REQUIRE(summary.has_value());
    // The two warnings this replaced ("animations ignored", "skins ignored") must be gone.
    for (const std::string& w : summary->warnings) {
        CHECK(w.find("skinning not supported") == std::string::npos);
        CHECK(w.find("animation(s) ignored") == std::string::npos);
    }
    REQUIRE(summary->rigs == 1);
    REQUIRE(s.rigs.size() == 1);
    const scene::SkinnedRig& rig = s.rigs[0];
    CHECK(rig.skeleton.valid());
    CHECK(rig.skeleton.paletteSize() == 49);
    // The joints the skin names, plus the ancestor the exporter animates above them.
    CHECK(rig.skeleton.jointCount() > 49);
    CHECK(rig.skeleton.find("mixamorig:Hips") >= 0);

    REQUIRE(rig.clips.size() == 3);
    REQUIRE(rig.findClip("Idle") >= 0);
    REQUIRE(rig.findClip("Walk") >= 0);
    REQUIRE(rig.findClip("Run") >= 0);
    CHECK(rig.clips[static_cast<std::size_t>(rig.findClip("Idle"))].duration == Approx(3.633f).margin(0.01));
    CHECK(rig.clips[static_cast<std::size_t>(rig.findClip("Walk"))].duration == Approx(1.10f).margin(0.02));
    CHECK(rig.clips[static_cast<std::size_t>(rig.findClip("Run"))].duration == Approx(0.90f).margin(0.02));
    for (const auto& clip : rig.clips) {
        CHECK(clip.valid());
        CHECK(clip.channels.size() == 150);
    }
    // Every state the default machine offers must name a clip that exists.
    CHECK(rig.player.states().size() == 3);
    for (const auto& state : rig.player.states()) {
        CHECK(state.clip < rig.clips.size());
    }

    // One skinned entity, and its mesh carries influences that sum to one.
    std::size_t skinned = 0;
    for (const scene::Entity& e : s.entities) {
        if (e.rig != scene::kInvalidRig) {
            ++skinned;
            REQUIRE(e.mesh < s.meshes.size());
            const scene::MeshData& mesh = s.meshes[e.mesh];
            REQUIRE(mesh.skinned());
            for (const auto& influence : mesh.skin) {
                const float sum = influence.weights.x + influence.weights.y + influence.weights.z +
                                  influence.weights.w;
                CHECK(sum == Approx(1.0f).margin(1e-4));
                for (const std::uint16_t joint : influence.joints) {
                    CHECK(joint < rig.skeleton.paletteSize());
                }
            }
        }
    }
    CHECK(skinned == 1);
}

TEST_CASE("the alien's clips actually move the rig", "[assets][gltf][skeleton]") {
    if (!std::filesystem::is_regular_file(alienPath())) {
        SKIP("assets/imported/alien.gltf is not present");
    }
    scene::Scene s;
    REQUIRE(assets::loadGltf(alienPath(), s).has_value());
    scene::SkinnedRig& rig = s.rigs[0];
    REQUIRE(rig.player.play("Run", 0.0, 0.0f));

    REQUIRE(rig.evaluate(0.0));
    const std::vector<glm::mat4> first = rig.palette;
    REQUIRE(rig.evaluate(0.45)); // half a second into a 0.90 s clip
    const std::vector<glm::mat4> later = rig.palette;

    float maxShift = 0.0f;
    for (std::size_t i = 0; i < first.size(); ++i) {
        maxShift = std::max(maxShift, glm::length(glm::vec3(later[i][3]) - glm::vec3(first[i][3])));
    }
    // The asset is authored in centimetres, so a running figure's extremities move tens of units.
    CHECK(maxShift > 5.0f);

    // Sampling the same timeline second twice must give the same matrices, bit for bit.
    rig.evaluate(0.45);
    CHECK(rig.palette == later);

    // A looping clip returns to where it started after exactly one period.
    const float period = rig.clips[static_cast<std::size_t>(rig.findClip("Run"))].duration;
    rig.evaluate(0.0);
    const std::vector<glm::mat4> start = rig.palette;
    rig.evaluate(static_cast<double>(period));
    for (std::size_t i = 0; i < start.size(); ++i) {
        CHECK(glm::length(glm::vec3(rig.palette[i][3]) - glm::vec3(start[i][3])) < 1e-3f);
    }
}

// ---- the scene declaration ------------------------------------------------------------------

TEST_CASE("a scene file declares an animated character end to end", "[scene][composition][skeleton]") {
    const auto sceneFile = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "characters" /
                           "alien.scene.json";
    if (!std::filesystem::is_regular_file(sceneFile) || !std::filesystem::is_regular_file(alienPath())) {
        SKIP("the character example or its asset is not present");
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
    time.renderTime = 0.0;
    composition.update(time);
    const scene::Scene& s = composition.scene();
    // Three gltf nodes on the same file: three independent rigs, not one shared pose.
    REQUIRE(s.rigs.size() == 3);
    CHECK(composition.rigStats().rigs == 3);
    CHECK(composition.rigStats().posed == 3);

    // Each node entered the state its JSON asked for.
    std::vector<std::string> states;
    for (const scene::SkinnedRig& rig : s.rigs) {
        states.emplace_back(rig.player.currentState());
    }
    CHECK(std::find(states.begin(), states.end(), "Idle") != states.end());
    CHECK(std::find(states.begin(), states.end(), "Walk") != states.end());
    CHECK(std::find(states.begin(), states.end(), "Run") != states.end());

    // Every drawable entity that came from a skinned node names one of them.
    std::size_t skinnedEntities = 0;
    for (const scene::Entity& e : s.entities) {
        if (e.rig != scene::kInvalidRig) {
            ++skinnedEntities;
            CHECK(e.rig < s.rigs.size());
        }
    }
    CHECK(skinnedEntities == 3);

    // Advancing the timeline moves the poses, and the three rigs diverge because their clips do.
    const std::vector<glm::mat4> before = s.rigs[0].palette;
    time.renderTime = 0.4;
    composition.update(time);
    CHECK(s.rigs[0].palette != before);
    CHECK(s.rigs[0].palette != s.rigs[1].palette);

    // The seam a behaviour uses: name a node, name a state, name the second it happened.
    REQUIRE(composition.setNodeAnimation("idle", "Run", 0.4));
    time.renderTime = 0.45;
    composition.update(time);
    CHECK(composition.scene().rigs[0].player.currentState() == "Run");
    CHECK(composition.scene().rigs[0].player.blendWeight(0.45) < 1.0f); // mid cross-fade, not a cut

    auto* idlePosition = parameters.findAs<glm::vec3>("nodes/idle/position");
    REQUIRE(idlePosition != nullptr);
    idlePosition->setBase({100.0f, 0.0f, 0.0f});
    parameters.resetFinals();
    composition.update(time);
    std::size_t culledIdle = 0;
    for (const scene::Entity& entity : composition.scene().entities) {
        if (entity.rig == 0 && entity.cameraCulled) {
            ++culledIdle;
        }
    }
    CHECK(culledIdle > 0);
    idlePosition->setBase({-1.25f, 0.0f, 0.0f});
    parameters.resetFinals();
    composition.update(time);
    for (const scene::Entity& entity : composition.scene().entities) {
        if (entity.rig == 0) {
            CHECK_FALSE(entity.cameraCulled);
        }
    }
}

TEST_CASE("the animation block survives a save and reload", "[scene][composition][skeleton]") {
    const auto sceneFile = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "characters" /
                           "alien.scene.json";
    if (!std::filesystem::is_regular_file(sceneFile) || !std::filesystem::is_regular_file(alienPath())) {
        SKIP("the character example or its asset is not present");
    }
    assets::AssetRegistry registry{sceneFile.parent_path()};
    auto loaded = scene::Composition::loadFile(sceneFile, registry);
    REQUIRE(loaded.has_value());
    const nlohmann::json written = (*loaded)->toJson();
    auto again = scene::Composition::fromJson(written, registry);
    REQUIRE(again.has_value());
    const scene::CompositionNode* walk = (*again)->findNode("walk");
    REQUIRE(walk != nullptr);
    CHECK(walk->animation.state == "Walk");
    CHECK(walk->animation.blend == Approx(0.35f));
}

// ---- Phase 5.1: culling bounds must contain the pose, not the bind ------------------------------
//
// The renderer forensics plan's longest-standing open evidence gap. The failure it describes is a
// posed limb crossing a frustum plane while the bind pose does not, so the character is culled and
// the limb disappears.
//
// The first attempt at this used the alien composition and could not discriminate: with a T-pose
// bind, the bind-pose box is *wider* than every pose the clip animates into, so bind-pose bounds are
// conservative there and both implementations agree. Recorded so it is not retried. The instrument
// this actually needs is a rig that reaches *past* its bind pose, which is what the bar below does:
// its tip joint swings the top half a metre and a half sideways, well outside the bind box.

namespace {

// A vertical bar, subdivided so it can bend, weighted by height between a root and a tip joint.
scene::MeshData reachingBar() {
    scene::MeshData m;
    constexpr float half = 0.2f;
    constexpr float height = 3.0f;
    constexpr int segments = 12;
    for (int s = 0; s <= segments; ++s) {
        const float y = height * static_cast<float>(s) / static_cast<float>(segments);
        const glm::vec3 corners[4] = {{-half, y, -half}, {half, y, -half}, {half, y, half}, {-half, y, half}};
        for (const glm::vec3& c : corners) {
            m.vertices.push_back({c, glm::vec3(0.0f, 1.0f, 0.0f), {0.0f, 0.0f}});
        }
    }
    for (int s = 0; s < segments; ++s) {
        const auto a = static_cast<std::uint32_t>(s * 4);
        const auto b = static_cast<std::uint32_t>((s + 1) * 4);
        for (std::uint32_t c = 0; c < 4; ++c) {
            const std::uint32_t n = (c + 1) % 4;
            m.indices.insert(m.indices.end(), {a + c, b + c, b + n, a + c, b + n, a + n});
        }
    }
    m.skin.assign(m.vertices.size(), scene::SkinInfluence{});
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const float t = std::clamp(m.vertices[i].position.y / height, 0.0f, 1.0f);
        m.skin[i].joints[0] = 0;
        m.skin[i].joints[1] = 1;
        m.skin[i].weights = {1.0f - t, t, 0.0f, 0.0f};
    }
    return m;
}

// The palette for a bar whose tip joint is rotated `radians` about +Z, which swings the top of the
// bar out along -X. At a right angle the tip reaches about 1.5 m past the bind box.
std::vector<glm::mat4> barPalette(float radians) {
    const glm::mat4 identity(1.0f);
    const glm::mat4 toTip = glm::translate(identity, glm::vec3(0.0f, 1.5f, 0.0f));
    const glm::mat4 fromTip = glm::translate(identity, glm::vec3(0.0f, -1.5f, 0.0f));
    const glm::mat4 bend = toTip * glm::mat4_cast(glm::angleAxis(radians, glm::vec3(0.0f, 0.0f, 1.0f))) * fromTip;
    return {identity, bend};
}

} // namespace

TEST_CASE("cull bounds contain a pose that reaches outside the bind pose", "[scene][skeleton][culling]") {
    scene::Scene scene;
    const auto mesh = scene.addMesh(reachingBar());
    REQUIRE(scene.meshes[mesh].skinned());

    scene::SkinnedRig rig;
    rig.skeleton.name = "bar";
    scene::Joint root;
    root.name = "root";
    root.parent = -1;
    rig.skeleton.joints.push_back(root);
    scene::Joint tip;
    tip.name = "tip";
    tip.parent = 0;
    tip.rest.position = {0.0f, 1.5f, 0.0f};
    rig.skeleton.joints.push_back(tip);
    rig.skeleton.palette = {0, 1};
    scene.rigs.push_back(rig);

    auto& entity = scene.addEntity("bar", mesh);
    entity.rig = 0;

    const auto [bindLo, bindHi] = scene.meshes[mesh].bounds();

    // At rest the palette is the identity, so the posed box is the bind box.
    scene.rigs[0].palette = barPalette(0.0f);
    const scene::CullBounds rest = scene::entityCullBounds(scene, entity);
    CHECK(rest.posed);
    CHECK(rest.min.x == Approx(bindLo.x - ((bindHi.x - bindLo.x) * 0.25f + 0.25f)).margin(1e-4));

    // Bent a right angle: the top half swings out along -X, past anything the bind pose covers.
    scene.rigs[0].palette = barPalette(1.5707963f);
    const scene::CullBounds bent = scene::entityCullBounds(scene, entity);
    REQUIRE(bent.posed);

    // The discriminating fact: the pose genuinely leaves the bind box. Without this the rest of the
    // test would pass against a bind-pose implementation, which is how the first attempt at this
    // regression fooled itself.
    INFO("bind x " << bindLo.x << ".." << bindHi.x << ", posed cull box x " << bent.min.x << ".." << bent.max.x);
    REQUIRE(bent.min.x < bindLo.x - 1.0f);

    // And the box contains every posed vertex, which is the property culling depends on.
    const scene::MeshData& bar = scene.meshes[mesh];
    std::size_t checked = 0;
    for (std::size_t i = 0; i < bar.vertices.size(); ++i) {
        glm::vec3 posed(0.0f);
        float sum = 0.0f;
        for (std::size_t j = 0; j < scene::kJointInfluences; ++j) {
            const float w = bar.skin[i].weights[j];
            if (w <= 0.0f) {
                continue;
            }
            posed += glm::vec3(scene.rigs[0].palette[bar.skin[i].joints[j]] *
                               glm::vec4(bar.vertices[i].position, 1.0f)) * w;
            sum += w;
        }
        if (sum <= 1e-6f) {
            continue;
        }
        posed /= sum;
        REQUIRE(posed.x >= bent.min.x);
        REQUIRE(posed.x <= bent.max.x);
        REQUIRE(posed.y >= bent.min.y);
        REQUIRE(posed.y <= bent.max.y);
        ++checked;
    }
    CHECK(checked == bar.vertices.size());

    // The counterfactual, stated as an assertion rather than as a comment: the bind-pose box does
    // *not* contain the posed geometry, so an implementation using it would cull this character
    // while part of it was still on screen.
    const glm::vec3 bindPad = (bindHi - bindLo) * 0.25f + glm::vec3(0.25f);
    CHECK(bent.min.x < bindLo.x - bindPad.x);
}

TEST_CASE("an unskinned entity uses its mesh bounds and says so", "[scene][skeleton][culling]") {
    scene::Scene scene;
    const auto mesh = scene.addMesh(reachingBar());
    scene.meshes[mesh].skin.clear(); // no influences: nothing to pose with
    auto& entity = scene.addEntity("static", mesh);
    entity.transform.position = {5.0f, 0.0f, -2.0f};

    const scene::CullBounds bounds = scene::entityCullBounds(scene, entity);
    CHECK_FALSE(bounds.posed);
    // Moved with the entity, which is the other half of what the box is for.
    CHECK(bounds.min.x > 3.0f);
    CHECK(bounds.max.x > 5.0f);
}
