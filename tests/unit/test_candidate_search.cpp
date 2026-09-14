// The shared candidate-search layer (ADR-172, ADR-173). Deliberately generator-free: every fixture
// here is an abstract two-axis space, because the layer is shared between Glowmere Valley 2's hero
// mushrooms and the Tree of Life and a test that reached for either would be evidence that the
// layer had leaked.

#include "search/candidate_search.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

search::GeneratorSchema twoAxisSchema() {
    search::GeneratorSchema s;
    s.generatorName = "fixture";
    s.generatorVersion = 1;
    s.parameters = search::ParameterSchema({
        {"a", 0.0f, 1.0f, false, "first axis"},
        {"b", -10.0f, 10.0f, false, "second axis"},
    });
    s.featureNames = {"fa", "fb"};
    return s;
}

search::Candidate at(std::uint32_t index, float fa, float fb, float score) {
    search::Candidate c;
    c.index = index;
    c.features = {fa, fb};
    c.score.components.push_back({"only", score, score, 1.0f});
    return c;
}

} // namespace

TEST_CASE("a Sobol sample is reproducible, indexable and extensible", "[search][sampling]") {
    const search::GeneratorSchema schema = twoAxisSchema();

    SECTION("the same index always gives the same parameters") {
        for (std::uint32_t i : {0u, 1u, 7u, 184u, 65535u}) {
            REQUIRE(search::sampleAt(schema.parameters, i) == search::sampleAt(schema.parameters, i));
        }
    }

    SECTION("every value is inside its declared range") {
        for (std::uint32_t i = 0; i < 512; ++i) {
            const search::Parameters p = search::sampleAt(schema.parameters, i);
            REQUIRE(p.size() == 2);
            REQUIRE(p[0] >= 0.0f);
            REQUIRE(p[0] <= 1.0f);
            REQUIRE(p[1] >= -10.0f);
            REQUIRE(p[1] <= 10.0f);
        }
    }

    SECTION("candidate 0 is not the degenerate all-minimum corner") {
        // The scramble exists for this. Without it the unscrambled sequence starts at the origin and
        // candidate 0 is every parameter at its minimum, which is a corner nobody wants to look at.
        const search::Parameters p = search::sampleAt(schema.parameters, 0);
        REQUIRE(p[0] > 0.0f);
        REQUIRE(p[1] > -10.0f);
    }

    SECTION("growing the population does not move any earlier candidate") {
        // The extensibility property Sobol was chosen for over Latin hypercube (ADR-173): a 200-point
        // run grown to 400 keeps every identity. Under LHS this assertion is false by construction.
        std::vector<search::Parameters> first;
        for (std::uint32_t i = 0; i < 200; ++i) {
            first.push_back(search::sampleAt(schema.parameters, i));
        }
        for (std::uint32_t i = 0; i < 200; ++i) {
            REQUIRE(search::sampleAt(schema.parameters, i) == first[i]);
        }
    }

    SECTION("it stratifies better than a hash does") {
        // The claim the sampler exists to make. Split axis 0 into 16 equal bins over 64 points: a
        // low-discrepancy sequence puts exactly 4 in each, and the test asserts the strong form
        // because anything weaker would also pass for a mediocre sampler.
        std::array<int, 16> bins{};
        for (std::uint32_t i = 0; i < 64; ++i) {
            const float u = search::sampleAt(schema.parameters, i)[0];
            bins[std::min<std::size_t>(15, static_cast<std::size_t>(u * 16.0f))]++;
        }
        for (const int n : bins) {
            REQUIRE(n == 4);
        }
    }

    SECTION("an integral axis reaches both endpoints and only whole values") {
        const search::ParameterSchema ints({{"n", 3.0f, 9.0f, true, "count"}});
        std::set<float> seen;
        for (std::uint32_t i = 0; i < 256; ++i) {
            const float v = search::sampleAt(ints, i)[0];
            REQUIRE(v == std::floor(v));
            REQUIRE(v >= 3.0f);
            REQUIRE(v <= 9.0f);
            seen.insert(v);
        }
        REQUIRE(seen.count(3.0f) == 1);
        REQUIRE(seen.count(9.0f) == 1);
    }
}

TEST_CASE("a score band falls off on both sides and cannot be made monotone", "[search][scoring]") {
    const search::ScoreBand band{0.0f, 0.4f, 0.6f, 1.0f};
    REQUIRE(band.validate().has_value());

    SECTION("the plateau is 1 and outside the edges is 0") {
        REQUIRE_THAT(static_cast<double>(band(0.5f)), WithinAbs(static_cast<double>(1.0f), 1e-6));
        REQUIRE_THAT(static_cast<double>(band(0.4f)), WithinAbs(static_cast<double>(1.0f), 1e-6));
        REQUIRE_THAT(static_cast<double>(band(0.6f)), WithinAbs(static_cast<double>(1.0f), 1e-6));
        REQUIRE_THAT(static_cast<double>(band(-0.1f)), WithinAbs(static_cast<double>(0.0f), 1e-6));
        REQUIRE_THAT(static_cast<double>(band(1.5f)), WithinAbs(static_cast<double>(0.0f), 1e-6));
    }

    SECTION("more is not better -- the whole point of ADR-172") {
        // The negative control for the decision: a monotone component would have band(0.9) >
        // band(0.5). Overshooting the plateau must cost exactly as much as undershooting it.
        REQUIRE(band(0.9f) < band(0.5f));
        REQUIRE(band(0.1f) < band(0.5f));
        REQUIRE_THAT(static_cast<double>(band(0.2f)), WithinAbs(static_cast<double>(band(0.8f)), 1e-6));
    }

    SECTION("it is continuous at the edges") {
        REQUIRE_THAT(static_cast<double>(band(0.001f)), WithinAbs(static_cast<double>(0.0f), 0.01));
        REQUIRE_THAT(static_cast<double>(band(0.999f)), WithinAbs(static_cast<double>(0.0f), 0.01));
    }

    SECTION("an infinite edge is refused rather than silently behaving as a maximum") {
        const search::ScoreBand unbounded{0.0f, 0.4f, 0.6f, std::numeric_limits<float>::infinity()};
        REQUIRE_FALSE(unbounded.validate().has_value());
        const search::ScoreBand misordered{1.0f, 0.4f, 0.6f, 2.0f};
        REQUIRE_FALSE(misordered.validate().has_value());
    }

    SECTION("a NaN input scores zero rather than propagating") {
        REQUIRE_THAT(static_cast<double>(band(std::numeric_limits<float>::quiet_NaN())), WithinAbs(static_cast<double>(0.0f), 1e-6));
    }
}

TEST_CASE("the overall score is a weighted mean less unclamped penalties", "[search][scoring]") {
    search::Score s;
    s.components.push_back({"x", 0.0f, 1.0f, 3.0f});
    s.components.push_back({"y", 0.0f, 0.0f, 1.0f});
    REQUIRE_THAT(static_cast<double>(s.overall()), WithinAbs(static_cast<double>(0.75f), 1e-6));
    s.penalties.push_back({"artifact", 1.0f});
    // Deliberately negative: a heavily penalised candidate must sort below a merely mediocre one,
    // and a clamp at zero would tie them.
    REQUIRE(s.overall() < 0.0f);
    REQUIRE(s.find("x") != nullptr);
    REQUIRE(s.find("absent") == nullptr);
}

TEST_CASE("mesh hygiene names the rule it rejected on", "[search][validity]") {
    const auto quad = [] {
        scene::MeshData m;
        m.vertices = {{{0, 0, 0}, {0, 1, 0}, {0, 0}},
                      {{1, 0, 0}, {0, 1, 0}, {1, 0}},
                      {{1, 0, 1}, {0, 1, 0}, {1, 1}},
                      {{0, 0, 1}, {0, 1, 0}, {0, 1}}};
        m.indices = {0, 1, 2, 0, 2, 3};
        return m;
    };
    const search::HygieneLimits limits{};

    REQUIRE_FALSE(search::meshHygiene(quad(), limits).has_value());

    SECTION("a non-finite position is caught") {
        scene::MeshData m = quad();
        m.vertices[2].position.y = std::numeric_limits<float>::quiet_NaN();
        const auto r = search::meshHygiene(m, limits);
        REQUIRE(r.has_value());
        REQUIRE(r->rule == "non-finite-position");
    }

    SECTION("an out-of-range index is caught before it is dereferenced") {
        scene::MeshData m = quad();
        m.indices[4] = 99;
        const auto r = search::meshHygiene(m, limits);
        REQUIRE(r.has_value());
        REQUIRE(r->rule == "index-range");
    }

    SECTION("degenerate triangles above the fraction are caught") {
        scene::MeshData m = quad();
        m.indices = {0, 1, 2, 0, 1, 1}; // the second triangle has zero area
        const auto r = search::meshHygiene(m, limits);
        REQUIRE(r.has_value());
        REQUIRE(r->rule == "degenerate");
    }

    SECTION("an empty mesh is a rejection, not a crash") {
        REQUIRE(search::meshHygiene(scene::MeshData{}, limits)->rule == "empty");
    }

    SECTION("a triangle budget is enforced") {
        search::HygieneLimits tight = limits;
        tight.maxTriangles = 1;
        REQUIRE(search::meshHygiene(quad(), tight)->rule == "triangle-budget");
    }
}

TEST_CASE("diverse selection trades quality against spread, and alpha is the trade", "[search][diversity]") {
    // Three near-identical high scorers in one corner, one lone mediocre candidate far away. This is
    // exactly the population that makes top-N-by-score fail, which is why it is the fixture.
    std::vector<search::Candidate> pool{
        at(0, 0.00f, 0.00f, 0.95f),
        at(1, 0.01f, 0.01f, 0.94f),
        at(2, 0.02f, 0.00f, 0.93f),
        at(3, 1.00f, 1.00f, 0.60f),
    };

    SECTION("alpha = 1 is top-N by score and picks the three near-duplicates") {
        const std::vector<std::size_t> picked = search::selectDiverse(pool, 3, 1.0f);
        REQUIRE(picked.size() == 3);
        REQUIRE(std::find(picked.begin(), picked.end(), 3) == picked.end());
    }

    SECTION("a diversity-weighted alpha reaches the distant candidate") {
        const std::vector<std::size_t> picked = search::selectDiverse(pool, 2, 0.2f);
        REQUIRE(picked.size() == 2);
        REQUIRE(picked[0] == 0); // seeded with the highest scorer
        REQUIRE(picked[1] == 3); // then the farthest, not the second best
    }

    SECTION("it is deterministic and never repeats a candidate") {
        const std::vector<std::size_t> a = search::selectDiverse(pool, 4, 0.5f);
        REQUIRE(a == search::selectDiverse(pool, 4, 0.5f));
        REQUIRE(std::set<std::size_t>(a.begin(), a.end()).size() == a.size());
    }

    SECTION("rejected candidates are never selected") {
        pool[3].rejected = search::Rejection{"degenerate", "zero area"};
        const std::vector<std::size_t> picked = search::selectDiverse(pool, 4, 0.0f);
        REQUIRE(std::find(picked.begin(), picked.end(), 3) == picked.end());
    }

    SECTION("asking for more than exist returns what exists") {
        REQUIRE(search::selectDiverse(pool, 99, 0.5f).size() == pool.size());
        REQUIRE(search::selectDiverse({}, 6, 0.5f).empty());
    }
}

TEST_CASE("a candidate record round-trips and refuses a moved schema", "[search][record]") {
    const search::GeneratorSchema schema = twoAxisSchema();
    search::Candidate c;
    c.index = 184;
    c.parameters = search::sampleAt(schema.parameters, 184);
    c.features = {1.5f, 2.5f};
    c.score.components.push_back({"silhouette", 0.83f, 0.91f, 2.0f});
    c.triangles = 4200;

    const nlohmann::json j = search::candidateToJson(schema, c, "promoted over #91: better rim");
    REQUIRE(j.at("index").get<std::uint32_t>() == 184);
    REQUIRE(j.at("parameters").contains("a"));
    REQUIRE(j.at("features").at("fa").get<float>() == 1.5f);
    REQUIRE(j.at("scores").at("components").at("silhouette").at("raw").get<float>() == 0.83f);
    REQUIRE(j.at("selectionNote").get<std::string>().starts_with("promoted"));
    // No mesh is stored. The parameters are the mushroom.
    REQUIRE_FALSE(j.contains("vertices"));

    SECTION("the same schema regenerates the same parameters from the index alone") {
        const auto back = search::candidateFromJson(schema, j);
        REQUIRE(back.has_value());
        REQUIRE(back->index == 184);
        REQUIRE(back->parameters == c.parameters);
    }

    SECTION("a widened range moves the hash and the record refuses rather than lying") {
        // The failure this field exists to catch: without the hash, index 184 against a changed
        // schema silently names a *different* individual under the same name.
        search::GeneratorSchema moved = schema;
        moved.parameters = search::ParameterSchema({
            {"a", 0.0f, 2.0f, false, "first axis, widened"},
            {"b", -10.0f, 10.0f, false, "second axis"},
        });
        REQUIRE(moved.hash() != schema.hash());
        const auto back = search::candidateFromJson(moved, j);
        REQUIRE_FALSE(back.has_value());
        REQUIRE(back.error().message.find("schema") != std::string::npos);
    }

    SECTION("a bumped generator version refuses too") {
        search::GeneratorSchema bumped = schema;
        bumped.generatorVersion = 2;
        REQUIRE_FALSE(search::candidateFromJson(bumped, j).has_value());
    }

    SECTION("reordering the schema changes the hash, because the order is the sample") {
        search::GeneratorSchema reordered;
        reordered.generatorName = schema.generatorName;
        reordered.generatorVersion = schema.generatorVersion;
        reordered.parameters = search::ParameterSchema({
            {"b", -10.0f, 10.0f, false, "second axis"},
            {"a", 0.0f, 1.0f, false, "first axis"},
        });
        reordered.featureNames = schema.featureNames;
        REQUIRE(reordered.hash() != schema.hash());
    }
}

TEST_CASE("a schema validates its own shape", "[search][schema]") {
    REQUIRE(twoAxisSchema().validate().has_value());

    SECTION("an empty schema is refused") {
        REQUIRE_FALSE(search::ParameterSchema(std::vector<search::ParameterSpec>{}).validate().has_value());
    }
    SECTION("an inverted range is refused") {
        REQUIRE_FALSE(search::ParameterSchema({{"a", 1.0f, 0.0f, false, ""}}).validate().has_value());
    }
    SECTION("a duplicate name is refused") {
        REQUIRE_FALSE(search::ParameterSchema({{"a", 0.0f, 1.0f, false, ""},
                                               {"a", 0.0f, 1.0f, false, ""}})
                          .validate()
                          .has_value());
    }
    SECTION("a schema with no feature axes has nothing to be diverse on") {
        search::GeneratorSchema s = twoAxisSchema();
        s.featureNames.clear();
        REQUIRE_FALSE(s.validate().has_value());
    }
    SECTION("a schema wider than the sampler's direction numbers is refused loudly") {
        std::vector<search::ParameterSpec> many;
        for (int i = 0; i < 40; ++i) {
            many.push_back({"p" + std::to_string(i), 0.0f, 1.0f, false, ""});
        }
        REQUIRE_FALSE(search::ParameterSchema(std::move(many)).validate().has_value());
    }
}

TEST_CASE("the default preview views include a low angle", "[search][preview]") {
    const std::vector<search::PreviewView> views = search::defaultViews();
    REQUIRE(views.size() == 6);
    // Not a style preference. `docs/visual-cookbook/bioluminescence.md` records gills being added and
    // being invisible because the geometry above enclosed them; no camera at subject height catches
    // that, so a view below the horizon is part of the contract.
    REQUIRE(std::any_of(views.begin(), views.end(),
                        [](const search::PreviewView& v) { return v.elevationDegrees < -10.0f; }));
    REQUIRE(std::any_of(views.begin(), views.end(),
                        [](const search::PreviewView& v) { return v.elevationDegrees > 40.0f; }));
}

TEST_CASE("a generated source is what the scene stores and the editor edits", "[search][editor]") {
    const search::GeneratorSchema schema = twoAxisSchema();
    search::GeneratedSource src;
    src.generatorName = schema.generatorName;
    src.generatorVersion = schema.generatorVersion;
    src.schemaHash = schema.hash();
    src.index = 184;
    src.values = search::sampleAt(schema.parameters, 184);

    REQUIRE(src.validateAgainst(schema).has_value());

    SECTION("an untouched hero's values are exactly what the sampler produced") {
        // The provenance assertion: `index` says where these numbers came from, and for a hero
        // nobody has edited the two agree exactly.
        REQUIRE(src.matchesSample(schema.parameters));
    }

    SECTION("an artist's edit diverges from the sample and stays authoritative") {
        src.values[0] += 0.1f;
        REQUIRE_FALSE(src.matchesSample(schema.parameters));
        // Still valid: the edit is the authored truth, and the index still records the origin.
        REQUIRE(src.validateAgainst(schema).has_value());
        REQUIRE(src.index == 184);
    }

    SECTION("it round-trips through JSON unchanged") {
        const auto back = search::generatedSourceFromJson(search::generatedSourceToJson(src));
        REQUIRE(back.has_value());
        REQUIRE(back->generatorName == src.generatorName);
        REQUIRE(back->index == src.index);
        REQUIRE(back->schemaHash == src.schemaHash);
        REQUIRE(back->values == src.values);
    }

    SECTION("a moved schema is refused, because the values are positional") {
        search::GeneratorSchema moved = schema;
        moved.parameters = search::ParameterSchema({
            {"b", -10.0f, 10.0f, false, ""},
            {"a", 0.0f, 1.0f, false, ""},
        });
        REQUIRE_FALSE(src.validateAgainst(moved).has_value());
    }

    SECTION("a wrong-sized value vector is refused") {
        src.values.pop_back();
        REQUIRE_FALSE(src.validateAgainst(schema).has_value());
    }

    SECTION("a non-finite value is refused before it reaches a generator") {
        src.values[1] = std::numeric_limits<float>::quiet_NaN();
        REQUIRE_FALSE(src.validateAgainst(schema).has_value());
    }

    SECTION("it registers one keyable parameter path per axis, under the node's own prefix") {
        const std::vector<std::string> paths = search::parameterPaths(schema, "procedural/elder-2/");
        REQUIRE(paths.size() == schema.parameters.size());
        REQUIRE(paths[0] == "procedural/elder-2/generated/a");
        REQUIRE(paths[1] == "procedural/elder-2/generated/b");
    }
}
