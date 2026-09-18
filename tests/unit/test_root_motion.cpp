// Root motion (ADR-335), P9 of `docs/character-ai-plan.md`.
//
// ADR-161 decided root motion was not implemented because the content had none; ADR-260 re-ran the
// check across all 168 clips this project can load and found five per alien that do, `Landing`'s
// -0.567 m among them, and left it as an inventory. This file is the measurement of the fix.
//
// ---- what is actually being claimed, because it is not the obvious thing -----------------------
//
// Root motion is a **transfer of authority, not a change to the picture**. A clip that carries
// displacement already draws a body moving; what nothing knows is that the body moved. So the
// claim under test is a conjunction, and neither half is interesting alone:
//
//   * the simulation position -- `state().position()`, ADR-260's first column -- moves by the
//     clip's displacement, in world metres, through the node's scale;
//   * the **drawn** position does not move at all, because the same displacement is taken out of
//     the pose in the same frame.
//
// A test that only checked the first would pass on an implementation that moved the body twice. A
// test that only checked the second would pass on an implementation that did nothing. The controls
// below are chosen so that each half can fail on its own.
//
// ---- the five arms and what each one's control is (ADR-182) -------------------------------------
//
//   A  extraction, off the asset          control: `Walking`, an in-place clip, through the same
//                                         code path, must measure ~0. A sampler that was reading
//                                         the rest pose twice would report 0 for `Landing` too,
//                                         and this is what tells those apart.
//   B  the carrier                        control: compensating at `root.x` -- the textbook target
//                                         -- instead of at the skeleton root. It must leave the
//                                         torso descending, because on this rig the spine is a
//                                         *sibling* of `root.x`. The arm exists because the
//                                         obvious implementation is the wrong one here and the
//                                         difference is 0.57 m.
//   C  the three positions, in the engine control: the same scene with the opt-in removed. Every
//                                         number this arm asserts must collapse, and the *drawn*
//                                         joint must start moving by the amount the simulation
//                                         stopped moving by.
//   D  the other 163 clips                control: the arm is symmetric. Every clip that is not
//                                         opted in must be **bit-identical** in its full joint
//                                         palette, and the one that is opted in must differ. A
//                                         comparison that could not see a difference would pass
//                                         the first half vacuously.
//   E  the generation                     control: the same two samples with the generation held
//                                         equal, which must produce the teleport the real code
//                                         does not.
//
// Bands, never floors. `Landing`'s dY is -0.5666 and the assertions bracket it within a
// centimetre; an assertion of `< -0.10` would stay green through an implementation that had
// doubled it, which is the exact bug ADR-182 is named after.

#include "assets/asset_registry.hpp"
#include "assets/gltf_loader.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/animation.hpp"
#include "scene/composition.hpp"
#include "scene/root_motion.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"
#include "signals/signal_bus.hpp"
#include "support/stride_speed.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

fs::path sourceRoot() { return fs::path(AVGEN_SOURCE_DIR); }
fs::path scoutPath() { return sourceRoot() / "assets" / "aliens" / "alien-scout.glb"; }
bool assetsPresent() { return fs::exists(scoutPath()); }

scene::Scene loadScout() {
    scene::Scene s;
    REQUIRE(assets::loadGltf(scoutPath(), s));
    REQUIRE(!s.rigs.empty());
    return s;
}

// The model-space position of a joint with one clip sampled at one second. Deliberately the
// test's *own* arithmetic (`tests/support/stride_speed.hpp`, which predates this unit by two
// ADRs) rather than `scene::jointModelPosition`, so arm A is not the implementation agreeing
// with itself.
glm::vec3 jointAt(const scene::SkinnedRig& rig, const scene::AnimationClip& clip, int joint,
                  float t) {
    scene::Pose pose;
    std::vector<glm::mat4> model;
    return testing::jointPositionAt(rig, clip, joint, t, pose, model);
}

// ---- arm C's fixture ---------------------------------------------------------------------------
//
// Written to a temp file rather than added to `examples/labs/character/`, and that is a decision
// rather than tidiness: `character-intelligence-lab.scene.json` carries six bodies who perceive
// and score one another, and case 15's golden position trace is taken from it. A body added there
// to demonstrate root motion would change what the other five see.
//
// `behaviors` is empty in the ungrounded arm. That is the whole of what makes the vertical
// measurable: `ground` and `explore` both assign `state.travel.y = ground.height - anchor.y`, so
// on a grounded body the y component of root motion is overwritten inside the same update that
// produced it. The grounded arm asserts exactly that, because it is the finding an author needs
// and not a defect to be hidden.
std::string sceneJson(bool optIn, bool grounded, float scale) {
    json node;

    node["name"] = "subject";
    node["kind"] = "gltf";
    node["asset"] = scoutPath().string();
    node["position"] = json::array({0.0, 0.0, 0.0});
    node["rotation"] = json::array({0.0, 0.0, 0.0});
    node["scale"] = json::array({scale, scale, scale});
    json anim;
    anim["state"] = "Landing";
    anim["blend"] = 0.0;
    anim["cullDistance"] = 0.0;
    if (optIn) {
        anim["rootMotion"] = json::array({"Landing"});
    }
    node["animation"] = anim;

    json entity;
    entity["name"] = "subject";
    entity["node"] = "subject";
    entity["seed"] = 7;
    entity["cullDistance"] = 0.0;
    entity["clips"] = json{{"idle", "Landing"}};
    // Zero cross-fade. The default gait blend is 0.2 s and the *entity's* value overrides the
    // node's, so without this the first twelve frames of the arm are a fade out of whatever state
    // an imported rig happens to make current -- `Button_push`, on this asset -- and the baseline
    // frame is a pose from a different clip. Measured once as a "drawn toe" that started at the
    // clip's *end* height; recorded here rather than worked around.
    entity["gait"] = json{{"blend", 0.0}};
    json behaviors = json::array();
    if (grounded) {
        behaviors.push_back(json{{"kind", "ground"}});
    }
    entity["behaviors"] = std::move(behaviors);

    json scene;
    scene["format"] = "avgen-scene";
    scene["version"] = 1;
    scene["name"] = "root-motion-arm";
    scene["nodes"] = json::array({node});
    scene["entities"] = json::array({entity});
    return scene.dump(1);
}

struct EngineArm {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;
    fs::path file;

    EngineArm(bool optIn, bool grounded, float scale, const std::string& tag)
        : registry(sourceRoot()) {
        file = fs::temp_directory_path() / fmt::format("avgen-rootmotion-{}.scene.json", tag);
        {
            std::ofstream out(file);
            out << sceneJson(optIn, grounded, scale);
        }
        auto loaded = scene::Composition::loadFile(file, registry);
        INFO((loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(1280, 720);
        comp->scene().detailLimits.entityDistanceCull = false;
    }
    ~EngineArm() {
        std::error_code ec;
        fs::remove(file, ec);
    }
    EngineArm(const EngineArm&) = delete;
    EngineArm& operator=(const EngineArm&) = delete;

    // The application's own order: fields, routes, behaviour, then `Composition::update`, which is
    // where the rigs are posed. Root motion is read inside `updateBehaviour` and compensated
    // inside `update`, one stage apart, which is the ordering this unit had to answer for.
    int frame = 0; // frames played so far, across every call

    // Continues from wherever the last call stopped, which is load-bearing rather than tidy: an
    // arm that reads a baseline after one frame and then plays a clip must end on the same
    // timeline second as one that played it straight through, or the two differ for a reason that
    // has nothing to do with root motion (ADR-300 §7's first lesson, paid for once already).
    void play(double seconds, double hz = 60.0) {
        const double step = 1.0 / hz;
        const auto frames = static_cast<int>(std::llround(seconds * hz));
        FrameTime time;
        const int first = frame;
        for (int i = first; i < first + frames; ++i) {
            frame = i + 1;
            time.renderTime = static_cast<double>(i) * step;
            time.deltaTime = i == 0 ? 0.0 : step;
            time.frameIndex = static_cast<std::uint64_t>(i);
            params.resetFinals();
            bus.clearEvents();
            comp->updateFields(time, bus, modulator);
            modulator.applyRoutes(bus, params, time.deltaTime);
            comp->updateBehaviour(time, bus);
            comp->update(time);
        }
    }

    [[nodiscard]] const entity::Entity& subject() const {
        const entity::Entity* e = comp->entityWorld().find("subject");
        REQUIRE(e != nullptr);
        return *e;
    }
    // ADR-260's third position: the node parameter `applyOffsets` writes and the renderer draws.
    [[nodiscard]] glm::vec3 nodeParameter() {
        const auto* p = params.findAs<glm::vec3>("nodes/subject/position");
        REQUIRE(p != nullptr);
        return glm::vec3(p->finalComponent(0), p->finalComponent(1), p->finalComponent(2));
    }
    [[nodiscard]] const scene::SkinnedRig& rig() const {
        for (const scene::SkinnedRig& r : comp->scene().rigs) {
            if (r.name.rfind("subject/", 0) == 0) {
                return r;
            }
        }
        FAIL("no rig on the subject node");
        return comp->scene().rigs.front();
    }
    // Where the renderer actually puts a joint: the node parameter plus the posed joint through
    // the node's scale. The composition of ADR-260's third position with ADR-274's entity-local
    // one, which is the only pair of numbers whose sum anybody can see.
    [[nodiscard]] glm::vec3 drawnJoint(const char* joint) {
        const scene::SkinnedRig& r = rig();
        const int index = r.skeleton.find(joint);
        REQUIRE(index >= 0);
        REQUIRE(r.pose.size() == r.skeleton.jointCount());
        const glm::vec3 local = scene::jointModelPosition(r.skeleton, r.pose, index);
        const auto* s = params.findAs<glm::vec3>("nodes/subject/scale");
        REQUIRE(s != nullptr);
        const glm::vec3 scale(s->finalComponent(0), s->finalComponent(1), s->finalComponent(2));
        return nodeParameter() + local * scale;
    }
};

} // namespace

// ================================================================================================
// A. The extraction, measured off the asset.
// ================================================================================================

TEST_CASE("root motion extraction reads Landing's displacement and Walking's absence of one",
          "[unit][rootmotion]") {
    if (!assetsPresent()) {
        SKIP("assets/aliens/alien-scout.glb not present");
    }
    const scene::Scene s = loadScout();
    const scene::SkinnedRig& rig = s.rigs[0];

    scene::RootMotionSet set;
    const std::vector<std::string> problems =
        set.bind({scene::RootMotionSpec{"Landing", "", {}}, scene::RootMotionSpec{"Walking", "", {}}},
                 rig.skeleton, rig.clips);
    INFO(fmt::format("binder said: [{}]", fmt::join(problems, "; ")));
    REQUIRE(problems.empty());
    REQUIRE(set.size() == 2);

    // The joint the rule picks, named rather than assumed. ADR-260's first attempt took
    // `joints[0]` and got 168 clips of silent zero, because `rig` is an armature wrapper no clip
    // animates.
    const scene::RootMotionBinding& landing = *set.find(rig.findClip("Landing"));
    CHECK(landing.jointName == "root.x");
    CHECK(landing.joint == 1);
    // And the carrier, which is not the same joint and is the whole of arm B.
    CHECK(landing.carrier == 0);
    CHECK(rig.skeleton.joints[0].name == "rig");
    CHECK(rig.skeleton.joints[0].parent == -1);

    scene::Pose scratch;
    const scene::AnimationClip& clip = rig.clips[static_cast<std::size_t>(landing.clip)];
    const glm::vec3 d =
        scene::rootMotionDisplacement(rig.skeleton, rig.clips, landing, clip.duration, scratch);

    // Bands. ADR-260 reports 0.022 m of XZ and -0.567 m of dY for this clip; the probe that opened
    // this unit reproduced (+0.0151, -0.5666, -0.0158) exactly. A floor would let a doubling
    // through.
    const float xz = std::hypot(d.x, d.z);
    INFO(fmt::format("Landing root displacement ({:+.4f}, {:+.4f}, {:+.4f}), |XZ| {:.4f}", d.x, d.y,
                     d.z, xz));
    CHECK(d.y < -0.560f);
    CHECK(d.y > -0.575f);
    CHECK(xz > 0.019f);
    CHECK(xz < 0.025f);

    // The measurement re-taken by the test's own arithmetic, which is not the code under test.
    const glm::vec3 a = jointAt(rig, clip, landing.joint, clip.start);
    const glm::vec3 b = jointAt(rig, clip, landing.joint, clip.duration);
    CHECK_THAT(glm::length((b - a) - d), Catch::Matchers::WithinAbs(0.0, 1e-5));

    // ---- the control -------------------------------------------------------------------------
    // `Walking` through the identical code path. ADR-161 measured 0.0000 of net displacement on
    // the Mixamo walk and the alien pack's walk is the same kind of take: the first key and the
    // last key of `root.x` are equal to the bit. If the sampler were dead -- reading a channel
    // that is not there and handing back the rest pose twice -- `Landing` would have measured zero
    // as well, and this is the arm that says it did not.
    const scene::RootMotionBinding& walking = *set.find(rig.findClip("Walking"));
    const scene::AnimationClip& walk = rig.clips[static_cast<std::size_t>(walking.clip)];
    const glm::vec3 w =
        scene::rootMotionDisplacement(rig.skeleton, rig.clips, walking, walk.duration, scratch);
    INFO(fmt::format("Walking root displacement ({:+.6f}, {:+.6f}, {:+.6f})", w.x, w.y, w.z));
    CHECK(glm::length(w) < 1e-4f);
    // ...and it is not zero because the sampler never moved: the walk's root sways and returns.
    // The span is the control on the control.
    float span = 0.0f;
    for (int i = 0; i <= 32; ++i) {
        const float t = walk.start + walk.length() * static_cast<float>(i) / 32.0f;
        span = std::max(span, glm::length(jointAt(rig, walk, walking.joint, t) -
                                          jointAt(rig, walk, walking.joint, walk.start)));
    }
    INFO(fmt::format("Walking root span over the cycle {:.4f} m", span));
    CHECK(span > 0.01f);

    // The axis mask, which is the setting `Landing` in a world with ground in it actually wants.
    scene::RootMotionSet xzOnly;
    REQUIRE(xzOnly.bind({scene::RootMotionSpec{"Landing", "", {true, false, true}}}, rig.skeleton,
                        rig.clips)
                .empty());
    const glm::vec3 masked = scene::rootMotionDisplacement(
        rig.skeleton, rig.clips, *xzOnly.find(rig.findClip("Landing")), clip.duration, scratch);
    CHECK(masked.y == 0.0f);
    CHECK_THAT(static_cast<double>(std::hypot(masked.x, masked.z)),
               Catch::Matchers::WithinAbs(static_cast<double>(xz), 1e-6));
}

// ================================================================================================
// B. The carrier. The arm that says the textbook implementation is wrong on this content.
// ================================================================================================

TEST_CASE("compensating at the root joint tears the body; compensating at the skeleton root does not",
          "[unit][rootmotion]") {
    if (!assetsPresent()) {
        SKIP("assets/aliens/alien-scout.glb not present");
    }
    const scene::Scene s = loadScout();
    const scene::SkinnedRig& rig = s.rigs[0];
    const int clipIndex = rig.findClip("Landing");
    REQUIRE(clipIndex >= 0);
    const scene::AnimationClip& clip = rig.clips[static_cast<std::size_t>(clipIndex)];

    // The structural fact this whole arm rests on, asserted rather than described: `root.x` is not
    // an ancestor of the torso. If a future asset swap makes it one, this is the line that says so
    // before the numbers below stop meaning anything.
    const int rootX = rig.skeleton.find("root.x");
    const int spine = rig.skeleton.find("spine_01.x");
    const int head = rig.skeleton.find("head.x");
    REQUIRE(rootX == 1);
    REQUIRE(spine > 0);
    REQUIRE(head > 0);
    CHECK(rig.skeleton.joints[static_cast<std::size_t>(spine)].parent == 0);
    CHECK(rig.skeleton.joints[static_cast<std::size_t>(head)].parent == 0);
    CHECK(rig.skeleton.joints[static_cast<std::size_t>(rootX)].parent == 0);

    scene::RootMotionSet set;
    REQUIRE(set.bind({scene::RootMotionSpec{"Landing", "", {}}}, rig.skeleton, rig.clips).empty());
    const scene::RootMotionBinding real = *set.find(clipIndex);

    // The same binding with the carrier moved to the read joint: what "zero the root channel"
    // would do, expressed in this code's own vocabulary so the two arms differ in one field.
    scene::RootMotionBinding textbook = real;
    textbook.carrier = rootX;

    scene::Pose scratch;
    const auto residual = [&](const scene::RootMotionBinding& binding, int joint) {
        scene::Pose first;
        scene::setRestPose(rig.skeleton, first);
        scene::sampleClip(clip, clip.start, first);
        scene::applyRootMotionCompensation(
            binding, scene::rootMotionDisplacement(rig.skeleton, rig.clips, binding, clip.start, scratch),
            first);
        scene::Pose last;
        scene::setRestPose(rig.skeleton, last);
        scene::sampleClip(clip, clip.duration, last);
        scene::applyRootMotionCompensation(
            binding,
            scene::rootMotionDisplacement(rig.skeleton, rig.clips, binding, clip.duration, scratch),
            last);
        return glm::length(scene::jointModelPosition(rig.skeleton, last, joint) -
                           scene::jointModelPosition(rig.skeleton, first, joint));
    };

    // With the real carrier, a joint that carries no animation of its own past the root's is held
    // still to the micron. `root.x` is that joint by construction; `spine_01.x` is that joint by
    // measurement, because its own displacement over `Landing` matches the root's to 0.7 mm.
    const float rootHeld = residual(real, rootX);
    const float spineHeld = residual(real, spine);
    INFO(fmt::format("carrier = skeleton root: root.x moves {:.6f} m, spine_01.x moves {:.6f} m",
                     rootHeld, spineHeld));
    CHECK(rootHeld < 1e-5f);
    CHECK(spineHeld < 0.002f);

    // ---- the control ---------------------------------------------------------------------------
    // The textbook carrier holds `root.x` just as still -- so an arm that only looked at the root
    // would have reported both implementations as correct -- and leaves the spine falling the
    // whole 0.567 m. That is a character whose legs stay put while its torso drops through them.
    const float rootTextbook = residual(textbook, rootX);
    const float spineTextbook = residual(textbook, spine);
    INFO(fmt::format("carrier = root.x: root.x moves {:.6f} m, spine_01.x moves {:.6f} m",
                     rootTextbook, spineTextbook));
    CHECK(rootTextbook < 1e-5f);
    CHECK(spineTextbook > 0.55f);
    CHECK(spineTextbook < 0.58f);
}

// ================================================================================================
// C. The three positions of ADR-260, in the engine, with the opt-in as the control.
// ================================================================================================

TEST_CASE("Landing's displacement reaches the simulation and the node parameter, and not the picture",
          "[unit][rootmotion]") {
    if (!assetsPresent()) {
        SKIP("assets/aliens/alien-scout.glb not present");
    }
    // Glowmere's own scale band, 3.344x to 3.610x (ADR-274 §5). The number matters: a joint offset
    // is in the asset's units, so the world metres a clip is worth are the clip's units times this.
    constexpr float kScale = 3.61f;
    // Landing's keys run 0.0333 s to 1.1000 s, a playable length of 1.0667 s, and the default
    // states this engine gives an imported clip **loop**. So the arm stops just inside the last
    // key rather than on it: one frame past the end and the clip has wrapped back to its first
    // pose, and the measurement would be of a body that had landed and then teleported back into
    // the air.
    constexpr double kClip = 1.0;

    EngineArm on(true, false, kScale, "on");
    EngineArm off(false, false, kScale, "off");

    const glm::vec3 onStart = on.subject().state().position();
    const glm::vec3 offStart = off.subject().state().position();
    on.play(1.0 / 60.0);
    off.play(1.0 / 60.0);
    const glm::vec3 onDrawnStart = on.drawnJoint("toes_01.l");
    const glm::vec3 offDrawnStart = off.drawnJoint("toes_01.l");

    on.play(kClip);
    off.play(kClip);

    // The opt-in really did bind. A silent miss would make every assertion below pass as the
    // control does, which is the failure mode ADR-274 is about.
    REQUIRE(on.rig().rootMotion.size() == 1);
    REQUIRE(off.rig().rootMotion.empty());
    CHECK(on.rig().rootMotionApplied);

    // ---- ADR-260's first position: the simulation -------------------------------------------
    const glm::vec3 sim = on.subject().state().position() - onStart;
    const float expected = -0.5666f * kScale; // -2.045 m
    INFO(fmt::format("simulation moved ({:+.4f}, {:+.4f}, {:+.4f}); expected dY {:+.4f}", sim.x,
                     sim.y, sim.z, expected));
    CHECK(sim.y < expected + 0.03f);
    CHECK(sim.y > expected - 0.03f);
    CHECK(std::hypot(sim.x, sim.z) > 0.06f);
    CHECK(std::hypot(sim.x, sim.z) < 0.10f);

    // ---- ADR-260's second: visualPosition. No behaviour writes a MotionOffset here, so it must
    // equal the first exactly -- and saying so is what makes the third number below a claim about
    // the renderer rather than about an offset nobody set.
    CHECK_THAT(glm::length(on.subject().visualPosition() - on.subject().state().position()),
               Catch::Matchers::WithinAbs(0.0, 1e-6));

    // ---- ADR-260's third: the node parameter, which is what the renderer draws ---------------
    const float nodeY = on.nodeParameter().y;
    INFO(fmt::format("node parameter y {:+.4f}", nodeY));
    CHECK(nodeY < expected + 0.03f);
    CHECK(nodeY > expected - 0.03f);

    // ---- and the picture, which must not have moved ------------------------------------------
    // The composition of the third position with the pose. The two writes are equal and opposite,
    // so the drawn toe ends the clip within a millimetre of where it ends without the opt-in.
    const glm::vec3 onDrawn = on.drawnJoint("toes_01.l");
    const glm::vec3 offDrawn = off.drawnJoint("toes_01.l");
    INFO(fmt::format("drawn toe with opt-in ({:+.4f}, {:+.4f}, {:+.4f}), without ({:+.4f}, {:+.4f}, "
                     "{:+.4f})",
                     onDrawn.x, onDrawn.y, onDrawn.z, offDrawn.x, offDrawn.y, offDrawn.z));
    CHECK(glm::length(onDrawn - offDrawn) < 0.001f);

    // ---- the control ---------------------------------------------------------------------------
    // Without the opt-in every number above collapses, and the picture is the same picture. The
    // second half of that sentence is the sensitivity check: the drawn toe descends 2.8 m over
    // this clip in *both* arms, so the measurement above is not blind to 2 m of movement -- it is
    // looking at 2 m of movement and finding the two arms agree on it to a millimetre.
    const glm::vec3 offSim = off.subject().state().position() - offStart;
    INFO(fmt::format("control simulation moved ({:+.6f}, {:+.6f}, {:+.6f})", offSim.x, offSim.y,
                     offSim.z));
    CHECK(glm::length(offSim) < 1e-5f);
    CHECK(std::abs(off.nodeParameter().y) < 1e-5f);

    const float onFell = onDrawnStart.y - onDrawn.y;
    const float offFell = offDrawnStart.y - offDrawn.y;
    INFO(fmt::format("the drawn toe fell {:.4f} m with the opt-in and {:.4f} m without", onFell,
                     offFell));
    CHECK(offFell > 2.5f);
    CHECK(onFell > 2.5f);
    CHECK(std::abs(onFell - offFell) < 0.001f);
}

TEST_CASE("a grounded body keeps no vertical root motion, which is why the axis mask exists",
          "[unit][rootmotion]") {
    if (!assetsPresent()) {
        SKIP("assets/aliens/alien-scout.glb not present");
    }
    constexpr float kScale = 3.61f;
    EngineArm grounded(true, true, kScale, "grounded");
    EngineArm loose(true, false, kScale, "loose");
    grounded.play(1.0);
    loose.play(1.0);

    // `ground` assigns rather than adds: `state.travel.y = ground.height - anchor.y`, every frame,
    // after root motion has run. So the vertical is gone and the horizontal survives. This is not
    // a defect in either layer -- a body standing on terrain has its height decided by the terrain
    // -- and it is the measurement behind ADR-335 §5's recommendation that a world with ground in
    // it opts `Landing` in on `xz` and not on `xyz`.
    const glm::vec3 g = grounded.subject().state().position();
    const glm::vec3 l = loose.subject().state().position();
    INFO(fmt::format("grounded ({:+.4f}, {:+.4f}, {:+.4f}) vs ungrounded ({:+.4f}, {:+.4f}, {:+.4f})",
                     g.x, g.y, g.z, l.x, l.y, l.z));
    CHECK(std::abs(g.y) < 1e-4f);         // erased
    CHECK(l.y < -2.0f);                   // and the control kept it
    CHECK(l.y > -2.1f);
    // The horizontal is the half that survives grounding, and it is the half `xz` keeps.
    CHECK_THAT(static_cast<double>(std::hypot(g.x, g.z)),
               Catch::Matchers::WithinAbs(static_cast<double>(std::hypot(l.x, l.z)), 1e-4));
    CHECK(std::hypot(g.x, g.z) > 0.06f);
}

// ================================================================================================
// D. The other 163 clips, bit-identical -- proved rather than asserted.
// ================================================================================================

TEST_CASE("every clip nobody opted in poses bit-identically, and the one that did does not",
          "[unit][rootmotion]") {
    if (!assetsPresent()) {
        SKIP("assets/aliens/alien-scout.glb not present");
    }
    struct Asset {
        std::string label;
        fs::path path;
    };
    std::vector<Asset> assetsUnderTest;
    for (const char* a : {"alien-scout", "alien-pilot", "alien-diver", "alien-elder",
                          "alien-ranger", "alien-trooper"}) {
        const fs::path p = sourceRoot() / "assets" / "aliens" / (std::string(a) + ".glb");
        if (fs::exists(p)) {
            assetsUnderTest.push_back({a, p});
        }
    }
    for (const char* a : {"bull", "chick", "chicken", "cow", "goat", "horse", "pig", "rooster",
                          "sheep"}) {
        const fs::path p = sourceRoot() / "assets" / "farm" / (std::string(a) + ".glb");
        if (fs::exists(p)) {
            assetsUnderTest.push_back({a, p});
        }
    }
    REQUIRE(assetsUnderTest.size() >= 4);

    int clipsCompared = 0;
    int clipsIdentical = 0;
    int clipsDifferent = 0;
    std::vector<std::string> differing;

    for (const Asset& a : assetsUnderTest) {
        scene::Scene s;
        REQUIRE(assets::loadGltf(a.path, s));
        if (s.rigs.empty()) {
            continue;
        }
        for (std::size_t r = 0; r < s.rigs.size(); ++r) {
            // Two copies of the same rig. One has no opt-in at all -- the shipped configuration of
            // every rig in this repository. The other opts in `Landing`, and nothing else.
            scene::SkinnedRig plain = s.rigs[r];
            scene::SkinnedRig opted = s.rigs[r];
            plain.addDefaultStates(0.0f);
            opted.addDefaultStates(0.0f);
            (void)opted.rootMotion.bind({scene::RootMotionSpec{"Landing", "", {}}}, opted.skeleton,
                                        opted.clips);

            for (std::size_t c = 0; c < plain.clips.size(); ++c) {
                const scene::AnimationClip& clip = plain.clips[c];
                const std::string state(
                    clip.name.substr(clip.name.rfind('|') == std::string::npos
                                         ? 0
                                         : clip.name.rfind('|') + 1));
                if (plain.player.stateIndex(state) < 0) {
                    continue;
                }
                REQUIRE(plain.player.play(state, 0.0, 0.0f));
                REQUIRE(opted.player.play(state, 0.0, 0.0f));
                bool identical = true;
                for (int k = 0; k <= 8; ++k) {
                    const double t = static_cast<double>(clip.start) +
                                     static_cast<double>(clip.length()) * k / 8.0;
                    // A distinct second each time, because `evaluate` short-circuits on an
                    // unchanged sample second and two rigs that both declined to re-pose would
                    // compare equal for the most boring possible reason.
                    plain.evaluate(t, 0.0f);
                    opted.evaluate(t, 0.0f);
                    REQUIRE(plain.palette.size() == opted.palette.size());
                    // memcmp, not an epsilon. "Bit-identical" is the claim and an epsilon would be
                    // a different, weaker one.
                    if (std::memcmp(plain.palette.data(), opted.palette.data(),
                                    plain.palette.size() * sizeof(glm::mat4)) != 0) {
                        identical = false;
                    }
                }
                ++clipsCompared;
                if (identical) {
                    ++clipsIdentical;
                } else {
                    ++clipsDifferent;
                    differing.push_back(a.label + "/" + clip.name);
                }
            }
        }
    }

    INFO(fmt::format("{} clips compared, {} bit-identical, {} changed: [{}]", clipsCompared,
                     clipsIdentical, clipsDifferent, fmt::join(differing, ", ")));
    REQUIRE(clipsCompared >= 100);
    // Exactly the opted-in clip changed, on exactly the assets that carry it. Both halves can
    // fail: if the opt-in leaked, `clipsDifferent` climbs; if the comparison were blind, it is
    // zero and the second assertion catches it.
    CHECK(clipsDifferent > 0);
    for (const std::string& d : differing) {
        INFO(d);
        CHECK(d.find("Landing") != std::string::npos);
    }
    CHECK(clipsIdentical == clipsCompared - clipsDifferent);
    CHECK(clipsIdentical > 100);
}

// ================================================================================================
// E. The generation. The piece most likely to be wrong, with the wrongness written out.
// ================================================================================================

TEST_CASE("a clip change ends a run of root motion rather than unwinding it", "[unit][rootmotion]") {
    if (!assetsPresent()) {
        SKIP("assets/aliens/alien-scout.glb not present");
    }
    scene::Scene s = loadScout();
    scene::SkinnedRig rig = s.rigs[0];
    rig.addDefaultStates(0.0f);
    REQUIRE(rig.rootMotion.bind({scene::RootMotionSpec{"Landing", "", {}}}, rig.skeleton, rig.clips)
                .empty());

    // 1.00 s rather than 1.10 s: `addDefaultStates` makes every imported clip a **looping**
    // state, `Landing`'s playable length is 1.0667 s, and one sample past the end is the clip's
    // first pose again. An arm that read the last key would have measured a body that landed and
    // then teleported back into the air, and reported -0.06 for a -0.57 displacement.
    REQUIRE(rig.player.play("Landing", 0.0, 0.0f));
    const scene::RootMotionSample begun = rig.rootMotionAt(0.0);
    const scene::RootMotionSample ended = rig.rootMotionAt(1.00);
    REQUIRE(begun.active);
    REQUIRE(ended.active);
    CHECK(begun.generation == ended.generation);
    INFO(fmt::format("within one run: {:+.4f} -> {:+.4f}", begun.displacement.y,
                     ended.displacement.y));
    CHECK(begun.displacement.y == 0.0f); // the clip's first key is the origin of the measure
    CHECK(ended.displacement.y - begun.displacement.y < -0.55f);
    CHECK(ended.displacement.y - begun.displacement.y > -0.58f);

    // Cross-fade out of it. The clip is no longer opted in, so the seam reports inactive and the
    // entity's rule -- "an inactive sample ends the run" -- stops it subtracting anything.
    REQUIRE(rig.player.play("Idle", 1.05, 0.0f));
    CHECK_FALSE(rig.rootMotionAt(1.3).active);

    // And back into it, restarted. A naive consumer holding only the previous displacement would
    // now compute `0 - (-0.5666)`: the whole of `Landing` backwards in one step, a 2 m teleport at
    // scale. The generation is what prevents it, so the generation must actually change.
    REQUIRE(rig.player.play("Landing", 2.0, 0.0f));
    const scene::RootMotionSample again = rig.rootMotionAt(2.02);
    REQUIRE(again.active);
    INFO(fmt::format("generation {} then {}", ended.generation, again.generation));
    CHECK(again.generation != ended.generation);

    // ---- the control ---------------------------------------------------------------------------
    // The teleport the generation prevents, computed here so the number is on the record rather
    // than being a thing the comment claims. This is the step a consumer that ignored the
    // generation would take, and it is 0.56 model units -- 2.05 m at Glowmere's 3.61x.
    const float naive = again.displacement.y - ended.displacement.y;
    INFO(fmt::format("a generation-blind consumer would step {:+.4f} model units here", naive));
    CHECK(naive > 0.52f);
    CHECK(naive < 0.60f);
    // The real consumer's step across the same boundary is the displacement of the new run alone.
    CHECK(std::abs(again.displacement.y) < 0.08f);
}

// ================================================================================================
// F. Determinism. The frame rate decides how many samples, never what they add up to.
// ================================================================================================

TEST_CASE("the accumulated root motion is the clip's closed form, at any frame rate",
          "[unit][rootmotion]") {
    if (!assetsPresent()) {
        SKIP("assets/aliens/alien-scout.glb not present");
    }
    constexpr float kScale = 3.61f;

    // The closed form: the clip's own displacement at the second the arm stops on, times the
    // scale. The entity gets there by summing ~60 differences, and the sum of a telescoping series
    // is its endpoints -- which is exactly the property a running total kept on the rig would not
    // have had, and the reason `rootMotionAt` measures from the clip's first key every time
    // rather than from the previous frame (ADR-091, ADR-267 D4).
    const scene::Scene s = loadScout();
    const scene::SkinnedRig& asset = s.rigs[0];
    scene::RootMotionSet set;
    REQUIRE(set.bind({scene::RootMotionSpec{"Landing", "", {}}}, asset.skeleton, asset.clips).empty());
    const scene::RootMotionBinding& b = *set.find(asset.findClip("Landing"));
    scene::Pose scratch;

    struct Arm {
        double hz;
        const char* tag;
    };
    std::vector<glm::vec3> errors;
    for (const Arm& arm : {Arm{60.0, "60hz"}, Arm{40.0, "40hz"}, Arm{24.0, "24hz"}}) {
        EngineArm e(true, false, kScale, arm.tag);
        e.play(1.0, arm.hz);
        // The last second this arm actually rendered, which is not 1.0 s and is not the same on
        // all three: an arm that compared final positions between rates rather than each against
        // its own closed form would be measuring the sub-frame remainder, not determinism.
        const auto frames = static_cast<int>(std::llround(1.0 * arm.hz));
        const double last = static_cast<double>(frames - 1) / arm.hz;
        // `clip.start`, not zero. A state's local clock starts at zero and the *clip* it plays
        // starts wherever its keys do -- 1/30 s on every take in the alien pack, because Blender
        // writes the frame range it was given. Omitting it put the closed form 33 ms of clip
        // early and the arm reported a systematic 3.9 mm error at all three rates, which is what
        // a constant offset looks like when you were expecting accumulation noise.
        const float clipSeconds =
            asset.clips[static_cast<std::size_t>(b.clip)].start + static_cast<float>(last);
        const glm::vec3 closed =
            scene::rootMotionDisplacement(asset.skeleton, asset.clips, b, clipSeconds, scratch) *
            kScale;
        const glm::vec3 got = e.subject().state().position();
        const glm::vec3 err = got - closed;
        INFO(fmt::format("{}: {} frames to t={:.5f}; summed ({:+.5f}, {:+.5f}, {:+.5f}), closed "
                         "form ({:+.5f}, {:+.5f}, {:+.5f}), error {:.7f} m",
                         arm.tag, frames, last, got.x, got.y, got.z, closed.x, closed.y, closed.z,
                         glm::length(err)));
        // A tenth of a millimetre over two metres of travel and sixty accumulations.
        CHECK(glm::length(err) < 1e-4f);
        // And the closed form is not trivially zero, which is what would make the check above
        // pass on a body that never moved.
        CHECK(closed.y < -1.9f);
        errors.push_back(err);
    }

    // ---- the control ---------------------------------------------------------------------------
    // The three rates land on three different seconds and therefore three different positions, so
    // this is not three copies of the same arithmetic: they disagree with one another by more than
    // the tolerance each one meets against its own closed form.
    REQUIRE(errors.size() == 3);
    EngineArm a60(true, false, kScale, "c60");
    EngineArm a24(true, false, kScale, "c24");
    a60.play(1.0, 60.0);
    a24.play(1.0, 24.0);
    const float between =
        glm::length(a60.subject().state().position() - a24.subject().state().position());
    INFO(fmt::format("60 Hz and 24 Hz stop {:.5f} m apart, and each is within 1e-4 m of its own "
                     "closed form",
                     between));
    CHECK(between > 1e-3f);
}
