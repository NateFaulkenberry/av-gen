// Phase C §21/§64/§65: every clip of a pack measured from one body.
//
// The Glowmere alien's own clips carry their travel on `root.x`, at hip height. A §21 variant and a
// retargeted clip carry it on `rig`, above it, because the spine, hands and knees are children of
// `rig` and must travel with the body (ADR-624 amendment). ADR-337's per-clip rule then measured the
// two kinds from different origins: every variant's feet sat 0.78 higher than its own source's, and
// the matcher paid ~5 in pose to leave a straight walk for a turn that fitted the request far
// better. Extraction v8 measures every clip from the travel joint that lies below all the others.
//
// And the strafe: 100STYLE's sidesteps, retargeted, read as sideways motion in the body's frame,
// because a clip is faced by its body (the travel joint's rotation), not by where it travels.

#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <filesystem>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::vector<float> rawJoints(const scene::MotionDatabase& db, std::uint32_t s) {
    const auto layout = scene::motionFeatureLayout(db.config);
    std::vector<float> out;
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] == scene::MotionFeatureGroup::JointPosition) {
            out.push_back((db.featuresFor(s)[d] / db.scale[d]) + db.mean[d]);
        }
    }
    return out;
}

std::uint32_t firstSampleOf(const scene::MotionDatabase& db, const std::string& clip) {
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        if (db.clipNames[db.sampleClip[s]] == clip) {
            return s;
        }
    }
    return scene::MotionDatabase::kInvalid;
}

} // namespace

TEST_CASE("v8: a turn variant's first pose is its source's first pose", "[motionmatching][augment][aliens][phaseC]") {
    const fs::path dir = fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens-scout-augmented-pack";
    if (!fs::exists(dir / "pack.json")) {
        SKIP("the augmented scout pack is not present (see the phase log, §65)");
    }
    auto pack = scene::readMotionPack(dir);
    REQUIRE(pack.has_value());
    scene::MotionDatabaseOptions options;
    options.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto db = scene::buildMotionDatabase(*pack, options);
    REQUIRE(db.has_value());
    const std::uint32_t source = firstSampleOf(*db, "Walking");
    const std::uint32_t variant = firstSampleOf(*db, "Walking~turn+1.40");
    REQUIRE(source != scene::MotionDatabase::kInvalid);
    REQUIRE(variant != scene::MotionDatabase::kInvalid);
    // At its first frame the variant has not turned yet: it is the source, so its pose features must
    // be the source's. Under the per-clip rule they differed by 0.78 in every height.
    const std::vector<float> a = rawJoints(*db, source);
    const std::vector<float> b = rawJoints(*db, variant);
    REQUIRE(a.size() == b.size());
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    WARN(fmt::format("worst joint-feature difference, Walking vs its turn variant at frame 0: {:.5f}", worst));
    CHECK(worst < 1e-3f);
}

TEST_CASE("the retargeted sidesteps read as sideways motion in the body's frame", "[motionmatching][100style][aliens][phaseC]") {
    const fs::path dir = fs::path(AVGEN_SOURCE_DIR) / "assets" / "100style-scout-sidestep-pack";
    if (!fs::exists(dir / "pack.json")) {
        SKIP("the retargeted sidestep pack is not present (see the phase log, §65)");
    }
    auto pack = scene::readMotionPack(dir);
    REQUIRE(pack.has_value());
    scene::MotionDatabaseOptions options;
    options.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto db = scene::buildMotionDatabase(*pack, options);
    REQUIRE(db.has_value());
    const auto layout = scene::motionFeatureLayout(db->config);
    std::size_t rv = 0;
    while (layout[rv] != scene::MotionFeatureGroup::RootVelocity) {
        ++rv;
    }
    std::string report;
    for (std::size_t c = 0; c < db->clipNames.size(); ++c) {
        int lateral = 0;
        int forward = 0;
        for (std::uint32_t s = 0; s < db->sampleCount(); ++s) {
            if (db->sampleClip[s] != c) {
                continue;
            }
            const float x = (db->featuresFor(s)[rv] / db->scale[rv]) + db->mean[rv];
            const float z = (db->featuresFor(s)[rv + 2] / db->scale[rv + 2]) + db->mean[rv + 2];
            if (std::hypot(x, z) < 0.2f) {
                continue;
            }
            (std::abs(x) > std::abs(z) ? lateral : forward) += 1;
        }
        report += fmt::format("  {:<28} moving samples: {} sideways, {} forward/back\n", db->clipNames[c], lateral, forward);
        CHECK(lateral > 2 * forward);
    }
    WARN(report);
}
