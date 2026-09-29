// Stem/crown alignment, as an invariant over the whole generator rather than a check on the six that
// shipped.
//
// The argument for an invariant is that this has happened before by one route and can happen again by
// another: "the cap tilted about the world origin and swung itself off the stem" was one of five
// geometry defects the first contact sheet caught, and a spot fix for that one would not have caught
// a different transform applied to a different pivot.
//
// The anchors are read back off the **generated meshes**. A parameter-space check would agree with
// itself -- recompute the attachment from `aspect` and `capThickness`, find it exactly where the
// builder put it, and sail past a wrong transform entirely.
//
// And it runs across the population rather than the winners, because ADR-180's point is that a search
// finds only what its parameterisation expresses: an invariant checked on the selected few has only
// been checked on the region the scorer liked.

#include "organism/mushroom.hpp"
#include "search/candidate_search.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <vector>

using namespace avgen;

namespace {

// The point of triangle abc nearest p (Ericson, Real-Time Collision Detection, 5.1.5).
glm::vec3 closestOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return a;
    }
    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        return a + ab * (d1 / (d1 - d3));
    }
    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        return a + ac * (d2 / (d2 - d6));
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    }
    const float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

struct Nearest {
    float distance = 1e9f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // the face's, oriented as the mesh winds it
};

Nearest nearestOnMesh(const glm::vec3& p, const scene::MeshData& mesh) {
    Nearest out;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const glm::vec3& a = mesh.vertices[mesh.indices[t]].position;
        const glm::vec3& b = mesh.vertices[mesh.indices[t + 1]].position;
        const glm::vec3& c = mesh.vertices[mesh.indices[t + 2]].position;
        const glm::vec3 q = closestOnTriangle(p, a, b, c);
        const float d = glm::distance(p, q);
        if (d < out.distance) {
            out.distance = d;
            out.point = q;
            const glm::vec3 n = glm::cross(b - a, c - a);
            out.normal = glm::length(n) > 1e-12f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
    return out;
}

// Where the stem meets the cap, as the eye sees it: whether the hole the underside leaves for the stem is
// filled, and whether the stem's top reaches the underside, in the generator's unit frame (multiply by
// the scene's scale for metres).
//   * rimToStem: the widest distance from the underside's hole rim (its innermost ring) to the stem's
//     surface. Filled, it is the underside's 3.5% tuck inside the stem; open, it is the gap around it.
//   * topBelow: the furthest a vertex of the stem's top (every ring at or past its end, uv.y >= 1) stands
//     OUTSIDE the cap -- below the underside, on the side its face points to -- from it. Inside the cap
//     it is hidden, 0.
//   * pokeThrough: the furthest one stands above the cap's upper surface: a stem through the cap's top.
// `stemRaise` lifts the stem in the unit frame first, for a scene that moved it (Glowmere's elder).
struct StemCapGap {
    float rimToStem = 0.0f;
    float topBelow = 0.0f;
    float pokeThrough = 0.0f;
    float stemTopRadius = 0.0f;
    float gap() const { return std::max(rimToStem, topBelow); }
};

StemCapGap stemCapGap(const search::Subject& s, float stemRaise = 0.0f) {
    StemCapGap out;
    const scene::MeshData& cap = s.parts[0].mesh;
    const scene::MeshData& under = s.parts[1].mesh;
    scene::MeshData stem = s.parts[2].mesh;
    for (scene::Vertex& v : stem.vertices) {
        v.position.y += stemRaise;
    }
    float innerU = 1e9f;
    for (const scene::Vertex& v : under.vertices) {
        innerU = std::min(innerU, v.uv.y);
    }
    for (const scene::Vertex& v : under.vertices) {
        if (v.uv.y <= innerU + 1e-6f) {
            out.rimToStem = std::max(out.rimToStem, nearestOnMesh(v.position, stem).distance);
        }
    }
    // The ring at t = 1 (and any past it), and its size.
    glm::vec3 centre(0.0f);
    int count = 0;
    for (const scene::Vertex& v : stem.vertices) {
        if (std::fabs(v.uv.y - 1.0f) < 1e-6f) {
            centre += v.position;
            ++count;
        }
    }
    centre /= static_cast<float>(std::max(count, 1));
    for (const scene::Vertex& v : stem.vertices) {
        if (v.uv.y < 1.0f - 1e-6f) {
            continue;
        }
        if (std::fabs(v.uv.y - 1.0f) < 1e-6f) {
            out.stemTopRadius = std::max(out.stemTopRadius, glm::distance(v.position, centre));
        }
        // The underside is wound to face down, out of the cap: a point on that side of it is outside.
        const Nearest n = nearestOnMesh(v.position, under);
        if (glm::dot(v.position - n.point, n.normal) > 0.0f) {
            out.topBelow = std::max(out.topBelow, n.distance);
        }
        // The upper surface faces up and out: a point on that side of it has come through the cap.
        const Nearest c = nearestOnMesh(v.position, cap);
        if (glm::dot(v.position - c.point, c.normal) > 0.0f) {
            out.pokeThrough = std::max(out.pokeThrough, c.distance);
        }
    }
    return out;
}

} // namespace

TEST_CASE("every mushroom the generator can produce has its cap on its stem",
          "[unit][mushroom][alignment]") {
    const organism::MushroomGenerator generator;
    const search::GeneratorSchema& schema = generator.schema();
    REQUIRE(schema.validate().has_value());

    // The whole default population. Deterministic, so a failure names a candidate index somebody can
    // regenerate and look at.
    constexpr std::uint32_t kPopulation = 800;

    std::map<std::string, int> defects;
    std::vector<std::pair<float, std::uint32_t>> worstOffset;
    int built = 0;
    int plausible = 0;
    float worstPlanarRatio = 0.0f;
    std::uint32_t worstIndex = 0;

    int legacyFailures = 0;
    float worstLegacyRatio = 0.0f;
    float worstLegacyMetres = 0.0f;
    std::uint32_t worstLegacyIndex = 0;
    for (std::uint32_t i = 0; i < kPopulation; ++i) {
        const search::Parameters values = search::sampleAt(schema.parameters, i);
        auto subject = generator.build(values);
        REQUIRE(subject.has_value());
        ++built;

        // Alignment is asserted on candidates that pass the plausibility gate, because a candidate the
        // gate rejects is never built into a scene. The rejected ones are still *measured*, below,
        // because "the ones we would ship are fine" is the claim and "the ones we would not are
        // broken in the same way" would be worth knowing.
        const bool ok = !organism::mushroomPlausibility(*subject, values).has_value();
        if (ok) {
            ++plausible;
        }

        const organism::MushroomAnchors a = organism::mushroomAnchors(*subject);
        REQUIRE(a.valid);
        const float planar =
            glm::length(glm::vec2(a.capAttach.x - a.stemTop.x, a.capAttach.z - a.stemTop.z));
        const float ratio = planar / std::max(a.stemTopRadius, 1e-5f);
        if (ok && ratio > worstPlanarRatio) {
            worstPlanarRatio = ratio;
            worstIndex = i;
        }

        // **How bad it was, measured rather than remembered.**
        //
        // The defect the user fixed by hand was that the cap was lathed about the *origin* while the
        // stem's top leans away from it by `curvature * t^2`. So the error that shipped is, for any
        // candidate, exactly how far the stem's top stands from the organism's axis -- which the
        // fixed generator can still compute. That makes "how bad is this really" a number this suite
        // reproduces on demand instead of a figure in a report that nobody can check.
        if (ok) {
            const float legacy = glm::length(glm::vec2(a.stemTop.x, a.stemTop.z));
            const float legacyRatio = legacy / std::max(a.stemTopRadius, 1e-5f);
            if (legacyRatio > 1.25f) {
                ++legacyFailures;
            }
            if (legacyRatio > worstLegacyRatio) {
                worstLegacyRatio = legacyRatio;
                worstLegacyMetres = legacy;
                worstLegacyIndex = i;
            }
        }

        for (const organism::AlignmentDefect& d : organism::checkMushroomAlignment(*subject)) {
            if (ok) {
                defects[d.rule]++;
                worstOffset.emplace_back(d.metres, i);
            }
        }
    }

    std::printf("\n===== mushroom alignment over %u candidates =====\n", kPopulation);
    std::printf("  built %d, plausible %d\n", built, plausible);
    std::printf("  worst cap-to-stem offset among plausible candidates: %.4f stem radii (#%u)\n",
                static_cast<double>(worstPlanarRatio), worstIndex);
    std::printf("  before the fix, the same population: %d of %d plausible candidates over tolerance"
                " (%.1f%%), worst %.4f stem radii = %.4f m at unit scale (#%u)\n",
                legacyFailures, plausible,
                100.0 * static_cast<double>(legacyFailures) / std::max(1, plausible),
                static_cast<double>(worstLegacyRatio), static_cast<double>(worstLegacyMetres),
                worstLegacyIndex);
    if (defects.empty()) {
        std::printf("  no alignment defects\n");
    }
    for (const auto& [rule, n] : defects) {
        std::printf("  %-20s %4d\n", rule.c_str(), n);
    }
    std::sort(worstOffset.begin(), worstOffset.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t i = 0; i < worstOffset.size() && i < 5; ++i) {
        std::printf("    worst #%u at %.4f m\n", worstOffset[i].second,
                    static_cast<double>(worstOffset[i].first));
    }
    std::fflush(stdout);

    REQUIRE(plausible > kPopulation / 2);
    CHECK(defects.empty());
}

TEST_CASE("the alignment check can actually fail", "[unit][mushroom][alignment]") {
    // ADR-182's rule: a diagnostic arm that cannot fail is not a diagnostic. The check above passing
    // means nothing unless a broken assembly makes it fail, so here is one -- the exact defect the
    // first contact sheet found, reproduced by moving the cap off its stem.
    const organism::MushroomGenerator generator;
    auto subject = generator.build(search::sampleAt(generator.schema().parameters, 131));
    REQUIRE(subject.has_value());
    REQUIRE(organism::checkMushroomAlignment(*subject).empty());

    const organism::MushroomAnchors before = organism::mushroomAnchors(*subject);
    REQUIRE(before.valid);

    SECTION("a cap slid sideways off its stem is caught") {
        const float shove = before.stemTopRadius * 4.0f;
        for (std::size_t part : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {
            for (scene::Vertex& v : subject->parts[part].mesh.vertices) {
                v.position.x += shove;
            }
        }
        const auto defects = organism::checkMushroomAlignment(*subject);
        REQUIRE_FALSE(defects.empty());
        REQUIRE(defects.front().rule == "cap-off-stem");
        REQUIRE(defects.front().metres > shove * 0.8f);
    }

    SECTION("a cap floating above its stem is caught") {
        for (std::size_t part : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {
            for (scene::Vertex& v : subject->parts[part].mesh.vertices) {
                v.position.y += before.stemTopRadius * 5.0f;
            }
        }
        const auto defects = organism::checkMushroomAlignment(*subject);
        REQUIRE_FALSE(defects.empty());
        REQUIRE(defects.front().rule == "cap-floats");
    }

    SECTION("gills pushed up through the cap are caught") {
        const auto cap = subject->parts[0].mesh.bounds();
        for (scene::Vertex& v : subject->parts[3].mesh.vertices) {
            v.position.y = cap.second.y + 1.0f;
        }
        const auto defects = organism::checkMushroomAlignment(*subject);
        REQUIRE(std::any_of(defects.begin(), defects.end(), [](const organism::AlignmentDefect& d) {
            return d.rule == "gills-through-cap";
        }));
    }
}

TEST_CASE("the spore anchor is under the cap and on its axis", "[unit][mushroom][alignment]") {
    // The emitter position is this same quantity rather than a second hand-placed offset, so it is
    // asserted here: under the cap's underside, on the attachment axis, and inside the cap's footprint
    // for every candidate that would be shipped.
    const organism::MushroomGenerator generator;
    for (std::uint32_t i : {4u, 131u, 515u, 644u, 755u, 776u, 787u}) {
        auto subject = generator.build(search::sampleAt(generator.schema().parameters, i));
        REQUIRE(subject.has_value());
        const organism::MushroomAnchors a = organism::mushroomAnchors(*subject);
        REQUIRE(a.valid);
        INFO("candidate #" << i);
        // On the axis the cap attaches by.
        REQUIRE(glm::length(glm::vec2(a.gillLow.x - a.capAttach.x, a.gillLow.z - a.capAttach.z)) < 1e-4f);
        // Below the underside it falls from, and above the ground the stem stands on.
        REQUIRE(a.gillLow.y <= a.capAttach.y + 1e-4f);
        REQUIRE(a.gillLow.y > subject->parts[2].mesh.bounds().first.y);
        // Inside the cap's own footprint: spores that fall from outside the rim are rain.
        const auto cap = subject->parts[0].mesh.bounds();
        REQUIRE(a.gillLow.x >= cap.first.x);
        REQUIRE(a.gillLow.x <= cap.second.x);
        REQUIRE(a.gillLow.z >= cap.first.z);
        REQUIRE(a.gillLow.z <= cap.second.z);
    }
}

// A probe, not a test: the spore emitter offsets for the selected heroes, in the generator's unit
// frame. Baked into `tools/make_glowmere_valley_2.py` the same way ground heights are, because a
// scene node carries an explicit transform and the anchor is mesh-derived.
TEST_CASE("probe: hero spore anchors", "[.probe][mushroom]") {
    const organism::MushroomGenerator generator;
    std::printf("\n===== hero spore anchors (unit frame) =====\n");
    // The last two are the GV3 art pass's heroes (the search's eleventh and twelfth picks).
    for (const std::uint32_t i : {131u, 776u, 644u, 4u, 684u, 515u, 675u, 755u, 508u, 380u, 251u, 707u}) {
        auto subject = generator.build(search::sampleAt(generator.schema().parameters, i));
        REQUIRE(subject.has_value());
        const organism::MushroomAnchors a = organism::mushroomAnchors(*subject);
        REQUIRE(a.valid);
        const auto cap = subject->parts[0].mesh.bounds();
        std::printf("  #%-4u gillLow (%7.4f, %7.4f, %7.4f)  gillRadius %6.4f  capRadius %6.4f  totalHeight %6.4f\n",
                    i, static_cast<double>(a.gillLow.x), static_cast<double>(a.gillLow.y),
                    static_cast<double>(a.gillLow.z), static_cast<double>(a.gillRadius),
                    static_cast<double>(std::max(cap.second.x - cap.first.x, cap.second.z - cap.first.z) * 0.5f),
                    static_cast<double>(cap.second.y - subject->parts[2].mesh.bounds().first.y));
    }
    std::fflush(stdout);
}

// A probe, not a test: how the twelve heroes' stems meet their caps, in the unit frame and in metres at the
// scale each stands at in Glowmere Valley 3 (the art pass's revision round 1: "check all new hero mushroom
// stems properly connect to their caps").
TEST_CASE("probe: hero stem-to-cap gaps", "[.probe][mushroom]") {
    const organism::MushroomGenerator generator;
    struct Hero {
        const char* name;
        std::uint32_t index;
        float scale;
        float stemRaise = 0.0f; // metres: GV2's projects stand the elder's stem 0.6034 m higher than its
                                // cap (nodes/elder-2-stem/position), a hand fix for this; GV3 no longer does
    };
    const Hero heroes[] = {{"elder-2", 131u, 11.132f, 0.6034f}, {"lantern", 776u, 5.9897f}, {"spire", 644u, 3.8047f},
                           {"bloom", 4u, 8.4128f},     {"veil", 684u, 3.1767f},    {"umbra", 515u, 3.9149f},
                           {"cairn", 675u, 5.5391f},   {"ridge", 755u, 3.742f},    {"scree", 508u, 5.4984f},
                           {"ember", 380u, 3.4518f},   {"opal", 251u, 5.072f},     {"sail", 707u, 5.3709f}};
    std::printf("\n===== hero stem-to-cap gaps (unit frame; metres at GV3's scale) =====\n");
    for (const Hero& h : heroes) {
        auto subject = generator.build(search::sampleAt(generator.schema().parameters, h.index));
        REQUIRE(subject.has_value());
        const std::vector<float> raises = h.stemRaise != 0.0f ? std::vector<float>{0.0f, h.stemRaise}
                                                              : std::vector<float>{0.0f};
        for (const float raise : raises) {
            const StemCapGap g = stemCapGap(*subject, raise / h.scale);
            std::printf("  %-8s #%-4u stem top r %.4f | hole rim to stem %.4f (%.2f r, %.3f m) | top below the "
                        "underside %.4f (%.2f r, %.3f m) | through the top %.3f m%s\n",
                        h.name, h.index, static_cast<double>(g.stemTopRadius), static_cast<double>(g.rimToStem),
                        static_cast<double>(g.rimToStem / g.stemTopRadius),
                        static_cast<double>(g.rimToStem * h.scale), static_cast<double>(g.topBelow),
                        static_cast<double>(g.topBelow / g.stemTopRadius), static_cast<double>(g.topBelow * h.scale),
                        static_cast<double>(g.pokeThrough * h.scale), raise != 0.0f ? " (stem raised, as GV2's projects stand it)" : "");
        }
    }
    std::fflush(stdout);
}

// A probe: the stem-to-cap gap over the whole population, and whether the search still picks the twelve it
// picked before the stem was joined to its cap (ADR-986) -- the selection reads the stem's bounds.
TEST_CASE("probe: stem-to-cap gaps over the population, and the search's picks", "[.probe][mushroom]") {
    const organism::MushroomGenerator generator;
    const search::GeneratorSchema& schema = generator.schema();
    std::vector<search::Candidate> population;
    std::vector<float> gaps;
    float worst = 0.0f;
    float worstPoke = 0.0f;
    std::uint32_t worstIndex = 0;
    for (std::uint32_t i = 0; i < 800; ++i) {
        search::Candidate c;
        c.index = i;
        c.parameters = search::sampleAt(schema.parameters, i);
        auto subject = generator.build(c.parameters);
        REQUIRE(subject.has_value());
        for (const search::SubjectPart& part : subject->parts) {
            c.triangles += static_cast<std::uint32_t>(part.mesh.indices.size() / 3);
            if (!c.rejected) {
                if (auto bad = search::meshHygiene(part.mesh, search::HygieneLimits{})) {
                    c.rejected = *bad;
                }
            }
        }
        if (!c.rejected) {
            if (auto bad = organism::mushroomPlausibility(*subject, c.parameters)) {
                c.rejected = *bad;
            }
        }
        if (!c.rejected) {
            c.score.components = generator.domainScores(*subject, c.parameters);
            c.features = generator.features(*subject, c.parameters);
            const StemCapGap g = stemCapGap(*subject);
            const float ratio = g.gap() / std::max(g.stemTopRadius, 1e-5f);
            gaps.push_back(ratio);
            if (ratio > worst) {
                worst = ratio;
                worstIndex = i;
            }
            worstPoke = std::max(worstPoke, g.pokeThrough / std::max(g.stemTopRadius, 1e-5f));
        }
        population.push_back(std::move(c));
    }
    std::sort(gaps.begin(), gaps.end());
    std::printf("\n===== stem-to-cap over %zu candidates that pass hygiene and plausibility =====\n", gaps.size());
    std::printf("  gap in stem radii: median %.3f, p90 %.3f, p99 %.3f, worst %.3f (#%u); through the top, worst %.3f\n",
                static_cast<double>(gaps[gaps.size() / 2]), static_cast<double>(gaps[gaps.size() * 9 / 10]),
                static_cast<double>(gaps[gaps.size() * 99 / 100]), static_cast<double>(worst), worstIndex,
                static_cast<double>(worstPoke));
    const std::vector<std::size_t> winners = search::selectDiverse(population, 12, 0.45f);
    std::printf("  the search's twelve:");
    for (const std::size_t w : winners) {
        std::printf(" %u", population[w].index);
    }
    std::printf("\n");
    std::fflush(stdout);
}

// ADR-986. The stem's top and the underside's opening meet, for every mushroom the search would ship (the
// hygiene and plausibility gates): the widest the opening stands off the stem, or the stem's top below the
// underside, is a small fraction of the stem's top radius (the underside tucks 3.5% inside the stem and both are
// polygons). Before ADR-986 the same gates passed 660 and measured a median of 0.45 stem radii and a worst of
// 1.05 (Glowmere's sail: 0.52 m); they now pass 689, at a median of 0.056 and a worst of 0.108.
//
// Through the top: none of the twelve heroes, and the population's worst went from 0.61 stem radii to 0.12. What
// is left is caps thinner at the opening than the stem's own ring is tilted -- the thinnest the schema allows,
// with a depressed centre whose upper surface dips toward the underside (#89) -- held here at the bound it has.
TEST_CASE("every mushroom's stem closes the opening its cap leaves for it", "[unit][mushroom][alignment][adr986]") {
    const organism::MushroomGenerator generator;
    const search::GeneratorSchema& schema = generator.schema();
    int checked = 0;
    float worst = 0.0f;
    float worstPoke = 0.0f;
    for (std::uint32_t i = 0; i < 800; ++i) {
        const search::Parameters values = search::sampleAt(schema.parameters, i);
        auto subject = generator.build(values);
        REQUIRE(subject.has_value());
        // The search's own gate: what it would ever pick.
        bool shipped = !organism::mushroomPlausibility(*subject, values).has_value();
        for (const search::SubjectPart& part : subject->parts) {
            shipped = shipped && !search::meshHygiene(part.mesh, search::HygieneLimits{}).has_value();
        }
        if (!shipped) {
            continue;
        }
        ++checked;
        const StemCapGap g = stemCapGap(*subject);
        REQUIRE(g.stemTopRadius > 0.0f);
        const float ratio = g.gap() / g.stemTopRadius;
        const float poke = g.pokeThrough / g.stemTopRadius;
        INFO("candidate #" << i << ": gap " << ratio << " stem radii, through the top " << poke);
        CHECK(ratio < 0.15f);
        CHECK(poke < 0.15f);
        worst = std::max(worst, ratio);
        worstPoke = std::max(worstPoke, poke);
    }
    INFO(checked << " plausible candidates; worst gap " << worst << " stem radii, worst through the top " << worstPoke);
    REQUIRE(checked > 400);

    // THE CONTROL: the measurement does see a gap. The sail's stem dropped by half its top radius opens one.
    auto sail = generator.build(search::sampleAt(schema.parameters, 707));
    REQUIRE(sail.has_value());
    const float r = stemCapGap(*sail).stemTopRadius;
    for (scene::Vertex& v : sail->parts[2].mesh.vertices) {
        v.position.y -= 0.5f * r;
    }
    CHECK(stemCapGap(*sail).gap() > 0.4f * r);
}
