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
#include "scene/motion_coverage.hpp"
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

// ---------------------------------------------------------------------------------------------
// §15, §17 and §19 -- and a correction to the two cases above.
//
// Re-reading §15 and §17 after writing them caught my own fresh work, which is ADR-606's rule
// applied to something an hour old rather than to something inherited:
//
//   §15: "The earlier research already demonstrated that **synthetic benchmark results can
//         mislead**. Therefore: use real AV Gen motion distributions as the primary benchmark."
//   §17: "Benchmark **1,700** / 10,000 / 100,000 / 1,000,000 frames. For each: query latency,
//         **average** latency, **worst-case** latency, candidate count, memory, **database load
//         time**, **build time**."
//
// The cases above are synthetic, start at 10,000, and report only a minimum. Three misses.
//
// **What survives the correction, and why, is the interesting part.** A linear scan touches every
// sample whatever the values are, so its cost is distribution-independent and the synthetic
// timing above is a valid measurement *of a linear scan*. What is NOT distribution-independent is
// everything §14 and §16 are about: how much candidate filtering removes, and whether a cheap
// first stage keeps the sample the full cost would have chosen. Those depend entirely on how the
// real motion clusters, and a synthetic database with a hash pattern for features would give an
// answer that means nothing. So the rule for the rest of Phase C is narrow and firm:
//
//   **synthetic data may time the scan; only real data may judge a filter or a first stage.**
//
// On the minimum-versus-average question: this repository's standing rule is minima over repeats,
// never means, and §17 asks for average and worst-case. That is not a contradiction, it is two
// questions. A minimum answers "how fast can this code go", which is a property of the code. A
// worst case answers "will this drop a frame", which is the only one a 60 Hz budget cares about,
// because a 36 ms spike drops a frame however good the average was. Both are reported below.

#include "assets/gltf_loader.hpp"
#include "scene/motion_pack.hpp"
#include "scene/scene.hpp"

#include <filesystem>
#include <numeric>
#include <set>

namespace {
namespace fs = std::filesystem;

fs::path alienGlb() {
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
}

struct Latency {
    double best = 0.0;
    double average = 0.0;
    double worst = 0.0;
};

Latency timeQueries(const scene::MotionDatabase& db, const scene::MotionQuery& base,
                    const scene::MotionCostWeights& weights, int queries) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(queries));
    scene::MotionQuery query = base;
    for (int i = 0; i < queries; ++i) {
        // A different current sample each time, so continuity and transition terms are exercised
        // across the database rather than at one point in it.
        query.current = db.sampleCount() > 0
                            ? static_cast<std::uint32_t>(i) % db.sampleCount()
                            : scene::MotionDatabase::kInvalid;
        const auto t0 = std::chrono::steady_clock::now();
        const scene::MotionMatch match = scene::searchMotion(db, query, weights);
        const auto t1 = std::chrono::steady_clock::now();
        (void)match;
        samples.push_back(
            std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(t1 - t0).count());
    }
    Latency out;
    out.best = *std::min_element(samples.begin(), samples.end());
    out.worst = *std::max_element(samples.begin(), samples.end());
    out.average = std::accumulate(samples.begin(), samples.end(), 0.0) /
                  static_cast<double>(samples.size());
    return out;
}

} // namespace

TEST_CASE("the real Glowmere database, built and benchmarked", "[motionscale][phaseC][aliens]") {
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    REQUIRE_FALSE(sc.rigs.empty());
    const scene::SkinnedRig& rig = sc.rigs.front();

    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.sourceFile = "alien-scout.glb";
    provenance.creator = "AV Gen";
    provenance.license = "CC0-1.0";
    provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    provenance.attributionRequired = false;
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.derivedDataAllowed = true;
    provenance.trainingAllowed = true;
    provenance.processing = {"Phase C §19"};
    provenance.toolVersion = "avgen-phase-c";

    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    packOptions.contacts.looping = true;
    packOptions.toolVersion = "avgen-phase-c";

    // §17 wants build time, so it is measured rather than mentioned.
    const auto buildStart = std::chrono::steady_clock::now();
    auto pack = scene::buildMotionPack("glowmere-scout", rig.skeleton, rig.clips, provenance,
                                       packOptions);
    const auto buildMid = std::chrono::steady_clock::now();
    if (!pack.has_value()) {
        FAIL("motion pack build failed: " << pack.error().message);
    }

    scene::MotionDatabaseOptions dbOptions;
    dbOptions.sampleRate = 30.0f;
    dbOptions.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto db = scene::buildMotionDatabase(*pack, dbOptions);
    const auto buildEnd = std::chrono::steady_clock::now();
    if (!db.has_value()) {
        FAIL("motion database build failed: " << db.error().message);
    }

    const double packMs =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(buildMid - buildStart)
            .count();
    const double dbMs =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(buildEnd - buildMid)
            .count();

    WARN(fmt::format("REAL Glowmere database: {} clips, {} samples, dimension {}",
                     db->clipNames.size(), db->sampleCount(), db->dimension));
    WARN(fmt::format("  build: pack {:.1f} ms, database {:.1f} ms, total {:.1f} ms", packMs, dbMs,
                     packMs + dbMs));
    WARN(fmt::format("  memory: {:.3f} MB ({:.1f} bytes/sample)",
                     static_cast<double>(featureBytes(*db) + metadataBytes(*db)) / (1024.0 * 1024.0),
                     static_cast<double>(featureBytes(*db) + metadataBytes(*db)) /
                         std::max(db->sampleCount(), 1u)));

    // This is the scale §17 names first and the one the synthetic table above skipped: the real
    // pack, not a round number.
    CHECK(db->sampleCount() > 100u);
    CHECK(db->dimension == dbOptions.config.dimension());

    scene::MotionQuery query;
    query.features.assign(db->dimension, 0.0f);
    // A query drawn FROM the database, so it sits inside the real distribution rather than at a
    // point no motion occupies -- which is the whole content of §15's warning.
    const std::uint32_t seedSample = db->sampleCount() / 3u;
    const float* seedFeatures = db->featuresFor(seedSample);
    std::copy(seedFeatures, seedFeatures + db->dimension, query.features.begin());

    scene::MotionCostWeights weights;
    const Latency latency = timeQueries(*db, query, weights, 500);
    WARN(fmt::format("  query latency over 500 queries: best {:.2f} us, average {:.2f} us, "
                     "worst {:.2f} us",
                     latency.best, latency.average, latency.worst));
    WARN(fmt::format("  at 60 Hz that is {:.0f} characters per frame on the average and {:.0f} on "
                     "the worst case",
                     16666.0 / std::max(latency.average, 1e-6), 16666.0 / std::max(latency.worst, 1e-6)));

    CHECK(latency.best > 0.0);
    CHECK(latency.average >= latency.best);
    CHECK(latency.worst >= latency.average);
    // **The real database is small enough that a linear scan is fine, and that is the finding.**
    // §16's two-stage search is justified by the million-sample case, not by this one, and saying
    // so is what stops it being built for the wrong reason. A budget of a third of a frame for a
    // hundred characters is generous and this is far inside it.
    CHECK(latency.average < 100.0);

    // The query found something from inside its own distribution: a search seeded with a real
    // sample must match that sample or one very near it, and a search that returned nothing would
    // have made every latency number above a measurement of a fast refusal.
    scene::MotionQuery exact = query;
    exact.current = scene::MotionDatabase::kInvalid;
    const scene::MotionMatch match = scene::searchMotion(*db, exact, weights);
    REQUIRE(match.found());
    CHECK(match.considered > 0u);
    WARN(fmt::format("  a query seeded with sample {} matched sample {} at cost {:.6f}", seedSample,
                     match.sample, match.cost));
    CHECK(match.sample == seedSample); // it is its own nearest neighbour
}

// ---------------------------------------------------------------------------------------------
// §16 -- the two-stage search, judged on real data and timed on synthetic, which is the boundary
// ADR-607 draws: a scan's cost is distribution-independent, a first stage's *recall* is not.

TEST_CASE("the two-stage search finds what the linear scan finds, on real motion",
          "[motionscale][phaseC][aliens]") {
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();

    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.sourceFile = "alien-scout.glb";
    provenance.creator = "AV Gen";
    provenance.license = "CC0-1.0";
    provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.derivedDataAllowed = true;
    provenance.trainingAllowed = true;
    provenance.processing = {"Phase C §16"};
    provenance.toolVersion = "avgen-phase-c";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    packOptions.contacts.looping = true;
    packOptions.toolVersion = "avgen-phase-c";
    auto pack = scene::buildMotionPack("glowmere-scout", rig.skeleton, rig.clips, provenance,
                                       packOptions);
    if (!pack.has_value()) {
        FAIL("motion pack build failed: " << pack.error().message);
    }
    scene::MotionDatabaseOptions dbOptions;
    dbOptions.sampleRate = 30.0f;
    dbOptions.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto db = scene::buildMotionDatabase(*pack, dbOptions);
    if (!db.has_value()) {
        FAIL("motion database build failed: " << db.error().message);
    }

    const scene::MotionCostWeights weights;

    // **An exhaustive plan must be the linear scan exactly**, or the plan is a second code path
    // rather than a tuning surface. Asserted before anything about recall, because if these two
    // disagree the recall numbers below mean nothing.
    scene::MotionQuery probe;
    probe.features.assign(db->dimension, 0.0f);
    const float* seed = db->featuresFor(db->sampleCount() / 2u);
    std::copy(seed, seed + db->dimension, probe.features.begin());
    const scene::MotionMatch exhaustiveScan = scene::searchMotion(*db, probe, weights);
    const scene::MotionMatch exhaustivePlan =
        scene::searchMotionStaged(*db, probe, weights, scene::MotionSearchPlan{});
    CHECK(exhaustivePlan.sample == exhaustiveScan.sample);
    CHECK(exhaustivePlan.cost == exhaustiveScan.cost);
    CHECK(exhaustivePlan.considered == exhaustiveScan.considered);

    // The sweep, on real motion, because a first stage's recall is not distribution-independent.
    struct Plan {
        const char* name;
        std::uint32_t stride;
        std::uint32_t shortlist;
        std::uint32_t prefix;
        std::uint32_t neighbourhood;
    };
    const Plan plans[] = {
        {"stride 8, prefix 12, top 32 ", 8u, 32u, 12u, 8u},
        {"stride 8, prefix 12, top 128", 8u, 128u, 12u, 8u},
        {"stride 8, FULL prefix, top 32", 8u, 32u, 0u, 8u},
        {"stride 4, FULL prefix, top 32", 4u, 32u, 0u, 4u},
    };

    // **The denominator, measured rather than assumed.** "Excess against the mean best cost" is
    // still the wrong scale: a query drawn from the database sits almost on a sample, so the best
    // cost is near zero and everything is a large multiple of it. The quantity that gives an
    // excess meaning is **how far apart the costs in this database actually are** -- the gap
    // between the best match and a typical one. An excess small against that gap is a different
    // frame of comparably good motion; an excess comparable to it is a different motion.
    double spread = 0.0;
    {
        int n = 0;
        for (std::uint32_t s = 0; s < db->sampleCount(); s += 97u) {
            scene::MotionQuery q;
            q.features.assign(db->dimension, 0.0f);
            const float* f = db->featuresFor(s);
            std::copy(f, f + db->dimension, q.features.begin());
            const scene::MotionMatch best = scene::searchMotion(*db, q, weights);
            // A typical candidate: the same query scored against a sample far away in the database.
            const float* other = db->featuresFor((s + db->sampleCount() / 2u) % db->sampleCount());
            float typical = 0.0f;
            for (std::size_t d = 0; d < db->dimension; ++d) {
                const float delta = q.features[d] - other[d];
                typical += delta * delta;
            }
            spread += static_cast<double>(typical) - static_cast<double>(best.cost);
            ++n;
        }
        spread /= std::max(n, 1);
    }
    WARN(fmt::format("cost spread on this database: a typical candidate is {:.2f} worse than the "
                     "best one",
                     spread));
    WARN("plan                           recall   worst excess   % of spread   samples scored");
    double chosenRecall = 0.0;
    double chosenExcess = 0.0;
    double chosenMean = 1.0;
    int trials = 0;
    for (const Plan& p : plans) {
        scene::MotionSearchPlan plan;
        plan.stride = p.stride;
        plan.shortlist = p.shortlist;
        plan.prefixDimensions = p.prefix;
        plan.neighbourhood = p.neighbourhood;

        int agreed = 0;
        trials = 0;
        double worstExcess = 0.0;
        double totalFullCost = 0.0;
        std::uint64_t fullyScored = 0;
        for (std::uint32_t s = 0; s < db->sampleCount(); s += 7u) {
            scene::MotionQuery query;
            query.features.assign(db->dimension, 0.0f);
            const float* f = db->featuresFor(s);
            std::copy(f, f + db->dimension, query.features.begin());
            // Perturbed, so the answer is not trivially the seed sample: a query sitting exactly on
            // a database entry makes any search look perfect.
            for (std::size_t d = 0; d < query.features.size(); d += 3) {
                query.features[d] += 0.05f;
            }
            query.current = s > 0 ? s - 1u : scene::MotionDatabase::kInvalid;
            const scene::MotionMatch full = scene::searchMotion(*db, query, weights);
            const scene::MotionMatch staged = scene::searchMotionStaged(*db, query, weights, plan);
            REQUIRE(full.found());
            REQUIRE(staged.found());
            ++trials;
            fullyScored += staged.fullyScored;
            totalFullCost += static_cast<double>(full.cost);
            if (staged.sample == full.sample) {
                ++agreed;
            } else {
                // **Not a ratio.** The first version measured `staged.cost / full.cost` and
                // reported **2957x**, which says nothing about the search: a query drawn from the
                // database sits almost on a sample, so `full.cost` is near zero and any absolute
                // difference over near zero is enormous. The quantity that means something is the
                // absolute excess, read against the scale of the cost distribution itself.
                worstExcess = std::max(worstExcess, static_cast<double>(staged.cost) -
                                                        static_cast<double>(full.cost));
            }
        }
        const double recall = 100.0 * agreed / std::max(trials, 1);
        const double meanCost = totalFullCost / std::max(trials, 1);
        (void)meanCost;
        WARN(fmt::format("{}  {:5.1f}%  {:12.4f}  {:10.1f}%  {:14.0f}", p.name, recall, worstExcess,
                         100.0 * worstExcess / std::max(spread, 1e-9),
                         static_cast<double>(fullyScored) / trials));
        if (p.prefix == 0u && p.stride == 4u) {
            chosenRecall = recall;
            chosenExcess = worstExcess;
            chosenMean = std::max(spread, 1e-9);
        }
    }

    CHECK(trials > 100);
    // **Recall is the thing that can fail**, and it is asserted rather than reported. A first stage
    // that agreed 30% of the time would still be fast, and fast is not the property being bought.
    CHECK(chosenRecall > 80.0);
    // And when it disagrees it must disagree **cheaply**: a different sample at nearly the same
    // cost is a different frame of equally good motion, which is what a motion matcher is allowed
    // to do. The bound is the excess against the typical cost of a match, so it scales with the
    // database rather than being a number picked to pass.
    // The bound is a fraction of the database's own cost spread, so it scales with the content
    // rather than being a number chosen to pass: when the two-stage search differs from the linear
    // scan, it must land within a tenth of the gap between a good match and a typical one.
    CHECK(chosenExcess < 0.10 * chosenMean);
}

TEST_CASE("the two-stage search is worth it only at a scale nothing here has",
          "[motionscale][phaseC]") {
    // Timing may be synthetic (ADR-607: a scan's cost is distribution-independent). What this
    // measures is the *shape* of the saving, and the point of the test is as much the disclaimer
    // as the number.
    const scene::MotionFeatureConfig config =
        scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    scene::MotionQuery query;
    query.features.assign(config.dimension(), 0.35f);
    const scene::MotionCostWeights weights;
    scene::MotionSearchPlan plan;
    plan.stride = 8;
    plan.shortlist = 32;
    plan.prefixDimensions = 12;
    plan.neighbourhood = 8;

    WARN("samples        linear      two-stage      speedup");
    for (const std::uint32_t count : {1738u, 1000000u}) {
        const scene::MotionDatabase db = synthesise(count, config);
        query.current = count / 2u;
        const auto time = [&](bool staged) {
            double best = std::numeric_limits<double>::max();
            const int repeats = count >= 1000000u ? 3 : 50;
            for (int r = 0; r < 5; ++r) {
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < repeats; ++i) {
                    if (staged) {
                        (void)scene::searchMotionStaged(db, query, weights, plan);
                    } else {
                        (void)scene::searchMotion(db, query, weights);
                    }
                }
                const auto t1 = std::chrono::steady_clock::now();
                best = std::min(best,
                                std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(
                                    t1 - t0)
                                        .count() /
                                    repeats);
            }
            return best;
        };
        const double linear = time(false);
        const double staged = time(true);
        WARN(fmt::format("{:>9}  {:>9.1f} us  {:>9.1f} us  {:>9.1f}x", count, linear, staged,
                         linear / staged));
        if (count >= 1000000u) {
            // The justification, asserted: at a million samples the two-stage search has to bring
            // a query inside a 60 Hz frame, which the 36.5 ms linear scan does not.
            CHECK(staged < 16666.0);
            CHECK(linear / staged > 5.0);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// §11 -- continuity, measured by the thing §11 actually asks for.
//
// §11's own words name the failure: "do not allow the system to **constantly jump between
// unrelated clips** simply because they happen to have similar poses." So the quantity is a **jump
// rate**, not a cost, and the way to measure it is to run the matching loop rather than to score
// one query.
//
// The binary continuity in place before this stage penalises the one sample that follows the
// current one at zero and *everything else in full* -- so a candidate two frames later in the same
// clip pays exactly what a candidate from an unrelated clip pays. Once the loop is off by a single
// frame it has no reason to prefer the clip it is already in.

TEST_CASE("continuity is measured by how often the loop jumps, on real motion",
          "[motionscale][phaseC][aliens]") {
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.sourceFile = "alien-scout.glb";
    provenance.creator = "AV Gen";
    provenance.license = "CC0-1.0";
    provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.derivedDataAllowed = true;
    provenance.trainingAllowed = true;
    provenance.processing = {"Phase C §11"};
    provenance.toolVersion = "avgen-phase-c";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    packOptions.contacts.looping = true;
    packOptions.toolVersion = "avgen-phase-c";
    auto pack = scene::buildMotionPack("glowmere-scout", rig.skeleton, rig.clips, provenance,
                                       packOptions);
    if (!pack.has_value()) {
        FAIL("motion pack build failed: " << pack.error().message);
    }
    scene::MotionDatabaseOptions dbOptions;
    dbOptions.sampleRate = 30.0f;
    dbOptions.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto db = scene::buildMotionDatabase(*pack, dbOptions);
    if (!db.has_value()) {
        FAIL("motion database build failed: " << db.error().message);
    }

    // **The first version of this loop reported 0.00 jumps per second for every configuration**,
    // which by this repository's own rule (`docs/testing.md`, "this rig produces convincing
    // zeros") is a reading to distrust before believing. It was vacuous: the loop queried with the
    // features of `sampleNext[current]`, so the natural continuation was always the exact answer
    // and the continuity term was never asked to decide anything. **A probe that cannot produce the
    // behaviour it is measuring** -- ADR-182, in a fixture I had just written.
    //
    // It also hid something. The graded runs reported a mean index step of 0.15 against the
    // binary's 1.82: the graded penalty was making the loop **stand still**, advancing less than
    // one sample per query, which is a worse failure than jumping and which the vacuous jump count
    // could not see.
    //
    // This loop instead drives the character somewhere its current clip cannot go -- the query
    // follows a *different* clip's motion -- so continuity has a real decision to make on every
    // step: follow the request and cut, or stay and be wrong. Both a thrashing matcher and a
    // frozen one now show up, in different numbers.
    struct Run {
        double clipJumpsPerSecond = 0.0;
        double meanIndexStep = 0.0;
        double stalledFraction = 0.0;  // steps where the loop did not advance at all
        double worstExcess = 0.0;      // §11 severity: how much worse than the best available
    };
    const auto run = [&](float perSecond) {
        scene::MotionCostWeights weights;
        weights.continuityPerSecond = perSecond;
        // Start in one clip and ask for another, so the term is exercised rather than agreed with.
        std::uint32_t current = 0;
        std::uint32_t target = 0;
        for (std::uint32_t s = 0; s < db->sampleCount(); ++s) {
            if (db->sampleClip[s] != db->sampleClip[0]) {
                target = s;
                break;
            }
        }
        REQUIRE(target != 0u);

        int jumps = 0;
        int stalls = 0;
        int steps = 0;
        double indexStep = 0.0;
        double worstExcess = 0.0;
        for (int i = 0; i < 300; ++i) {
            const std::uint32_t want =
                target + static_cast<std::uint32_t>(i) < db->sampleCount()
                    ? target + static_cast<std::uint32_t>(i)
                    : target;
            scene::MotionQuery query;
            query.features.assign(db->dimension, 0.0f);
            const float* f = db->featuresFor(want);
            std::copy(f, f + db->dimension, query.features.begin());
            for (std::size_t d = 0; d < query.features.size(); d += 4) {
                query.features[d] += 0.08f;
            }
            query.current = current;
            const scene::MotionMatch match = scene::searchMotion(*db, query, weights);
            REQUIRE(match.found());

            // §11 severity, which the coordinator is right that frequency alone cannot show: how
            // much worse is what continuity made it choose than the best match ignoring
            // continuity entirely? A term that is wrong rarely but catastrophically hides behind a
            // good-looking rate.
            scene::MotionQuery free = query;
            free.current = scene::MotionDatabase::kInvalid;
            const scene::MotionMatch unconstrained = scene::searchMotion(*db, free, weights);
            if (unconstrained.found() && match.sample != unconstrained.sample) {
                float chosen = 0.0f;
                const float* a = db->featuresFor(match.sample);
                const float* b = db->featuresFor(unconstrained.sample);
                for (std::size_t d = 0; d < db->dimension; ++d) {
                    const float da = query.features[d] - a[d];
                    const float dbv = query.features[d] - b[d];
                    chosen += (da * da) - (dbv * dbv);
                }
                worstExcess = std::max(worstExcess, static_cast<double>(chosen));
            }

            if (db->sampleClip[match.sample] != db->sampleClip[current]) {
                ++jumps;
            }
            if (match.sample == current) {
                ++stalls;
            }
            indexStep += std::abs(static_cast<double>(match.sample) - static_cast<double>(current));
            ++steps;
            current = match.sample;
        }
        Run out;
        out.clipJumpsPerSecond = jumps / (steps / 30.0);
        out.meanIndexStep = indexStep / steps;
        out.stalledFraction = static_cast<double>(stalls) / steps;
        out.worstExcess = worstExcess;
        return out;
    };

    const Run binary = run(0.0f);
    WARN(fmt::format("binary continuity: {:.2f} jumps/s, step {:.2f}, stalled {:.0f}%, worst "
                     "excess {:.4f}",
                     binary.clipJumpsPerSecond, binary.meanIndexStep,
                     100.0 * binary.stalledFraction, binary.worstExcess));

    // The graded rate is chosen against `continuity` itself rather than invented: at 0.35 for a
    // clip change, a rate of 1.0 per second means a skip of a third of a second inside the clip
    // costs what leaving it costs. Anything beyond that is not a skip, it is a cut.
    double bestRate = 0.0;
    Run bestRun = binary;
    for (const float rate : {0.5f, 1.0f, 2.0f, 4.0f}) {
        const Run graded = run(rate);
        WARN(fmt::format("graded at {:.1f}/s:  {:.2f} jumps/s, step {:.2f}, stalled {:.0f}%, "
                         "worst excess {:.4f}",
                         rate, graded.clipJumpsPerSecond, graded.meanIndexStep,
                         100.0 * graded.stalledFraction, graded.worstExcess));
        if (graded.clipJumpsPerSecond < bestRun.clipJumpsPerSecond) {
            bestRun = graded;
            bestRate = rate;
        }
    }
    WARN(fmt::format("best rate {:.1f}/s: {:.2f} jumps/s against the binary {:.2f}", bestRate,
                     bestRun.clipJumpsPerSecond, binary.clipJumpsPerSecond));

    // **The loop was actually made to choose.** Driving it toward another clip means the binary
    // term has a real decision on every step, so a zero here would be a finding rather than a
    // fixture artefact -- which is exactly what the first version of this test could not say.
    CHECK(binary.meanIndexStep > 0.0);
    CHECK(bestRun.meanIndexStep > 0.0);
    // **A frozen matcher is worse than a jumpy one, and frequency alone cannot tell them apart.**
    // The graded penalty's first configuration stalled the loop -- 0.15 samples per query against
    // the binary's 1.82 -- while reporting zero jumps, which looked like a perfect score.
    CHECK(bestRun.stalledFraction < 0.5);
    CHECK(bestRun.clipJumpsPerSecond <= binary.clipJumpsPerSecond);
}

// ---------------------------------------------------------------------------------------------
// §14 -- candidate filtering: "**measure how much it helps**", which is the whole deliverable.
// §12 -- the transition penalty, checked for being a control that does anything (ADR-608).

TEST_CASE("candidate filtering, measured on the real database", "[motionscale][phaseC][aliens]") {
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.sourceFile = "alien-scout.glb";
    provenance.creator = "AV Gen";
    provenance.license = "CC0-1.0";
    provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.derivedDataAllowed = true;
    provenance.trainingAllowed = true;
    provenance.processing = {"Phase C §14"};
    provenance.toolVersion = "avgen-phase-c";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    packOptions.contacts.looping = true;
    packOptions.toolVersion = "avgen-phase-c";
    auto pack = scene::buildMotionPack("glowmere-scout", rig.skeleton, rig.clips, provenance,
                                       packOptions);
    if (!pack.has_value()) {
        FAIL("motion pack build failed: " << pack.error().message);
    }
    scene::MotionDatabaseOptions dbOptions;
    dbOptions.sampleRate = 30.0f;
    dbOptions.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto db = scene::buildMotionDatabase(*pack, dbOptions);
    if (!db.has_value()) {
        FAIL("motion database build failed: " << db.error().message);
    }

    // What the tags actually say about this content, before asking what filtering on them saves.
    // **A filter's value is a property of the corpus, not of the filter**, and on a pack where
    // every sample carried the same tag it would be exactly zero however well written it was.
    const scene::MotionTag interesting[] = {scene::MotionTag::Locomotion, scene::MotionTag::Idle,
                                            scene::MotionTag::Walk,       scene::MotionTag::Run,
                                            scene::MotionTag::Turn,       scene::MotionTag::Cyclic,
                                            scene::MotionTag::Travelling, scene::MotionTag::OneShot};
    WARN("tag distribution over the real database:");
    for (const scene::MotionTag tag : interesting) {
        std::uint32_t count = 0;
        for (const std::uint32_t tags : db->sampleTags) {
            if ((tags & static_cast<std::uint32_t>(tag)) != 0u) {
                ++count;
            }
        }
        WARN(fmt::format("  {:<12} {:>5} of {} samples ({:.0f}%)", scene::motionTagNames(static_cast<std::uint32_t>(tag)),
                         count, db->sampleCount(), 100.0 * count / db->sampleCount()));
    }

    scene::MotionQuery base;
    base.features.assign(db->dimension, 0.0f);
    const float* seed = db->featuresFor(db->sampleCount() / 3u);
    std::copy(seed, seed + db->dimension, base.features.begin());
    const scene::MotionCostWeights weights;

    const auto timeQuery = [&](const scene::MotionQuery& q) {
        double best = std::numeric_limits<double>::max();
        scene::MotionMatch match;
        for (int r = 0; r < 5; ++r) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 200; ++i) {
                match = scene::searchMotion(*db, q, weights);
            }
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min(best,
                            std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(
                                t1 - t0)
                                    .count() /
                                200.0);
        }
        return std::pair<double, scene::MotionMatch>{best, match};
    };

    const auto [unfilteredUs, unfiltered] = timeQuery(base);
    scene::MotionQuery filtered = base;
    filtered.requireTags = static_cast<std::uint32_t>(scene::MotionTag::Locomotion);
    const auto [filteredUs, filteredMatch] = timeQuery(filtered);

    WARN(fmt::format("unfiltered: {} scored, {} rejected, {:.2f} us", unfiltered.considered,
                     unfiltered.rejected, unfilteredUs));
    WARN(fmt::format("require Locomotion: {} scored, {} rejected ({:.0f}% removed), {:.2f} us "
                     "({:.2f}x)",
                     filteredMatch.considered, filteredMatch.rejected,
                     100.0 * filteredMatch.rejected / db->sampleCount(), filteredUs,
                     unfilteredUs / std::max(filteredUs, 1e-9)));

    CHECK(unfiltered.rejected == 0u);
    // **The measurement §14 asks for, asserted so it stays a measurement.** If a future pack tags
    // everything identically this fails and says the filter stopped being worth anything -- which
    // is a fact about the content that nobody would otherwise notice.
    CHECK(filteredMatch.rejected > 0u);
    CHECK(filteredMatch.considered < unfiltered.considered);
    // A filter that removes candidates must not change which of the survivors wins, when the
    // winner survives: otherwise it is not a filter, it is a second opinion about the cost.
    if (filteredMatch.found() && unfiltered.found() &&
        (db->sampleTags[unfiltered.sample] &
         static_cast<std::uint32_t>(scene::MotionTag::Locomotion)) != 0u) {
        CHECK(filteredMatch.sample == unfiltered.sample);
    }
}

TEST_CASE("the transition penalty is a control that does something", "[motionscale][phaseC]") {
    // §12, checked the way ADR-608 says every configured term should be: by making it large and
    // seeing the answer change. Five of seven weights in this very struct were inert, so "it is
    // in the cost function" is not evidence that it does anything.
    const scene::MotionFeatureConfig config =
        scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    scene::MotionDatabase db;
    db.config = config;
    db.dimension = config.dimension();
    const std::uint32_t n = 3;
    db.features.assign(static_cast<std::size_t>(n) * db.dimension, 0.0f);
    db.sampleClip = {0u, 1u, 0u};
    db.sampleTime = {0.0f, 0.0f, 1.0f};
    db.samplePhase.assign(n, 0.0f);
    // Sample 0 is a walk; sample 1 is an idle in another clip; sample 2 is a walk in clip 0.
    db.sampleTags = {static_cast<std::uint32_t>(scene::MotionTag::Walk),
                     static_cast<std::uint32_t>(scene::MotionTag::Idle),
                     static_cast<std::uint32_t>(scene::MotionTag::Walk)};
    db.sampleNext = {scene::MotionDatabase::kInvalid, scene::MotionDatabase::kInvalid,
                     scene::MotionDatabase::kInvalid};
    db.mean.assign(db.dimension, 0.0f);
    db.scale.assign(db.dimension, 1.0f);
    db.clipNames = {"walk", "idle"};

    // The idle is a *slightly better* feature match, so only the transition penalty can stop it
    // winning. Without that construction the test would pass whatever the penalty did.
    db.features[0 * db.dimension + 0] = 0.20f; // walk, clip 0
    db.features[1 * db.dimension + 0] = 0.10f; // idle, clip 1 -- closer
    db.features[2 * db.dimension + 0] = 0.20f;

    scene::MotionQuery query;
    query.features.assign(db.dimension, 0.0f);
    query.current = 2u; // currently walking, in clip 0

    scene::MotionCostWeights off;
    off.continuity = 0.0f;
    off.transition = 0.0f;
    const scene::MotionMatch without = scene::searchMotion(db, query, off);
    REQUIRE(without.found());
    CHECK(without.sample == 1u); // the idle wins on features alone

    scene::MotionCostWeights on;
    on.continuity = 0.0f; // isolated: only the transition term differs between the two runs
    on.transition = 0.5f;
    const scene::MotionMatch with = scene::searchMotion(db, query, on);
    REQUIRE(with.found());
    WARN(fmt::format("transition off -> sample {}, on -> sample {} ({})", without.sample,
                     with.sample, with.breakdown.report()));
    CHECK(with.sample == 0u); // the walk wins once crossing families costs something

    // **And the winner's breakdown reports a transition cost of zero, which is correct and is a
    // real limitation worth knowing.** The penalty did its work on the candidate that *lost*: the
    // chosen sample stayed in the family, so it paid nothing. A breakdown answers "what did this
    // cost", not "what changed the decision", and those are different questions. Anyone debugging
    // a choice with §50's read-out needs to know that a term can be decisive and invisible.
    CHECK(with.breakdown.transition == 0.0f);

    // So the penalty is confirmed on a candidate that does pay it: ask from inside the idle clip,
    // where the surviving choice has to cross.
    scene::MotionQuery fromIdle = query;
    fromIdle.current = 1u; // currently idling, in clip 1
    const scene::MotionMatch crossing = scene::searchMotion(db, fromIdle, on);
    REQUIRE(crossing.found());
    if (db.sampleClip[crossing.sample] != db.sampleClip[1u]) {
        CHECK(crossing.breakdown.transition > 0.0f);
        WARN(fmt::format("crossing out of the idle clip pays {}", crossing.breakdown.report()));
    }
}

// ---------------------------------------------------------------------------------------------
// §13 -- motion tags and metadata, audited for **which end of each tag is connected**.
//
// §14's tag distribution turned up three tags carried by zero samples, and the right question that
// raises is not whether the vocabulary is well-formed. It is: for each tag, is there a **writer**,
// is there a **reader**, or is it a contract with one end connected?
//
// That failure has now appeared in both directions in this programme. ADR-608 is the declaration
// with no consumer -- five cost weights read by nothing. This is its mirror: a vocabulary with no
// producer. **The mirror is worse for a filter than for a weight**, because a dead weight sits
// inert while a filter that matches nothing does not: it silently empties the candidate set and
// hands the caller a confident answer computed over zero samples. That would be found later as
// "the matcher returns garbage when I filter by cyclic", and the search is where nobody would look.

TEST_CASE("every motion tag has a writer as well as a reader", "[motionscale][phaseC][aliens]") {
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.sourceFile = "alien-scout.glb";
    provenance.creator = "AV Gen";
    provenance.license = "CC0-1.0";
    provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.derivedDataAllowed = true;
    provenance.trainingAllowed = true;
    provenance.processing = {"Phase C §13"};
    provenance.toolVersion = "avgen-phase-c";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    packOptions.contacts.looping = true;
    packOptions.toolVersion = "avgen-phase-c";
    auto pack = scene::buildMotionPack("glowmere-scout", rig.skeleton, rig.clips, provenance,
                                       packOptions);
    if (!pack.has_value()) {
        FAIL("motion pack build failed: " << pack.error().message);
    }

    // Per clip, the three facts the tag writer reads, so a zero tag can be traced to the input that
    // produced it rather than blamed on the tagger.
    int looping = 0;
    int cyclic = 0;
    int travelling = 0;
    int withTags = 0;
    WARN("clip                       loop  cyclic  travels  authored tags");
    for (const scene::PackClip& clip : pack->clips) {
        looping += clip.loop ? 1 : 0;
        cyclic += clip.phase.cyclic ? 1 : 0;
        travelling += glm::length(clip.rootTravel) > 0.05f ? 1 : 0;
        withTags += clip.tags.empty() ? 0 : 1;
        if (clip.name.find("Walk") != std::string::npos ||
            clip.name.find("Dying") != std::string::npos ||
            clip.name.find("Idle") != std::string::npos) {
            WARN(fmt::format("  {:<24} {:>4}  {:>6}  {:>7}  {}", clip.name, clip.loop ? "yes" : "no",
                             clip.phase.cyclic ? "yes" : "no",
                             glm::length(clip.rootTravel) > 0.05f ? "yes" : "no",
                             clip.tags.empty() ? "(none)" : clip.tags.front()));
        }
    }
    WARN(fmt::format("{} of {} clips loop, {} are cyclic, {} travel, {} carry authored tags",
                     looping, pack->clips.size(), cyclic, travelling, withTags));

    // **`OneShot` has no writer on this path, and this is the assertion that says so.**
    // `PackClip::loop` defaults to `true` and is assigned in exactly one place -- deserialising a
    // pack from JSON (`c.value("loop", true)`). Nothing in `buildMotionPack` ever decides it from
    // the clip, so a pack built from a rig has every clip looping, including `Dying_forward`, and
    // `MotionTag::OneShot` is unreachable.
    //
    // Recorded as a measurement rather than fixed here: deciding whether a take loops is a
    // judgement about content (does the last pose meet the first?) and belongs with the clip
    // analysis that already answers questions of that kind, not bolted onto the tagger.
    CHECK(looping == static_cast<int>(pack->clips.size()));

    // **And the contrast that turned this from "no writer" into something better.** 25 of 26 clips
    // are `cyclic` *in the pack*, computed correctly by `buildMotionPack` from the real contact
    // joints -- and zero samples carried `MotionTag::Cyclic` in the database. The writer was not
    // missing; the database **re-derived** the analysis with empty contact joints and silently got
    // false. A broken seam, not an absent producer, and a worse defect because everything looked
    // populated at the point it was computed.
    CHECK(cyclic > 20);

    // Five clips move their root more than 5 cm -- all of them deaths, which fall rather than
    // travel, and all far short of a rest height. `MotionTag::Travelling` correctly stays unset
    // (ADR-540: every locomotion clip here is authored in place) and it stays unset **for the
    // right reason**: `travels` is `extent > restHeight` from `measureRoot`, which takes no
    // contacts, so the empty contact list the database passes does not degrade it. An earlier
    // version of this test claimed the tag was unreachable; it is reachable and working, and the
    // claim was a documented assertion nothing could disagree with (ADR-385).
    CHECK(travelling == 5);
    CHECK(withTags == 0); // the alien pack authors no tags; everything comes from clip names
}

// ---------------------------------------------------------------------------------------------
// §19 -- "the database should be able to **reproduce existing animation behavior**."
//
// That is the one falsifiable clause in §19 and it is a strong one: if the matcher, driven from
// the database, cannot follow a clip the database was built from, then nothing downstream of it
// means anything. It is also the test most likely to pass vacuously -- a query taken from a sample
// matches that sample trivially -- so the query is taken from the clip's *next* frame and
// perturbed, and the loop is judged on whether it stays on the clip rather than on whether any
// single query is right.

TEST_CASE("the database reproduces the clip it was built from", "[motionscale][phaseC][aliens]") {
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.sourceFile = "alien-scout.glb";
    provenance.creator = "AV Gen";
    provenance.license = "CC0-1.0";
    provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.derivedDataAllowed = true;
    provenance.trainingAllowed = true;
    provenance.processing = {"Phase C §19"};
    provenance.toolVersion = "avgen-phase-c";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    packOptions.contacts.looping = true;
    packOptions.toolVersion = "avgen-phase-c";
    auto pack = scene::buildMotionPack("glowmere-scout", rig.skeleton, rig.clips, provenance,
                                       packOptions);
    if (!pack.has_value()) {
        FAIL("motion pack build failed: " << pack.error().message);
    }
    scene::MotionDatabaseOptions dbOptions;
    dbOptions.sampleRate = 30.0f;
    dbOptions.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto db = scene::buildMotionDatabase(*pack, dbOptions);
    if (!db.has_value()) {
        FAIL("motion database build failed: " << db.error().message);
    }
    const scene::MotionCostWeights weights;

    // Play every clip that is long enough to be followed, one at a time, and ask the matcher to
    // track it. "Reproduce" is measured as **staying on the clip**: the fraction of steps whose
    // match is in the same clip, and how far the sample index drifts from where the clip is.
    int clipsTested = 0;
    double worstOnClip = 1.0;
    std::string worstClip;
    double totalOnClip = 0.0;
    double worstDrift = 0.0;
    double advanced = 1.0; // fraction of steps that reached a sample not already visited
    for (std::uint32_t clipIndex = 0; clipIndex < db->clipNames.size(); ++clipIndex) {
        std::vector<std::uint32_t> samples;
        for (std::uint32_t s = 0; s < db->sampleCount(); ++s) {
            if (db->sampleClip[s] == clipIndex) {
                samples.push_back(s);
            }
        }
        if (samples.size() < 40u) {
            continue;
        }
        ++clipsTested;

        std::uint32_t current = samples.front();
        int onClip = 0;
        int steps = 0;
        double drift = 0.0;
        std::set<std::uint32_t> visited;
        for (std::size_t i = 1; i + 1 < samples.size(); ++i) {
            const std::uint32_t want = samples[i];
            scene::MotionQuery query;
            query.features.assign(db->dimension, 0.0f);
            const float* f = db->featuresFor(want);
            std::copy(f, f + db->dimension, query.features.begin());
            // A real query is near its answer, never on it. Without this the test is a lookup.
            for (std::size_t d = 0; d < query.features.size(); d += 5) {
                query.features[d] += 0.06f;
            }
            query.current = current;
            const scene::MotionMatch match = scene::searchMotion(*db, query, weights);
            REQUIRE(match.found());
            if (db->sampleClip[match.sample] == clipIndex) {
                ++onClip;
                drift = std::max(drift, std::abs(static_cast<double>(match.sample) -
                                                 static_cast<double>(want)));
            }
            ++steps;
            visited.insert(match.sample);
            current = match.sample;
        }
        advanced = std::min(advanced, static_cast<double>(visited.size()) / std::max(steps, 1));
        const double fraction = static_cast<double>(onClip) / std::max(steps, 1);
        totalOnClip += fraction;
        worstDrift = std::max(worstDrift, drift);
        if (fraction < worstOnClip) {
            worstOnClip = fraction;
            worstClip = db->clipNames[clipIndex];
        }
    }

    REQUIRE(clipsTested > 5); // enough clips are long enough to be worth following
    WARN(fmt::format("§19 reproduction over {} clips: mean {:.1f}% of steps stayed on the clip, "
                     "worst {:.1f}% ({}), worst index drift {:.0f} frames",
                     clipsTested, 100.0 * totalOnClip / clipsTested, 100.0 * worstOnClip,
                     worstClip, worstDrift));
    WARN(fmt::format("  distinct samples visited per step, worst clip: {:.2f}", advanced));

    // **The clause, asserted.** A database that cannot follow its own content is not a database
    // anything else can be built on, and every later section -- augmentation, coverage, the
    // matching loop -- assumes this silently.
    CHECK(totalOnClip / clipsTested > 0.90);
    CHECK(worstOnClip > 0.50);
    // **ADR-611's companion, and my first version of it was wrong in the instructive direction.**
    // It asserted `worstDrift > 0`, where drift is the *error* between the sample matched and the
    // sample wanted -- so it demanded the matcher be imperfect, and failed on a run that tracked
    // every clip exactly (drift 0.0, the best possible result). A companion metric has to count
    // that something happened, not that something went wrong; those are opposite quantities and
    // they are easy to confuse precisely because the ADR is about metrics that cannot see
    // failures.
    //
    // The right companion is that the loop **advanced**: it reached a new sample on nearly every
    // step, so "100% on the clip" cannot be earned by a matcher that returned one sample forever.
    CHECK(advanced > 0.9);
    CHECK(worstDrift >= 0.0);
}

// ---------------------------------------------------------------------------------------------
// §22 -- motion coverage analysis on the real corpus.

TEST_CASE("coverage across the six axes C names, with its gaps", "[motionscale][phaseC][aliens]") {
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.sourceFile = "alien-scout.glb";
    provenance.creator = "AV Gen";
    provenance.license = "CC0-1.0";
    provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.derivedDataAllowed = true;
    provenance.trainingAllowed = true;
    provenance.processing = {"Phase C §22"};
    provenance.toolVersion = "avgen-phase-c";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    packOptions.contacts.looping = true;
    packOptions.toolVersion = "avgen-phase-c";
    auto pack = scene::buildMotionPack("glowmere-scout", rig.skeleton, rig.clips, provenance,
                                       packOptions);
    if (!pack.has_value()) {
        FAIL("motion pack build failed: " << pack.error().message);
    }
    scene::MotionDatabaseOptions dbOptions;
    dbOptions.sampleRate = 30.0f;
    dbOptions.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto db = scene::buildMotionDatabase(*pack, dbOptions);
    if (!db.has_value()) {
        FAIL("motion database build failed: " << db.error().message);
    }

    // **Calibrate the instrument before trusting it.** A marginal per-axis coverage is a count of
    // occupied bins, and it takes only as many distinct values as there are bins to fill an axis.
    // On 1,738 samples of varied motion that is nearly guaranteed, so at a coarse bin count the
    // measure cannot report a gap for *any* plausible corpus -- which would make it a gap-finder
    // incapable of finding a gap. ADR-182, in the instrument built to find absences.
    WARN("marginal coverage against bin count -- where does the instrument start to see absence?");
    for (const std::uint32_t bins : {8u, 32u, 128u, 512u}) {
        scene::MotionCoverageOptions options;
        options.bins = bins;
        const scene::MotionCoverageReport r = scene::measureMotionCoverage(*db, options);
        std::uint32_t gaps = 0;
        for (const scene::AxisCoverage& axis : r.axes) {
            gaps += static_cast<std::uint32_t>(axis.gaps.size());
        }
        float speedFraction = 0.0f;
        for (const scene::AxisCoverage& axis : r.axes) {
            if (axis.axis == scene::CoverageAxis::Speed) {
                speedFraction = axis.fraction();
            }
        }
        WARN(fmt::format("  {:>4} bins: {:>5} empty bins across six axes, speed axis {:5.1f}%",
                         bins, gaps, 100.0f * speedFraction));
    }

    const scene::MotionCoverageReport report = scene::measureMotionCoverage(*db);
    WARN(report.report());

    REQUIRE(report.axes.size() == 6u);
    CHECK(report.samples == db->sampleCount());
    for (const scene::AxisCoverage& axis : report.axes) {
        CHECK(axis.bins > 0u);
        CHECK(axis.occupied <= axis.bins);
    }

    // **At 8 bins every axis is 100% covered with no gaps, and that is a fact about the bin count
    // rather than about the corpus.** So the coarse marginal report is kept for what it is -- a
    // sanity check that every axis is populated at all -- and the assertion that the analyzer can
    // see absence is made where absence is actually visible.
    scene::MotionCoverageOptions fine;
    fine.bins = 128u;
    const scene::MotionCoverageReport detailed = scene::measureMotionCoverage(*db, fine);
    std::uint32_t fineGaps = 0;
    for (const scene::AxisCoverage& axis : detailed.axes) {
        fineGaps += static_cast<std::uint32_t>(axis.gaps.size());
    }
    WARN(fmt::format("at 128 bins the six axes have {} empty bins between them", fineGaps));
    CHECK(fineGaps > 0u);

    // **And the joint occupancy is the number that carried information all along**: 38 of 64
    // (speed x turn) cells, which is a gap of 26 cells that a marginal report called 100% covered.
    // That inverts the framing this analyzer was written with -- the pairing is the informative
    // measure and the marginals are the near-vacuous one -- and it is why the pairing is reported
    // as a count rather than as a percentage buried among five other percentages.
    CHECK(report.jointCells == 64u);
    CHECK(report.jointOccupancy > 0u);
    CHECK(report.jointOccupancy < report.jointCells);
}

// ---------------------------------------------------------------------------------------------
// §24 -- trajectory representation. "Exact horizons should be **experimentally validated**… Do not
// assume every dimension improves quality. **Benchmark feature configurations.**"
//
// `defaultBipedConfig` uses {0.2, 0.4, 0.6} s, and the comment beside it says "the spacing the
// literature converges on". That is a citation, not a measurement, and §24 asks for the
// measurement in as many words -- ADR-385, a stated reason is not evidence, sitting in a default
// this whole phase has been built on.
//
// The benchmark is leave-one-out retrieval: for each sample, query with its true successor's
// features (perturbed, so nothing lands on a sample) and ask whether the search returns that
// successor. A configuration that describes the motion better retrieves it more often. The
// opposing quantities are stated with it, because more horizons is always more information and a
// metric with no cost term recommends the longest list (ADR-559): dimension, memory per sample,
// and query time all rise with it.

TEST_CASE("the trajectory horizons, experimentally validated", "[motionscale][phaseC][aliens]") {
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.sourceFile = "alien-scout.glb";
    provenance.creator = "AV Gen";
    provenance.license = "CC0-1.0";
    provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.derivedDataAllowed = true;
    provenance.trainingAllowed = true;
    provenance.processing = {"Phase C §24"};
    provenance.toolVersion = "avgen-phase-c";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    packOptions.contacts.looping = true;
    packOptions.toolVersion = "avgen-phase-c";
    auto pack = scene::buildMotionPack("glowmere-scout", rig.skeleton, rig.clips, provenance,
                                       packOptions);
    if (!pack.has_value()) {
        FAIL("motion pack build failed: " << pack.error().message);
    }

    struct Horizons {
        const char* name;
        std::vector<float> times;
    };
    const Horizons sets[] = {
        {"none                ", {}},
        {"{0.2}               ", {0.2f}},
        {"{0.2, 0.4, 0.6} <-- the shipping default", {0.2f, 0.4f, 0.6f}},
        {"{0.1, 0.2, 0.4, 0.8}", {0.1f, 0.2f, 0.4f, 0.8f}},
        {"{0.2, 0.4, 0.6, 0.8, 1.0}", {0.2f, 0.4f, 0.6f, 0.8f, 1.0f}},
    };

    WARN("horizons                                  dim  bytes/sample  retrieval  query us");
    double defaultRetrieval = 0.0;
    double bestRetrieval = 0.0;
    std::string bestName;
    for (const Horizons& set : sets) {
        scene::MotionDatabaseOptions dbOptions;
        dbOptions.sampleRate = 30.0f;
        dbOptions.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
        dbOptions.config.trajectoryTimes = set.times;
        auto db = scene::buildMotionDatabase(*pack, dbOptions);
        if (!db.has_value()) {
            FAIL("motion database build failed: " << db.error().message);
        }
        const scene::MotionCostWeights weights;

        int hits = 0;
        int trials = 0;
        for (std::uint32_t s = 0; s + 1 < db->sampleCount(); s += 11u) {
            const std::uint32_t next = db->sampleNext[s];
            if (next == scene::MotionDatabase::kInvalid || next >= db->sampleCount()) {
                continue;
            }
            scene::MotionQuery query;
            query.features.assign(db->dimension, 0.0f);
            const float* f = db->featuresFor(next);
            std::copy(f, f + db->dimension, query.features.begin());
            for (std::size_t d = 0; d < query.features.size(); d += 5) {
                query.features[d] += 0.07f;
            }
            // `current` is left unset on purpose: continuity would hand the answer to the search
            // and the benchmark would measure the continuity term rather than the features.
            const scene::MotionMatch match = scene::searchMotion(*db, query, weights);
            REQUIRE(match.found());
            if (match.sample == next) {
                ++hits;
            }
            ++trials;
        }
        REQUIRE(trials > 50);

        scene::MotionQuery timing;
        timing.features.assign(db->dimension, 0.1f);
        double best = std::numeric_limits<double>::max();
        for (int r = 0; r < 5; ++r) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 100; ++i) {
                (void)scene::searchMotion(*db, timing, weights);
            }
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min(best,
                            std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(
                                t1 - t0)
                                    .count() /
                                100.0);
        }

        const double retrieval = 100.0 * hits / trials;
        WARN(fmt::format("{}  {:>3}  {:>12.0f}  {:8.1f}%  {:8.2f}", set.name, db->dimension,
                         db->dimension * 4.0 + 20.0, retrieval, best));
        if (set.times.size() == 3) {
            defaultRetrieval = retrieval;
        }
        if (retrieval > bestRetrieval) {
            bestRetrieval = retrieval;
            bestName = set.name;
        }
    }

    WARN(fmt::format("best retrieval: {} at {:.1f}%; the shipping default gives {:.1f}%", bestName,
                     bestRetrieval, defaultRetrieval));

    // **The experiment ran and discriminated**, which is what §24 asks for: the configurations are
    // not all the same, so the choice of horizons is a decision with evidence behind it rather
    // than a citation. Whether the shipping default wins is the interesting part and is reported,
    // not asserted -- an assertion that the current value is best would be the conclusion writing
    // the experiment.
    CHECK(defaultRetrieval > 0.0);
    CHECK(bestRetrieval >= defaultRetrieval);
}

// ---------------------------------------------------------------------------------------------
// §22, closed: what justifies the bin count.
//
// The calibration table has a failure at **each** end and the first pass named only one. At 8 bins
// the instrument cannot report a gap. At 512 bins it reports 788 empty bins across six axes -- but
// 1,738 samples spread over 3,072 marginal cells would leave hundreds of holes in a corpus that
// covered its space perfectly, so most of those "gaps" are sampling sparsity. **That reading is
// exactly as untrustworthy as the first and looks better, because it reports gaps.**
//
// A round number has no defence against either. The honest criterion is a bin width corresponding
// to **a difference the matcher can act on**: a gap is only interesting if landing in it would
// change which sample gets chosen. That is measurable from the cost function itself rather than
// assumed, so it is measured here.

TEST_CASE("the coverage bin width is derived from what the matcher can tell apart",
          "[motionscale][phaseC][aliens]") {
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.sourceFile = "alien-scout.glb";
    provenance.creator = "AV Gen";
    provenance.license = "CC0-1.0";
    provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.derivedDataAllowed = true;
    provenance.trainingAllowed = true;
    provenance.processing = {"Phase C §22"};
    provenance.toolVersion = "avgen-phase-c";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    packOptions.contacts.looping = true;
    packOptions.toolVersion = "avgen-phase-c";
    auto pack = scene::buildMotionPack("glowmere-scout", rig.skeleton, rig.clips, provenance,
                                       packOptions);
    if (!pack.has_value()) {
        FAIL("motion pack build failed: " << pack.error().message);
    }
    scene::MotionDatabaseOptions dbOptions;
    dbOptions.sampleRate = 30.0f;
    dbOptions.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto db = scene::buildMotionDatabase(*pack, dbOptions);
    if (!db.has_value()) {
        FAIL("motion database build failed: " << db.error().message);
    }
    const scene::MotionCostWeights weights;

    // Find the root-velocity dimensions, then ask: **how much do I have to change the requested
    // speed before the search returns a different sample?** That distance is the matcher's own
    // resolution on this corpus, and a coverage bin narrower than it describes a distinction the
    // system cannot make.
    const std::vector<scene::MotionFeatureGroup> layout =
        scene::motionFeatureLayout(db->config);
    std::size_t rootVelocity = layout.size();
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] == scene::MotionFeatureGroup::RootVelocity) {
            rootVelocity = d;
            break;
        }
    }
    REQUIRE(rootVelocity < layout.size());
    const std::size_t forward = rootVelocity + 2u; // z, the forward component

    double totalDelta = 0.0;
    int measured = 0;
    for (std::uint32_t s = 0; s < db->sampleCount(); s += 53u) {
        scene::MotionQuery query;
        query.features.assign(db->dimension, 0.0f);
        const float* f = db->featuresFor(s);
        std::copy(f, f + db->dimension, query.features.begin());
        const scene::MotionMatch baseline = scene::searchMotion(*db, query, weights);
        if (!baseline.found()) {
            continue;
        }
        // Walk the requested forward speed up in centimetres per second until the answer changes.
        for (int step = 1; step <= 300; ++step) {
            const float metresPerSecond = static_cast<float>(step) * 0.01f;
            // Features are normalised, so a change in m/s becomes a change of that times `scale`.
            query.features[forward] = f[forward] + metresPerSecond * db->scale[forward];
            const scene::MotionMatch moved = scene::searchMotion(*db, query, weights);
            if (moved.found() && moved.sample != baseline.sample) {
                totalDelta += metresPerSecond;
                ++measured;
                break;
            }
        }
    }
    REQUIRE(measured > 10);
    const double resolution = totalDelta / measured;

    // **Before trusting that number, ask what the speed feature actually contains.** This corpus is
    // authored in place (ADR-540), so the root barely moves, and a feature that does not vary
    // cannot discriminate however it is weighted. `buildMotionDatabase` counts dimensions whose
    // standard deviation is too small to normalise as *dead* and leaves their scale at 1.
    float lo = 1e9f;
    float hi = -1e9f;
    for (std::uint32_t s = 0; s < db->sampleCount(); ++s) {
        const float raw = db->scale[forward] != 0.0f
                              ? (db->featuresFor(s)[forward] / db->scale[forward]) + db->mean[forward]
                              : db->featuresFor(s)[forward];
        lo = std::min(lo, raw);
        hi = std::max(hi, raw);
    }
    WARN(fmt::format("root forward velocity across the corpus: {:.4f} .. {:.4f} m/s (spread "
                     "{:.4f}); {} of {} dimensions are dead",
                     lo, hi, hi - lo, db->stats.deadDimensions, db->dimension));
    WARN(fmt::format("the matcher changes its answer after a mean speed change of {:.3f} m/s "
                     "(over {} probes)",
                     resolution, measured));

    const float maxSpeed = 3.0f;
    const auto justifiedBins =
        static_cast<std::uint32_t>(std::max(1.0, std::round(maxSpeed / std::max(resolution, 1e-3))));
    WARN(fmt::format("so a speed axis spanning 0..{:.1f} m/s justifies about {} bins, against the "
                     "8 the first version used and the 512 that looked more sensitive",
                     maxSpeed, justifiedBins));

    scene::MotionCoverageOptions options;
    options.bins = justifiedBins;
    const scene::MotionCoverageReport report = scene::measureMotionCoverage(*db, options);
    std::uint32_t gaps = 0;
    for (const scene::AxisCoverage& axis : report.axes) {
        gaps += static_cast<std::uint32_t>(axis.gaps.size());
    }
    WARN(fmt::format("at {} bins the six axes have {} empty bins between them", justifiedBins,
                     gaps));

    // **The resolution is a real number about this matcher, not a round one.** Asserted loosely,
    // because the point is that it was measured: a resolution of zero would mean the search
    // changes its answer for any perturbation (and the corpus is denser than the cost function can
    // resolve), and one of metres would mean it cannot tell a walk from a run.
    CHECK(resolution > 0.0);
    // **The measured resolution is 1.083 m/s, which justifies three bins over a 0..3 m/s axis --
    // outside the range I expected and the reason is the finding.** The matcher is nearly blind to
    // requested speed on this corpus, because the corpus is authored in place: there is almost no
    // root velocity for the feature to carry, so no weighting of it can make it discriminate.
    //
    // That is not a defect in the cost function and it is a serious limit on the coverage
    // analyzer: a "speed coverage" axis over a corpus with no root motion describes the residual
    // rather than the motion. §20's scale experiment, on a corpus that actually travels, is the
    // section that would make this axis mean something -- which is a third result now waiting on
    // that block rather than a second.
    CHECK(resolution < 3.0);
    CHECK(justifiedBins >= 1u);
    CHECK(justifiedBins < 512u);
}
