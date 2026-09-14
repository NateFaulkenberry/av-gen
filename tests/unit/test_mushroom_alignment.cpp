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
    for (const std::uint32_t i : {131u, 776u, 644u, 4u, 684u, 515u, 675u, 755u, 508u, 380u}) {
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
