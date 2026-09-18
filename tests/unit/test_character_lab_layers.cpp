// Character Intelligence Lab -- the animation layer stack (ADR-300).
//
// The thing being proved here is not "a layer stack exists". It is that something which could not
// be said before can now be said: **a head that turns towards what a character is attending to
// while the legs go on walking.** `entity::behaviors.cpp`'s `LookAt` has always refused to turn the
// *body* while travelling, on the grounds that "where the eyes and head go on top of a walk cycle is
// the animation layer's business, and LocomotionState carries it there". It carried it to nothing.
// `LocomotionState::lookTarget`, `hasLookTarget` and `reaction` were written every frame and read by
// nobody, which ADR-225 calls a decoration rather than a seam.
//
// ADR-182 decides the shape of the evidence. "The head moved" alone proves nothing -- a clip that
// happens to move a head would satisfy it, and so would a layer that moved every joint in the rig.
// Three arms, and the two controls have to fail if the mask is wrong:
//
//   arm A   the look layer as the fixture authors it   -> the head group turns, the feet do not
//   arm B   the same layer, weight pinned to zero      -> nothing moves at all  (the layer is the cause)
//   arm C   the same layer, masked onto the FEET       -> the feet move and the head does not (the
//                                                        mask is the cause, not the layer)
//
// A and B together rule out "the clip was doing that anyway". A and C together rule out "the layer
// writes the whole pose and the mask is decoration". Delete the mask and C fails; delete the layer
// and A fails; ignore the intent and B passes vacuously, which is why B asserts an exact zero rather
// than a small number.
//
// The second half is the additive reaction: a flinch played on the upper body *over* a walk, driven
// by `LocomotionState::reaction`, with the same feet control.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/animation.hpp"
#include "scene/composition.hpp"
#include "scene/pose_layers.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fmt/format.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr float kDegrees = 57.2957795131f;

fs::path fixture() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "character" /
           "character-intelligence-lab.scene.json";
}

bool assetsPresent() {
    return fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}

// How the application ticks it, the same order `tests/unit/test_character_lab_sockets.cpp` uses:
// fields, routes, behaviour, then `Composition::update`, which is where the rigs are posed and
// therefore where the layers run.
struct Fixture {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;
    signals::SignalId impact{};
    int frame = 0; // frames played so far, across every call to play()

    Fixture() : registry(fixture().parent_path()) {
        auto loaded = scene::Composition::loadFile(fixture(), registry);
        INFO((loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(1280, 720);
        comp->scene().detailLimits.entityDistanceCull = false;
        impact = bus.declare("music.impact", 0.0f, 1.0f, true);
    }

    // Continues from wherever the last call stopped, which is load-bearing rather than tidy: an
    // arm that runs `play(0.5)`, re-binds a layer and then runs `play(8.0)` has to end on the *same
    // timeline second and the same number of simulation steps* as an arm that ran `play(8.5)`
    // straight through, or the two poses differ for a reason that has nothing to do with the layer.
    // The first version of this file restarted the clock on every call and reported 0.026 m and
    // 0.043 m of "masked" foot movement that was really half a second of walking.
    //
    // `fireAt` < 0 means never. The event is a one-frame pulse, which is what `Interest` reads.
    void play(double seconds, int fireFrame = -1, double hz = 60.0) {
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
            if (i == fireFrame) {
                bus.setEvent(impact, true, 1.0f);
                bus.set(impact, 1.0f);
            }
            comp->updateFields(time, bus, modulator);
            modulator.applyRoutes(bus, params, time.deltaTime);
            comp->updateBehaviour(time, bus);
            comp->update(time);
        }
    }

    [[nodiscard]] scene::SkinnedRig* rig(const std::string& node) {
        for (scene::SkinnedRig& r : comp->scene().rigs) {
            if (r.name.rfind(node + "/", 0) == 0) {
                return &r;
            }
        }
        return nullptr;
    }
};

// Model-space joint positions, which is the frame every answer in ADR-274 lives in: the rig's own,
// which is the entity's own. Not the palette -- `palette[k] = model[palette[k]] * inverseBind[k]`
// and its translation is 0.911 model units from the joint on this very asset.
std::vector<glm::vec3> jointPositions(const scene::SkinnedRig& rig) {
    std::vector<glm::mat4> model;
    scene::poseToModel(rig.skeleton, rig.pose, model);
    std::vector<glm::vec3> out(model.size());
    for (std::size_t i = 0; i < model.size(); ++i) {
        out[i] = glm::vec3(model[i][3]);
    }
    return out;
}

float positionOf(const std::vector<glm::vec3>& p, const scene::Skeleton& sk, const char* joint,
                 glm::vec3& out) {
    const int i = sk.find(joint);
    if (i < 0 || static_cast<std::size_t>(i) >= p.size()) {
        return -1.0f;
    }
    out = p[static_cast<std::size_t>(i)];
    return 1.0f;
}

// The head group's facing, as a scalar a person can argue with: the azimuth of the axis from the
// right eye to the left eye. It is a real axis of this rig rather than a convention, it is
// perpendicular to the gaze, and a rotation of the group about +Y moves it degree for degree.
float interocularAzimuthDegrees(const std::vector<glm::vec3>& p, const scene::Skeleton& sk) {
    glm::vec3 l(0.0f);
    glm::vec3 r(0.0f);
    if (positionOf(p, sk, "Eye_L", l) < 0.0f || positionOf(p, sk, "Eye_R", r) < 0.0f) {
        return 0.0f;
    }
    const glm::vec3 axis = l - r;
    return std::atan2(axis.x, axis.z) * kDegrees;
}

float signedAngleDelta(float a, float b) {
    float d = b - a;
    while (d > 180.0f) {
        d -= 360.0f;
    }
    while (d < -180.0f) {
        d += 360.0f;
    }
    return d;
}

// Replaces the watcher's look layer wholesale, re-resolving it against the rig it will run on. The
// return value is the binder's own prose, which is the thing a scene author sees in the log.
std::vector<std::string> rebindLook(scene::SkinnedRig& rig, const scene::PoseLayer& replacement) {
    std::vector<scene::PoseLayer> layers = rig.layers.layers();
    REQUIRE_FALSE(layers.empty());
    layers[0] = replacement;
    return rig.layers.bind(std::move(layers), rig.skeleton, rig.clips);
}

} // namespace

// ------------------------------------------------------------------------------------------------
// The aim, on its own, with no rig in the way
// ------------------------------------------------------------------------------------------------

TEST_CASE("an aim rotation turns towards a direction and stops at its limits",
          "[labs][character][layers]") {
    const float yawLimit = 70.0f / kDegrees;
    const float pitchLimit = 35.0f / kDegrees;
    const glm::vec3 forward(0.0f, 0.0f, 1.0f);

    // The control that has to fail if `aimRotation` ever returns something for nothing: asked to
    // turn from a direction to itself, it must return the identity exactly, because a layer that
    // "aims" at where it already points and moves anyway is a layer that drifts.
    const glm::quat none = scene::aimRotation(forward, forward, yawLimit, pitchLimit);
    CHECK(std::abs(glm::angle(none)) < 1e-5f);

    // 40 degrees to the left, inside the limit: taken whole.
    const glm::vec3 left(std::sin(40.0f / kDegrees), 0.0f, std::cos(40.0f / kDegrees));
    const glm::quat inside = scene::aimRotation(forward, left, yawLimit, pitchLimit);
    const glm::vec3 landed = inside * forward;
    CHECK(std::abs(std::atan2(landed.x, landed.z) * kDegrees - 40.0f) < 0.01f);

    // 140 degrees, outside it: clamped to exactly 70, not refused and not wrapped the short way
    // round to -140, which is the failure mode of clamping a rotation instead of a direction.
    const glm::vec3 behind(std::sin(140.0f / kDegrees), 0.0f, std::cos(140.0f / kDegrees));
    const glm::vec3 clamped = scene::aimRotation(forward, behind, yawLimit, pitchLimit) * forward;
    CHECK(std::abs(std::atan2(clamped.x, clamped.z) * kDegrees - 70.0f) < 0.01f);

    // And the pitch limit is its own number, not the same one.
    const glm::vec3 up(0.0f, std::sin(80.0f / kDegrees), std::cos(80.0f / kDegrees));
    const glm::vec3 pitched = scene::aimRotation(forward, up, yawLimit, pitchLimit) * forward;
    CHECK(std::abs(std::asin(std::clamp(pitched.y, -1.0f, 1.0f)) * kDegrees - 35.0f) < 0.01f);
}

// ------------------------------------------------------------------------------------------------
// A mask is resolved against the rig it will run on, and says what it could not find
// ------------------------------------------------------------------------------------------------

TEST_CASE("a joint mask resolves against a real skeleton and reports the names it could not find",
          "[labs][character][layers]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present; a mask needs a real rig");
        return;
    }
    Fixture fx;
    fx.play(0.5);
    scene::SkinnedRig* rig = fx.rig("watcher");
    REQUIRE(rig != nullptr);
    const scene::Skeleton& sk = rig->skeleton;
    INFO(fmt::format("{} has {} joints", rig->name, sk.jointCount()));

    // The measurement the default is built on. `descendants` is the obvious way to say "the head
    // and everything on it" and on this rig it says almost nothing: `head.x` has no children. The
    // eyes, the mouth and the antenna are its *siblings*.
    scene::JointMaskSpec subtree;
    subtree.joints = {"head.x"};
    subtree.descendants = true;
    const scene::JointMask fromSubtree = scene::resolveJointMask(sk, subtree);
    INFO(fmt::format("head.x + descendants covers {} joint(s) of {}", fromSubtree.joints,
                     sk.jointCount()));
    CHECK(fromSubtree.joints == 1);
    CHECK(fromSubtree.missing.empty());

    // The group that actually is the head on this rig, named.
    scene::JointMaskSpec group;
    group.joints = {"head.x", "Eye_L", "Eye_R", "Mouth", "Antenna"};
    const scene::JointMask fromGroup = scene::resolveJointMask(sk, group);
    CHECK(fromGroup.joints == 5);
    CHECK(fromGroup.nested == 0);
    CHECK(fromGroup.missing.empty());

    // The honest failure. A name this rig does not carry is *reported*, not folded into a weight of
    // zero that looks identical to "nobody asked". This is the same distinction ADR-274 had to make
    // for sockets, where a `bool` could not tell a hand from a body origin and cost 28.661 m.
    scene::JointMaskSpec wrong;
    wrong.joints = {"Head01", "Beak", "head.x"}; // the bull's, the chicken's, and this one's
    const scene::JointMask mixed = scene::resolveJointMask(sk, wrong);
    CHECK(mixed.named == 3);
    CHECK(mixed.joints == 1);
    REQUIRE(mixed.missing.size() == 2);
    CHECK(mixed.missing[0] == "Head01");
    CHECK(mixed.missing[1] == "Beak");

    // A weight per joint, and a mask that names nothing this rig has is empty and says so rather
    // than resolving to "the whole skeleton" or to "nothing was asked".
    scene::JointMaskSpec weighted;
    weighted.joints = {"head.x", "Antenna"};
    weighted.weights = {1.0f, 0.4f};
    const scene::JointMask w = scene::resolveJointMask(sk, weighted);
    CHECK(w.at(static_cast<std::size_t>(sk.find("head.x"))) == 1.0f);
    CHECK(w.at(static_cast<std::size_t>(sk.find("Antenna"))) == 0.4f);
    CHECK(w.at(static_cast<std::size_t>(sk.find("foot.l"))) == 0.0f);
}

// ------------------------------------------------------------------------------------------------
// A layer survives a save (ADR-225)
// ------------------------------------------------------------------------------------------------

TEST_CASE("an authored layer stack round-trips through the scene file", "[labs][character][layers]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    // A setting the application does not keep is not a setting, and this whole unit exists because
    // four fields were written every frame and read by nobody. A layer stack that could be authored
    // and not saved would be the same defect one level up.
    assets::AssetRegistry registry(fixture().parent_path());
    auto loaded = scene::Composition::loadFile(fixture(), registry);
    REQUIRE(loaded.has_value());
    const nlohmann::json written = (*loaded)->toJson();
    auto again = scene::Composition::fromJson(written, registry);
    INFO((again.has_value() ? std::string() : again.error().message));
    REQUIRE(again.has_value());

    const scene::CompositionNode* before = (*loaded)->findNode("watcher");
    const scene::CompositionNode* after = (*again)->findNode("watcher");
    REQUIRE(before != nullptr);
    REQUIRE(after != nullptr);
    REQUIRE(before->animation.layers.size() == 2);
    REQUIRE(after->animation.layers.size() == before->animation.layers.size());
    for (std::size_t i = 0; i < before->animation.layers.size(); ++i) {
        const scene::PoseLayer& a = before->animation.layers[i];
        const scene::PoseLayer& b = after->animation.layers[i];
        INFO(fmt::format("layer {} '{}'", i, a.name));
        CHECK(b.name == a.name);
        CHECK(b.kind == a.kind);
        CHECK(b.drive == a.drive);
        CHECK(b.mask.joints == a.mask.joints);
        CHECK(b.mask.descendants == a.mask.descendants);
        CHECK(b.pivot == a.pivot);
        CHECK(b.clip == a.clip);
        CHECK(b.maxYawDegrees == a.maxYawDegrees);
        CHECK(b.maxPitchDegrees == a.maxPitchDegrees);
        CHECK(b.forward == a.forward);
    }
    // The control: a node that authored no layers must not gain one on the way through, or "it
    // round-trips" would be a statement about a default rather than about what was written.
    const scene::CompositionNode* plain = (*again)->findNode("scout");
    REQUIRE(plain != nullptr);
    CHECK(plain->animation.layers.empty());
}

// ------------------------------------------------------------------------------------------------
// The thing that could not be said before
// ------------------------------------------------------------------------------------------------

TEST_CASE("the head turns towards what the character is attending to and the feet do not",
          "[labs][character][layers]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present; the layer arms need a real rig");
        return;
    }
    constexpr double kSeconds = 8.0;

    // ---- arm B first: the layer pinned off, which is this world with no layer in it -------------
    // Run first so its poses are the reference every other arm is measured against. Pinning the
    // weight rather than deleting the behaviour is what keeps the *simulation* identical: a layer is
    // ADR-260 `PoseOnly` and cannot feed back into where the body goes, so arm A and arm B walk the
    // same walk and any difference in the pose is the layer's and nothing else's.
    std::vector<glm::vec3> unlayered;
    scene::Skeleton skeleton;
    float unlayeredAzimuth = 0.0f;
    entity::Activity activity = entity::Activity::Idle;
    glm::vec3 lookTarget(0.0f);
    {
        Fixture fx;
        fx.play(0.5);
        scene::SkinnedRig* rig = fx.rig("watcher");
        REQUIRE(rig != nullptr);
        scene::PoseLayer off = rig->layers.layers()[0];
        off.drive = scene::PoseLayerDrive::Manual; // so `driveLayers` stops writing its weight
        off.weight = 0.0f;
        CHECK(rebindLook(*rig, off).empty());
        fx.play(kSeconds);
        rig = fx.rig("watcher");
        REQUIRE(rig != nullptr);
        skeleton = rig->skeleton;
        unlayered = jointPositions(*rig);
        unlayeredAzimuth = interocularAzimuthDegrees(unlayered, skeleton);
        CHECK(rig->layers.results()[0] == scene::LayerResolution::Inactive);
        const entity::Entity* watcher = fx.comp->entityWorld().find("watcher");
        REQUIRE(watcher != nullptr);
        activity = watcher->locomotion().activity;
        lookTarget = watcher->locomotion().lookTarget;
        // The seam is publishing. If this were false the rest of the file would be measuring
        // nothing, and it was false for every character in this engine until now.
        CHECK(watcher->locomotion().hasLookTarget);
    }

    // ---- arm A: the fixture as authored ---------------------------------------------------------
    std::vector<glm::vec3> layered;
    float layeredAzimuth = 0.0f;
    std::uint32_t layerJoints = 0;
    {
        Fixture fx;
        fx.play(kSeconds + 0.5);
        scene::SkinnedRig* rig = fx.rig("watcher");
        REQUIRE(rig != nullptr);
        REQUIRE(rig->layers.size() == 2);
        CHECK(rig->layers.results()[0] == scene::LayerResolution::Applied);
        layerJoints = rig->layerStats.joints;
        layered = jointPositions(*rig);
        layeredAzimuth = interocularAzimuthDegrees(layered, skeleton);
    }

    // ---- arm C: the same layer, the wrong mask ---------------------------------------------------
    std::vector<glm::vec3> misMasked;
    float misMaskedAzimuth = 0.0f;
    {
        Fixture fx;
        fx.play(0.5);
        scene::SkinnedRig* rig = fx.rig("watcher");
        REQUIRE(rig != nullptr);
        scene::PoseLayer feet = rig->layers.layers()[0];
        feet.mask.joints = {"foot.l", "foot.r"};
        feet.pivot = "foot.l";
        CHECK(rebindLook(*rig, feet).empty());
        fx.play(kSeconds);
        rig = fx.rig("watcher");
        REQUIRE(rig != nullptr);
        CHECK(rig->layers.results()[0] == scene::LayerResolution::Applied);
        misMasked = jointPositions(*rig);
        misMaskedAzimuth = interocularAzimuthDegrees(misMasked, skeleton);
    }

    REQUIRE(unlayered.size() == layered.size());
    REQUIRE(unlayered.size() == misMasked.size());

    const auto moved = [&](const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b,
                           const char* joint) {
        const int i = skeleton.find(joint);
        REQUIRE(i >= 0);
        return glm::length(b[static_cast<std::size_t>(i)] - a[static_cast<std::size_t>(i)]);
    };

    const float headTurn = signedAngleDelta(unlayeredAzimuth, layeredAzimuth);
    const float eyeMoved = moved(unlayered, layered, "Eye_L");
    const float leftFoot = moved(unlayered, layered, "foot.l");
    const float rightFoot = moved(unlayered, layered, "foot.r");
    const float hip = moved(unlayered, layered, "root.x");

    INFO(fmt::format("arm A: head turned {:+.2f} deg, Eye_L moved {:.4f} model units, feet moved "
                     "{:.6f} / {:.6f}, root {:.6f}; the layer wrote {} joints; activity {}",
                     headTurn, eyeMoved, leftFoot, rightFoot, hip, layerJoints,
                     entity::activityName(activity)));
    INFO(fmt::format("look target ({:.2f}, {:.2f}, {:.2f})", lookTarget.x, lookTarget.y, lookTarget.z));

    // The head moved.
    CHECK(std::abs(headTurn) > 5.0f);
    CHECK(eyeMoved > 0.02f);
    // The legs did not. Exactly, not approximately: a masked layer that touched them at all would
    // be a mask that is not a mask. The tolerance is float round-trip through a matrix decompose,
    // not a budget.
    CHECK(leftFoot < 1e-5f);
    CHECK(rightFoot < 1e-5f);
    CHECK(hip < 1e-5f);
    // Five joints, which is the head group of this rig and not the rig.
    CHECK(layerJoints == 5);

    // The control that makes the first half mean something: with the intent pinned off, the very
    // same frame of the very same walk has the head exactly where the clip left it.
    CHECK(std::abs(signedAngleDelta(unlayeredAzimuth, unlayeredAzimuth)) < 1e-6f);

    // The control that makes the *mask* mean something. Same layer, same intent, same target,
    // masked onto the feet: now the feet move and the head does not. If the mask were ignored and
    // the layer wrote the whole pose, both of these would fail; if the layer did nothing at all,
    // the first would.
    // Measured on `toes_01.l` and on `foot.r` rather than on `foot.l`: `foot.l` is this arm's pivot,
    // and a joint turned about its own origin keeps its position exactly. The first version of this
    // arm read `foot.l` and reported 0.000000061 m of movement from a layer that was working
    // perfectly -- a control that fails for the wrong reason is as useless as one that cannot fail.
    const float misMaskedHead = signedAngleDelta(unlayeredAzimuth, misMaskedAzimuth);
    const float misMaskedToe = moved(unlayered, misMasked, "toes_01.l");
    const float misMaskedOther = moved(unlayered, misMasked, "foot.r");
    INFO(fmt::format("arm C: masked onto the feet -- head turned {:+.4f} deg, toes_01.l moved {:.4f}, "
                     "foot.r moved {:.4f}",
                     misMaskedHead, misMaskedToe, misMaskedOther));
    CHECK(misMaskedToe > 0.01f);
    CHECK(misMaskedOther > 0.05f);
    CHECK(std::abs(misMaskedHead) < 1e-4f);
}

// ------------------------------------------------------------------------------------------------
// The aim points where it was asked to
// ------------------------------------------------------------------------------------------------

TEST_CASE("an aim layer turns the head group by the angle the target subtends, up to its limit",
          "[labs][character][layers]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    // Manual drive and an explicit entity-local target, so the number under test is the layer's
    // arithmetic rather than a behaviour's choice of subject. The target is placed 10 units away in
    // the rig's own frame at head height; 90 degrees against a 75-degree limit is the arm that has
    // to be clamped to exactly 75 rather than taken whole or refused.
    //
    // **Every arm runs the identical number of simulation steps**, including the reference. The
    // rebinding happens at the same point in every one of them, because the body's own yaw is what
    // the inter-ocular azimuth is measured against and half a second of extra walking moved it
    // 11.113 degrees in the first version of this arm -- a constant offset that looked exactly like
    // a bug in the clamp.
    const auto arm = [](bool aimAt, float wanted, float& azimuth, glm::vec3& headOut, float& limit) {
        Fixture fx;
        fx.play(0.5);
        scene::SkinnedRig* rig = fx.rig("watcher");
        REQUIRE(rig != nullptr);
        const std::vector<glm::vec3> base = jointPositions(*rig);
        glm::vec3 head(0.0f);
        REQUIRE(positionOf(base, rig->skeleton, "head.x", head) > 0.0f);
        headOut = head;
        scene::PoseLayer aim = rig->layers.layers()[0];
        aim.drive = scene::PoseLayerDrive::Manual; // the seam stops writing this layer's weight
        aim.weight = aimAt ? 1.0f : 0.0f;
        aim.hasTarget = aimAt;
        aim.target = head + 10.0f * glm::vec3(std::sin(wanted / kDegrees), 0.0f, std::cos(wanted / kDegrees));
        limit = aim.maxYawDegrees;
        CHECK(rebindLook(*rig, aim).empty());
        fx.play(0.5);
        rig = fx.rig("watcher");
        REQUIRE(rig != nullptr);
        azimuth = interocularAzimuthDegrees(jointPositions(*rig), rig->skeleton);
    };

    float reference = 0.0f;
    float limit = 0.0f;
    glm::vec3 head(0.0f);
    arm(false, 0.0f, reference, head, limit);

    for (const float wanted : {30.0f, 60.0f, 90.0f, -90.0f}) {
        float azimuth = 0.0f;
        glm::vec3 unused(0.0f);
        float armLimit = 0.0f;
        arm(true, wanted, azimuth, unused, armLimit);
        const float turned = signedAngleDelta(reference, azimuth);
        const float expected = std::clamp(wanted, -armLimit, armLimit);
        INFO(fmt::format("asked {:+.1f} deg, limit {:.1f}, turned {:+.3f}", wanted, armLimit, turned));
        CHECK(std::abs(turned - expected) < 1.5f);
    }
    // The control the four arms above need: with no target the same rig at the same second has the
    // head exactly where the clip left it, so "turned 75 degrees" is a turn and not an offset.
    float again = 0.0f;
    glm::vec3 unused(0.0f);
    float unusedLimit = 0.0f;
    arm(false, 90.0f, again, unused, unusedLimit);
    CHECK(std::abs(signedAngleDelta(reference, again)) < 1e-4f);
}

// ------------------------------------------------------------------------------------------------
// A layer that cannot work says so, and is a different answer from one nobody asked
// ------------------------------------------------------------------------------------------------

TEST_CASE("a mask that names a joint this rig does not have is reported, not silently nothing",
          "[labs][character][layers]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    Fixture fx;
    fx.play(0.5);
    scene::SkinnedRig* rig = fx.rig("watcher");
    REQUIRE(rig != nullptr);

    scene::PoseLayer wrong = rig->layers.layers()[0];
    wrong.mask.joints = {"Head01"}; // the bull's name for it
    wrong.pivot = "Head01";
    const std::vector<std::string> problems = rebindLook(*rig, wrong);
    INFO(fmt::format("{} problem(s): {}", problems.size(), problems.empty() ? "" : problems.front()));
    // Three separate complaints, because three separate things are wrong and an author fixing one
    // of them needs to know about the other two: the joint, the empty mask, the pivot.
    CHECK(problems.size() == 3);
    CHECK(problems[0].find("no joint 'Head01'") != std::string::npos);

    fx.play(0.5);
    rig = fx.rig("watcher");
    REQUIRE(rig != nullptr);
    // `NoJoints`, not `Inactive`. The whole point: "this layer was asked to do something and could
    // not" is a different sentence from "nothing asked it to", and before ADR-274 this engine had
    // exactly one word for both of them.
    CHECK(rig->layers.results()[0] == scene::LayerResolution::NoJoints);
    CHECK(rig->layers.results()[0] != scene::LayerResolution::Inactive);
}

// ------------------------------------------------------------------------------------------------
// The additive half: a flinch on the upper body, over a walk
// ------------------------------------------------------------------------------------------------

TEST_CASE("a reaction plays on the upper body while the legs keep the gait",
          "[labs][character][layers]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present");
        return;
    }
    constexpr double kSeconds = 4.0;
    // 3.75 s in. `Interest` decays `reaction` at 1.4 per second from a peak of 1, so it is back at
    // exactly zero 0.714 s later: firing at 3.0 s and reading at 4.0 s measures a flinch that has
    // already finished, which is what the first version of this arm did and reported as 0.0000.
    constexpr int kFireFrame = 225;

    // The control arm: the identical world, the identical second, and no impact. `Interest` decays
    // `reaction` at 1.4 per second from a cooldown-gated pulse, so without the event it is exactly
    // zero and the additive layer must be `Inactive` rather than "applied at zero weight".
    std::vector<glm::vec3> quiet;
    scene::Skeleton skeleton;
    {
        Fixture fx;
        fx.play(kSeconds);
        scene::SkinnedRig* rig = fx.rig("watcher");
        REQUIRE(rig != nullptr);
        skeleton = rig->skeleton;
        quiet = jointPositions(*rig);
        REQUIRE(rig->layers.size() == 2);
        CHECK(rig->layers.results()[1] == scene::LayerResolution::Inactive);
        const entity::Entity* watcher = fx.comp->entityWorld().find("watcher");
        REQUIRE(watcher != nullptr);
        CHECK(watcher->locomotion().reaction == 0.0f);
    }

    std::vector<glm::vec3> startled;
    float reaction = 0.0f;
    std::uint32_t wrote = 0;
    {
        Fixture fx;
        fx.play(kSeconds, kFireFrame);
        scene::SkinnedRig* rig = fx.rig("watcher");
        REQUIRE(rig != nullptr);
        CHECK(rig->layers.results()[1] == scene::LayerResolution::Applied);
        startled = jointPositions(*rig);
        const entity::Entity* watcher = fx.comp->entityWorld().find("watcher");
        REQUIRE(watcher != nullptr);
        reaction = watcher->locomotion().reaction;
        // Both layers ran this frame, which is the other thing one cross-fade between two clip
        // slots could not do: two things are happening to this body at once and neither is the gait.
        wrote = rig->layerStats.joints;
        CHECK(rig->layerStats.applied == 2);
    }

    REQUIRE(quiet.size() == startled.size());
    const auto moved = [&](const char* joint) {
        const int i = skeleton.find(joint);
        REQUIRE(i >= 0);
        return glm::length(startled[static_cast<std::size_t>(i)] - quiet[static_cast<std::size_t>(i)]);
    };

    const float chest = moved("spine_05.x");
    const float neck = moved("neck.x");
    const float foot = moved("foot.l");
    const float toe = moved("toes_01.l");
    INFO(fmt::format("reaction {:.4f}: chest moved {:.4f}, neck {:.4f}, foot {:.6f}, toe {:.6f}; "
                     "the two layers wrote {} joints between them",
                     reaction, chest, neck, foot, toe, wrote));

    // The seam reached a pose: `LocomotionState::reaction` is non-zero and the upper body shows it.
    CHECK(reaction > 0.0f);
    CHECK(chest > 0.005f);
    CHECK(neck > 0.005f);
    // And the gait underneath it is untouched, which is the whole difference between an additive
    // layer and the cross-fade this engine already had -- a cross-fade to a flinch would have
    // stopped the legs.
    CHECK(foot < 1e-5f);
    CHECK(toe < 1e-5f);
}
