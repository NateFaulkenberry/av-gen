#include "scene/motion_search_index.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <queue>

namespace avgen::scene {

namespace {

bool passes(const MotionDatabase& db, const MotionQuery& query, std::uint32_t s) {
    const std::uint32_t tags = db.sampleTags[s];
    return !((query.requireTags != 0 && (tags & query.requireTags) != query.requireTags) ||
             (query.rejectTags != 0 && (tags & query.rejectTags) != 0));
}

std::vector<float> weightedQuery(const MotionDatabase& db, const MotionQuery& query) {
    const std::vector<float> w = motionFeatureWeights(db.config);
    std::vector<float> out(db.dimension, 0.0f);
    for (std::size_t d = 0; d < db.dimension && d < query.features.size(); ++d) {
        out[d] = query.features[d] * std::sqrt(d < w.size() ? w[d] : 1.0f);
    }
    return out;
}

// The continuation is always a candidate: the linear scan scores it, and an approximate stage one
// that happened to miss it would turn every "carry on" into a switch.
void addContinuation(const MotionDatabase& db, const MotionQuery& query, std::vector<std::uint32_t>& candidates) {
    if (query.current != MotionDatabase::kInvalid && query.current < db.sampleNext.size()) {
        const std::uint32_t next = db.sampleNext[query.current];
        if (next != MotionDatabase::kInvalid) {
            candidates.push_back(next);
        }
    }
}

// Symmetric eigen decomposition by cyclic Jacobi rotations. Small matrices only (the feature
// dimension, tens), where it is exact enough and has no dependencies.
void jacobi(std::vector<double>& a, std::size_t n, std::vector<double>& values, std::vector<double>& vectors) {
    vectors.assign(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        vectors[(i * n) + i] = 1.0;
    }
    for (int sweep = 0; sweep < 64; ++sweep) {
        double off = 0.0;
        for (std::size_t p = 0; p < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                off += a[(p * n) + q] * a[(p * n) + q];
            }
        }
        if (off < 1e-18) {
            break;
        }
        for (std::size_t p = 0; p < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                const double apq = a[(p * n) + q];
                if (std::abs(apq) < 1e-300) {
                    continue;
                }
                const double theta = (a[(q * n) + q] - a[(p * n) + p]) / (2.0 * apq);
                const double t = (theta >= 0.0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt((theta * theta) + 1.0));
                const double c = 1.0 / std::sqrt((t * t) + 1.0);
                const double s = t * c;
                for (std::size_t k = 0; k < n; ++k) {
                    const double akp = a[(k * n) + p];
                    const double akq = a[(k * n) + q];
                    a[(k * n) + p] = (c * akp) - (s * akq);
                    a[(k * n) + q] = (s * akp) + (c * akq);
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const double apk = a[(p * n) + k];
                    const double aqk = a[(q * n) + k];
                    a[(p * n) + k] = (c * apk) - (s * aqk);
                    a[(q * n) + k] = (s * apk) + (c * aqk);
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const double vkp = vectors[(k * n) + p];
                    const double vkq = vectors[(k * n) + q];
                    vectors[(k * n) + p] = (c * vkp) - (s * vkq);
                    vectors[(k * n) + q] = (s * vkp) + (c * vkq);
                }
            }
        }
    }
    values.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        values[i] = a[(i * n) + i];
    }
}

float squaredDistance(const float* a, const float* b, std::size_t n) {
    float sum = 0.0f;
    for (std::size_t d = 0; d < n; ++d) {
        const float delta = a[d] - b[d];
        sum += delta * delta;
    }
    return sum;
}

} // namespace

std::vector<float> weightedFeatures(const MotionDatabase& db) {
    const std::vector<float> w = motionFeatureWeights(db.config);
    std::vector<float> out(db.features.size());
    const std::size_t dim = db.dimension;
    for (std::size_t i = 0; i < db.features.size(); ++i) {
        const std::size_t d = i % dim;
        out[i] = db.features[i] * std::sqrt(d < w.size() ? w[d] : 1.0f);
    }
    return out;
}

// ---- PCA ---------------------------------------------------------------------------------------

PcaIndex buildPcaIndex(const MotionDatabase& db, std::uint32_t components) {
    PcaIndex index;
    const std::size_t dim = db.dimension;
    const std::size_t n = db.sampleCount();
    index.dimension = static_cast<std::uint32_t>(dim);
    index.components = std::min<std::uint32_t>(components, static_cast<std::uint32_t>(dim));
    if (n == 0 || dim == 0) {
        return index;
    }
    const std::vector<float> x = weightedFeatures(db);
    std::vector<double> mean(dim, 0.0);
    for (std::size_t s = 0; s < n; ++s) {
        for (std::size_t d = 0; d < dim; ++d) {
            mean[d] += x[(s * dim) + d];
        }
    }
    for (double& m : mean) {
        m /= static_cast<double>(n);
    }
    std::vector<double> cov(dim * dim, 0.0);
    for (std::size_t s = 0; s < n; ++s) {
        const float* row = x.data() + (s * dim);
        for (std::size_t i = 0; i < dim; ++i) {
            const double di = row[i] - mean[i];
            for (std::size_t j = i; j < dim; ++j) {
                cov[(i * dim) + j] += di * (row[j] - mean[j]);
            }
        }
    }
    for (std::size_t i = 0; i < dim; ++i) {
        for (std::size_t j = i; j < dim; ++j) {
            cov[(i * dim) + j] /= static_cast<double>(n);
            cov[(j * dim) + i] = cov[(i * dim) + j];
        }
    }
    std::vector<double> values;
    std::vector<double> vectors;
    jacobi(cov, dim, values, vectors);
    std::vector<std::size_t> rank(dim);
    std::iota(rank.begin(), rank.end(), std::size_t{0});
    std::sort(rank.begin(), rank.end(), [&](std::size_t a, std::size_t b) { return values[a] > values[b]; });
    const double total = std::accumulate(values.begin(), values.end(), 0.0, [](double s, double v) { return s + std::max(v, 0.0); });
    index.mean.assign(mean.begin(), mean.end());
    index.basis.resize(static_cast<std::size_t>(index.components) * dim);
    for (std::uint32_t c = 0; c < index.components; ++c) {
        const std::size_t col = rank[c];
        for (std::size_t d = 0; d < dim; ++d) {
            index.basis[(static_cast<std::size_t>(c) * dim) + d] = static_cast<float>(vectors[(d * dim) + col]);
        }
        index.explained.push_back(total > 0.0 ? static_cast<float>(std::max(values[col], 0.0) / total) : 0.0f);
    }
    index.projected.resize(n * index.components);
    for (std::size_t s = 0; s < n; ++s) {
        const float* row = x.data() + (s * dim);
        for (std::uint32_t c = 0; c < index.components; ++c) {
            const float* b = index.basis.data() + (static_cast<std::size_t>(c) * dim);
            float dot = 0.0f;
            for (std::size_t d = 0; d < dim; ++d) {
                dot += (row[d] - index.mean[d]) * b[d];
            }
            index.projected[(s * index.components) + c] = dot;
        }
    }
    return index;
}

MotionMatch searchPca(const MotionDatabase& db, const PcaIndex& index, const MotionQuery& query,
                      const MotionCostWeights& weights, std::uint32_t shortlist) {
    const std::vector<float> wq = weightedQuery(db, query);
    const std::size_t dim = index.dimension;
    const std::uint32_t k = index.components;
    std::vector<float> pq(k, 0.0f);
    for (std::uint32_t c = 0; c < k; ++c) {
        const float* b = index.basis.data() + (static_cast<std::size_t>(c) * dim);
        for (std::size_t d = 0; d < dim; ++d) {
            pq[c] += (wq[d] - index.mean[d]) * b[d];
        }
    }
    std::vector<std::pair<float, std::uint32_t>> heap;
    heap.reserve(shortlist + 1u);
    std::uint32_t rejected = 0;
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        if (!passes(db, query, s)) {
            ++rejected;
            continue;
        }
        const float cost = squaredDistance(pq.data(), index.projected.data() + (static_cast<std::size_t>(s) * k), k);
        if (heap.size() < shortlist) {
            heap.emplace_back(cost, s);
            std::push_heap(heap.begin(), heap.end());
        } else if (!heap.empty() && cost < heap.front().first) {
            std::pop_heap(heap.begin(), heap.end());
            heap.back() = {cost, s};
            std::push_heap(heap.begin(), heap.end());
        }
    }
    std::vector<std::uint32_t> candidates;
    for (const auto& [cost, s] : heap) {
        candidates.push_back(s);
    }
    addContinuation(db, query, candidates);
    MotionMatch out = scoreMotionCandidates(db, query, weights, candidates);
    out.rejected = rejected;
    out.coarseConsidered = db.sampleCount() - rejected;
    return out;
}

// ---- VQ ----------------------------------------------------------------------------------------

VqIndex buildVqIndex(const MotionDatabase& db, std::uint32_t centroids, std::uint32_t iterations) {
    VqIndex index;
    const std::size_t dim = db.dimension;
    const std::size_t n = db.sampleCount();
    index.dimension = static_cast<std::uint32_t>(dim);
    if (n == 0 || dim == 0 || centroids == 0) {
        return index;
    }
    const std::uint32_t k = std::min<std::uint32_t>(centroids, static_cast<std::uint32_t>(n));
    index.centroids = k;
    const std::vector<float> x = weightedFeatures(db);
    // Train on at most 50,000 evenly spaced samples: k-means over every sample of a 400k corpus is
    // minutes, and the centroids of an even subsample are the centroids of the corpus to within the
    // subsample's own resolution.
    const std::size_t trainStep = std::max<std::size_t>(1, n / 50000);
    std::vector<std::size_t> train;
    for (std::size_t s = 0; s < n; s += trainStep) {
        train.push_back(s);
    }
    index.centre.resize(static_cast<std::size_t>(k) * dim);
    for (std::uint32_t c = 0; c < k; ++c) {
        const std::size_t s = train[(static_cast<std::size_t>(c) * train.size()) / k];
        std::copy(x.begin() + static_cast<std::ptrdiff_t>(s * dim), x.begin() + static_cast<std::ptrdiff_t>((s + 1) * dim),
                  index.centre.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(c) * dim));
    }
    const auto nearest = [&](const float* p) {
        std::uint32_t best = 0;
        float bestD = squaredDistance(p, index.centre.data(), dim);
        for (std::uint32_t c = 1; c < k; ++c) {
            const float d = squaredDistance(p, index.centre.data() + (static_cast<std::size_t>(c) * dim), dim);
            if (d < bestD) {
                bestD = d;
                best = c;
            }
        }
        return best;
    };
    std::vector<double> sum(static_cast<std::size_t>(k) * dim);
    std::vector<std::uint32_t> count(k);
    for (std::uint32_t it = 0; it < iterations; ++it) {
        std::fill(sum.begin(), sum.end(), 0.0);
        std::fill(count.begin(), count.end(), 0u);
        for (const std::size_t s : train) {
            const std::uint32_t c = nearest(x.data() + (s * dim));
            ++count[c];
            for (std::size_t d = 0; d < dim; ++d) {
                sum[(static_cast<std::size_t>(c) * dim) + d] += x[(s * dim) + d];
            }
        }
        for (std::uint32_t c = 0; c < k; ++c) {
            if (count[c] == 0) {
                continue; // an empty cluster keeps its centre
            }
            for (std::size_t d = 0; d < dim; ++d) {
                index.centre[(static_cast<std::size_t>(c) * dim) + d] =
                    static_cast<float>(sum[(static_cast<std::size_t>(c) * dim) + d] / count[c]);
            }
        }
    }
    index.lists.assign(k, {});
    for (std::size_t s = 0; s < n; ++s) {
        index.lists[nearest(x.data() + (s * dim))].push_back(static_cast<std::uint32_t>(s));
    }
    return index;
}

MotionMatch searchVq(const MotionDatabase& db, const VqIndex& index, const MotionQuery& query,
                     const MotionCostWeights& weights, std::uint32_t probes) {
    const std::vector<float> wq = weightedQuery(db, query);
    const std::size_t dim = index.dimension;
    std::vector<std::pair<float, std::uint32_t>> order;
    order.reserve(index.centroids);
    for (std::uint32_t c = 0; c < index.centroids; ++c) {
        order.emplace_back(squaredDistance(wq.data(), index.centre.data() + (static_cast<std::size_t>(c) * dim), dim), c);
    }
    const std::uint32_t p = std::min(probes, index.centroids);
    std::partial_sort(order.begin(), order.begin() + p, order.end());
    std::vector<std::uint32_t> candidates;
    for (std::uint32_t i = 0; i < p; ++i) {
        const auto& list = index.lists[order[i].second];
        candidates.insert(candidates.end(), list.begin(), list.end());
    }
    addContinuation(db, query, candidates);
    MotionMatch out = scoreMotionCandidates(db, query, weights, candidates);
    out.coarseConsidered = index.centroids;
    return out;
}

// ---- KD-tree -----------------------------------------------------------------------------------

KdTree buildKdTree(const MotionDatabase& db, std::uint32_t leafSize) {
    KdTree tree;
    const std::size_t dim = db.dimension;
    const std::size_t n = db.sampleCount();
    tree.dimension = static_cast<std::uint32_t>(dim);
    tree.points = weightedFeatures(db);
    tree.order.resize(n);
    std::iota(tree.order.begin(), tree.order.end(), 0u);
    if (n == 0) {
        return tree;
    }
    struct Work {
        std::uint32_t node;
    };
    tree.nodes.push_back({0u, static_cast<std::uint32_t>(n), -1, -1, 0u, 0.0f});
    std::vector<Work> stack{{0u}};
    while (!stack.empty()) {
        const std::uint32_t id = stack.back().node;
        stack.pop_back();
        KdTree::Node node = tree.nodes[id];
        const std::uint32_t count = node.end - node.begin;
        if (count <= leafSize) {
            continue;
        }
        // Split on the axis of greatest spread, at the median.
        std::uint32_t axis = 0;
        float spread = -1.0f;
        for (std::uint32_t d = 0; d < dim; ++d) {
            float lo = 1e30f;
            float hi = -1e30f;
            for (std::uint32_t i = node.begin; i < node.end; ++i) {
                const float v = tree.points[(static_cast<std::size_t>(tree.order[i]) * dim) + d];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            if (hi - lo > spread) {
                spread = hi - lo;
                axis = d;
            }
        }
        const std::uint32_t mid = node.begin + (count / 2u);
        std::nth_element(tree.order.begin() + node.begin, tree.order.begin() + mid, tree.order.begin() + node.end,
                         [&](std::uint32_t a, std::uint32_t b) {
                             return tree.points[(static_cast<std::size_t>(a) * dim) + axis] <
                                    tree.points[(static_cast<std::size_t>(b) * dim) + axis];
                         });
        node.axis = axis;
        node.split = tree.points[(static_cast<std::size_t>(tree.order[mid]) * dim) + axis];
        node.left = static_cast<std::int32_t>(tree.nodes.size());
        tree.nodes.push_back({node.begin, mid, -1, -1, 0u, 0.0f});
        node.right = static_cast<std::int32_t>(tree.nodes.size());
        tree.nodes.push_back({mid, node.end, -1, -1, 0u, 0.0f});
        tree.nodes[id] = node;
        stack.push_back({static_cast<std::uint32_t>(node.left)});
        stack.push_back({static_cast<std::uint32_t>(node.right)});
    }
    return tree;
}

MotionMatch searchKd(const MotionDatabase& db, const KdTree& tree, const MotionQuery& query,
                     const MotionCostWeights& weights, std::uint32_t keep, std::uint32_t* visited) {
    const std::vector<float> wq = weightedQuery(db, query);
    const std::size_t dim = tree.dimension;
    std::vector<std::pair<float, std::uint32_t>> heap; // max-heap of the best `keep`
    std::uint32_t scored = 0;
    const auto worst = [&]() { return heap.size() < keep ? 1e30f : heap.front().first; };
    // Depth first, nearer child first, pruning a far child whose splitting plane is already further
    // than the worst of the best found.
    struct Visit {
        std::int32_t node;
        float bound;
    };
    std::vector<Visit> stack;
    if (!tree.nodes.empty()) {
        stack.push_back({0, 0.0f});
    }
    while (!stack.empty()) {
        const Visit v = stack.back();
        stack.pop_back();
        if (v.bound >= worst()) {
            continue;
        }
        const KdTree::Node& node = tree.nodes[static_cast<std::size_t>(v.node)];
        if (node.left < 0) {
            for (std::uint32_t i = node.begin; i < node.end; ++i) {
                const std::uint32_t s = tree.order[i];
                if (!passes(db, query, s)) {
                    continue;
                }
                ++scored;
                const float d = squaredDistance(wq.data(), tree.points.data() + (static_cast<std::size_t>(s) * dim), dim);
                if (heap.size() < keep) {
                    heap.emplace_back(d, s);
                    std::push_heap(heap.begin(), heap.end());
                } else if (d < heap.front().first) {
                    std::pop_heap(heap.begin(), heap.end());
                    heap.back() = {d, s};
                    std::push_heap(heap.begin(), heap.end());
                }
            }
            continue;
        }
        const float delta = wq[node.axis] - node.split;
        const std::int32_t nearChild = delta < 0.0f ? node.left : node.right;
        const std::int32_t farChild = delta < 0.0f ? node.right : node.left;
        stack.push_back({farChild, std::max(v.bound, delta * delta)});
        stack.push_back({nearChild, v.bound});
    }
    if (visited != nullptr) {
        *visited = scored;
    }
    std::vector<std::uint32_t> candidates;
    for (const auto& [d, s] : heap) {
        candidates.push_back(s);
    }
    addContinuation(db, query, candidates);
    MotionMatch out = scoreMotionCandidates(db, query, weights, candidates);
    out.coarseConsidered = scored;
    return out;
}

} // namespace avgen::scene
