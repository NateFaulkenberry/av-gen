// Phase C §77 (the testing matrix's feature rows) and §73 (cinematic determinism).
//
// §77's database rows are covered by test_motion_database_io.cpp (serialization, deserialization,
// version mismatch, corrupt data) and test_motion_adversarial.cpp (empty and one-sample databases).
// Its search rows are covered by the adversarial file and test_motion_cost.cpp, and its runtime rows
// (no candidate, fallback, database swap, multiple characters) by test_motion_matching_wired.cpp and
// the perf harness. This file covers the feature rows it leaves: deterministic extraction,
// normalization, dimensions and missing joints. It also covers §73: the same pack, config and
// request sequence give the same answer, down to the bit, in a scene as well as in isolation.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/motion_database.hpp"
#include "signals/signal_bus.hpp"
#include "support/golden_motion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cmath>
#include <cstring>
#include <filesystem>

using namespace avgen;
namespace fs = std::filesystem;

TEST_CASE("§77 extraction is deterministic, to the bit", "[motionmatrix][determinism][phaseC]") {
    auto a = scene::buildMotionDatabase(testsupport::goldenPack(), testsupport::goldenOptions());
    auto b = scene::buildMotionDatabase(testsupport::goldenPack(), testsupport::goldenOptions());
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->features.size() == b->features.size());
    CHECK(std::memcmp(a->features.data(), b->features.data(), a->features.size() * sizeof(float)) == 0);
    CHECK(a->identity == b->identity);
    CHECK(a->identity != 0u);
    // The sensitivity arm: one input changed, a different database. Equal identities for different
    // inputs would make the equality above meaningless.
    scene::MotionDatabaseOptions other = testsupport::goldenOptions();
    other.config.trajectoryTimes = {0.2f, 0.4f};
    auto c = scene::buildMotionDatabase(testsupport::goldenPack(), other);
    REQUIRE(c.has_value());
    CHECK(c->identity != a->identity);
}

TEST_CASE("§77 features are standardised: zero mean and unit spread per live dimension",
          "[motionmatrix][phaseC]") {
    auto db = scene::buildMotionDatabase(testsupport::goldenPack(), testsupport::goldenOptions());
    REQUIRE(db.has_value());
    int live = 0;
    for (std::size_t d = 0; d < db->dimension; ++d) {
        double sum = 0.0;
        double sq = 0.0;
        for (std::uint32_t s = 0; s < db->sampleCount(); ++s) {
            const double v = db->featuresFor(s)[d];
            sum += v;
            sq += v * v;
        }
        const double n = static_cast<double>(db->sampleCount());
        const double mean = sum / n;
        const double sd = std::sqrt(std::max(0.0, (sq / n) - (mean * mean)));
        CHECK(std::abs(mean) < 1e-3);
        if (db->scale[d] != 1.0f || sd > 1e-6) { // a dead dimension keeps scale 1 and zero spread
            CHECK(std::abs(sd - 1.0) < 1e-3);
            ++live;
        }
    }
    CHECK(live > 10);
}

TEST_CASE("§77 the dimension count is the layout's, whatever the config enables", "[motionmatrix][phaseC]") {
    for (const bool phase : {false, true}) {
        for (const bool contacts : {false, true}) {
            scene::MotionDatabaseOptions options = testsupport::goldenOptions();
            options.config.phaseWeight = phase ? 1.0f : 0.0f;
            options.config.contactWeight = contacts ? 1.0f : 0.0f;
            auto db = scene::buildMotionDatabase(testsupport::goldenPack(), options);
            REQUIRE(db.has_value());
            const auto layout = scene::motionFeatureLayout(options.config);
            INFO(fmt::format("phase {} contacts {}", phase, contacts));
            CHECK(db->dimension == options.config.dimension());
            CHECK(layout.size() == db->dimension);
            CHECK(db->features.size() == static_cast<std::size_t>(db->dimension) * db->sampleCount());
        }
    }
}

TEST_CASE("§77 a feature joint the rig lacks is refused, by name", "[motionmatrix][phaseC]") {
    scene::MotionDatabaseOptions options = testsupport::goldenOptions();
    options.config.joints = {"foot.l", "hand.r"};
    auto db = scene::buildMotionDatabase(testsupport::goldenPack(), options);
    REQUIRE_FALSE(db.has_value());
    CHECK(db.error().message.find("hand.r") != std::string::npos);
}

namespace {

std::uint64_t labTrace() {
    const fs::path lab = fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "motionmatch" / "alien-match-lab.scene.json";
    assets::AssetRegistry registry(lab.parent_path());
    auto loaded = scene::Composition::loadFile(lab, registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    comp.setViewport(320, 180);
    comp.scene().detailLimits.entityDistanceCull = false;
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h = (h ^ b[i]) * 1099511628211ull;
        }
    };
    for (int f = 0; f <= 240; ++f) {
        FrameTime time;
        time.renderTime = static_cast<double>(f) / 60.0;
        time.deltaTime = f == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(f);
        params.resetFinals();
        comp.updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp.updateBehaviour(time, bus);
        comp.update(time);
        const entity::Entity* e = comp.entityWorld().find("alien-match");
        REQUIRE(e != nullptr);
        const entity::MotionMemory& m = e->motionMemory();
        mix(&m.selection, sizeof m.selection);
        mix(&m.localTime, sizeof m.localTime);
        for (const scene::SkinnedRig& rig : comp.scene().rigs) {
            for (const scene::Transform& t : rig.pose.local) {
                mix(&t.position, sizeof t.position);
                mix(&t.rotation, sizeof t.rotation);
            }
        }
    }
    return h;
}

} // namespace

TEST_CASE("§73 a matcher-driven scene reproduces itself to the bit", "[motionmatrix][determinism][aliens][phaseC]") {
    // §73: deterministic given the same pack, query sequence, configuration, initial state and seed.
    // Here all of them come from one scene file, loaded twice in one process, and four seconds of
    // the matched alien's memory and every rig's pose must agree to the bit.
    if (!fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb")) {
        SKIP("the scout is not present");
    }
    const std::uint64_t a = labTrace();
    const std::uint64_t b = labTrace();
    WARN(fmt::format("match lab trace {:016x} and {:016x}", a, b));
    CHECK(a == b);
}
