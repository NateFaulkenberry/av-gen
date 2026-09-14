#include "scene/candidate_search.hpp"
#include "scene/tree_evaluator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <set>

using namespace avgen;
using namespace avgen::scene;

namespace {

// A design space narrow enough that a search over it finishes in a few seconds. The shipping space
// is wider; what is under test is the machinery, not the art direction.
TreeDesignSpace testSpace() {
    TreeDesignSpace space;
    space.fixed.markerCount = 7000;
    space.fixed.iterations = 30;
    space.fixed.internodeLength = 0.62f;
    space.fixed.tipRadius = 0.046f;
    return space;
}

} // namespace

TEST_CASE("a metric band is bounded on both sides", "[tree][score]") {
    const MetricBand band{0.0f, 0.4f, 0.6f, 1.0f, 0.05f};
    CHECK(band.score(0.5f) == 1.0f);
    CHECK(band.score(0.4f) == 1.0f);
    CHECK(band.score(0.6f) == 1.0f);
    // The whole point: overshooting the ideal loses points exactly as undershooting does. A monotone
    // component would score 1.5 the same as 0.6.
    CHECK(band.score(1.2f) < band.score(0.6f));
    CHECK(band.score(-0.2f) < band.score(0.4f));
    CHECK(band.score(0.2f) > band.score(0.05f));
    CHECK(band.score(0.8f) > band.score(0.95f));
}

TEST_CASE("an unbounded band is refused at construction", "[tree][score]") {
    // This is the rule enforced rather than documented. An infinite bound is exactly how a band
    // becomes "more is better", which is the failure the whole design exists to prevent.
    MetricBand unbounded{0.0f, 0.4f, 0.6f, std::numeric_limits<float>::infinity(), 0.0f};
    CHECK_FALSE(unbounded.validate("test").has_value());
    MetricBand misordered{0.5f, 0.4f, 0.6f, 1.0f, 0.0f};
    CHECK_FALSE(misordered.validate("test").has_value());
    MetricBand fine{0.0f, 0.4f, 0.6f, 1.0f, 0.05f};
    CHECK(fine.validate("test").has_value());
}

TEST_CASE("every shipped score component is bounded and weighted", "[tree][score]") {
    float total = 0.0f;
    for (const ScoreComponent& c : treeScoreComponents()) {
        INFO("component " << c.name);
        REQUIRE(c.band.validate(c.name).has_value());
        CHECK(c.weight > 0.0f);
        // A component with no stated rationale is a number nobody can argue with, which is how an
        // aesthetic evaluator quietly stops meaning anything.
        CHECK_FALSE(c.rationale.empty());
        total += c.weight;
    }
    INFO("total weight " << total);
    CHECK(treeScoreComponents().size() == 14);
}

TEST_CASE("the same candidate scores the same twice", "[tree][score][determinism]") {
    const TreeSubject subject(testSpace(), TreeCameraView{});
    const std::vector<float> u = haltonPoint(3, subject.dimensions(), 1);
    const nlohmann::json parameters = subject.sample(u);

    CandidateRecord a;
    CandidateRecord b;
    REQUIRE(subject.measure(parameters, a).has_value());
    REQUIRE(subject.measure(parameters, b).has_value());
    scoreCandidate(subject, a);
    scoreCandidate(subject, b);

    INFO(fmt::format("score {:.6f}", a.score));
    REQUIRE(a.metrics.size() == b.metrics.size());
    for (std::size_t i = 0; i < a.metrics.size(); ++i) {
        INFO("metric " << a.metrics[i].name);
        REQUIRE(a.metrics[i].name == b.metrics[i].name);
        REQUIRE(a.metrics[i].value == b.metrics[i].value);
        REQUIRE(a.metrics[i].score == b.metrics[i].score);
    }
    REQUIRE(a.score == b.score);
    REQUIRE(a.features == b.features);
}

TEST_CASE("the evaluator separates trees a person would separate", "[tree][score]") {
    // Not a claim that the score is correct -- that needs the contact sheet -- but a check that it
    // discriminates at all. A deliberately bad tree must not outscore a deliberately reasonable one.
    const TreeCameraView camera;
    const TreeDesignSpace space = testSpace();

    TreeParams good = space.fixed;
    good.crown.baseHeight = 7.0f;
    good.crown.centreHeight = 13.5f;
    good.crown.halfHeight = 6.0f;
    good.crown.radius = 9.0f;

    // A pole: a crown so narrow the tree cannot branch sideways.
    TreeParams pole = good;
    pole.crown.radius = 1.6f;
    pole.crown.trunkCorridorRadius = 0.5f;

    // A ground-level bush: no bole at all.
    TreeParams bush = good;
    bush.crown.baseHeight = 0.8f;
    bush.crown.centreHeight = 4.0f;
    bush.crown.halfHeight = 3.2f;
    bush.crown.radius = 10.0f;

    const auto scoreOf = [&](const TreeParams& p) {
        const auto graph = generateTree(p);
        REQUIRE(graph.has_value());
        const TreeSilhouette mask = rasteriseTree(*graph, camera);
        const TreeMeasurements measured = measureTree(*graph, mask, camera);
        CandidateRecord record;
        record.metrics = measured.asMetrics();
        const TreeSubject subject(space, camera);
        return std::pair{scoreCandidate(subject, record), record};
    };

    const auto [goodScore, goodRecord] = scoreOf(good);
    const auto [poleScore, poleRecord] = scoreOf(pole);
    const auto [bushScore, bushRecord] = scoreOf(bush);
    const auto line = [](const char* name, float score, const CandidateRecord& r) {
        std::string s = fmt::format("{:<5} score {:.3f} |", name, score);
        for (const ScoredMetric& m : r.metrics) {
            s += fmt::format(" {}={:.2f}({:.2f})", m.name, m.value, m.score);
        }
        return s;
    };
    INFO(line("good", goodScore, goodRecord));
    INFO(line("pole", poleScore, poleRecord));
    INFO(line("bush", bushScore, bushRecord));
    CHECK(goodScore > poleScore);
    CHECK(goodScore > bushScore);
}

TEST_CASE("the search is deterministic and returns distinct candidates", "[tree][search]") {
    const TreeSubject subject(testSpace(), TreeCameraView{});
    SearchSettings settings;
    settings.population = 6;
    settings.generations = 2;
    settings.elite = 3;
    settings.seed = 5;

    const auto first = searchCandidates(subject, settings);
    const auto second = searchCandidates(subject, settings);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->all.size() == second->all.size());
    for (std::size_t i = 0; i < first->all.size(); ++i) {
        INFO("candidate " << i);
        REQUIRE(first->all[i].score == second->all[i].score);
        REQUIRE(first->all[i].parameters == second->all[i].parameters);
    }
    REQUIRE(first->ranked.size() == second->ranked.size());
    REQUIRE_FALSE(first->ranked.empty());

    // Ranked best-first.
    for (std::size_t i = 1; i < first->ranked.size(); ++i) {
        CHECK(first->ranked[i - 1].score >= first->ranked[i].score);
    }
    // Distinct parameter sets. A search that returns the same object repeatedly has a diversity
    // filter that is not working, and the contact sheet it feeds is useless.
    std::set<std::string> seen;
    for (const CandidateRecord& r : first->ranked) {
        seen.insert(r.parameters.dump());
    }
    CHECK(seen.size() == first->ranked.size());

    INFO(fmt::format("{} candidates, {} rejected, best {:.3f}, worst {:.3f}, {:.0f} ms", first->all.size(),
                     first->rejected, first->ranked.front().score, first->ranked.back().score, first->totalMs));
    CHECK(first->rejected == 0);
}

TEST_CASE("the winning parameters regenerate the winning tree", "[tree][search][serialisation]") {
    // Quality gate 12, end to end: the search's output is a JSON parameter block, and that block
    // alone must reproduce the tree -- not a cached graph, not a remembered seed.
    const TreeSubject subject(testSpace(), TreeCameraView{});
    SearchSettings settings;
    settings.population = 5;
    settings.generations = 1;
    settings.seed = 9;
    const auto result = searchCandidates(subject, settings);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->ranked.empty());

    const nlohmann::json winner = result->ranked.front().parameters;
    const std::string serialised = winner.dump();
    const auto reloaded = TreeParams::fromJson(nlohmann::json::parse(serialised));
    REQUIRE(reloaded.has_value());
    const auto regenerated = generateTree(*reloaded);
    REQUIRE(regenerated.has_value());

    const auto original = generateTree(*TreeParams::fromJson(winner));
    REQUIRE(original.has_value());
    CHECK(original->contentHash() == regenerated->contentHash());
    CHECK(regenerated->validateTopology().has_value());
}

TEST_CASE("tree probe: a candidate search, reported", "[.tree-probe]") {
    TreeDesignSpace space = testSpace();
    space.fixed.markerCount = 26000;
    space.fixed.iterations = 32;
    space.fixed.internodeLength = 0.38f;
    space.fixed.tipRadius = 0.028f;
    const TreeSubject subject(space, TreeCameraView{});
    SearchSettings settings;
    settings.population = 24;
    settings.generations = 3;
    settings.elite = 8;
    settings.seed = 11;
    const auto result = searchCandidates(subject, settings);
    REQUIRE(result.has_value());
    WARN(fmt::format("{} candidates, {} rejected, {} distinct after diversity, {:.1f} s",
                     result->all.size(), result->rejected, result->ranked.size(), result->totalMs / 1000.0));
    for (std::size_t i = 0; i < std::min<std::size_t>(6, result->ranked.size()); ++i) {
        const CandidateRecord& r = result->ranked[i];
        std::string s = fmt::format("#{} gen{} score {:.3f} |", r.index, r.generation, r.score);
        for (const ScoredMetric& m : r.metrics) {
            s += fmt::format(" {}={:.2f}", m.name, m.value);
        }
        WARN(s);
    }
}
