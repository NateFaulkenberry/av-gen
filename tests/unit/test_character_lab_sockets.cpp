// Character Intelligence Lab -- sockets, and the skeleton query underneath them (ADR-274).
//
// The defect this file exists because of is not that sockets were wrong. It is that they were
// wrong and said they were right. `Entity::socketTransform` resolved a socket against the entity's
// own frame whenever no `ISkeletonQuery` was installed -- and nothing implemented `ISkeletonQuery`,
// and `Entity::setSkeleton` had zero call sites, so *every socket in this engine* took that path
// and returned `true` doing it. A consumer could not tell a hand from a body origin. ADR-262 is
// what that costs when the consumer is a tractor beam.
//
// So the arms here are built around exactly that distinction, per ADR-182:
//
//   arm A  no skeleton installed        -> EntityFrame, and the socket sits at the body
//   arm B  skeleton installed, real joint -> Joint, and the socket sits at the hand
//   arm C  skeleton installed, socket names NO joint     -> EntityFrame  (the control)
//   arm D  skeleton installed, socket names a MISSING joint -> EntityFrame  (the control)
//
// A and C are what make B evidence. Without them a test that only checked "the hand socket is not
// at the origin" would pass on a socket offset somebody typed into the scene file, and would have
// passed on the broken engine too if the fixture had happened to author an offset.
//
// Two further measurements, because both are mistakes that look right:
//
//   * **The palette is not the pose.** `SkinnedRig::palette[k]` is `model[palette[k]] *
//     inverseBind[k]`; its translation is not where the joint is. The third case below measures
//     the gap, so the number is on the record rather than the warning being on trust.
//   * **A node's scale is part of the answer.** The aliens in `glowmere-valley-2` are drawn at
//     3.344x to 3.610x. A joint offset is in the asset's own units, so a socket that ignored the
//     node scale put a hand-mounted prop at 28% of the distance from the body the hand is at.
//     Measured here as a ratio, which is a structural quantity and does not depend on a timing.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path fixture() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "character" /
           "character-intelligence-lab.scene.json";
}

bool assetsPresent() {
    return fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}

// The lab fixture, loaded and ticked the way the application ticks it: fields, routes, behaviour,
// then `Composition::update`. Stopping at `updateBehaviour` is half a frame -- the rigs are posed
// inside `Composition::update`, and an unposed rig is exactly the state in which the skeleton
// query is *supposed* to decline.
struct Fixture {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;

    Fixture() : registry(fixture().parent_path()) {
        auto loaded = scene::Composition::loadFile(fixture(), registry);
        INFO((loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(1280, 720);
        // ADR-186: every entity updated however far from the view. The subject here is the world,
        // not the camera, and a culled body is not simulated at all.
        comp->scene().detailLimits.entityDistanceCull = false;
    }

    void play(double seconds, double hz = 60.0) {
        const double step = 1.0 / hz;
        const auto frames = static_cast<int>(std::llround(seconds * hz));
        FrameTime time;
        for (int i = 0; i < frames; ++i) {
            time.renderTime = static_cast<double>(i) * step;
            time.deltaTime = i == 0 ? 0.0 : step;
            time.frameIndex = static_cast<std::uint64_t>(i);
            params.resetFinals();
            comp->updateFields(time, bus, modulator);
            modulator.applyRoutes(bus, params, time.deltaTime);
            comp->updateBehaviour(time, bus);
            comp->update(time);
        }
    }

    [[nodiscard]] entity::Entity* scout() { return comp->entityWorld().find("scout"); }
};

float horizontal(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}

} // namespace

TEST_CASE("a socket on a joint resolves to the joint, and one without a joint says so",
          "[labs][character][sockets]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present; the socket arms need a real rig");
        return;
    }
    Fixture fx;
    fx.play(1.0);
    entity::Entity* scout = fx.scout();
    REQUIRE(scout != nullptr);

    scene::Transform hand;
    scene::Transform head;
    scene::Transform chest;
    scene::Transform tail;
    const entity::SocketResolution handHow = scout->socketTransform("hand", hand);
    const entity::SocketResolution headHow = scout->socketTransform("head", head);
    const entity::SocketResolution chestHow = scout->socketTransform("chest", chest);
    const entity::SocketResolution tailHow = scout->socketTransform("tail", tail);
    scene::Transform absent;
    const entity::SocketResolution absentHow = scout->socketTransform("no-such-socket", absent);

    const glm::vec3 body = scout->visualPosition();
    INFO(fmt::format("body {:.3f},{:.3f},{:.3f}  hand {:.3f},{:.3f},{:.3f}  head {:.3f},{:.3f},{:.3f}",
                     body.x, body.y, body.z, hand.position.x, hand.position.y, hand.position.z,
                     head.position.x, head.position.y, head.position.z));

    // ---- arm B: a real joint answers, and it answers as a joint ----
    CHECK(handHow == entity::SocketResolution::Joint);
    CHECK(headHow == entity::SocketResolution::Joint);

    // ---- arm C: a socket that names no joint takes the entity frame, with a skeleton installed ----
    // This is the control that makes arm B mean something. The skeleton is there; this socket did
    // not ask it; the answer is the body's own frame plus the authored offset.
    CHECK(chestHow == entity::SocketResolution::EntityFrame);

    // ---- arm D: a socket naming a joint the rig does not carry ----
    // Before ADR-274 this was indistinguishable from arm B: both returned `true` and both put the
    // prop at the body. A typo in a scene file was a silent 1.2 m error.
    CHECK(tailHow == entity::SocketResolution::EntityFrame);

    // ---- and a socket that does not exist at all is still a different answer again ----
    CHECK(absentHow == entity::SocketResolution::None);
    CHECK_FALSE(resolved(absentHow));
    CHECK(resolved(handHow));
    CHECK(resolved(chestHow));

    // ---- the joint is demonstrably not at the entity origin (ADR-182) ----
    // A hand and a head on a 3.61x alien are metres from the body's own frame. If the query were
    // silently falling back, these would be zero to the last bit -- `chest` proves what a fallback
    // looks like, because its only displacement is the offset the fixture authored.
    const float handOffset = glm::length(hand.position - body);
    const float headOffset = glm::length(head.position - body);
    const float chestOffset = glm::length(chest.position - body);
    INFO(fmt::format("hand {:.3f} m, head {:.3f} m, chest (authored offset) {:.3f} m from the body",
                     handOffset, headOffset, chestOffset));
    CHECK(handOffset > 1.0f);
    CHECK(headOffset > 1.0f);
    // The head is above the hand and the hand is out to one side: two joints, two different places,
    // which no fallback can produce because a fallback gives every jointed socket the same answer.
    CHECK(glm::length(hand.position - head.position) > 0.5f);
    CHECK(head.position.y > hand.position.y);
    // The authored chest offset is 1.1 m up and 0.25 m forward in asset units, scaled by the node's
    // 3.61 -- so about 4.1 m. It is displaced, and it is displaced by arithmetic this test can do
    // itself, which is the difference between a control and a second unknown.
    CHECK(chestOffset > 3.0f);
}

TEST_CASE("without a skeleton every socket is the entity frame, and says so",
          "[labs][character][sockets]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present; the socket arms need a real rig");
        return;
    }
    // Arm A. The engine as it stood before ADR-274, reproduced by taking the skeleton back off the
    // one entity that has one. Every socket then gives the same answer -- and the old `bool` return
    // called that answer success.
    Fixture fx;
    fx.play(1.0);
    entity::Entity* scout = fx.scout();
    REQUIRE(scout != nullptr);

    scene::Transform withJoint;
    REQUIRE(scout->socketTransform("hand", withJoint) == entity::SocketResolution::Joint);

    scout->setSkeleton(nullptr);
    scene::Transform hand;
    scene::Transform head;
    scene::Transform chest;
    CHECK(scout->socketTransform("hand", hand) == entity::SocketResolution::EntityFrame);
    CHECK(scout->socketTransform("head", head) == entity::SocketResolution::EntityFrame);
    CHECK(scout->socketTransform("chest", chest) == entity::SocketResolution::EntityFrame);

    // The two jointed sockets now agree with each other exactly, because neither of them is asking
    // anything: that identity is the signature of the fallback, and it is what arm B above must not
    // have. Bit-identical, not approximately -- they are the same arithmetic.
    CHECK(hand.position == head.position);

    const float moved = glm::length(hand.position - withJoint.position);
    INFO(fmt::format("the hand socket moves {:.3f} m when the skeleton is taken away", moved));
    CHECK(moved > 1.0f);
}

TEST_CASE("the joint moves when the clip does, and the palette is not the pose",
          "[labs][character][sockets]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present; the socket arms need a real rig");
        return;
    }
    Fixture fx;
    fx.play(0.5);
    entity::Entity* scout = fx.scout();
    REQUIRE(scout != nullptr);

    // Where the hand is relative to the body, so the body's own wandering is divided out: what is
    // left is the animation, and nothing else.
    const auto handInBody = [&]() {
        scene::Transform t;
        REQUIRE(scout->socketTransform("hand", t) == entity::SocketResolution::Joint);
        return t.position - scout->visualPosition();
    };
    const glm::vec3 first = handInBody();
    fx.play(3.0);
    // `play` restarts its own clock at zero each call, which is deliberate here: it is a second
    // window over the same idle cycle rather than a continuation, and the assertion below is that
    // the two windows land the hand somewhere different -- a rig posed once and never again would
    // give the same vector twice.
    const glm::vec3 later = handInBody();
    INFO(fmt::format("hand in body frame: ({:.4f},{:.4f},{:.4f}) then ({:.4f},{:.4f},{:.4f})",
                     first.x, first.y, first.z, later.x, later.y, later.z));
    CHECK(glm::length(later - first) > 1.0e-4f);

    // ---- ADR-260's warning, measured ----
    // The GPU palette is `model * inverseBind`. Its translation is where a *bind-pose vertex at the
    // origin* lands, which is not where the joint is, and on a T-posed arm the difference is most
    // of the arm. This is the mistake a skeleton overlay exists to catch; the number is recorded
    // here so nobody has to take the warning on trust.
    const scene::Scene& sc = fx.comp->scene();
    REQUIRE_FALSE(sc.rigs.empty());
    const scene::SkinnedRig* rig = nullptr;
    for (const scene::SkinnedRig& r : sc.rigs) {
        if (r.skeleton.find("hand.r") >= 0 && r.pose.size() == r.skeleton.jointCount()) {
            rig = &r;
            break;
        }
    }
    REQUIRE(rig != nullptr);
    std::vector<glm::mat4> model;
    scene::poseToModel(rig->skeleton, rig->pose, model);
    const int joint = rig->skeleton.find("hand.r");
    REQUIRE(joint >= 0);
    // The palette slot that carries this joint, when the mesh skins to it at all.
    int slot = -1;
    for (std::size_t k = 0; k < rig->skeleton.palette.size(); ++k) {
        if (rig->skeleton.palette[k] == static_cast<std::uint32_t>(joint)) {
            slot = static_cast<int>(k);
            break;
        }
    }
    REQUIRE(slot >= 0);
    REQUIRE(rig->palette.size() > static_cast<std::size_t>(slot));
    const glm::vec3 fromModel(model[static_cast<std::size_t>(joint)][3]);
    const glm::vec3 fromPalette(rig->palette[static_cast<std::size_t>(slot)][3]);
    const float gap = glm::length(fromModel - fromPalette);
    INFO(fmt::format("hand.r model-space ({:.4f},{:.4f},{:.4f}) vs palette translation "
                     "({:.4f},{:.4f},{:.4f}): {:.4f} model units apart",
                     fromModel.x, fromModel.y, fromModel.z, fromPalette.x, fromPalette.y,
                     fromPalette.z, gap));
    CHECK(gap > 0.01f);
}

TEST_CASE("the socket agrees with the joint position computed from first principles",
          "[labs][character][sockets]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present; the socket arms need a real rig");
        return;
    }
    // The independent arm. Every check above asks `socketTransform` what it thinks; this one
    // re-derives the answer without it -- read the rig's pose, run `poseToModel` (the method
    // `tests/support/stride_speed.hpp::jointPositionAt` uses, and the only working model-space joint
    // query this repository had, test-only, before ADR-274), then place it by hand: scale by the
    // node's scale, rotate by the body's yaw, translate to the body.
    //
    // If `AnimationSink::jointTransform` read the GPU palette instead of the pose, or if
    // `socketTransform` composed in the wrong order, this would disagree -- and it would disagree by
    // a plausible-looking amount, which is exactly the failure mode that needs an arm.
    Fixture fx;
    fx.play(1.5);
    entity::Entity* scout = fx.scout();
    REQUIRE(scout != nullptr);

    const scene::Scene& sc = fx.comp->scene();
    const scene::SkinnedRig* rig = nullptr;
    for (const scene::SkinnedRig& r : sc.rigs) {
        if (r.skeleton.find("hand.r") >= 0 && r.pose.size() == r.skeleton.jointCount()) {
            rig = &r;
            break;
        }
    }
    REQUIRE(rig != nullptr);
    std::vector<glm::mat4> model;
    scene::poseToModel(rig->skeleton, rig->pose, model);
    const int joint = rig->skeleton.find("hand.r");
    REQUIRE(joint >= 0);
    const glm::vec3 local(model[static_cast<std::size_t>(joint)][3]);

    auto* scaleParam = fx.params.findAs<glm::vec3>("nodes/scout/scale");
    REQUIRE(scaleParam != nullptr);
    const float scale = scaleParam->finalComponent(0);
    const float yaw = scout->state().yaw;
    const float c = std::cos(yaw);
    const float sn = std::sin(yaw);
    const glm::vec3 scaled = local * scale;
    // +Y rotation by `yaw`, written out rather than borrowed, so this arm shares no arithmetic with
    // the code under test.
    const glm::vec3 rotated(scaled.x * c + scaled.z * sn, scaled.y, -scaled.x * sn + scaled.z * c);
    const glm::vec3 expected = scout->visualPosition() + rotated;

    scene::Transform hand;
    REQUIRE(scout->socketTransform("hand", hand) == entity::SocketResolution::Joint);
    const float gap = glm::length(hand.position - expected);
    INFO(fmt::format("socket ({:.4f},{:.4f},{:.4f}) vs re-derived ({:.4f},{:.4f},{:.4f}): {:.6f} m",
                     hand.position.x, hand.position.y, hand.position.z, expected.x, expected.y,
                     expected.z, gap));
    CHECK(gap < 1.0e-3f);

    // The control: the same re-derivation with the scale left out, which is what the engine did
    // before ADR-274, must *not* agree. An arm that passed either way would be checking nothing.
    const glm::vec3 unscaled(local.x * c + local.z * sn, local.y, -local.x * sn + local.z * c);
    const float unscaledGap = glm::length(hand.position - (scout->visualPosition() + unscaled));
    INFO(fmt::format("the scale-blind re-derivation is {:.4f} m away", unscaledGap));
    CHECK(unscaledGap > 1.0f);
}

TEST_CASE("a socket carries the node's scale", "[labs][character][sockets]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present; the socket arms need a real rig");
        return;
    }
    // The node scale was never part of `socketTransform`, and could not be seen to be missing while
    // every socket resolved to the body. A joint offset is in the asset's own units; the aliens in
    // `glowmere-valley-2` are drawn between 3.344x and 3.610x, so the error is the scale itself.
    //
    // Measured as a ratio between two scales of the same rig at the same pose, which is a
    // structural quantity: it does not depend on a clock, a load average or a frame count.
    Fixture fx;
    fx.play(1.0);
    entity::Entity* scout = fx.scout();
    REQUIRE(scout != nullptr);

    scene::Transform at361;
    REQUIRE(scout->socketTransform("hand", at361) == entity::SocketResolution::Joint);
    const glm::vec3 body = scout->visualPosition();
    const float reach361 = glm::length(at361.position - body);

    auto* scale = fx.params.findAs<glm::vec3>("nodes/scout/scale");
    REQUIRE(scale != nullptr);
    const float authored = scale->finalComponent(0);
    INFO(fmt::format("authored node scale {:.3f}", authored));
    CHECK(authored > 3.0f);
    for (std::size_t c = 0; c < 3; ++c) {
        scale->setFinalComponent(c, authored * 0.5f);
    }
    scene::Transform atHalf;
    REQUIRE(scout->socketTransform("hand", atHalf) == entity::SocketResolution::Joint);
    const float reachHalf = glm::length(atHalf.position - body);
    INFO(fmt::format("hand reach {:.4f} m at {:.3f}x, {:.4f} m at {:.3f}x", reach361, authored,
                     reachHalf, authored * 0.5f));
    CHECK(reachHalf > 0.0f);
    CHECK(std::fabs(reach361 / reachHalf - 2.0f) < 0.02f);
}

TEST_CASE("the attachment lands on the socket", "[labs][character][sockets]") {
    if (!assetsPresent()) {
        WARN("assets/aliens is not present; the socket arms need a real rig");
        return;
    }
    // The end of the chain the lab exists to check: a node declared as an attachment on a socket is
    // written by `EntityWorld::applyAttachments` into that node's transform parameters. Before
    // ADR-274 the lantern below sat at the body's origin, on every frame, and reported success.
    //
    // **One frame of lag, stated rather than hidden.** `applyAttachments` runs inside
    // `updateBehaviour`; the rigs are posed later, in `Composition::update`. So the pose an
    // attachment reads is the previous frame's -- 16.7 ms of a walk cycle. It is visible here as a
    // residual between the parameter and a socket queried after the update, and it is not fixed in
    // this wave: the fix is an ordering change inside `EntityWorld::update`, which P1 owns.
    Fixture fx;
    fx.play(2.0);
    entity::Entity* scout = fx.scout();
    REQUIRE(scout != nullptr);

    auto* lantern = fx.params.findAs<glm::vec3>("nodes/lantern/position");
    REQUIRE(lantern != nullptr);
    const glm::vec3 placed(lantern->finalComponent(0), lantern->finalComponent(1),
                           lantern->finalComponent(2));
    scene::Transform hand;
    REQUIRE(scout->socketTransform("hand", hand) == entity::SocketResolution::Joint);
    const glm::vec3 body = scout->visualPosition();

    INFO(fmt::format("lantern ({:.3f},{:.3f},{:.3f})  hand ({:.3f},{:.3f},{:.3f})  body "
                     "({:.3f},{:.3f},{:.3f})",
                     placed.x, placed.y, placed.z, hand.position.x, hand.position.y,
                     hand.position.z, body.x, body.y, body.z));
    // It is at the hand, not at the body: the whole point.
    CHECK(glm::length(placed - hand.position) < glm::length(placed - body));
    // And it is within one frame of walking of the hand. The lag is horizontal (the body moved);
    // the height is the joint's and has no lag term of its own worth allowing for.
    CHECK(horizontal(placed, hand.position) < 0.2f);
    CHECK(std::fabs(placed.y - hand.position.y) < 0.2f);
    // The control: the body's own origin is a long way from where the lantern was put.
    CHECK(glm::length(placed - body) > 1.0f);
}
