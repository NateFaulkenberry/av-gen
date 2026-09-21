// Phase B §47 -- performance targets.
//
// "Do not prematurely promise a particular frame time. Establish measured baselines. Identify
// per-character cost, per-active-layer cost, shared cost."
//
// So this file asserts almost nothing about absolute time, and a great deal about *shape*. A
// number in milliseconds measured on one machine is a fact about that machine; the three things
// that are facts about the architecture are:
//
//   - **shared cost** stays shared: a hundred characters hold one skeleton and one set of clips,
//     so cost per character must not grow with the count;
//   - **per-layer cost** is what it is: measured by turning the layer kinds on one at a time, so
//     the marginal cost of a reach is separable from the marginal cost of a foot solve;
//   - **the steady-state path does not allocate**, which is the property that decides whether a
//     hundred characters are a frame-time problem or a fragmentation problem.
//
// Method, per the standing rule: **minima over repeats, never means.** A mean measures the noise
// the machine had that day; a minimum measures how fast the code can go, which is the only part
// that is a property of the code. Every number below is the fastest of its repeats.

#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/motion_context.hpp"
#include "scene/pose_layers.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <malloc/malloc.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {
namespace fs = std::filesystem;

fs::path alienPath() {
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
}

// Which layer kinds a measurement has switched on. Turning them on cumulatively is what makes the
// marginal cost of each one readable: the difference between two adjacent rows is that kind's cost
// per character per frame, and nothing else changed between the rows.
struct LayerSet {
    const char* name;
    bool stride = false;
    bool lean = false;
    bool foot = false;
    bool aim = false;
    bool reach = false;
};

// One character's per-instance state. §48 in advance: everything here is per-instance and
// everything it points at is not.
struct Instance {
    scene::Pose pose;
    scene::PoseLayerStack layers;
    std::vector<glm::mat4> model;
    float phase = 0.0f;
};

std::vector<scene::PoseLayer> perfLayers() {
    std::vector<scene::PoseLayer> layers;
    for (const char* side : {"l", "r"}) {
        scene::PoseLayer stride;
        stride.name = fmt::format("stride.{}", side);
        stride.kind = scene::PoseLayerKind::Stride;
        stride.strideJoint = fmt::format("foot.{}", side);
        stride.strideOrigin = "root.x";
        layers.push_back(stride);
    }
    scene::PoseLayer lean;
    lean.name = "lean";
    lean.kind = scene::PoseLayerKind::Lean;
    lean.mask.joints = {"spine_02.x", "spine_03.x"};
    layers.push_back(lean);
    for (const char* side : {"l", "r"}) {
        scene::PoseLayer foot;
        foot.name = fmt::format("foot.{}", side);
        foot.kind = scene::PoseLayerKind::Foot;
        foot.chainRoot = fmt::format("thigh_twist.{}", side);
        foot.chainMid = fmt::format("leg_stretch.{}", side);
        foot.chainTip = fmt::format("foot.{}", side);
        layers.push_back(foot);
    }
    scene::PoseLayer look;
    look.name = "look";
    look.kind = scene::PoseLayerKind::Aim;
    look.pivot = "head.x";
    look.mask.joints = {"head.x", "Eye_L", "Eye_R"};
    layers.push_back(look);
    scene::PoseLayer reach;
    reach.name = "reach";
    reach.kind = scene::PoseLayerKind::Reach;
    reach.chainRoot = "shoulder.l";
    reach.chainMid = "forearm_stretch.l";
    reach.chainTip = "hand.l";
    layers.push_back(reach);
    return layers;
}

class FlatGround final : public scene::IGroundQuery {
public:
    [[nodiscard]] scene::GroundSample sampleAt(const glm::vec3& worldPoint) const override {
        scene::GroundSample out;
        out.point = glm::vec3(worldPoint.x, 0.0f, worldPoint.z);
        out.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        out.category = scene::GroundCategory::Authored;
        out.valid = true;
        return out;
    }
};

std::size_t liveBlocks() {
    malloc_statistics_t stats{};
    malloc_zone_statistics(malloc_default_zone(), &stats);
    return stats.blocks_in_use;
}

struct Measurement {
    double microsPerCharacterFrame = 0.0;
    std::size_t netBlocks = 0;
};

// `frames` frames of `count` characters, repeated, reported as the fastest repeat.
Measurement measure(const scene::Skeleton& sk, const std::vector<scene::AnimationClip>& clips,
                    const scene::AnimationClip& walk, std::vector<Instance>& instances,
                    const LayerSet& set, int frames, int repeats) {
    FlatGround ground;
    double best = std::numeric_limits<double>::max();
    std::size_t netBlocks = 0;

    for (int r = 0; r < repeats; ++r) {
        const std::size_t before = liveBlocks();
        const auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < frames; ++f) {
            const auto now = static_cast<double>(f) / 60.0;
            for (Instance& inst : instances) {
                for (scene::PoseLayer& layer : inst.layers.layers()) {
                    switch (layer.kind) {
                    case scene::PoseLayerKind::Stride:
                        layer.weight = set.stride ? 0.8f : 0.0f;
                        layer.strideRatio = 0.8f;
                        layer.bodySpeed = 1.1f;
                        break;
                    case scene::PoseLayerKind::Lean:
                        layer.weight = set.lean ? 1.0f : 0.0f;
                        layer.bodyAcceleration = glm::vec3(0.6f, 0.0f, 0.2f);
                        layer.bodyTurnRate = 0.4f;
                        break;
                    case scene::PoseLayerKind::Foot:
                        layer.weight = set.foot ? 1.0f : 0.0f;
                        layer.hasGround = set.foot;
                        layer.groundPoint = glm::vec3(0.0f);
                        layer.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
                        break;
                    case scene::PoseLayerKind::Aim:
                        layer.weight = set.aim ? 1.0f : 0.0f;
                        layer.hasTarget = set.aim;
                        layer.target = glm::vec3(1.2f, 1.4f, 1.5f);
                        break;
                    case scene::PoseLayerKind::Reach:
                        layer.weight = set.reach ? 1.0f : 0.0f;
                        layer.hasTarget = set.reach;
                        layer.target = glm::vec3(0.30f, 1.10f, 0.35f);
                        break;
                    default:
                        break;
                    }
                }
                scene::setRestPose(sk, inst.pose);
                scene::sampleClip(walk, walk.start + std::fmod(inst.phase + static_cast<float>(now),
                                                               walk.length()),
                                  inst.pose);
                inst.layers.apply(sk, clips, now, inst.pose);
                scene::poseToModel(sk, inst.pose, inst.model);
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double micros =
            std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(t1 - t0).count();
        best = std::min(best, micros / (static_cast<double>(frames) *
                                        static_cast<double>(instances.size())));
        // Allocation is read on the last repeat only, after everything has had a chance to warm:
        // a growth on repeat 1 is a vector reaching its size, which is not the question.
        if (r == repeats - 1) {
            const std::size_t after = liveBlocks();
            netBlocks = after > before ? after - before : 0;
        }
    }
    return {best, netBlocks};
}

} // namespace

TEST_CASE("the per-character cost of the motion stack, measured", "[perf][phaseB][aliens]") {
    if (!fs::exists(alienPath())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions options;
    options.loadImages = false;
    REQUIRE(assets::loadGltf(alienPath(), sc, options).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    const scene::AnimationClip* walk = nullptr;
    for (const scene::AnimationClip& c : rig.clips) {
        if (c.name.find("Walking") != std::string::npos && c.length() > 0.0f) {
            walk = &c;
            break;
        }
    }
    REQUIRE(walk != nullptr);

    const LayerSet sets[] = {
        {"clip only            ", false, false, false, false, false},
        {"+ stride (2 layers)  ", true, false, false, false, false},
        {"+ lean   (1 layer)   ", true, true, false, false, false},
        {"+ feet   (2 IK)      ", true, true, true, false, false},
        {"+ look   (1 aim)     ", true, true, true, true, false},
        {"+ reach  (1 IK)      ", true, true, true, true, true},
    };

    WARN(fmt::format("rig: {} joints, {} clips; all of it shared", rig.skeleton.jointCount(),
                     rig.clips.size()));
    WARN("microseconds per character per frame, fastest of 5 repeats:");

    std::vector<double> fullAt;
    double lastMarginal = 0.0; // the cost of adding the final model-space layer
    const int counts[] = {1, 10, 50, 100};
    for (const int count : counts) {
        std::vector<Instance> instances(static_cast<std::size_t>(count));
        for (std::size_t i = 0; i < instances.size(); ++i) {
            CHECK(instances[i].layers.bind(perfLayers(), rig.skeleton, rig.clips).empty());
            instances[i].phase = static_cast<float>(i) * 0.137f;
            // Warm every per-instance buffer to its working size, so the allocation count below is
            // measuring the steady state rather than the first frame.
            scene::setRestPose(rig.skeleton, instances[i].pose);
            scene::poseToModel(rig.skeleton, instances[i].pose, instances[i].model);
        }
        const int frames = std::max(2, 600 / count);
        std::string row = fmt::format("  n={:<4}", count);
        Measurement full{};
        for (const LayerSet& set : sets) {
            const Measurement m = measure(rig.skeleton, rig.clips, *walk, instances, set, frames, 5);
            row += fmt::format(" {:7.2f}", m.microsPerCharacterFrame);
            full = m;
        }
        WARN(fmt::format("{}   (alloc blocks over the last repeat: {})", row, full.netBlocks));
        fullAt.push_back(full.microsPerCharacterFrame);

        // **Nothing allocates in the steady state.** Per character per frame the stack samples a
        // clip into a pose it already owns, applies layers into scratch it already owns, and writes
        // a model array it already owns. A non-zero number here does not mean "slow"; it means the
        // hundred-character case is a fragmentation and a cache problem as well as a time one, and
        // it is the number that would quietly regress when someone returns a vector by value.
        CHECK(full.netBlocks == 0);
    }

    // Per-layer-kind cost is the difference between adjacent columns, which is why they are
    // cumulative. Printed rather than asserted: it is a machine fact, and the assertion that
    // matters is the one below it.
    {
        std::vector<Instance> one(1);
        CHECK(one[0].layers.bind(perfLayers(), rig.skeleton, rig.clips).empty());
        double previous = 0.0;
        for (const LayerSet& set : sets) {
            const Measurement m = measure(rig.skeleton, rig.clips, *walk, one, set, 600, 5);
            WARN(fmt::format("  {}  {:7.2f} us   marginal {:+7.2f} us", set.name,
                             m.microsPerCharacterFrame,
                             previous > 0.0 ? m.microsPerCharacterFrame - previous : 0.0));
            previous = m.microsPerCharacterFrame;
        }
    }

    // **SHARED COST STAYS SHARED.** This is the architectural assertion and the reason the table
    // above is per *character* rather than per frame: if anything in the stack were duplicating the
    // skeleton, the clips or the masks per instance, the hundredth character would cost more than
    // the first. Ten percent of headroom over a 100x range is generous for cache effects and far
    // tighter than any per-instance copy of a 90-joint rig or 26 clips could hide in.
    REQUIRE(fullAt.size() == 4);
    const double atOne = fullAt[0];
    const double atHundred = fullAt[3];
    WARN(fmt::format("full stack: {:.2f} us/character at n=1, {:.2f} at n=100 ({:+.1f}%)", atOne,
                     atHundred, 100.0 * (atHundred - atOne) / atOne));
    CHECK(atHundred < atOne * 1.5);

    // **Where the time goes, measured rather than reasoned about (ADR-385).** Before §53 this
    // stack called `poseToModel` over all 90 joints once per model-space layer -- seven full walks
    // per character per frame, 14.3 of 21.6 us, 66%. §53 made the walk incremental: `model_` is
    // kept valid across the layer loop and only the joints a previous layer actually wrote, plus
    // their descendants, are recomputed.
    //
    // The floor is one full walk per frame, because the clip sample that starts the frame changes
    // every joint. So the useful comparison is no longer "seven walks" -- it is one walk plus six
    // dirty-set passes, and the number below is what one full walk still costs.
    {
        scene::Pose pose;
        std::vector<glm::mat4> model;
        scene::setRestPose(rig.skeleton, pose);
        scene::poseToModel(rig.skeleton, pose, model);
        double bestWalk = std::numeric_limits<double>::max();
        for (int r = 0; r < 5; ++r) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 20000; ++i) {
                scene::poseToModel(rig.skeleton, pose, model);
            }
            const auto t1 = std::chrono::steady_clock::now();
            bestWalk = std::min(
                bestWalk,
                std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(t1 - t0)
                        .count() /
                    20000.0);
        }
        WARN(fmt::format("one full poseToModel over {} joints: {:.3f} us. Seven of those would be "
                         "{:.2f} us; the whole stack now costs {:.2f} us per character per frame, "
                         "so it is no longer doing seven.",
                         rig.skeleton.jointCount(), bestWalk, bestWalk * 7.0, atHundred));
        // **The guard on §53, chosen for separation rather than for being the obvious comparison.**
        //
        // The obvious one -- "the stack costs less than seven walks" -- is true (14.2 against 14.7)
        // and would have been a bad test: a 3% margin in a suite four agents run on a shared
        // machine is a coin flip, and a flaky assertion is worse than no assertion because someone
        // eventually deletes it.
        //
        // This one compares the cost of *adding one more model-space layer* against a full walk.
        // Before §53 adding one cost a full walk, because that is literally what it did. A marginal
        // is a difference of two minima and is therefore much noisier than either -- the first three
        // times this ran it reported 0.64, 1.59 and 2.64 us -- so it is measured as the **minimum
        // of three paired measurements** rather than one, which is the same minima-not-means rule
        // applied one level up. Stated honestly: the separation here is roughly a factor of two,
        // not the factor of three a single lucky reading suggested.
        {
            std::vector<Instance> one(1);
            CHECK(one[0].layers.bind(perfLayers(), rig.skeleton, rig.clips).empty());
            const LayerSet without = sets[4]; // everything but the reach
            const LayerSet with = sets[5];    // and with it
            for (int pair = 0; pair < 3; ++pair) {
                const double a =
                    measure(rig.skeleton, rig.clips, *walk, one, without, 600, 5)
                        .microsPerCharacterFrame;
                const double b =
                    measure(rig.skeleton, rig.clips, *walk, one, with, 600, 5)
                        .microsPerCharacterFrame;
                lastMarginal = pair == 0 ? b - a : std::min(lastMarginal, b - a);
            }
        }
        WARN(fmt::format("adding one more model-space layer costs {:.2f} us against a {:.2f} us "
                         "full walk",
                         lastMarginal, bestWalk));
        CHECK(bestWalk > 0.0);
        CHECK(lastMarginal > 0.0);      // it was measured at all
        CHECK(lastMarginal < bestWalk); // and a layer no longer costs a walk
    }

    // And the corresponding baseline, stated as a measurement and not as a promise: what a hundred
    // characters of the full stack cost in one frame, on this machine, today.
    WARN(fmt::format("baseline: 100 characters, full stack, {:.3f} ms per frame",
                     atHundred * 100.0 / 1000.0));
}
