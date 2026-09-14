#include "scene/tree_generator.hpp"
#include "scene/tree_mesh.hpp"
#include "scene/tree_rig.hpp"
#include "scene/tree_scene.hpp"
#include "search/candidate_search.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::scene;
using Catch::Matchers::WithinAbs;

namespace {

TreeParams rigParams(std::uint32_t seed = 5) {
    TreeParams p;
    p.seed = seed;
    p.internodeLength = 0.78f;
    p.markerCount = 9000;
    p.iterations = 30;
    p.tipRadius = 0.050f;
    return p;
}

struct Built {
    TreeGraph graph;
    TreeMeshes meshes;
    TreeRig rig;
};

Built build(std::uint32_t seed = 5) {
    Built out;
    auto graph = generateTree(rigParams(seed));
    REQUIRE(graph.has_value());
    out.graph = std::move(*graph);
    auto meshes = buildTreeMeshes(out.graph, TreeMeshSettings{});
    REQUIRE(meshes.has_value());
    out.meshes = std::move(*meshes);
    auto rig = buildTreeRig(out.graph, TreeRigSettings{});
    REQUIRE(rig.has_value());
    out.rig = std::move(*rig);
    return out;
}

} // namespace

TEST_CASE("the branch rig fits the palette and validates", "[tree][rig]") {
    Built built = build();
    INFO(fmt::format("{} joints over {} axes", built.rig.size(), built.graph.axes.size()));
    CHECK(built.rig.size() > 8);
    CHECK(built.rig.size() <= kMaxPaletteJoints);
    CHECK(built.rig.skeleton.valid());
    // The Skeleton contract: parents strictly precede children. It is also what makes poseToModel a
    // single forward pass, so it is worth asserting here and not only inside valid().
    for (std::size_t i = 0; i < built.rig.skeleton.joints.size(); ++i) {
        const int parent = built.rig.skeleton.joints[i].parent;
        CHECK(parent < static_cast<int>(i));
    }
    // The trunk must be rigged. Everything else hangs off it.
    CHECK(built.rig.jointAxis[0] == 0);
}

TEST_CASE("every skinned vertex is bound to real joints with unit weight", "[tree][rig]") {
    Built built = build();
    REQUIRE(skinTreeMeshes(built.graph, built.rig, built.meshes).has_value());
    std::size_t checked = 0;
    for (const auto& [role, mesh] : built.meshes.parts()) {
        if (mesh->vertices.empty()) {
            continue;
        }
        INFO("part " << role);
        REQUIRE(mesh->skinned());
        for (const SkinInfluence& influence : mesh->skin) {
            for (std::uint32_t k = 0; k < kJointInfluences; ++k) {
                REQUIRE(influence.joints[k] < built.rig.size());
            }
            const float sum = influence.weights.x + influence.weights.y + influence.weights.z + influence.weights.w;
            REQUIRE_THAT(static_cast<double>(sum), WithinAbs(1.0, 1e-4));
            ++checked;
        }
    }
    INFO(checked << " vertices bound");
    CHECK(checked > 1000);
}

TEST_CASE("a parent joint carries its descendants", "[tree][rig][animation]") {
    // Section 40 of the brief, as an assertion: this is transform INHERITANCE, not an amplitude
    // ladder that resembles it. Rotating one joint must move every joint below it in the chain and
    // nothing above or beside it.
    Built built = build();
    const Skeleton& skeleton = built.rig.skeleton;

    // Find a joint with descendants, and one that is not among them.
    int subject = -1;
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
        int descendants = 0;
        for (std::size_t j = i + 1; j < skeleton.joints.size(); ++j) {
            int walk = skeleton.joints[j].parent;
            while (walk >= 0) {
                if (walk == static_cast<int>(i)) {
                    ++descendants;
                    break;
                }
                walk = skeleton.joints[static_cast<std::size_t>(walk)].parent;
            }
        }
        if (descendants >= 3 && i > 0) {
            subject = static_cast<int>(i);
            break;
        }
    }
    REQUIRE(subject > 0);

    std::vector<glm::mat4> before;
    poseToModel(skeleton, restPose(skeleton), before);

    Pose bent = restPose(skeleton);
    bent.local[static_cast<std::size_t>(subject)].rotation =
        glm::angleAxis(0.30f, glm::vec3(0.0f, 0.0f, 1.0f)) * bent.local[static_cast<std::size_t>(subject)].rotation;
    std::vector<glm::mat4> after;
    poseToModel(skeleton, bent, after);

    int moved = 0;
    int held = 0;
    for (std::size_t j = 0; j < skeleton.joints.size(); ++j) {
        bool descendant = false;
        int walk = static_cast<int>(j);
        while (walk >= 0) {
            if (walk == subject) {
                descendant = true;
                break;
            }
            walk = skeleton.joints[static_cast<std::size_t>(walk)].parent;
        }
        const float delta = glm::length(glm::vec3(after[j][3]) - glm::vec3(before[j][3]));
        if (descendant && j != static_cast<std::size_t>(subject)) {
            // Strictly moved: a descendant that did not move means the chain is broken.
            CHECK(delta > 1e-4f);
            ++moved;
        } else if (!descendant) {
            CHECK(delta < 1e-5f);
            ++held;
        }
    }
    INFO(moved << " descendants moved, " << held << " unrelated joints held still");
    CHECK(moved >= 3);
    CHECK(held > 0);
}

TEST_CASE("the springs settle, and settle further out", "[tree][rig][animation]") {
    Built built = build();
    TreeAnimator animator;
    TreeMotionInputs inputs;
    inputs.windSpeed = 0.8f;
    inputs.gust = 0.0f;
    inputs.flutter = 0.0f;

    animator.reset(built.rig);
    for (int i = 0; i < 600; ++i) {
        inputs.time = i / 60.0;
        animator.step(built.rig, inputs, 1.0f / 60.0f);
    }
    // Ten seconds of steady wind: nothing may have diverged. A spring integrated near its own
    // period gains energy rather than merely losing accuracy, so this is the assertion that catches
    // a substep rule that has stopped working.
    double trunk = 0.0;
    double outer = 0.0;
    int trunkCount = 0;
    int outerCount = 0;
    for (std::size_t i = 0; i < built.rig.size(); ++i) {
        const float amount = glm::length(animator.bend()[i]);
        REQUIRE(std::isfinite(amount));
        REQUIRE(amount < 0.4f);
        if (built.rig.jointTier[i] == BranchTier::Trunk) {
            trunk += amount;
            ++trunkCount;
        } else if (built.rig.jointTier[i] == BranchTier::Secondary) {
            outer += amount;
            ++outerCount;
        }
    }
    REQUIRE(trunkCount > 0);
    REQUIRE(outerCount > 0);
    const double trunkMean = trunk / trunkCount;
    const double outerMean = outer / outerCount;
    INFO(fmt::format("mean bend: trunk {:.4f} rad, secondary {:.4f} rad", trunkMean, outerMean));
    // The brief's constraint, measured: the trunk must feel massive and must not bend like grass.
    CHECK(trunkMean < outerMean);
    CHECK(trunkMean < 0.02);
}

TEST_CASE("the same inputs produce the same motion", "[tree][rig][determinism]") {
    Built built = build();
    const auto run = [&built] {
        TreeAnimator animator;
        TreeMotionInputs inputs;
        inputs.windSpeed = 0.7f;
        animator.reset(built.rig);
        for (int i = 0; i < 120; ++i) {
            inputs.time = i / 60.0;
            inputs.gust = 0.3f + 0.2f * std::sin(i * 0.11f);
            animator.step(built.rig, inputs, 1.0f / 60.0f);
        }
        std::vector<glm::vec2> out(animator.bend().begin(), animator.bend().end());
        return out;
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i] == b[i]);
    }
}

TEST_CASE("a second of wind is a second of wind at any frame rate", "[tree][rig][animation]") {
    // The substep count comes from the stiffest joint rather than from the frame, so the same
    // elapsed time must integrate to nearly the same pose however it was divided up. Not bitwise --
    // the substep boundaries differ -- but a tree that visibly moves differently at 24 fps and at
    // 120 fps is a tree whose offline render does not match its preview.
    Built built = build();
    const auto run = [&built](int fps) {
        TreeAnimator animator;
        TreeMotionInputs inputs;
        inputs.windSpeed = 0.7f;
        inputs.gust = 0.4f;
        animator.reset(built.rig);
        const float dt = 1.0f / static_cast<float>(fps);
        for (int i = 0; i < fps * 4; ++i) {
            inputs.time = static_cast<double>(i) * dt;
            animator.step(built.rig, inputs, dt);
        }
        double sum = 0.0;
        for (const glm::vec2& b : animator.bend()) {
            sum += glm::length(b);
        }
        return sum / static_cast<double>(animator.bend().size());
    };
    const double slow = run(24);
    const double fast = run(120);
    INFO(fmt::format("mean bend at 24 fps {:.5f}, at 120 fps {:.5f}", slow, fast));
    CHECK_THAT(slow, WithinAbs(fast, 0.01));
}

TEST_CASE("an animated tree assembles into a scene with one rig", "[tree][rig][scene]") {
    const search::Parameters params = search::sampleAt(treeSchema().parameters, kHeroCandidate);
    const auto treeParams = treeParamsFrom(params);
    REQUIRE(treeParams.has_value());
    const auto built = buildAnimatedTree(*treeParams);
    REQUIRE(built.has_value());
    REQUIRE(built->scene.rigs.size() == 1);
    // Disabled, so `updateRigs` leaves the palette the animator wrote alone instead of evaluating
    // an animation player this rig does not have.
    CHECK_FALSE(built->scene.rigs[0].enabled);
    CHECK(built->scene.rigs[0].palette.size() == built->rig.size());

    int skinned = 0;
    std::vector<std::string> unskinned;
    for (const Entity& entity : built->scene.entities) {
        if (entity.rig == kInvalidRig) {
            unskinned.push_back(entity.name);
            continue;
        }
        ++skinned;
        INFO("entity " << entity.name);
        CHECK(built->scene.meshes[entity.mesh].skinned());
    }
    for (const std::string& name : unskinned) {
        INFO("unskinned: " << name);
    }
    CHECK(skinned >= 5);
    // The environment is not part of the tree and must not be skinned to it: naming them rather
    // than counting them means adding a third silently passes instead of silently failing.
    const std::vector<std::string> expected{"tree.distant", "tree.ground"};
    std::vector<std::string> sorted = unskinned;
    std::sort(sorted.begin(), sorted.end());
    CHECK(sorted == expected);
}
