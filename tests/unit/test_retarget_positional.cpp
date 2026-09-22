// Phase C §64/§92: 100STYLE onto the Glowmere alien, through IK. ADR-553's finding is the test.
//
// ADR-553 measured the rotation retarget's legs at 0.970 of their length on every frame of every
// clip: the rest reach, welded. Its orientation and bone-length numbers were perfect, which is why
// the test here is the one that caught it: **does the retargeted leg's reach vary as a walking
// leg's does?** The rotation retarget is run beside the positional one as the control, and it must
// still be pinned, or the comparison would not be measuring the repair.

#include "assets/bvh_loader.hpp"
#include "assets/gltf_loader.hpp"
#include "scene/retarget.hpp"
#include "scene/retarget_positional.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cmath>
#include <filesystem>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

struct Reach {
    float mean = 0.0f;
    float lo = 1e9f;
    float hi = -1e9f;
    float sd = 0.0f;
};

// ADR-553's `reach`: hip-to-ankle distance over the leg's rest length, per frame.
Reach reachOf(const scene::Skeleton& sk, const scene::AnimationClip& clip, const char* hip, const char* knee,
              const char* ankle) {
    const int h = sk.find(hip);
    const int k = sk.find(knee);
    const int a = sk.find(ankle);
    REQUIRE(h >= 0);
    REQUIRE(k >= 0);
    REQUIRE(a >= 0);
    scene::Pose pose;
    std::vector<glm::mat4> model;
    scene::setRestPose(sk, pose);
    scene::poseToModel(sk, pose, model);
    const auto p = [&](int j) { return glm::vec3(model[static_cast<std::size_t>(j)][3]); };
    const float length = glm::length(p(k) - p(h)) + glm::length(p(a) - p(k));
    std::vector<float> r;
    for (int f = 0; f <= static_cast<int>(clip.length() * 30.0f); ++f) {
        scene::setRestPose(sk, pose);
        scene::sampleClip(clip, std::min(clip.start + static_cast<float>(f) / 30.0f, clip.duration), pose);
        scene::poseToModel(sk, pose, model);
        r.push_back(glm::length(p(a) - p(h)) / length);
    }
    Reach out;
    double sum = 0.0;
    double sq = 0.0;
    for (const float v : r) {
        sum += v;
        sq += static_cast<double>(v) * v;
        out.lo = std::min(out.lo, v);
        out.hi = std::max(out.hi, v);
    }
    out.mean = static_cast<float>(sum / static_cast<double>(r.size()));
    out.sd = static_cast<float>(std::sqrt(std::max(0.0, (sq / static_cast<double>(r.size())) - (out.mean * out.mean))));
    return out;
}

} // namespace

TEST_CASE("§64/§92 100STYLE onto the alien: the legs flex, where the rotation retarget welded them",
          "[retarget][aliens][phaseC]") {
    const fs::path bvh = fs::path(AVGEN_SOURCE_DIR) / "assets" / "100style" / "Neutral_FW.bvh";
    const fs::path glb = fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
    if (!fs::exists(bvh) || !fs::exists(glb)) {
        SKIP("100STYLE or the scout is not present (100STYLE is gitignored; see assets/100STYLE-ATTRIBUTION.md)");
    }
    assets::BvhLoadOptions load;
    load.scale = 0.01f;
    auto source = assets::loadBvh(bvh, load);
    REQUIRE(source.has_value());
    // Six seconds is plenty of walking, and keeps the test quick.
    scene::AnimationClip clip = source->clip;
    clip.duration = std::min(clip.duration, clip.start + 6.0f);
    scene::Scene sc;
    assets::GltfLoadOptions gltf;
    gltf.loadImages = false;
    REQUIRE(assets::loadGltf(glb, sc, gltf).has_value());
    const scene::Skeleton& alien = sc.rigs.front().skeleton;

    // ADR-553's explicit nine-joint map.
    scene::RetargetProfile profile;
    profile.name = "100style-to-scout";
    for (const auto& [s, t] : {std::pair{"Hips", "root.x"}, std::pair{"LeftHip", "thigh_twist.l"},
                               std::pair{"LeftKnee", "leg_stretch.l"}, std::pair{"LeftAnkle", "foot.l"},
                               std::pair{"LeftToe", "toes_01.l"}, std::pair{"RightHip", "thigh_twist.r"},
                               std::pair{"RightKnee", "leg_stretch.r"}, std::pair{"RightAnkle", "foot.r"},
                               std::pair{"RightToe", "toes_01.r"}}) {
        profile.joints.push_back(scene::JointMapping{s, t, scene::roleForJointName(t)});
    }
    const scene::RetargetBinding binding = scene::bindRetarget(source->skeleton, alien, profile);
    REQUIRE(binding.usable());
    const scene::AnimationClip rotation = scene::retargetClip(clip, source->skeleton, alien, binding);

    const std::vector<scene::PositionalLeg> legs = {
        {"LeftHip", "LeftKnee", "LeftAnkle", {"thigh_twist.l", "leg_stretch.l", "foot.l"}},
        {"RightHip", "RightKnee", "RightAnkle", {"thigh_twist.r", "leg_stretch.r", "foot.r"}},
    };
    scene::PositionalRetargetStats stats;
    const scene::AnimationClip positional =
        scene::retargetLegsPositional(clip, source->skeleton, rotation, alien, legs, 30.0f, &stats);
    INFO(stats.problem);
    REQUIRE(stats.problem.empty());

    const Reach sourceReach = reachOf(source->skeleton, clip, "LeftHip", "LeftKnee", "LeftAnkle");
    const Reach welded = reachOf(alien, rotation, "thigh_twist.l", "leg_stretch.l", "foot.l");
    const Reach flexed = reachOf(alien, positional, "thigh_twist.l", "leg_stretch.l", "foot.l");
    WARN(fmt::format("reach (hip to ankle over leg length), left leg:\n"
                     "  source 100STYLE   mean {:.3f}  range {:.3f}..{:.3f}  sd {:.4f}\n"
                     "  rotation retarget mean {:.3f}  range {:.3f}..{:.3f}  sd {:.4f}   (ADR-553: 0.970, pinned)\n"
                     "  positional        mean {:.3f}  range {:.3f}..{:.3f}  sd {:.4f}\n"
                     "  leg scale {:.3f} / {:.3f}; worst foot miss {:.2f}% of the leg",
                     sourceReach.mean, sourceReach.lo, sourceReach.hi, sourceReach.sd, welded.mean, welded.lo, welded.hi,
                     welded.sd, flexed.mean, flexed.lo, flexed.hi, flexed.sd, stats.legScale.at(0), stats.legScale.at(1),
                     stats.worstShortfall * 100.0f));
    // **A planted source foot stays planted on the target**, which needs the root's travel and the
    // feet on one scale. On the source's planted frames (its ankle moving under 0.1 m/s), the target
    // foot's speed is measured with the root rescaled to the legs' ratio and without.
    scene::PositionalRootRescale rescale;
    rescale.targetRoot = "root.x";
    rescale.rootScale = binding.rootScale;
    const scene::AnimationClip rescaled =
        scene::retargetLegsPositional(clip, source->skeleton, rotation, alien, legs, 30.0f, nullptr, rescale);
    const auto footSpeed = [](const scene::Skeleton& sk, const scene::AnimationClip& c, const char* joint, int f) {
        scene::Pose pose;
        std::vector<glm::mat4> model;
        const auto pos = [&](float time) {
            scene::setRestPose(sk, pose);
            scene::sampleClip(c, time, pose);
            scene::poseToModel(sk, pose, model);
            return glm::vec3(model[static_cast<std::size_t>(sk.find(joint))][3]);
        };
        const glm::vec3 a = pos(c.start + static_cast<float>(f) / 30.0f);
        const glm::vec3 b = pos(c.start + static_cast<float>(f + 1) / 30.0f);
        return std::hypot(b.x - a.x, b.z - a.z) * 30.0f;
    };
    double slideOn = 0.0;
    double slideOff = 0.0;
    double slideSource = 0.0;
    int planted = 0;
    for (int f = 0; f + 1 < static_cast<int>(clip.length() * 30.0f); ++f) {
        const float s = footSpeed(source->skeleton, clip, "LeftAnkle", f);
        if (s < 0.1f) {
            slideSource += s;
            slideOn += footSpeed(alien, rescaled, "foot.l", f);
            slideOff += footSpeed(alien, positional, "foot.l", f);
            ++planted;
        }
    }
    REQUIRE(planted > 30);
    // The source's own "planted" foot is not perfectly still (the threshold is 0.1 m/s), so the
    // right target is the source's residual scaled by the legs' ratio, not zero.
    const double expected = stats.legScale.at(0) * (slideSource / planted);
    WARN(fmt::format("on {} planted source frames the source foot slides {:.3f} m/s; the target foot slides "
                     "{:.3f} m/s with the root on the legs' scale (the source's, scaled: {:.3f}) and {:.3f} m/s "
                     "without (root scale {:.3f}, leg scale {:.3f})",
                     planted, slideSource / planted, slideOn / planted, expected, slideOff / planted,
                     binding.rootScale, stats.legScale.at(0)));
    CHECK(std::abs((slideOn / planted) - expected) < 0.25 * expected + 0.003);
    CHECK((slideOff / planted) - (slideOn / planted) > 0.01);

    // The control: the rotation retarget is still welded.
    CHECK(welded.sd < 0.005f);
    // The repair: the leg's reach now varies, and by about as much as the source's does.
    CHECK(flexed.sd > 0.5f * sourceReach.sd);
    CHECK(flexed.hi - flexed.lo > 0.5f * (sourceReach.hi - sourceReach.lo));
    CHECK(std::abs(flexed.mean - sourceReach.mean) < 0.1f);
}
