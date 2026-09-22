#pragma once

// Phase C §54: approximate search, investigated. Three structures, each finishing with the same exact
// scorer as the linear scan (`scoreMotionCandidates`), so none of them can disagree with it about
// what a candidate costs, only about which candidates it looked at.
//
// §54 names KD-tree, PCA, ANN, vector quantization and coarse feature bins, and forbids adopting any
// of them unless the exact search is too slow, the data shows it, and the quality cost is understood.
// So these exist to be **measured**, beside the linear scan and the strided plan §16 already built,
// by `test_motion_search_perf.cpp` (the §55 matrix). None of them is installed in the matcher.
//
//   * `PcaIndex`: every sample projected onto the top k principal components of the weighted,
//     standardised features. Stage one scans the k-dimensional projections, and the shortlist gets
//     the full cost. This is §16's "cheap prefix" done properly: the prefix is the k directions that
//     carry the most variance, not the first k dimensions in layout order.
//   * `VqIndex`: vector quantization / coarse bins. k-means centroids, and an inverted list per
//     centroid. A query scores the members of its `probes` nearest centroids.
//   * `KdTree`: exact nearest neighbour by branch and bound over the weighted features. Continuity
//     and transition are not in the tree, so the tree finds the best feature match and the exact
//     scorer adds the rest over the tree's best few. It is here to measure the curse of
//     dimensionality on this data: how much of a 33-dimension tree a query actually visits.

#include "scene/motion_database.hpp"

#include <cstdint>
#include <vector>

namespace avgen::scene {

// Weighted, standardised features: sqrt(weight) times each stored dimension, so a squared Euclidean
// distance between two of these is exactly the weighted feature cost.
[[nodiscard]] std::vector<float> weightedFeatures(const MotionDatabase& db);

struct PcaIndex {
    std::uint32_t components = 0;
    std::uint32_t dimension = 0;
    std::vector<float> mean;       // of the weighted features
    std::vector<float> basis;      // components x dimension, row-major, orthonormal rows
    std::vector<float> projected;  // samples x components
    std::vector<float> explained;  // variance fraction per component
    [[nodiscard]] std::size_t bytes() const {
        return (mean.size() + basis.size() + projected.size() + explained.size()) * sizeof(float);
    }
};
[[nodiscard]] PcaIndex buildPcaIndex(const MotionDatabase& db, std::uint32_t components);
[[nodiscard]] MotionMatch searchPca(const MotionDatabase& db, const PcaIndex& index, const MotionQuery& query,
                                    const MotionCostWeights& weights, std::uint32_t shortlist);

struct VqIndex {
    std::uint32_t centroids = 0;
    std::uint32_t dimension = 0;
    std::vector<float> centre;                     // centroids x dimension, weighted space
    std::vector<std::vector<std::uint32_t>> lists; // members of each centroid
    [[nodiscard]] std::size_t bytes() const {
        std::size_t n = centre.size() * sizeof(float);
        for (const auto& l : lists) {
            n += l.size() * sizeof(std::uint32_t);
        }
        return n;
    }
};
// Deterministic k-means: seeded by evenly spaced samples, a fixed number of Lloyd iterations.
[[nodiscard]] VqIndex buildVqIndex(const MotionDatabase& db, std::uint32_t centroids, std::uint32_t iterations = 8);
[[nodiscard]] MotionMatch searchVq(const MotionDatabase& db, const VqIndex& index, const MotionQuery& query,
                                   const MotionCostWeights& weights, std::uint32_t probes);

struct KdTree {
    struct Node {
        std::uint32_t begin = 0;   // into `order`
        std::uint32_t end = 0;
        std::int32_t left = -1;
        std::int32_t right = -1;
        std::uint32_t axis = 0;
        float split = 0.0f;
    };
    std::uint32_t dimension = 0;
    std::vector<Node> nodes;
    std::vector<std::uint32_t> order;
    std::vector<float> points; // weighted features, samples x dimension
    [[nodiscard]] std::size_t bytes() const {
        return nodes.size() * sizeof(Node) + order.size() * sizeof(std::uint32_t) + points.size() * sizeof(float);
    }
};
[[nodiscard]] KdTree buildKdTree(const MotionDatabase& db, std::uint32_t leafSize = 16);
// Exact on the feature cost. `visited` reports how many samples the branch and bound had to score:
// the number the curse of dimensionality is measured by.
[[nodiscard]] MotionMatch searchKd(const MotionDatabase& db, const KdTree& tree, const MotionQuery& query,
                                   const MotionCostWeights& weights, std::uint32_t keep, std::uint32_t* visited);

} // namespace avgen::scene
