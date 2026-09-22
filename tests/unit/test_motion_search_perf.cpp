// Phase C §50–§55 and §67: performance, measured on real motion, and the search-method matrix.
//
// Hidden (`[.perf]`): it takes minutes, it needs the 100STYLE packs that live only on the machine
// that fetched them (gitignored under assets/, never redistributed), and its product is numbers for
// the phase log rather than a pass/fail. It asserts only what would make those numbers meaningless:
// that each corpus loaded, and that every method found an answer.
//
// **Timing rules** (docs/testing.md): nothing else may run on the machine; each latency is a
// per-query steady_clock measurement over hundreds of queries, reported as percentiles, never a mean
// alone; and the queries are the §20 instrument's own (a sample's features perturbed by the derived
// duplicate radius, with the previous sample as current), so the linear scan is not timed on queries
// that happen to be easy.

#include "assets/gltf_loader.hpp"
#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_database_io.hpp"
#include "scene/motion_library.hpp"
#include "scene/motion_quality_report.hpp"
#include "scene/motion_search_index.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <numeric>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

double microseconds(Clock::duration d) {
    return std::chrono::duration<double, std::micro>(d).count();
}

struct Corpus {
    std::string name;
    scene::MotionPack pack;
    scene::MotionDatabase db;
    double buildSeconds = 0.0;
};

std::optional<scene::MotionPack> readPack(const fs::path& dir) {
    if (!fs::exists(dir)) {
        return std::nullopt;
    }
    auto pack = scene::readMotionPack(dir);
    if (!pack) {
        return std::nullopt;
    }
    return std::move(*pack);
}

scene::MotionPack firstClips(const scene::MotionPack& pack, std::size_t targetSamples) {
    scene::MotionPack out = pack;
    out.clips.clear();
    out.animation.clear();
    std::size_t samples = 0;
    for (std::size_t c = 0; c < pack.clips.size() && samples < targetSamples; ++c) {
        out.clips.push_back(pack.clips[c]);
        out.animation.push_back(pack.animation[c]);
        samples += static_cast<std::size_t>(pack.clips[c].length * 30.0f) + 1u;
    }
    return out;
}

scene::MotionPack concat(const scene::MotionPack& a, const scene::MotionPack& b) {
    scene::MotionPack out = a;
    const auto offset = static_cast<std::uint32_t>(out.provenance.size());
    out.provenance.insert(out.provenance.end(), b.provenance.begin(), b.provenance.end());
    for (std::size_t c = 0; c < b.clips.size(); ++c) {
        scene::PackClip meta = b.clips[c];
        meta.provenance += offset;
        out.clips.push_back(meta);
        out.animation.push_back(b.animation[c]);
    }
    return out;
}

Corpus build(std::string name, scene::MotionPack pack, scene::MotionFeatureConfig config) {
    Corpus c;
    c.name = std::move(name);
    c.pack = std::move(pack);
    scene::MotionDatabaseOptions options;
    options.sampleRate = 30.0f;
    options.config = std::move(config);
    const auto t0 = Clock::now();
    auto db = scene::buildMotionDatabase(c.pack, options);
    c.buildSeconds = std::chrono::duration<double>(Clock::now() - t0).count();
    REQUIRE(db.has_value());
    c.db = std::move(*db);
    return c;
}

std::vector<scene::MotionQuery> realisticQueries(const scene::MotionDatabase& db, std::size_t count) {
    const scene::MotionQualitySummary quality = scene::analyseMotionQuality(db);
    const float nudge =
        quality.duplicateRadius > 0.0f ? quality.duplicateRadius / std::sqrt(static_cast<float>(db.dimension)) : 0.05f;
    std::vector<scene::MotionQuery> out;
    const std::size_t step = std::max<std::size_t>(1, db.sampleCount() / count);
    for (std::uint32_t s = 1; s < db.sampleCount() && out.size() < count; s += static_cast<std::uint32_t>(step)) {
        scene::MotionQuery q;
        q.features.assign(db.featuresFor(s), db.featuresFor(s) + db.dimension);
        for (std::size_t d = 0; d < q.features.size(); d += 3) {
            q.features[d] += nudge;
        }
        q.current = s - 1u;
        out.push_back(std::move(q));
    }
    return out;
}

struct Percentiles {
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double worst = 0.0;
};

Percentiles percentiles(std::vector<double> v) {
    Percentiles p;
    if (v.empty()) {
        return p;
    }
    std::sort(v.begin(), v.end());
    const auto at = [&](double q) { return v[std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size() - 1) + 0.5))]; };
    p.p50 = at(0.50);
    p.p95 = at(0.95);
    p.p99 = at(0.99);
    p.worst = v.back();
    return p;
}

using Search = std::function<scene::MotionMatch(const scene::MotionQuery&)>;

// The pose-only distance between two samples: the joint-position dimensions, weighted as the cost
// weights them. What a switch to `chosen` instead of carrying on costs in pose, which is what a
// transition shows.
float poseGap(const scene::MotionDatabase& db, std::uint32_t a, std::uint32_t b) {
    const auto layout = scene::motionFeatureLayout(db.config);
    const auto weights = scene::motionFeatureWeights(db.config);
    float sum = 0.0f;
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] == scene::MotionFeatureGroup::JointPosition) {
            const float delta = db.featuresFor(a)[d] - db.featuresFor(b)[d];
            sum += delta * delta * weights[d];
        }
    }
    return std::sqrt(sum);
}

} // namespace

TEST_CASE("§50-§55: the search-method matrix on real motion", "[.perf][motionperf][phaseC]") {
    const fs::path assets = fs::path(AVGEN_SOURCE_DIR) / "assets";
    const auto fw = readPack(assets / "100style-fw-pack");
    const auto mixed = readPack(assets / "100style-mixed-pack");
    if (!fw || !mixed) {
        SKIP("the 100STYLE FW and MIXED packs are not present (see assets/100STYLE-ATTRIBUTION.md)");
    }
    const auto config = [] {
        auto c = scene::defaultBipedConfig("LeftAnkle", "RightAnkle", "Head");
        c.trajectoryTimes = {0.2f, 0.4f, 0.6f};
        return c;
    }();
    std::vector<Corpus> corpora;
    corpora.push_back(build("100STYLE 10k", firstClips(*fw, 10000), config));
    corpora.push_back(build("100STYLE 100k", firstClips(*fw, 100000), config));
    corpora.push_back(build("100STYLE FW 434k", *fw, config));
    corpora.push_back(build("FW+MIXED 714k", concat(*fw, *mixed), config));

    std::string report = "corpus               samples  build s  samples/s   MB  | method                      p50 us   p95 us   p99 us  recall  excess/gap  poseGap  index MB\n";
    for (const Corpus& c : corpora) {
        const scene::MotionDatabase& db = c.db;
        const auto queries = realisticQueries(db, 250);
        const scene::MotionCostWeights weights;
        // The typical gap, by the sweep definition: a sample halfway across the corpus against the best.
        double spread = 0.0;
        std::vector<scene::MotionMatch> exact;
        for (const auto& q : queries) {
            exact.push_back(scene::searchMotion(db, q, weights));
            const std::uint32_t other = (q.current + db.sampleCount() / 2u) % db.sampleCount();
            scene::MotionQuery o = q;
            (void)o;
            const std::vector<std::uint32_t> one{other};
            spread += static_cast<double>(scene::scoreMotionCandidates(db, q, weights, one).cost) -
                      static_cast<double>(exact.back().cost);
        }
        spread /= static_cast<double>(std::max<std::size_t>(queries.size(), 1));

        const auto pcaT0 = Clock::now();
        const scene::PcaIndex pca = scene::buildPcaIndex(db, 8);
        const double pcaBuild = std::chrono::duration<double>(Clock::now() - pcaT0).count();
        const auto vqT0 = Clock::now();
        const scene::VqIndex vq = scene::buildVqIndex(db, 256);
        const double vqBuild = std::chrono::duration<double>(Clock::now() - vqT0).count();
        const auto kdT0 = Clock::now();
        const scene::KdTree kd = scene::buildKdTree(db, 16);
        const double kdBuild = std::chrono::duration<double>(Clock::now() - kdT0).count();
        std::uint64_t kdVisited = 0;

        scene::MotionSearchPlan stride4;
        stride4.stride = 4;
        stride4.shortlist = 32;
        stride4.neighbourhood = 4;
        struct Method {
            const char* name;
            Search search;
            std::size_t indexBytes;
        };
        const std::vector<Method> methods = {
            {"linear (exact)", [&](const scene::MotionQuery& q) { return scene::searchMotion(db, q, weights); }, 0},
            {"stride 4, full prefix, 32", [&](const scene::MotionQuery& q) { return scene::searchMotionStaged(db, q, weights, stride4); }, 0},
            {"PCA 8 comps, top 256", [&](const scene::MotionQuery& q) { return scene::searchPca(db, pca, q, weights, 256); }, pca.bytes()},
            {"VQ 256 cells, 8 probes", [&](const scene::MotionQuery& q) { return scene::searchVq(db, vq, q, weights, 8); }, vq.bytes()},
            {"KD-tree, keep 32", [&](const scene::MotionQuery& q) {
                 std::uint32_t visited = 0;
                 auto m = scene::searchKd(db, kd, q, weights, 32, &visited);
                 kdVisited += visited;
                 return m;
             }, kd.bytes()},
        };
        const double dbMB = static_cast<double>((db.features.size() + db.mean.size() + db.scale.size()) * sizeof(float) +
                                                db.sampleCount() * (4u * sizeof(std::uint32_t) + 2u * sizeof(float))) /
                            1048576.0;
        bool first = true;
        for (const Method& m : methods) {
            std::vector<double> times;
            int agreed = 0;
            double worst = 0.0;
            double gapSum = 0.0;
            int gapN = 0;
            kdVisited = 0;
            for (std::size_t i = 0; i < queries.size(); ++i) {
                const auto t0 = Clock::now();
                const scene::MotionMatch got = m.search(queries[i]);
                times.push_back(microseconds(Clock::now() - t0));
                REQUIRE(got.found());
                if (got.sample == exact[i].sample) {
                    ++agreed;
                } else {
                    worst = std::max(worst, static_cast<double>(got.cost) - static_cast<double>(exact[i].cost));
                }
                const std::uint32_t follow = db.sampleNext[queries[i].current];
                if (follow != scene::MotionDatabase::kInvalid && got.sample != follow) {
                    gapSum += poseGap(db, got.sample, follow);
                    ++gapN;
                }
            }
            const Percentiles p = percentiles(times);
            report += fmt::format("{:<20} {:>7}  {:>7.2f}  {:>9.0f}  {:>5.1f} | {:<26} {:>8.1f} {:>8.1f} {:>8.1f}  {:>5.1f}%  {:>9.3f}x  {:>7.3f}  {:>7.2f}\n",
                                  first ? c.name : "", first ? std::to_string(db.sampleCount()) : "",
                                  first ? c.buildSeconds : 0.0, first ? db.sampleCount() / std::max(c.buildSeconds, 1e-9) : 0.0,
                                  first ? dbMB : 0.0, m.name, p.p50, p.p95, p.p99,
                                  100.0 * agreed / static_cast<double>(queries.size()), worst / std::max(spread, 1e-9),
                                  gapN > 0 ? gapSum / gapN : 0.0, static_cast<double>(m.indexBytes) / 1048576.0);
            if (std::string(m.name).rfind("KD", 0) == 0) {
                report += fmt::format("{:<20} {:>7}  {:>7}  {:>9}  {:>5} |   KD-tree scored {:.1f}% of the corpus per query on average\n", "", "", "", "", "",
                                      100.0 * static_cast<double>(kdVisited) / (static_cast<double>(queries.size()) * db.sampleCount()));
            }
            first = false;
        }
        report += fmt::format("{:<20} index build: PCA {:.2f} s ({:.1f}% of variance in 8 comps), VQ {:.2f} s, KD {:.2f} s\n", "",
                              pcaBuild, 100.0 * std::accumulate(pca.explained.begin(), pca.explained.end(), 0.0f), vqBuild, kdBuild);
    }
    WARN(report);
}

TEST_CASE("§50/§51/§67: many characters sharing one database, CPU per frame", "[.perf][motionperf][phaseC]") {
    const fs::path assets = fs::path(AVGEN_SOURCE_DIR) / "assets";
    const auto fw = readPack(assets / "100style-fw-pack");
    const auto mixed = readPack(assets / "100style-mixed-pack");
    if (!fw || !mixed) {
        SKIP("the 100STYLE FW and MIXED packs are not present");
    }
    auto config = scene::defaultBipedConfig("LeftAnkle", "RightAnkle", "Head");
    config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
    struct Row {
        int characters;
        std::size_t samples; // 0 = FW+MIXED
    };
    // §51's grid, with 1M replaced by the largest real corpus here (714k). Synthetic samples would
    // make the numbers about white noise, which ADR-540 measured as the wrong distribution.
    const std::vector<Row> grid = {{1, 10000}, {10, 100000}, {50, 100000}, {100, 0}, {1, 100000}, {10, 0}};
    std::string report = "characters  samples   frame ms p50   p95     worst   searches/frame  us/search\n";
    for (const Row& row : grid) {
        const scene::MotionPack pack = row.samples == 0 ? concat(*fw, *mixed) : firstClips(*fw, row.samples);
        scene::MotionDatabaseOptions options;
        options.sampleRate = 30.0f;
        options.config = config;
        auto db = scene::buildMotionDatabase(pack, options);
        REQUIRE(db.has_value());
        entity::MatchMotionProvider matcher(&*db, &pack.animation, "match");
        std::vector<entity::MotionMemory> memory(static_cast<std::size_t>(row.characters));
        std::vector<double> frameMs;
        const float dt = 1.0f / 60.0f;
        const std::uint64_t searchesBefore = matcher.counters().searches;
        for (int f = 0; f < 120; ++f) {
            const auto t0 = Clock::now();
            for (int c = 0; c < row.characters; ++c) {
                // Each character wants something different: speed, heading and a phase offset in
                // time, so their searches do not all land on the same frame.
                entity::MotionRequest request;
                const float yaw = 0.37f * static_cast<float>(c) + 0.3f * std::sin(0.05f * static_cast<float>(f + c));
                const float speed = 0.6f + 1.5f * (0.5f + 0.5f * std::sin(0.7f * static_cast<float>(c) + 0.02f * static_cast<float>(f)));
                request.desiredVelocity = glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw)) * speed;
                request.bodyFacing = glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
                entity::MotionMemory next;
                const double time = (static_cast<double>(f) + 0.37 * c) / 60.0;
                (void)matcher.advance(request, memory[static_cast<std::size_t>(c)], time, dt, next);
                memory[static_cast<std::size_t>(c)] = next;
            }
            frameMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
        }
        const std::uint64_t searches = matcher.counters().searches - searchesBefore;
        const Percentiles p = percentiles(frameMs);
        const double total = std::accumulate(frameMs.begin(), frameMs.end(), 0.0);
        report += fmt::format("{:>10}  {:>7}   {:>10.3f} {:>7.3f} {:>8.3f}   {:>14.2f}  {:>9.1f}\n", row.characters,
                              db->sampleCount(), p.p50, p.p95, p.worst, static_cast<double>(searches) / 120.0,
                              searches > 0 ? 1000.0 * total / static_cast<double>(searches) : 0.0);
    }
    WARN(report);
}

TEST_CASE("§50/§53: database load, and what contiguous storage buys", "[.perf][motionperf][phaseC]") {
    const fs::path assets = fs::path(AVGEN_SOURCE_DIR) / "assets";
    const auto fw = readPack(assets / "100style-fw-pack");
    if (!fw) {
        SKIP("the 100STYLE FW pack is not present");
    }
    auto config = scene::defaultBipedConfig("LeftAnkle", "RightAnkle", "Head");
    config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
    std::string report;
    // ---- load: write once, read twice (the first read pays the file system, the second is warm) ----
    {
        scene::MotionDatabaseOptions options;
        options.config = config;
        auto db = scene::buildMotionDatabase(*fw, options);
        REQUIRE(db.has_value());
        const fs::path file = fs::temp_directory_path() / "avgen-perf-fw.motiondb";
        REQUIRE(scene::writeMotionDatabase(*db, file).has_value());
        const auto t0 = Clock::now();
        auto first = scene::readMotionDatabase(file);
        const double firstMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        const auto t1 = Clock::now();
        auto second = scene::readMotionDatabase(file);
        const double secondMs = std::chrono::duration<double, std::milli>(Clock::now() - t1).count();
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        report += fmt::format("load: {} samples, {:.1f} MB file; first read {:.1f} ms, second read {:.1f} ms (verified both times)\n",
                              db->sampleCount(), static_cast<double>(fs::file_size(file)) / 1048576.0, firstMs, secondMs);
        std::error_code ec;
        fs::remove(file, ec);
    }
    // ---- §53: ns per sample across corpus sizes, and contiguous against scattered -------------------
    for (const std::size_t target : {std::size_t{2000}, std::size_t{20000}, std::size_t{100000}, std::size_t{0}}) {
        const scene::MotionPack pack = target == 0 ? *fw : firstClips(*fw, target);
        scene::MotionDatabaseOptions options;
        options.config = config;
        auto db = scene::buildMotionDatabase(pack, options);
        REQUIRE(db.has_value());
        const auto queries = realisticQueries(*db, 100);
        std::vector<double> contiguous;
        for (const auto& q : queries) {
            const auto t0 = Clock::now();
            (void)scene::searchMotion(*db, q, {});
            contiguous.push_back(1000.0 * microseconds(Clock::now() - t0) / db->sampleCount());
        }
        // The same features, one heap allocation per sample, visited through pointers: the
        // "pointer-heavy" layout §53 says to prefer contiguous arrays over, measured rather than assumed.
        std::vector<std::unique_ptr<float[]>> scattered;
        scattered.reserve(db->sampleCount());
        for (std::uint32_t s = 0; s < db->sampleCount(); ++s) {
            auto row = std::make_unique<float[]>(db->dimension + 13u); // odd size, so rows do not pack
            std::copy(db->featuresFor(s), db->featuresFor(s) + db->dimension, row.get());
            scattered.push_back(std::move(row));
        }
        // Shuffle the visiting order the way a pointer graph would: rows in allocation order are
        // nearly contiguous anyway, so visit them in a fixed pseudo-random order.
        std::vector<std::uint32_t> order(db->sampleCount());
        std::iota(order.begin(), order.end(), 0u);
        for (std::size_t i = order.size(); i > 1; --i) {
            std::swap(order[i - 1], order[(i * 2654435761u) % i]);
        }
        const auto weights = scene::motionFeatureWeights(db->config);
        std::vector<double> pointer;
        float sink = 0.0f;
        for (const auto& q : queries) {
            const auto t0 = Clock::now();
            float best = 1e30f;
            for (const std::uint32_t s : order) {
                const float* f = scattered[s].get();
                float cost = 0.0f;
                for (std::size_t d = 0; d < db->dimension; ++d) {
                    cost += scene::motionFeatureTerm(q.features[d] - f[d], weights[d]);
                }
                best = std::min(best, cost);
            }
            sink += best;
            pointer.push_back(1000.0 * microseconds(Clock::now() - t0) / db->sampleCount());
        }
        const Percentiles a = percentiles(contiguous);
        const Percentiles b = percentiles(pointer);
        report += fmt::format("{:>7} samples ({:>6.1f} MB of features): contiguous {:.2f} ns/sample p50 ({:.2f} p95); "
                              "scattered and shuffled {:.2f} ns/sample p50 ({:.2f} p95), {:.1f}x  [checksum {:.3f}]\n",
                              db->sampleCount(), static_cast<double>(db->features.size() * sizeof(float)) / 1048576.0,
                              a.p50, a.p95, b.p50, b.p95, b.p50 / std::max(a.p50, 1e-9), sink);
    }
    WARN(report);
}
