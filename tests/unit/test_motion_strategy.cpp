// Phase C §42 (retargeted database strategy) and §43 (character-specific databases), measured on the
// Glowmere cast: six alien variants, one authored motion set.
//
// §42 asks for a decision between
//   A  one source database, retargeted to each character at runtime,
//   B  a database per target skeleton, retargeted offline,
//   C  a hybrid,
// "measured, not chosen on aesthetics", over memory, load time, runtime CPU, reuse and diversity.
// This file is the measurement; the decision is recorded in the phase log under §42.
//
// The cast is the realistic case for this repository: the one corpus that animates these rigs is
// the scout's own (ADR-553: a BVH corpus cannot), and five other aliens want to play it.

#include "scene/motion_database_io.hpp"
#include "scene/retarget.hpp"

#include "support/motion_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <chrono>
#include <limits>
#include <set>

using namespace avgen;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

std::size_t clipBytes(const std::vector<scene::AnimationClip>& clips) {
    std::size_t bytes = 0;
    for (const scene::AnimationClip& c : clips) {
        for (const scene::AnimationChannel& ch : c.channels) {
            bytes += ch.times.size() * sizeof(float) + ch.values.size() * sizeof(glm::vec4);
        }
    }
    return bytes;
}

double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// Every joint to the joint of the same name: the cast shares one rig layout, so the map is the
// names, and the binding's per-joint scales carry the proportions.
scene::RetargetProfile sameNames(const scene::Skeleton& sk) {
    scene::RetargetProfile p;
    p.name = "cast";
    for (const scene::Joint& j : sk.joints) {
        p.joints.push_back(scene::JointMapping{j.name, j.name, scene::HumanoidRole::None});
    }
    return p;
}

} // namespace

TEST_CASE("§42/§43: source-plus-runtime-retarget against retargeted-per-rig, on the cast",
          "[motionstrategy][phaseC][aliens]") {
    auto source = testsupport::glowmereMotion();
    if (!source) {
        SKIP("the Glowmere alien is not present");
    }
    const std::vector<std::string> cast = {"scout", "elder", "diver", "pilot", "ranger", "trooper"};
    struct Rig {
        std::string name;
        scene::Skeleton skeleton;
        std::string digest;
    };
    std::vector<Rig> rigs;
    for (const std::string& name : cast) {
        const fs::path path = fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / ("alien-" + name + ".glb");
        scene::Scene sc;
        assets::GltfLoadOptions options;
        options.loadImages = false;
        if (!fs::exists(path) || !assets::loadGltf(path, sc, options).has_value() || sc.rigs.empty()) {
            continue;
        }
        rigs.push_back({name, sc.rigs.front().skeleton, scene::skeletonDigest(sc.rigs.front().skeleton)});
    }
    REQUIRE(rigs.size() >= 2);
    std::set<std::string> digests;
    for (const Rig& r : rigs) {
        digests.insert(r.digest);
    }
    WARN(fmt::format("{} rigs, {} distinct skeleton digests", rigs.size(), digests.size()));

    const scene::Skeleton& sourceSkeleton = source->pack.skeleton;
    const std::vector<scene::AnimationClip>& sourceClips = source->pack.animation;
    const std::size_t sourceClipBytes = clipBytes(sourceClips);
    const std::size_t sourceDbBytes = source->db.stats.totalBytes();
    std::uint32_t sourceFrames = 0;
    for (const scene::PackClip& c : source->pack.clips) {
        sourceFrames += c.frames;
    }
    REQUIRE(sourceFrames > 1000);

    // ---- runtime cost per posed frame --------------------------------------------------------
    // B: sample a clip that is already the target's. A: sample the source's, then retarget the
    // pose -- measured as a whole-clip retarget divided by its frames, which is the same per-frame
    // work (`retargetClip` IS the per-pose retarget in a loop) plus writing the keys.
    const Rig& target = rigs.back();
    const scene::RetargetBinding binding = scene::bindRetarget(sourceSkeleton, target.skeleton, sameNames(target.skeleton));
    REQUIRE(binding.usable());
    const scene::AnimationClip& probeClip = sourceClips.front();
    const auto probeFrames = static_cast<double>(std::max(1.0f, std::floor(probeClip.length() * 30.0f)) + 1.0f);

    double perPoseA = std::numeric_limits<double>::max();
    double perPoseB = std::numeric_limits<double>::max();
    scene::AnimationClip retargeted;
    for (int r = 0; r < 5; ++r) {
        const auto t0 = Clock::now();
        retargeted = scene::retargetClip(probeClip, sourceSkeleton, target.skeleton, binding);
        const auto t1 = Clock::now();
        perPoseA = std::min(perPoseA, ms(t0, t1) * 1000.0 / probeFrames);

        scene::Pose pose;
        const auto t2 = Clock::now();
        for (int f = 0; f < static_cast<int>(probeFrames); ++f) {
            scene::setRestPose(target.skeleton, pose);
            scene::sampleClip(retargeted, static_cast<float>(f) / 30.0f, pose);
        }
        const auto t3 = Clock::now();
        perPoseB = std::min(perPoseB, ms(t2, t3) * 1000.0 / probeFrames);
    }

    // ---- the offline cost of B, per target rig -----------------------------------------------
    double retargetAllMs = std::numeric_limits<double>::max();
    double buildDbMs = std::numeric_limits<double>::max();
    std::size_t targetClipBytes = 0;
    std::size_t targetDbBytes = 0;
    for (int r = 0; r < 2; ++r) {
        const auto t0 = Clock::now();
        std::vector<scene::AnimationClip> clips;
        for (const scene::AnimationClip& c : sourceClips) {
            clips.push_back(scene::retargetClip(c, sourceSkeleton, target.skeleton, binding));
        }
        const auto t1 = Clock::now();
        scene::Provenance provenance = source->pack.provenance.front();
        provenance.processing.push_back("retarget profile=cast");
        scene::PackBuildOptions packOptions;
        packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                     scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
        packOptions.contacts.looping = true;
        auto pack = scene::buildMotionPack(target.name, target.skeleton, clips, provenance, packOptions);
        REQUIRE(pack.has_value());
        auto db = scene::buildMotionDatabase(*pack, source->options);
        REQUIRE(db.has_value());
        const auto t2 = Clock::now();
        retargetAllMs = std::min(retargetAllMs, ms(t0, t1));
        buildDbMs = std::min(buildDbMs, ms(t1, t2));
        targetClipBytes = clipBytes(pack->animation);
        targetDbBytes = db->stats.totalBytes();
    }

    // ---- A's per-character runtime state -------------------------------------------------------
    double bindMs = std::numeric_limits<double>::max();
    for (int r = 0; r < 5; ++r) {
        const auto t0 = Clock::now();
        const scene::RetargetBinding b = scene::bindRetarget(sourceSkeleton, target.skeleton, sameNames(target.skeleton));
        bindMs = std::min(bindMs, ms(t0, Clock::now()));
        REQUIRE(b.usable());
    }
    const std::size_t bindingBytes = binding.links.size() * sizeof(scene::RetargetBinding::Link);

    const auto mb = [](std::size_t b) { return static_cast<double>(b) / (1024.0 * 1024.0); };
    WARN(fmt::format("§42 on the cast ({} frames of source motion, target '{}'):\n"
                     "  runtime per posed frame: A source+retarget {:.2f} us   B pre-retargeted {:.2f} us  ({:.1f}x)\n"
                     "  per rig, B offline: retarget {:.1f} ms + pack/db build {:.1f} ms; stores clips {:.2f} MB + db {:.2f} MB\n"
                     "  per character, A: bind {:.3f} ms, {} links = {:.1f} KB; shares clips {:.2f} MB + db {:.2f} MB",
                     sourceFrames, target.name, perPoseA, perPoseB, perPoseA / std::max(perPoseB, 1e-9),
                     retargetAllMs, buildDbMs, mb(targetClipBytes), mb(targetDbBytes), bindMs,
                     binding.links.size(), static_cast<double>(bindingBytes) / 1024.0, mb(sourceClipBytes),
                     mb(sourceDbBytes)));

    // The scaling that decides it: T distinct target rigs, N characters drawn at 60 Hz.
    for (const int characters : {6, 60}) {
        const double aCpu = characters * 60.0 * perPoseA / 1000.0; // ms of CPU per second
        const double bCpu = characters * 60.0 * perPoseB / 1000.0;
        const std::size_t rigsNeeded = std::min<std::size_t>(digests.size(), static_cast<std::size_t>(characters));
        const std::size_t aMem = sourceClipBytes + sourceDbBytes + characters * bindingBytes;
        const std::size_t bMem = rigsNeeded * (targetClipBytes + targetDbBytes);
        WARN(fmt::format("  {:>3} characters over {} rigs: A {:.1f} ms CPU/s, {:.2f} MB   B {:.1f} ms CPU/s, {:.2f} MB",
                         characters, rigsNeeded, aCpu, mb(aMem), bCpu, mb(bMem)));
    }

    // What the measurement must show for the §42 decision (phase log) to stand. If a change to
    // the retargeter or the sampler moves these, the decision is re-opened by this test failing.
    CHECK(perPoseA > perPoseB * 5.0);
    // A retargeted pack costs the same order of memory as the source's -- B's cost is per RIG.
    CHECK(targetClipBytes > sourceClipBytes / 4);
}
