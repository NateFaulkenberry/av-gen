#include "scene/tree_generator.hpp"
#include "scene/tree_mesh.hpp"
#include "search/candidate_search.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <set>

using namespace avgen;
using namespace avgen::scene;

namespace {

// A coarser tree than the showcase one, at the same marker density so the competition -- which is
// what the model turns on -- is unchanged. See the note in test_tree.cpp for why coarser rather
// than smaller.
TreeParams coarseParams(std::uint32_t seed = 7) {
    TreeParams p;
    p.seed = seed;
    p.internodeLength = 0.78f;
    p.markerCount = 9000;
    p.iterations = 30;
    p.tipRadius = 0.050f;
    return p;
}

} // namespace

TEST_CASE("every branch axis becomes one unbroken swept tube", "[tree][mesh]") {
    const auto graph = generateTree(coarseParams());
    REQUIRE(graph.has_value());
    const auto meshes = buildTreeMeshes(*graph, TreeMeshSettings{});
    REQUIRE(meshes.has_value());

    int emptyParts = 0;
    for (const auto& [role, mesh] : meshes->parts()) {
        INFO("part " << role << ": " << mesh->vertices.size() << " verts, " << mesh->indices.size() / 3 << " tris");
        if (mesh->vertices.empty()) {
            ++emptyParts;
            continue;
        }
        REQUIRE(mesh->valid());
        for (std::uint32_t index : mesh->indices) {
            REQUIRE(index < mesh->vertices.size());
        }
        for (const Vertex& v : mesh->vertices) {
            REQUIRE(std::isfinite(v.position.x));
            REQUIRE(std::isfinite(v.position.y));
            REQUIRE(std::isfinite(v.position.z));
            // A zero normal shades black and is the usual symptom of a degenerate sweep row.
            REQUIRE(glm::length(v.normal) > 0.5f);
        }
    }
    CHECK(emptyParts == 0);
    INFO("total " << meshes->triangles << " triangles in " << meshes->buildMs << " ms");
    CHECK(meshes->triangles > 1000);
}

TEST_CASE("the meshes pass the shared hygiene gate", "[tree][mesh]") {
    // The same gate the candidate search applies, run here so a geometry regression fails in the
    // geometry test rather than as a mysterious rejection rate in the search.
    const auto graph = generateTree(coarseParams(11));
    REQUIRE(graph.has_value());
    const auto meshes = buildTreeMeshes(*graph, TreeMeshSettings{});
    REQUIRE(meshes.has_value());
    const search::HygieneLimits limits;
    for (const auto& [role, mesh] : meshes->parts()) {
        if (mesh->vertices.empty()) {
            continue;
        }
        const auto rejection = search::meshHygiene(*mesh, limits);
        INFO(role << (rejection ? ": " + rejection->rule + " -- " + rejection->detail : ": clean"));
        CHECK_FALSE(rejection.has_value());
    }
}

TEST_CASE("mesh generation is deterministic", "[tree][mesh][determinism]") {
    const auto graph = generateTree(coarseParams(3));
    REQUIRE(graph.has_value());
    const auto a = buildTreeMeshes(*graph, TreeMeshSettings{});
    const auto b = buildTreeMeshes(*graph, TreeMeshSettings{});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->triangles == b->triangles);
    const auto partsA = a->parts();
    const auto partsB = b->parts();
    for (std::size_t i = 0; i < partsA.size(); ++i) {
        REQUIRE(partsA[i].second->vertices.size() == partsB[i].second->vertices.size());
        for (std::size_t v = 0; v < partsA[i].second->vertices.size(); ++v) {
            REQUIRE(partsA[i].second->vertices[v].position == partsB[i].second->vertices[v].position);
        }
    }
}

TEST_CASE("appendMesh offsets indices and preserves both meshes", "[tree][mesh]") {
    MeshData a;
    a.vertices = {Vertex{{0, 0, 0}, {0, 1, 0}, {0, 0}}, Vertex{{1, 0, 0}, {0, 1, 0}, {1, 0}},
                  Vertex{{0, 0, 1}, {0, 1, 0}, {0, 1}}};
    a.indices = {0, 1, 2};
    const MeshData b = a;
    appendMesh(a, b);
    REQUIRE(a.vertices.size() == 6);
    REQUIRE(a.indices.size() == 6);
    CHECK(a.indices[3] == 3);
    CHECK(a.indices[4] == 4);
    CHECK(a.indices[5] == 5);
}

TEST_CASE("the tree generator satisfies the shared contract", "[tree][search]") {
    // The concept is checked at compile time by a static_assert in the header; this checks the
    // behaviour behind it -- that the schema validates and that the four members agree on sizes.
    const TreeGenerator generator;
    REQUIRE(generator.schema().validate().has_value());
    CHECK(generator.schema().generatorName == "tree");
    CHECK(generator.schema().parameters.size() == 19);

    const search::Parameters params = search::sampleAt(generator.schema().parameters, 0);
    REQUIRE(params.size() == generator.schema().parameters.size());
    const auto subject = generator.build(params);
    REQUIRE(subject.has_value());
    REQUIRE_FALSE(subject->parts.empty());
    CHECK(subject->framingRadius > 1.0f);

    // One part per tier, each a name the editor will show.
    std::set<std::string> roles;
    for (const search::SubjectPart& part : subject->parts) {
        CHECK_FALSE(part.role.empty());
        CHECK(roles.insert(part.role).second); // unique within a Subject
    }
    for (const std::string& role : roles) {
        INFO("role " << role);
    }
    CHECK(roles.count("trunk") == 1);
    CHECK(roles.count("foliage0") == 1);
    // The accent tint is allowed to be absent from any one individual -- it is a region of a noise
    // field, and a small crown may not contain one. What must not happen is that it is absent from
    // EVERY individual, which is what a badly scaled field produces and what raw fbm3 did produce.
    int withAccent = 0;
    for (std::uint32_t index = 0; index < 8; ++index) {
        const auto probe = generator.build(search::sampleAt(generator.schema().parameters, index));
        REQUIRE(probe.has_value());
        for (const search::SubjectPart& part : probe->parts) {
            if (part.role == "foliage2" && !part.mesh.vertices.empty()) {
                ++withAccent;
            }
        }
    }
    INFO(withAccent << " of 8 candidates carry the accent tint");
    CHECK(withAccent >= 3);

    const auto features = generator.features(*subject, params);
    CHECK(features.size() == generator.schema().featureNames.size());
    const auto scores = generator.domainScores(*subject, params);
    CHECK(scores.size() == treeBands().size());
    for (const search::ScoreComponent& c : scores) {
        INFO("component " << c.name << " raw " << c.raw << " score " << c.score);
        CHECK(c.score >= 0.0f);
        CHECK(c.score <= 1.0f);
        CHECK(c.weight > 0.0f);
    }
}

TEST_CASE("every band is bounded on both sides and carries its argument", "[tree][score]") {
    float total = 0.0f;
    for (const TreeBand& band : treeBands()) {
        INFO("band " << band.name);
        REQUIRE(band.band.validate().has_value());
        CHECK(band.weight > 0.0f);
        // A component with no stated rationale is a number nobody can argue with, which is how an
        // aesthetic evaluator quietly stops meaning anything.
        CHECK(band.rationale.size() > 40);
        // The rule, checked rather than assumed: overshooting the ideal must cost as much as
        // undershooting it. A monotone component would score these equal.
        const float mid = 0.5f * (band.band.lowPlateau + band.band.highPlateau);
        CHECK(band.band(mid) > band.band(band.band.highEdge + 1.0f));
        CHECK(band.band(mid) > band.band(band.band.lowEdge - 1.0f));
        total += band.weight;
    }
    INFO("total weight " << total);
    CHECK(treeBands().size() == 15);
}

TEST_CASE("the same index always gives the same candidate", "[tree][search][determinism]") {
    const TreeGenerator generator;
    const auto first = search::sampleAt(generator.schema().parameters, 17);
    const auto second = search::sampleAt(generator.schema().parameters, 17);
    REQUIRE(first == second);

    const auto a = generator.build(first);
    const auto b = generator.build(second);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    const auto scoresA = generator.domainScores(*a, first);
    const auto scoresB = generator.domainScores(*b, second);
    REQUIRE(scoresA.size() == scoresB.size());
    for (std::size_t i = 0; i < scoresA.size(); ++i) {
        INFO("component " << scoresA[i].name);
        REQUIRE(scoresA[i].raw == scoresB[i].raw);
        REQUIRE(scoresA[i].score == scoresB[i].score);
    }
    REQUIRE(generator.features(*a, first) == generator.features(*b, second));
}

TEST_CASE("a candidate round-trips through the shared record", "[tree][search][serialisation]") {
    // Quality gate 12 under the shared representation: no mesh is stored, so the record has to
    // regenerate the individual from (schema, index) alone.
    const TreeGenerator generator;
    search::Candidate candidate;
    candidate.index = 23;
    candidate.parameters = search::sampleAt(generator.schema().parameters, candidate.index);
    const auto subject = generator.build(candidate.parameters);
    REQUIRE(subject.has_value());
    candidate.score.components = generator.domainScores(*subject, candidate.parameters);
    candidate.features = generator.features(*subject, candidate.parameters);

    const nlohmann::json record = search::candidateToJson(generator.schema(), candidate, "chosen for the report");
    const auto restored = search::candidateFromJson(generator.schema(), nlohmann::json::parse(record.dump()));
    REQUIRE(restored.has_value());
    REQUIRE(restored->index == candidate.index);
    REQUIRE(restored->parameters == candidate.parameters);

    const auto regenerated = generator.build(restored->parameters);
    REQUIRE(regenerated.has_value());
    const auto before = treeParamsFrom(candidate.parameters);
    const auto after = treeParamsFrom(restored->parameters);
    REQUIRE(before.has_value());
    REQUIRE(after.has_value());
    const auto graphBefore = generateTree(*before);
    const auto graphAfter = generateTree(*after);
    REQUIRE(graphBefore.has_value());
    REQUIRE(graphAfter.has_value());
    CHECK(graphBefore->contentHash() == graphAfter->contentHash());
}

TEST_CASE("a search returns distinct, valid, ranked candidates", "[tree][search]") {
    const TreeGenerator generator;
    search::SearchSettings settings;
    settings.population = 8;
    settings.select = 4;
    const auto result = search::runSearch(generator, settings);
    REQUIRE(result.has_value());
    INFO(fmt::format("{} built of {}, {} selected, {:.1f} s", result->built, settings.population,
                     result->selected.size(), result->totalMs / 1000.0));
    for (const auto& [rule, count] : result->rejections) {
        WARN(fmt::format("rejected {} x {}", count, rule));
    }
    CHECK(result->built == settings.population);
    REQUIRE_FALSE(result->selected.empty());

    // Farthest-point selection seeds with the highest scorer, so the first selection is the best.
    float best = -1e9f;
    for (const search::Candidate& c : result->candidates) {
        if (!c.rejected) {
            best = std::max(best, c.score.overall());
        }
    }
    CHECK(result->candidates[result->selected.front()].score.overall() == best);

    std::set<std::size_t> unique(result->selected.begin(), result->selected.end());
    CHECK(unique.size() == result->selected.size());
}

TEST_CASE("tree probe: showcase geometry and a candidate search", "[.tree-probe]") {
    const TreeGenerator generator;
    search::SearchSettings settings;
    settings.population = 48;
    settings.select = 10;
    const auto result = search::runSearch(generator, settings);
    REQUIRE(result.has_value());
    WARN(fmt::format("{} built of {}, {} selected, {:.1f} s total ({:.1f} s building)", result->built,
                     settings.population, result->selected.size(), result->totalMs / 1000.0,
                     result->buildMs / 1000.0));
    for (const auto& [rule, count] : result->rejections) {
        WARN(fmt::format("rejected {} x {}", count, rule));
    }
    for (std::size_t rank = 0; rank < std::min<std::size_t>(6, result->selected.size()); ++rank) {
        const search::Candidate& c = result->candidates[result->selected[rank]];
        std::string line = fmt::format("#{:<4} score {:.3f} tris {:<7} |", c.index, c.score.overall(), c.triangles);
        for (const search::ScoreComponent& m : c.score.components) {
            line += fmt::format(" {}={:.2f}", m.name, m.raw);
        }
        WARN(line);
    }
}

TEST_CASE("tree probe: which bands actually discriminate", "[.tree-probe]") {
    // A COMPONENT WITH NO VARIANCE ACROSS THE POPULATION IS NOT A CRITERION, IT IS A CONSTANT.
    //
    // A band whose ideal interval sits entirely outside the distribution the generator actually
    // produces scores the same for everything -- 1.0 if the population is inside it, its floor if
    // outside -- and contributes a constant to every candidate's total while selecting nothing. The
    // weight it carries is then weight taken away from the components that are doing the work.
    // This prints the raw range and the score's standard deviation per component so a band can be
    // set from the measured distribution instead of guessed.
    const TreeGenerator generator;
    constexpr int kPopulation = 32;
    std::vector<std::vector<search::ScoreComponent>> population;
    for (int i = 0; i < kPopulation; ++i) {
        const search::Parameters params = search::sampleAt(generator.schema().parameters, static_cast<std::uint32_t>(i));
        const auto subject = generator.build(params);
        if (!subject) {
            continue;
        }
        population.push_back(generator.domainScores(*subject, params));
    }
    REQUIRE(population.size() > 8);

    const std::size_t count = population.front().size();
    for (std::size_t c = 0; c < count; ++c) {
        double rawMin = 1e30, rawMax = -1e30, scoreSum = 0.0, scoreSq = 0.0, scoreMin = 1e30, scoreMax = -1e30;
        for (const auto& row : population) {
            const double raw = row[c].raw;
            const double score = row[c].score;
            rawMin = std::min(rawMin, raw);
            rawMax = std::max(rawMax, raw);
            scoreMin = std::min(scoreMin, score);
            scoreMax = std::max(scoreMax, score);
            scoreSum += score;
            scoreSq += score * score;
        }
        const auto n = static_cast<double>(population.size());
        const double mean = scoreSum / n;
        const double sd = std::sqrt(std::max(scoreSq / n - mean * mean, 0.0));
        WARN(fmt::format("{:<21} raw [{:8.3f} .. {:8.3f}]  score [{:.2f} .. {:.2f}] mean {:.2f} sd {:.3f}{}",
                         population.front()[c].name, rawMin, rawMax, scoreMin, scoreMax, mean, sd,
                         sd < 0.05 ? "   <-- CONSTANT, not a criterion" : ""));
    }
}

TEST_CASE("tree probe: is silhouetteComplexity measuring foliage or limbs", "[.tree-probe]") {
    // A ONE-VARIABLE TEST, run before any band is touched so the two changes cannot confound each
    // other. The isoperimetric quotient is a correct measure of outline complexity. The question is
    // whether it is being applied to the right subject: a canopy of thousands of small cards has an
    // enormous perimeter however dull the limbs beneath it are, so the full-silhouette number may be
    // dominated by foliage edge and blind to branch structure entirely.
    //
    // If the full number sits in a narrow band across the population while the branch-only number
    // varies widely, the metric is measuring the canopy and must change subject rather than range.
    const TreeGenerator generator;
    std::vector<std::pair<float, float>> pairs;
    for (int i = 0; i < 24; ++i) {
        const search::Parameters params = search::sampleAt(generator.schema().parameters, static_cast<std::uint32_t>(i));
        const auto treeParams = treeParamsFrom(params);
        if (!treeParams) {
            continue;
        }
        const auto graph = generateTree(*treeParams);
        if (!graph) {
            continue;
        }
        const TreeSilhouette mask = rasteriseTree(*graph, generator.camera());
        if (mask.empty()) {
            continue;
        }
        const TreeMeasurements m = measureTree(*graph, mask, generator.camera());
        pairs.emplace_back(m.silhouetteComplexity, m.branchComplexity);
    }
    REQUIRE(pairs.size() > 8);

    const auto report = [&pairs](const char* label, bool branch) {
        double lo = 1e30, hi = -1e30, sum = 0.0, sq = 0.0;
        for (const auto& [full, only] : pairs) {
            const double v = branch ? only : full;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            sum += v;
            sq += v * v;
        }
        const auto n = static_cast<double>(pairs.size());
        const double mean = sum / n;
        const double sd = std::sqrt(std::max(sq / n - mean * mean, 0.0));
        WARN(fmt::format("{:<18} range [{:7.2f} .. {:7.2f}] mean {:7.2f} sd {:6.2f} relative-sd {:.3f}", label, lo,
                         hi, mean, sd, sd / std::max(mean, 1e-6)));
    };
    report("full silhouette", false);
    report("branches only", true);
}

TEST_CASE("tree probe: re-run the selection", "[.tree-probe]") {
    // The hero is whatever the scoring picks, so a re-banded scoring means a new hero. Running this
    // is the difference between someone judging THE TREE and judging the system that picked it.
    const TreeGenerator generator;
    search::SearchSettings settings;
    settings.population = 96;
    settings.select = 12;
    const auto result = search::runSearch(generator, settings);
    REQUIRE(result.has_value());
    WARN(fmt::format("{} built of {}, {} selected, {:.1f} s", result->built, settings.population,
                     result->selected.size(), result->totalMs / 1000.0));
    for (const auto& [rule, count] : result->rejections) {
        WARN(fmt::format("rejected {} x {}", count, rule));
    }
    for (std::size_t rank = 0; rank < std::min<std::size_t>(6, result->selected.size()); ++rank) {
        const search::Candidate& c = result->candidates[result->selected[rank]];
        WARN(fmt::format("rank {}  index {:<4} score {:.4f}  tris {}", rank, c.index, c.score.overall(),
                         c.triangles));
    }
}
