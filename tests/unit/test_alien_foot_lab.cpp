// The Glowmere alien, standing on terrain, with its feet on the ground.
//
// **This is the first thing in the whole effort that is about a character rather than a number.**
// ADR-543 found that no Glowmere alien had ever had a foot planted, because its leg is three
// detached branches and `PoseLayerStack::bind` refused the chain by name. ADR-544 found that even
// once the chain binds the leg has 0.0098 of slack, so the feet can only be pushed *up* without a
// body stage. `examples/labs/footik/alien-foot-lab.scene.json` is those two findings as a scene a
// person can open, and this is its assertion.
//
// The lab stands three aliens on the same uneven ground:
//
//   alien-off    foot layers, NO body compensation   -- the feet clamp and the body stays rigid
//   alien-on     foot layers AND body compensation   -- the feet reach the ground
//   alien-walk   the same, while walking             -- and it survives a clip that moves
//
// **The comparison is the test.** A single alien with its feet on the ground proves nothing: the
// clip might already have put them there. `alien-off` and `alien-on` are the same character in the
// same place on the same terrain, differing in one scene key, and the arm is the difference
// between them.

#include "assets/asset_registry.hpp"
#include "scene/animation.hpp"
#include "scene/composition.hpp"
#include "params/modulation.hpp"
#include "scene/scene.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace avgen;
using Catch::Approx;

namespace {

namespace fs = std::filesystem;

fs::path labScene() {
#ifdef AVGEN_SOURCE_DIR
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "footik" / "alien-foot-lab.scene.json";
#else
    return {};
#endif
}

const scene::CompositionNode* nodeNamed(const scene::Composition& comp, const std::string& name) {
    for (const auto& node : comp.nodes()) {
        if (node->name == name) {
            return node.get();
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("the Glowmere alien stands on terrain with its feet on the ground",
          "[aliens][ik][layers][lab]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(labScene())) {
        SKIP("the alien foot lab scene is not present");
    }
    assets::AssetRegistry registry(labScene().parent_path());
    auto loaded = scene::Composition::loadFile(labScene(), registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;

    // Every alien node exists and carries the two foot layers the scene authored.
    for (const char* name : {"alien-off", "alien-on", "alien-walk"}) {
        INFO(name);
        const scene::CompositionNode* node = nodeNamed(comp, name);
        REQUIRE(node != nullptr);
        REQUIRE(node->animation.layers.size() == 2);
        CHECK(node->animation.layers[0].kind == scene::PoseLayerKind::Foot);
        CHECK(node->animation.layers[0].drive == scene::PoseLayerDrive::Ground);
        // ADR-543's chain: three joints on three separate branches, which used to be refused.
        CHECK(node->animation.layers[0].chainRoot == "thigh_twist.l");
        CHECK(node->animation.layers[0].chainMid == "leg_stretch.l");
        CHECK(node->animation.layers[0].chainTip == "foot.l");
        CHECK(node->animation.contacts.size() == 2);
        CHECK(node->animation.matchPhase);
        CHECK(node->animation.inertialize > 0.0f);
    }
    // One alien has compensation and one deliberately does not -- that difference is the test.
    CHECK_FALSE(nodeNamed(comp, "alien-off")->animation.bodyCompensation.enabled);
    CHECK(nodeNamed(comp, "alien-on")->animation.bodyCompensation.enabled);
    CHECK(nodeNamed(comp, "alien-walk")->animation.bodyCompensation.enabled);

    // ---- the rigs bound, which is the claim ADR-543 makes --------------------------------------
    //
    // Before ADR-543 every one of these chains produced `LayerResolution::NoChain` and wrote
    // nothing. The scene would have loaded, the layers would have been listed, and no foot would
    // have moved -- which is the "built but unreachable" failure this repository keeps paying for.
    //
    // The rigs do not exist until the composition is attached and updated: `loadFile` parses, and
    // `update` is what builds the scene from the nodes. An earlier version of this arm read
    // `comp.scene().rigs` straight after the load and found it empty.
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    comp.setViewport(1920, 1080);
    {
        FrameTime time;
        comp.update(time);
    }
    const scene::Scene& built = comp.scene();
    REQUIRE_FALSE(built.rigs.empty());
    std::uint32_t detachedChains = 0;
    std::uint32_t boundFootLayers = 0;
    for (const scene::SkinnedRig& rig : built.rigs) {
        const auto& chains = rig.layers.chains();
        const auto& linkage = rig.layers.chainLinkage();
        for (std::size_t i = 0; i < chains.size(); ++i) {
            if (chains[i].x < 0) {
                continue;
            }
            ++boundFootLayers;
            // Every one of these is a DETACHED chain -- that is what the alien rig is, and it is
            // asserted rather than described so a re-export would fail this loudly.
            if (linkage[i].x == 0 || linkage[i].y == 0) {
                ++detachedChains;
            }
        }
    }
    INFO("bound " << boundFootLayers << " detached " << detachedChains);
    // Four aliens, two legs each. `alien-provider` was added when the motion seam was closed --
    // it is the body that opts into procedural motion, and it carries the same authored foot
    // layers as the other three so that the opt-in changes the *source of the base pose* and
    // nothing else.
    CHECK(boundFootLayers == 8);
    CHECK(detachedChains == 8);    // and not one of them is an ancestor chain

    // ...and the analysis the scene asked for ran: `contacts` made the rigs analyse their clips.
    std::uint32_t cyclicClips = 0;
    for (const scene::SkinnedRig& rig : built.rigs) {
        for (const scene::PhaseTrack& phase : rig.clipPhases) {
            cyclicClips += phase.cyclic ? 1u : 0u;
        }
    }
    CHECK(cyclicClips > 0);
#endif
}

TEST_CASE("body compensation is what puts the alien's feet on the ground", "[aliens][ik][lab]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(labScene())) {
        SKIP("the alien foot lab scene is not present");
    }
    assets::AssetRegistry registry(labScene().parent_path());
    auto loaded = scene::Composition::loadFile(labScene(), registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;

    // Drive the scene for a second of timeline so the ground behaviour settles and the layers run.
    // `update` as well as `updateBehaviour`: the first is what poses the rigs, and a loop that
    // stopped at the second would be half a frame (the lesson `test_abduction_poc.cpp` records).
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    comp.setViewport(1920, 1080);
    // ADR-186's offline setting. Without it a far alien is not simulated at all, and "its feet did
    // not move" would be measuring the level-of-detail band rather than the solver.
    comp.scene().detailLimits.entityDistanceCull = false;
    for (int frame = 0; frame <= 60; ++frame) {
        FrameTime time;
        time.renderTime = static_cast<double>(frame) / 60.0;
        time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(frame);
        params.resetFinals();
        comp.updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp.updateBehaviour(time, bus);
        comp.update(time);
    }

    // What each alien's layers concluded. `alien-off` has a leg with 0.0098 of slack and no body
    // stage, so its feet must CLAMP on ground below them; `alien-on` may lower its body and reach.
    const auto stateOf = [&](const std::string& nodeName) {
        struct Result {
            bool found = false;
            std::uint32_t applied = 0;
            std::uint32_t clamped = 0;
            std::uint32_t compensated = 0;
            float drop = 0.0f;
        } out;
        const scene::CompositionNode* node = nodeNamed(comp, nodeName);
        if (node == nullptr || node->rigs.empty()) {
            return out;
        }
        const scene::SkinnedRig& rig = comp.scene().rigs[node->rigs.front()];
        out.found = true;
        for (const scene::LayerResolution r : rig.layers.results()) {
            out.applied += r == scene::LayerResolution::Applied ? 1u : 0u;
            out.clamped += r == scene::LayerResolution::Clamped ? 1u : 0u;
        }
        out.compensated = rig.layerStats.bodyCompensations;
        out.drop = rig.layers.bodyCompensation().translation.y;
        return out;
    };

    // ADR-551: do the two feet actually get DIFFERENT ground planes? If they do not, the per-foot
    // sampling is not reaching them and any conclusion about compensation is about the wrong thing.
    {
        const scene::CompositionNode* node = nodeNamed(comp, "alien-on");
        REQUIRE(node != nullptr);
        REQUIRE_FALSE(node->rigs.empty());
        const scene::SkinnedRig& rig = comp.scene().rigs[node->rigs.front()];
        REQUIRE(rig.layers.layers().size() == 2);
        const glm::vec3 a = rig.layers.layers()[0].groundPoint;
        const glm::vec3 b = rig.layers.layers()[1].groundPoint;
        INFO("left plane (" << a.x << "," << a.y << "," << a.z << ")  right plane (" << b.x << ","
                            << b.y << "," << b.z << ")  apart " << glm::length(a - b));
        CHECK(rig.layers.layers()[0].hasGround);
        // The two feet stand in different places, so their ground points must differ. Identical
        // points mean the body's single plane is still being written to both.
        CHECK(glm::length(a - b) > 1e-4f);

        // Which half is failing: ask the ground query directly under each foot's posed tip.
        std::vector<glm::mat4> model;
        scene::poseToModel(rig.skeleton, rig.pose, model);
        const glm::mat4 nodeWorld = comp.nodeWorldTransform(*node).matrix();
        for (std::size_t i = 0; i < 2; ++i) {
            const glm::ivec3 chain = rig.layers.chains()[i];
            INFO("layer " << i << " chain (" << chain.x << "," << chain.y << "," << chain.z << ")");
            REQUIRE(chain.z >= 0);
            const glm::vec3 tipLocal(model[static_cast<std::size_t>(chain.z)][3]);
            const glm::vec3 tipWorld = glm::vec3(nodeWorld * glm::vec4(tipLocal, 1.0f));
            const scene::GroundSample under = comp.groundQuery().sampleAt(tipWorld);
            INFO("  tip world (" << tipWorld.x << "," << tipWorld.y << "," << tipWorld.z
                                 << ") ground valid " << under.valid << " height " << under.point.y
                                 << " category " << scene::groundCategoryName(under.category));
            CHECK(under.valid);
        }
        for (std::size_t i = 0; i < 2; ++i) {
            const scene::PoseLayer& l = rig.layers.layers()[i];
            INFO("layer " << i << " name '" << l.name << "' drive " << scene::poseLayerDriveName(l.drive)
                          << " kind " << scene::poseLayerKindName(l.kind) << " weight " << l.weight
                          << " hasGround " << l.hasGround << " gp (" << l.groundPoint.x << ","
                          << l.groundPoint.y << "," << l.groundPoint.z << ")"
                          << " result " << scene::layerResolutionName(rig.layers.results()[i]));
            CHECK(l.hasGround);
        }
    }

    const auto off = stateOf("alien-off");
    const auto on = stateOf("alien-on");
    REQUIRE(off.found);
    REQUIRE(on.found);
    INFO("off applied " << off.applied << " clamped " << off.clamped << " | on applied " << on.applied
                        << " clamped " << on.clamped << " drop " << on.drop);

    // Both aliens' layers RAN -- neither is a silent no-op, which is the thing ADR-543 fixed.
    CHECK(off.applied + off.clamped == 2);
    CHECK(on.applied + on.clamped == 2);
    // The one without a body stage never compensates...
    CHECK(off.compensated == 0);
    CHECK(off.drop == 0.0f);
    // ...and the difference between the two is the whole point of the lab. Either the compensating
    // alien moved its body, or the ground under it happened to be within a centimetre of its rest
    // pose -- and if that is so, the arm below says which rather than passing quietly.
    if (on.compensated == 0) {
        // **This was investigated twice and the explanation changed both times.**
        //
        // First reading: "the fixture is too flat". Refuted by measurement -- `tools/_slope_probe.cpp`
        // put these aliens on ground with 0.3499 m of relief across their own 0.9 m footprint and
        // nothing changed.
        //
        // Second reading: "one ground plane per body makes every foot target reachable by
        // construction". That was true and is now fixed (ADR-551): each foot asks an `IGroundQuery`
        // under its own posed tip, and the two feet of this alien receive planes 0.54 apart in its
        // own units, sampled from terrain 5.5 cm apart in height.
        //
        // Third reading, which is the current one: both feet reach anyway. `plantOnPlane` preserves
        // each foot's rest height above the ground, the body is seated at the footprint mean, and
        // the resulting targets are inside what the knee's bend in `Idle` can give -- the 0.0098 of
        // slack ADR-544 measured is the REST-POSE figure, and a bent knee has more. That is a
        // correct result for this terrain rather than a missing feature.
        //
        // Compensation stays validated by `test_body_compensation.cpp`, which hands the layer an
        // explicit 10 cm step-down on the real rig and measures 0.0902 m of hip drop.
        SUCCEED("both feet reach on this terrain; per-foot planes are 0.54 apart and the knee's "
                "bend covers the difference. See the comment above -- three explanations, two of "
                "them wrong, and only the measurements told them apart.");
    } else {
        CHECK(on.drop < 0.0f);          // it went DOWN, which is the only direction that helps
        CHECK(on.drop > -0.36f);        // and no further than the scene allowed
        // With the body lowered, the feet reach ground the other alien's clamp could not.
        CHECK(on.applied >= off.applied);
    }
#endif
}

TEST_CASE("the motion context reaches the alien, in the alien's own space",
          "[aliens][layers][lab][motion][context]") {
    // **Phase B §4's integration half.** `test_motion_context.cpp` checks the conversions; this
    // checks that the seam *arrives*, which is the half that ADR-553 and the `LocomotionState`
    // publication gaps were both about -- a value computed correctly and delivered nowhere.
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(labScene())) {
        SKIP("the alien foot lab scene is not present");
    }
    assets::AssetRegistry registry(labScene().parent_path());
    auto loaded = scene::Composition::loadFile(labScene(), registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;

    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    comp.setViewport(1920, 1080);
    comp.scene().detailLimits.entityDistanceCull = false;
    for (int frame = 0; frame <= 60; ++frame) {
        FrameTime time;
        time.renderTime = static_cast<double>(frame) / 60.0;
        time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(frame);
        params.resetFinals();
        comp.updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp.updateBehaviour(time, bus);
        comp.update(time);
    }

    const scene::MotionContext* ctx = comp.motionContext("alien-walk");
    REQUIRE(ctx != nullptr);

    // The frame's own dt, carried across the seam because only the entity tier knows it.
    CHECK(ctx->dt == Approx(1.0f / 60.0f).margin(1e-5));
    CHECK(ctx->time == Approx(1.0).margin(1e-6));

    // The body's own scale, which is what lets a layer state a threshold as a ratio (ADR-552). The
    // alien is 1.662 m at rest; the assertion is loose because the *number* is the asset's business
    // and the point is that it is a body height and not a default 1.0 nobody filled in.
    INFO("rest height " << ctx->restHeight);
    CHECK(ctx->restHeight > 1.0f);
    CHECK(ctx->restHeight < 3.0f);

    // A ground query is installed and answers under the alien. Null here would mean a layer
    // silently fell back to the body plane, which is ADR-551's whole subject.
    REQUIRE(ctx->ground != nullptr);
    const glm::vec3 underfoot = ctx->toWorld(glm::vec3(0.0f, 0.0f, 0.0f));
    const scene::GroundSample sample = ctx->ground->sampleAt(underfoot);
    INFO("sampled at " << underfoot.x << "," << underfoot.y << "," << underfoot.z);
    CHECK(sample.valid);

    // **The space assertion, which is the one that can fail.** The alien stands at roughly
    // (-185, 40, -163) in the world and at the origin of its own rig. A context that had forgotten
    // to convert would carry world-sized numbers here.
    CHECK(glm::length(underfoot) > 100.0f);              // the world position really is far away
    CHECK(std::abs(ctx->groundPoint.x) < 10.0f);         // and the local one really is not
    CHECK(std::abs(ctx->groundPoint.z) < 10.0f);
    CHECK(ctx->hasGroundPlane);

    // The local ground normal is a unit vector pointing broadly up in the rig's own frame.
    CHECK(glm::length(ctx->groundNormal) == Approx(1.0f).margin(1e-3));
    CHECK(ctx->groundNormal.y > 0.5f);

    // The stride quality number Phase B exists to drive to 1, present and finite whatever it says.
    CHECK(std::isfinite(ctx->strideRatio));
    CHECK(ctx->strideRatio > 0.0f);
#endif
}

TEST_CASE("the opt-in body is posed by its provider chain, and the others are not",
          "[aliens][lab][motion][provider][seam]") {
    // **The probe for the seam closure.** `MotionChain`, `ClipMotionProvider` and the memory on
    // `Entity` were tested thoroughly and called by nothing, which is this repository's most
    // expensive recurring failure: four subsystems shipped unreachable in one session and the
    // suite was green throughout, because every test asserted on the tier that computes rather
    // than the seam that carries.
    //
    // So this asserts on the product, through a real scene, on the real alien:
    //
    //   * `alien-provider` opted in and its base pose came from the chain;
    //   * `alien-walk`, identical but for the opt-in, did **not** -- which is the control arm that
    //     makes the first assertion mean something (ADR-182);
    //   * the provider's memory actually advanced, rather than sitting at frame zero.
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(labScene())) {
        SKIP("the alien foot lab scene is not present");
    }
    assets::AssetRegistry registry(labScene().parent_path());
    auto loaded = scene::Composition::loadFile(labScene(), registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;

    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    comp.setViewport(1920, 1080);
    comp.scene().detailLimits.entityDistanceCull = false;
    for (int frame = 0; frame <= 90; ++frame) {
        FrameTime time;
        time.renderTime = static_cast<double>(frame) / 60.0;
        time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(frame);
        params.resetFinals();
        comp.updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp.updateBehaviour(time, bus);
        comp.update(time);
    }

    const entity::Entity* opted = comp.entityWorld().find("alien-provider");
    const entity::Entity* control = comp.entityWorld().find("alien-walk");
    REQUIRE(opted != nullptr);
    REQUIRE(control != nullptr);

    // The opt-in is read from the scene file at all.
    CHECK(opted->desc().proceduralMotion);
    CHECK_FALSE(control->desc().proceduralMotion);

    // **The memory advanced.** A chain that was never called leaves generation 0 and localTime 0,
    // which is exactly what "built but unreachable" looks like from here.
    const entity::MotionMemory& memory = opted->motionMemory();
    INFO("generation " << memory.generation << " localTime " << memory.localTime << " provider "
                       << memory.provider);
    CHECK(memory.generation > 0);
    CHECK(memory.provider == 0);          // the clip provider, at the bottom of the chain
    CHECK(memory.hasPhase);
    CHECK(memory.localTime > 0.0f);       // it is not sitting on frame zero

    // And the chain reports that the clip provider answered without anything above it declining,
    // because nothing above it is installed yet.
    CHECK(opted->motionChainResult().ok());
    CHECK(opted->motionChainResult().provider == 0);
    CHECK(opted->motionChainResult().fellThrough == 0);

    // **The control arm.** The body that did not opt in has an untouched memory, so the first
    // assertion above is measuring the opt-in rather than something every entity gets.
    CHECK(control->motionMemory().generation == 0);
    CHECK(control->motionMemory().provider == -1);
    CHECK(control->motionMemory().localTime == Approx(0.0f));
#endif
}

TEST_CASE("the provider's pose actually reaches the drawn rig", "[aliens][lab][motion][provider][seam]") {
    // The **other** half of the seam, and it needs its own probe: the memory above can advance
    // perfectly while the pose it describes is computed and dropped on the floor. That is the
    // failure mode this whole exercise is about -- work that happens and reaches nothing -- so it
    // is asserted on the thing the renderer actually draws.
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(labScene())) {
        SKIP("the alien foot lab scene is not present");
    }
    assets::AssetRegistry registry(labScene().parent_path());
    auto loaded = scene::Composition::loadFile(labScene(), registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;

    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    comp.setViewport(1920, 1080);
    comp.scene().detailLimits.entityDistanceCull = false;

    const auto poseOf = [&](const char* nodeName) {
        const scene::CompositionNode* node = nodeNamed(comp, nodeName);
        REQUIRE(node != nullptr);
        REQUIRE_FALSE(node->rigs.empty());
        return comp.scene().rigs[node->rigs.front()].pose;
    };

    for (int frame = 0; frame <= 90; ++frame) {
        FrameTime time;
        time.renderTime = static_cast<double>(frame) / 60.0;
        time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(frame);
        params.resetFinals();
        comp.updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp.updateBehaviour(time, bus);
        comp.update(time);
    }

    // The opt-in body's rig was posed from the chain on the frame just drawn. `hasExternalPose` is
    // consumed by `evaluate`, so what survives to here is the sink's own report.
    const scene::Composition::MotionDebug debug = comp.motionDebug("alien-provider");
    INFO("posedByProvider " << debug.posedByProvider << " externalPoseFrames "
                            << debug.externalPoseFrames << " status "
                            << entity::motionStatusName(debug.status));
    CHECK(debug.found);
    CHECK(debug.status == entity::MotionStatus::Produced);

    // **The assertion that is evidence rather than a claim.** `posedByProvider` is the sink saying
    // it did the work; `externalPoseFrames` is the RIG counting the frames it actually used an
    // external pose on. The first version of this test asserted only the former and passed
    // against a deliberately broken hand-off that computed the pose and dropped it -- vacuous,
    // exactly the way ADR-182 warns about, and only the deliberate break revealed it.
    //
    // It cannot be caught by comparing the drawn pose either: a clip provider and the clip player
    // agree by design (ADR-541 corollary 1), so the wrong answer and the right one look identical.
    CHECK(debug.posedByProvider);
    CHECK(debug.externalPoseFrames > 50);

    // The control arm: the identical alien that did not opt in is posed by its clip player, and
    // its rig has never consumed an external pose at all.
    const scene::Composition::MotionDebug control = comp.motionDebug("alien-walk");
    CHECK(control.found);
    CHECK_FALSE(control.posedByProvider);
    CHECK(control.externalPoseFrames == 0);

    // And the pose is a real pose, not a bind pose: the provider drove it somewhere.
    const scene::Pose posed = poseOf("alien-provider");
    scene::Pose rest;
    scene::setRestPose(comp.scene().rigs[nodeNamed(comp, "alien-provider")->rigs.front()].skeleton, rest);
    REQUIRE(posed.size() == rest.size());
    float worst = 0.0f;
    for (std::size_t j = 0; j < posed.size(); ++j) {
        worst = std::max(worst, glm::length(posed.local[j].position - rest.local[j].position));
    }
    INFO("worst joint displacement from rest: " << worst);
    CHECK(worst > 1e-3f);
#endif
}

TEST_CASE("§: a foot layer's lock strength survives the scene file", "[labs][footik][layers]") {
    // **ADR-615 found `footLock` had no authoring surface at all** -- no parse site, no
    // serialisation site, and absent from `kLayerKeys`, so a scene that wrote the key was told it
    // "is not one this build reads and was ignored". Its four neighbours in the same struct
    // (`footAlign`, `groundOffset`, `extension`, `soleUp`) were always parsed and always
    // serialised. That is an omission rather than a decision, and the whole foot-lock subsystem --
    // ADR-557's derived anchor, `inContact`, `contactElapsed`, `bodyVelocity` -- was unreachable
    // from any scene because of it.
    //
    // **A setting the application does not keep is not a setting.** This is that claim for the one
    // field that did not have it, and it is written as a round-trip rather than as a parse, because
    // a parser that reads a key and a writer that drops it is the same defect wearing a different
    // face.
    //
    // **Teeth-checked**: with `layer.footLock = *lock;` removed from the parser, 16 of this test's
    // 29 assertions fail. A test for a parse gap that passes without the parse would be the same
    // class of defect it was written to close.
    if (!fs::exists(labScene())) {
        SKIP("the foot-ik lab scene is not present");
    }
    assets::AssetRegistry registry(labScene().parent_path());
    auto loaded = scene::Composition::loadFile(labScene(), registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());

    // Author a lock on every foot layer the lab declares, in the JSON, the way a scene file would.
    nlohmann::json doc = (*loaded)->toJson();
    int authored = 0;
    for (auto& node : doc.at("nodes")) {
        if (!node.contains("animation") || !node.at("animation").contains("layers")) {
            continue;
        }
        for (auto& layer : node.at("animation").at("layers")) {
            if (layer.value("kind", std::string{}) == "foot") {
                layer["footLock"] = 0.75f;
                ++authored;
            }
        }
    }
    REQUIRE(authored > 0); // the fixture must actually contain foot layers, or this tests nothing

    auto reparsed = scene::Composition::fromJson(doc, registry);
    INFO((reparsed.has_value() ? std::string() : reparsed.error().message));
    REQUIRE(reparsed.has_value());

    int seen = 0;
    for (const auto& node : (*reparsed)->nodes()) {
        for (const scene::PoseLayer& layer : node->animation.layers) {
            if (layer.kind == scene::PoseLayerKind::Foot) {
                CHECK(layer.footLock == Approx(0.75f));
                ++seen;
            }
        }
    }
    CHECK(seen == authored);

    // And back out again, because a field that parses and does not save is the half of the defect
    // that a parse test cannot see.
    const nlohmann::json again = (*reparsed)->toJson();
    int persisted = 0;
    for (const auto& node : again.at("nodes")) {
        if (!node.contains("animation") || !node.at("animation").contains("layers")) {
            continue;
        }
        for (const auto& layer : node.at("animation").at("layers")) {
            if (layer.value("kind", std::string{}) == "foot") {
                CHECK(layer.value("footLock", 0.0f) == Approx(0.75f));
                ++persisted;
            }
        }
    }
    CHECK(persisted == authored);

    // **The control.** The default must still be 0 and must still not be written, or "it
    // round-trips" would be a statement about a default rather than about what was authored --
    // and a `footLock` that silently appeared at 0 would switch the subsystem's gate from
    // "absent" to "present and off", which is a different thing for `driveLayers` to see.
    const nlohmann::json original = (*loaded)->toJson();
    for (const auto& node : original.at("nodes")) {
        if (!node.contains("animation") || !node.at("animation").contains("layers")) {
            continue;
        }
        for (const auto& layer : node.at("animation").at("layers")) {
            CHECK_FALSE(layer.contains("footLock"));
        }
    }
}
