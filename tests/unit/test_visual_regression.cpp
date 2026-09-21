// Phase B §52 -- visual regression, numerically.
//
// "Where practical, create deterministic animation fixtures and compare joint positions,
// end-effector positions, contact error, root trajectory, pose continuity. **Do not rely
// exclusively on screenshots. Numerical regression is preferable where possible.**"
//
// A screenshot comparison answers "does this look different" with a number nobody can act on: a
// diff of 0.3% is a renamed joint, a changed easing, a driver rewrite or an anti-aliasing change,
// and the image cannot say which. The quantities below are the ones a character animator would
// name, they are recorded per frame, and when one of them moves the failure says which joint at
// which second by how far.
//
// **On the regeneration escape hatch.** `AVGEN_WRITE_MOTION_BASELINE=1` rewrites the file. That is
// necessary -- a deliberate change to the stack has to be able to land -- and it is also the way
// this kind of test dies: a regression appears, someone regenerates, the diff is a wall of numbers
// nobody reads, and the baseline now records the bug. The mitigation is that the baseline is a
// **text file of named, human-readable quantities in metres**, small enough to read in a diff, so
// that regenerating it puts the change in front of a reviewer instead of hiding it in a hash. A
// single checksum would have been easier to write and worthless to review.

#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/ik.hpp"
#include "scene/pose_layers.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {
namespace fs = std::filesystem;

fs::path alienPath() {
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
}
fs::path baselinePath() {
    return fs::path(AVGEN_SOURCE_DIR) / "tests" / "data" / "motion-baseline.txt";
}

// The quantities, named. Each row of the baseline is one of these at one sample second.
struct Row {
    std::string key;
    float value = 0.0f;
};

std::vector<scene::PoseLayer> regressionLayers() {
    std::vector<scene::PoseLayer> layers;
    for (const char* side : {"l", "r"}) {
        scene::PoseLayer stride;
        stride.name = fmt::format("stride.{}", side);
        stride.kind = scene::PoseLayerKind::Stride;
        stride.strideJoint = fmt::format("foot.{}", side);
        stride.strideOrigin = "root.x";
        stride.weight = 0.8f;
        stride.strideRatio = 0.8f;
        stride.bodySpeed = 1.1f;
        layers.push_back(stride);
    }
    scene::PoseLayer lean;
    lean.name = "lean";
    lean.kind = scene::PoseLayerKind::Lean;
    lean.mask.joints = {"spine_02.x", "spine_03.x"};
    lean.weight = 1.0f;
    lean.bodyAcceleration = glm::vec3(0.6f, 0.0f, 0.2f);
    lean.bodyTurnRate = 0.4f;
    layers.push_back(lean);
    for (const char* side : {"l", "r"}) {
        scene::PoseLayer foot;
        foot.name = fmt::format("foot.{}", side);
        foot.kind = scene::PoseLayerKind::Foot;
        foot.chainRoot = fmt::format("thigh_twist.{}", side);
        foot.chainMid = fmt::format("leg_stretch.{}", side);
        foot.chainTip = fmt::format("foot.{}", side);
        foot.weight = 1.0f;
        foot.hasGround = true;
        foot.groundPoint = glm::vec3(0.0f);
        foot.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        layers.push_back(foot);
    }
    scene::PoseLayer look;
    look.name = "look";
    look.kind = scene::PoseLayerKind::Aim;
    look.pivot = "head.x";
    look.mask.joints = {"head.x", "Eye_L", "Eye_R"};
    look.weight = 1.0f;
    look.hasTarget = true;
    look.target = glm::vec3(1.2f, 1.4f, 1.5f);
    layers.push_back(look);
    scene::PoseLayer reach;
    reach.name = "reach";
    reach.kind = scene::PoseLayerKind::Reach;
    reach.chainRoot = "shoulder.l";
    reach.chainMid = "forearm_stretch.l";
    reach.chainTip = "hand.l";
    reach.weight = 1.0f;
    reach.hasTarget = true;
    reach.target = glm::vec3(0.30f, 1.10f, 0.35f);
    layers.push_back(reach);
    return layers;
}

std::vector<Row> capture(const scene::Skeleton& sk, const std::vector<scene::AnimationClip>& clips,
                         const scene::AnimationClip& walk, scene::PoseLayerStack& stack) {
    std::vector<Row> rows;
    scene::Pose pose;
    std::vector<glm::mat4> model;
    std::vector<glm::vec3> previous;

    const char* tracked[] = {"root.x", "foot.l", "foot.r", "hand.l", "head.x", "spine_03.x"};
    const int footL = sk.find("foot.l");
    const int footR = sk.find("foot.r");

    // Sixteen evenly spaced samples of one loop: enough that a change to easing shows up in more
    // than one row, few enough that the file stays readable.
    for (int s = 0; s < 16; ++s) {
        const float t = walk.length() * static_cast<float>(s) / 16.0f;
        const auto now = static_cast<double>(t);
        scene::setRestPose(sk, pose);
        scene::sampleClip(walk, walk.start + t, pose);
        stack.apply(sk, clips, now, pose);
        scene::poseToModel(sk, pose, model);

        for (const char* name : tracked) {
            const int j = sk.find(name);
            if (j < 0) {
                continue;
            }
            const glm::vec3 p = glm::vec3(model[static_cast<std::size_t>(j)][3]);
            rows.push_back({fmt::format("f{:02d}.{}.x", s, name), p.x});
            rows.push_back({fmt::format("f{:02d}.{}.y", s, name), p.y});
            rows.push_back({fmt::format("f{:02d}.{}.z", s, name), p.z});
        }
        // Contact error: how far each planted foot is from the ground plane it was asked to stand
        // on. This is the quantity a viewer reads as "the feet are floating", and it is invisible
        // to a joint-position diff that happens to move both feet together.
        for (const int foot : {footL, footR}) {
            if (foot >= 0) {
                rows.push_back({fmt::format("f{:02d}.contact.{}", s, foot == footL ? "l" : "r"),
                                glm::vec3(model[static_cast<std::size_t>(foot)][3]).y});
            }
        }
        // Pose continuity: the largest single-joint move since the previous sample. A change that
        // leaves every position the same but reorders the pipeline shows up here and nowhere else.
        std::vector<glm::vec3> positions(model.size());
        for (std::size_t j = 0; j < model.size(); ++j) {
            positions[j] = glm::vec3(model[j][3]);
        }
        if (!previous.empty()) {
            float worst = 0.0f;
            for (std::size_t j = 0; j < positions.size(); ++j) {
                worst = std::max(worst, glm::length(positions[j] - previous[j]));
            }
            rows.push_back({fmt::format("f{:02d}.continuity", s), worst});
        }
        previous = positions;
    }
    return rows;
}

} // namespace

TEST_CASE("the posed alien matches its recorded baseline", "[regression][phaseB][aliens]") {
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

    scene::PoseLayerStack stack;
    REQUIRE(stack.bind(regressionLayers(), rig.skeleton, rig.clips).empty());
    const std::vector<Row> rows = capture(rig.skeleton, rig.clips, *walk, stack);
    REQUIRE(rows.size() > 300); // the fixture really captured a run

    if (std::getenv("AVGEN_WRITE_MOTION_BASELINE") != nullptr) {
        fs::create_directories(baselinePath().parent_path());
        std::ofstream out(baselinePath());
        REQUIRE(out.good());
        out << "# Phase B §52. Regenerate with AVGEN_WRITE_MOTION_BASELINE=1 -- and read the diff.\n";
        out << "# metres, alien-scout.glb, clip 'Walking', full seven-layer stack.\n";
        for (const Row& row : rows) {
            out << fmt::format("{} {:.5f}\n", row.key, row.value);
        }
        WARN(fmt::format("baseline rewritten: {} rows to {}", rows.size(), baselinePath().string()));
        return;
    }

    std::ifstream in(baselinePath());
    if (!in.good()) {
        FAIL("no baseline at " << baselinePath().string()
                               << "; generate it with AVGEN_WRITE_MOTION_BASELINE=1 and commit it");
    }
    std::map<std::string, float> expected;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream parse(line);
        std::string key;
        float value = 0.0f;
        if (parse >> key >> value) {
            expected[key] = value;
        }
    }
    REQUIRE(expected.size() == rows.size()); // a row added or removed is itself a regression

    // 0.1 mm. Loose enough that a compiler's fused multiply-add does not fail it, and tight enough
    // that no change a person would make to a layer survives it -- the smallest effect measured in
    // this phase was the 0.052 m foot slide, five hundred times this.
    int drifted = 0;
    float worst = 0.0f;
    std::string worstKey;
    for (const Row& row : rows) {
        const auto it = expected.find(row.key);
        if (it == expected.end()) {
            FAIL("baseline has no row named " << row.key);
        }
        const float delta = std::abs(row.value - it->second);
        if (delta > worst) {
            worst = delta;
            worstKey = row.key;
        }
        if (delta > 1e-4f) {
            ++drifted;
            if (drifted <= 5) {
                WARN(fmt::format("{}: baseline {:.5f}, now {:.5f} ({:+.5f} m)", row.key, it->second,
                                 row.value, row.value - it->second));
            }
        }
    }
    INFO(fmt::format("worst drift {:.6f} m on {}", worst, worstKey));
    CHECK(drifted == 0);
}
