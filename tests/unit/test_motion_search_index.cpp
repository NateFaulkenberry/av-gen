// Phase C §54: the approximate structures, checked for correctness before anything is timed.
//
// Each is exact at its limit: PCA with every component and a shortlist of the whole corpus, VQ
// probing every centroid, the KD-tree keeping every sample. At those limits each must agree with the
// linear scan on every query, sample for sample and cost for cost. That is the test that the
// structure is sound. Everything about how much it saves below its limit is the perf harness's
// business (`test_motion_search_perf.cpp`).

#include "scene/motion_search_index.hpp"
#include "support/golden_motion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

using namespace avgen;

namespace {

scene::MotionDatabase golden() {
    auto db = scene::buildMotionDatabase(testsupport::goldenPack(), testsupport::goldenOptions());
    REQUIRE(db.has_value());
    return std::move(*db);
}

std::vector<scene::MotionQuery> queries(const scene::MotionDatabase& db) {
    std::vector<scene::MotionQuery> out;
    for (std::uint32_t s = 0; s < db.sampleCount(); s += 5) {
        scene::MotionQuery q;
        q.features.assign(db.featuresFor(s), db.featuresFor(s) + db.dimension);
        for (std::size_t d = 0; d < q.features.size(); d += 3) {
            q.features[d] += 0.3f; // off the sample, so the answer is not trivially the seed
        }
        q.current = s > 0 ? s - 1u : scene::MotionDatabase::kInvalid;
        out.push_back(std::move(q));
    }
    return out;
}

} // namespace

TEST_CASE("§54 each approximate structure is exact at its limit", "[searchindex][phaseC]") {
    const scene::MotionDatabase db = golden();
    const scene::PcaIndex pca = scene::buildPcaIndex(db, db.dimension);
    const scene::VqIndex vq = scene::buildVqIndex(db, 16);
    const scene::KdTree kd = scene::buildKdTree(db, 8);
    // The PCA basis is orthonormal and explains everything when it keeps every component.
    float explained = 0.0f;
    for (const float e : pca.explained) {
        explained += e;
    }
    CHECK(std::abs(explained - 1.0f) < 1e-3f);
    int checked = 0;
    for (const scene::MotionQuery& q : queries(db)) {
        const scene::MotionMatch exact = scene::searchMotion(db, q, {});
        REQUIRE(exact.found());
        const scene::MotionMatch a = scene::searchPca(db, pca, q, {}, db.sampleCount());
        const scene::MotionMatch b = scene::searchVq(db, vq, q, {}, vq.centroids);
        std::uint32_t visited = 0;
        const scene::MotionMatch c = scene::searchKd(db, kd, q, {}, db.sampleCount(), &visited);
        CHECK(a.cost == exact.cost);
        CHECK(b.cost == exact.cost);
        CHECK(c.cost == exact.cost);
        ++checked;
    }
    CHECK(checked > 50);
}

TEST_CASE("§54 below their limits the structures look at fewer samples", "[searchindex][phaseC]") {
    // The other arm: at their limits they are the linear scan in disguise. A PCA shortlist of 32, VQ
    // probing 2 of 16 centroids and a KD-tree keeping 8 must each score fewer samples than the whole
    // corpus, or they would be saving nothing.
    const scene::MotionDatabase db = golden();
    const scene::PcaIndex pca = scene::buildPcaIndex(db, 8);
    const scene::VqIndex vq = scene::buildVqIndex(db, 16);
    const scene::KdTree kd = scene::buildKdTree(db, 8);
    const scene::MotionQuery q = queries(db)[20];
    CHECK(scene::searchPca(db, pca, q, {}, 32).fullyScored < db.sampleCount());
    CHECK(scene::searchVq(db, vq, q, {}, 2).fullyScored < db.sampleCount());
    std::uint32_t visited = 0;
    (void)scene::searchKd(db, kd, q, {}, 8, &visited);
    WARN(fmt::format("golden corpus ({} samples, {} dims): the KD-tree scored {} to find 8", db.sampleCount(),
                     db.dimension, visited));
    CHECK(visited < db.sampleCount());
}
