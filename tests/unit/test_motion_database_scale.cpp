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
