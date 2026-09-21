// Phase C §6 -- database memory design, and §17 -- search benchmark.
//
// §6: "**Before implementing the search system, calculate expected memory. Measure at 10,000 /
// 100,000 / 1,000,000 samples.** Estimate/measure feature memory, sample metadata, trajectory
// memory, pose data, search structure, total MotionPack size."
//
// Phase C arrives with sixteen sections already marked done from earlier work, and ADR-606 is the
// standing rule for exactly this situation: check a stage against the phase's text, not against
// the memory of what was built. `MotionDatabase` does satisfy §4, §5, §7, §8, §9 and §13 -- it is
// struct-of-arrays with indices rather than poses, versioned, normalized by `mean`/`scale`,
// data-driven through `MotionFeatureConfig`, and tagged. It carries `featureBytes` and
// `metadataBytes` fields, which is what made §6 *look* done.
//
// **Nothing had ever exercised them at scale.** The Glowmere alien's 26 clips at 30 Hz come to a
// few thousand samples; §6 names a million, and the three scales it names are the deliverable. A
// field that reports a number is not a measurement of that number.
//
// The databases below are synthesised rather than built from a pack, deliberately: there is no
// million-sample pack in this repository and inventing one would measure the generator. The
// layout is the real layout -- the same arrays, the same dimension from the same config -- so the
// bytes are the bytes the real builder would produce.

#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

// A database with `count` samples in the real layout. Feature values are a cheap deterministic
// pattern rather than random: §17 is timing a linear scan, and a scan's cost does not depend on
// the values, but reproducibility does.
scene::MotionDatabase synthesise(std::uint32_t count, const scene::MotionFeatureConfig& config) {
    scene::MotionDatabase db;
    db.name = "scale";
    db.config = config;
    db.dimension = config.dimension();
    db.features.resize(static_cast<std::size_t>(count) * db.dimension);
    for (std::size_t i = 0; i < db.features.size(); ++i) {
        db.features[i] = static_cast<float>((i * 2654435761u) % 1000u) * 0.001f;
    }
    db.sampleClip.assign(count, 0u);
    db.sampleTime.resize(count);
    db.samplePhase.resize(count);
    db.sampleTags.assign(count, static_cast<std::uint32_t>(scene::MotionTag::Locomotion) |
                                    static_cast<std::uint32_t>(scene::MotionTag::Walk));
    db.sampleNext.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        db.sampleTime[i] = static_cast<float>(i) / 30.0f;
        db.samplePhase[i] = static_cast<float>(i % 30u) / 30.0f;
        db.sampleNext[i] = i + 1u < count ? i + 1u : scene::MotionDatabase::kInvalid;
    }
    db.mean.assign(db.dimension, 0.0f);
    db.scale.assign(db.dimension, 1.0f);
    db.clipNames.push_back("synthetic");
    return db;
}

std::size_t featureBytes(const scene::MotionDatabase& db) {
    return db.features.size() * sizeof(float);
}

// Everything per-sample that is NOT the feature vector. Counted field by field rather than as one
// number, because §6 asks which part the memory is in and "metadata" is four arrays.
std::size_t metadataBytes(const scene::MotionDatabase& db) {
    return db.sampleClip.size() * sizeof(std::uint32_t) + db.sampleTime.size() * sizeof(float) +
           db.samplePhase.size() * sizeof(float) + db.sampleTags.size() * sizeof(std::uint32_t) +
           db.sampleNext.size() * sizeof(std::uint32_t);
}

} // namespace

TEST_CASE("the motion database's memory at the three scales C names", "[motionscale][phaseC]") {
    const scene::MotionFeatureConfig config =
        scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    const std::uint32_t dimension = config.dimension();
    REQUIRE(dimension > 0);

    WARN(fmt::format("feature dimension: {} floats ({} bytes per sample of feature)", dimension,
                     dimension * sizeof(float)));
    WARN("samples        features      metadata         total    bytes/sample");

    std::vector<double> totals;
    const std::uint32_t scales[] = {10000u, 100000u, 1000000u};
    for (const std::uint32_t count : scales) {
        const scene::MotionDatabase db = synthesise(count, config);
        const std::size_t f = featureBytes(db);
        const std::size_t m = metadataBytes(db);
        const std::size_t total = f + m;
        WARN(fmt::format("{:>9}  {:>10.2f} MB  {:>8.2f} MB  {:>8.2f} MB  {:>8.1f}", count,
                         static_cast<double>(f) / (1024.0 * 1024.0),
                         static_cast<double>(m) / (1024.0 * 1024.0),
                         static_cast<double>(total) / (1024.0 * 1024.0),
                         static_cast<double>(total) / count));
        totals.push_back(static_cast<double>(total));

        // **The layout is what it claims to be.** Per sample the database holds `dimension` floats
        // plus five 4-byte fields and nothing else -- no per-sample object, no pose copy, no
        // pointer. Asserting the exact arithmetic is what would catch someone adding a
        // `std::string` or a `Pose` to the per-sample data, which is the failure §5 and §6 are
        // written to prevent and which a "roughly linear" check would not see.
        const std::size_t expected =
            static_cast<std::size_t>(count) * (dimension * sizeof(float) + 5u * 4u);
        CHECK(total == expected);
    }

    // Linear in samples, to the byte: a hundred times the samples is a hundred times the memory.
    REQUIRE(totals.size() == 3);
    CHECK(totals[1] == Approx(totals[0] * 10.0));
    CHECK(totals[2] == Approx(totals[0] * 100.0));
}

TEST_CASE("a linear scan at a million samples, timed", "[motionscale][phaseC]") {
    // §17: "search benchmark". Measured before §16's two-stage search is built rather than after,
    // for ADR-603's reason -- a number written down beforehand can be wrong, and an optimisation
    // whose starting point was never recorded cannot be shown to have helped.
    const scene::MotionFeatureConfig config =
        scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    scene::MotionQuery query;
    query.features.assign(config.dimension(), 0.35f);
    query.requireTags = static_cast<std::uint32_t>(scene::MotionTag::Walk);
    scene::MotionCostWeights weights;

    WARN("samples      microseconds per query (fastest of 5)   queries/frame at 60 Hz");
    std::vector<double> micros;
    const std::uint32_t scales[] = {10000u, 100000u, 1000000u};
    for (const std::uint32_t count : scales) {
        const scene::MotionDatabase db = synthesise(count, config);
        query.current = count / 2u;
        double best = std::numeric_limits<double>::max();
        scene::MotionMatch match;
        for (int r = 0; r < 5; ++r) {
            const auto t0 = std::chrono::steady_clock::now();
            const int repeats = count >= 1000000u ? 2 : 20;
            for (int i = 0; i < repeats; ++i) {
                match = scene::searchMotion(db, query, weights);
            }
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min(best,
                            std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(
                                t1 - t0)
                                    .count() /
                                repeats);
        }
        WARN(fmt::format("{:>9}  {:>12.1f} us  {:>10.0f}", count, best, 16666.0 / best));
        micros.push_back(best);
        // The scan really scanned: every sample carries the required tag, so nothing was filtered
        // out and the timing is over the whole database rather than over an empty candidate set.
        CHECK(match.found());
        CHECK(match.considered == count);
        CHECK(match.rejected == 0u);
    }

    REQUIRE(micros.size() == 3);
    // **Linear, which is the finding.** A linear scan is the right first implementation and the
    // wrong last one, and the ratio here is what says so: ten times the database is ten times the
    // query. The bound is loose because a scan at 10,000 fits in cache and one at 1,000,000 does
    // not, so the large end is worse than linear rather than better.
    WARN(fmt::format("scaling 10k->1M: {:.1f}x for 100x the samples", micros[2] / micros[0]));
    CHECK(micros[2] > micros[0] * 20.0);
    CHECK(micros[2] < micros[0] * 500.0);
}

TEST_CASE("a hundred characters share one database rather than carrying one each",
          "[motionscale][phaseC]") {
    // §4: the database "should be versioned, serializable, inspectable, deterministic, **shared
    // between character instances**." That clause is easy to read past and the measurement above
    // is why it must not be: at a million samples the database is **144.96 MB**, so a hundred
    // characters each holding one is 14.5 GB.
    //
    // Phase B §48 found exactly this failure one tier up -- `SkinnedRig` holds its `Skeleton` and
    // every `AnimationClip` **by value**, and 78% of the Glowmere scene's rig memory is a
    // byte-identical second copy (ADR-604). The same mistake here would be two orders of magnitude
    // worse, and it would be invisible to §47's timing for the same reason it was there: a loop
    // that never reads the duplicates does not slow down.
    //
    // It is presently correct -- `MatchMotionProvider` holds a `const MotionDatabase*` -- and this
    // asserts it stays correct, because "it is a pointer today" is not a property anything checks.
    const scene::MotionFeatureConfig config =
        scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    const scene::MotionDatabase db = synthesise(1000u, config);

    std::vector<entity::MatchMotionProvider> characters;
    characters.reserve(100);
    for (int i = 0; i < 100; ++i) {
        characters.emplace_back(&db, nullptr, fmt::format("character-{}", i));
    }
    // Every one of them points at the same storage. A provider that had copied the database would
    // have its own `features` array, and this compares the address of the bytes rather than of the
    // struct, so a shallow copy of the struct would still fail.
    for (const entity::MatchMotionProvider& c : characters) {
        REQUIRE(c.database() != nullptr);
        CHECK(c.database()->features.data() == db.features.data());
    }
}
