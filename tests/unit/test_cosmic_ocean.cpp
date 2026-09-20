// The Cosmic Ocean's data model (ADR-390), with no GPU and no scene.
//
// The assertions here are chosen against the failures this project actually has, rather than
// against the fields:
//
//  * ADR-350's round trip, because two hand-maintained lists drift. Not "the key sets are equal" --
//    that would fail on `speedScale` vs `speed`, which are deliberately two namespaces.
//  * ADR-382's panel paths, computed by the test the same way the panel computes them.
//  * ADR-091's determinism, because an offline render of second N has to be a live frame at N.
//  * A reader for every field, because a parameter that registers and reaches nothing is the
//    failure family this codebase keeps finding one instance at a time.

#include "ui/cosmic_ocean_rows.hpp"
#include "world/cosmic_ocean.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace avgen;

namespace {

// A value inside the field's hard range that is not its default and not a round number, so a
// setter that writes the wrong member is caught by the value rather than by luck.
[[nodiscard]] float probeValue(const world::CosmicFloatField& f, std::size_t index) {
    const float span = f.hi - f.lo;
    const float t = 0.17f + 0.61f * static_cast<float>(index % 7) / 7.0f;
    return f.lo + span * t;
}

[[nodiscard]] std::vector<float> lanes(const world::CosmicOceanGpu& g) {
    std::vector<float> out(sizeof(g) / sizeof(float));
    std::memcpy(out.data(), &g, sizeof(g));
    return out;
}

} // namespace

TEST_CASE("The default cosmic ocean validates and is the beautiful one", "[cosmic][ocean]") {
    const world::CosmicOcean neutral;
    REQUIRE(neutral.validate());

    const world::CosmicOcean look = world::defaultCosmicOcean();
    REQUIRE(look.validate());
    CHECK(look.active());

    // §30/§32: the background must sit two orders of magnitude below the tree's lit leaves (~0.5)
    // and its emissive specks (>1.0). Asserted on the authored radiances rather than on a render,
    // because this is the composition rule and it is decided here.
    const auto luma = [](glm::vec3 c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; };
    CHECK(luma(look.color.deepSpace) < 0.02f);
    CHECK(look.nebulaFar.stratum.brightness < 0.15f);
    CHECK(look.nebulaMid.stratum.brightness < 0.15f);
    // ...while the near stars are allowed to be genuine light sources, which is what makes them
    // read as points rather than as grey dots.
    CHECK(look.starsNear.stratum.brightness > 1.0f);

    // §10: the strata really are ordered in depth, and the parallax really does increase as they
    // come closer. A "depth system" whose layers are all at one distance is the failure mode.
    CHECK(look.galaxies.stratum.depth > look.nebulaFar.stratum.depth);
    CHECK(look.nebulaFar.stratum.depth > look.nebulaMid.stratum.depth);
    CHECK(look.starsUltra.stratum.depth > look.starsFar.stratum.depth);
    CHECK(look.starsFar.stratum.depth > look.starsMid.stratum.depth);
    CHECK(look.starsMid.stratum.depth > look.starsNear.stratum.depth);
    CHECK(look.starsNear.stratum.depth > look.planets.stratum.depth);
    CHECK(look.planets.stratum.depth > look.dust.stratum.depth);
    CHECK(look.dust.stratum.parallax > look.planets.stratum.parallax);
    CHECK(look.planets.stratum.parallax > look.starsMid.stratum.parallax);
    CHECK(look.starsMid.stratum.parallax > look.nebulaFar.stratum.parallax);
}

TEST_CASE("Every field table entry survives a set and a get", "[cosmic][ocean]") {
    // The half of ADR-350's round trip that does not need a serialiser: an accessor pair that
    // reads a different member from the one it writes is silent in every other test.
    std::size_t i = 0;
    for (const auto& f : world::cosmicFloatFields()) {
        world::CosmicOcean o;
        const float v = probeValue(f, i++);
        f.set(o, v);
        INFO("float leaf " << f.leaf);
        CHECK(f.get(o) == v);
        CHECK(f.lo <= f.hi);
        CHECK(f.slo <= f.shi);
        CHECK(f.lo <= f.slo);
        CHECK(f.shi <= f.hi);
    }
    for (const auto& c : world::cosmicColorFields()) {
        world::CosmicOcean o;
        const glm::vec3 v{0.13f, 0.57f, 0.91f};
        c.set(o, v);
        INFO("colour leaf " << c.leaf);
        CHECK(c.get(o) == v);
    }
    for (const auto& b : world::cosmicBoolFields()) {
        world::CosmicOcean o;
        b.set(o, false);
        INFO("bool leaf " << b.leaf);
        CHECK(b.get(o) == false);
        b.set(o, true);
        CHECK(b.get(o) == true);
    }
}

TEST_CASE("No two field table leaves collide", "[cosmic][ocean]") {
    // A duplicate leaf is a parameter path registered twice: the second registration is what the
    // UI reaches and the first is a control that silently stopped working. Cheap to assert, and
    // impossible to see by reading a hundred and thirty rows.
    std::set<std::string> seen;
    const auto add = [&](std::string_view leaf) {
        INFO("leaf " << leaf);
        CHECK(seen.insert(std::string(leaf)).second);
    };
    for (const auto& f : world::cosmicFloatFields()) add(f.leaf);
    for (const auto& c : world::cosmicColorFields()) add(c.leaf);
    for (const auto& b : world::cosmicBoolFields()) add(b.leaf);
}

TEST_CASE("Every field table entry moves at least one byte of the GPU block", "[cosmic][ocean]") {
    // The rule `cosmic_ocean.hpp` states, enforced rather than asserted in a comment: a field with
    // no reader is ADR-385's `bloomLevels` and ADR-390's `emissiveBoost`, pre-written. If a leaf
    // can be driven to the far end of its hard range and every one of the 184 floats that reach
    // the shader is unchanged, that parameter cannot affect a pixel and this test says which one.
    //
    // A handful of leaves legitimately do not: they are the ones the *CPU* consumes before packing.
    // Each is named, with the reason, because "expected failures" with no list is how a real one
    // hides among them.
    const std::set<std::string> cpuOnly = {
        // The quality counts are read by `packCosmicOcean` to clamp other lanes; they do reach the
        // block, but through `min`, so a value below the ceiling changes nothing. Asserted
        // separately below.
        "qualityNebulaOctaves",
    };

    // Two probe seconds, and a leaf passes if it moves the block at *either*.
    //
    // Not belt and braces -- the first draft used one second, 3.5, and this test failed on
    // `colorFastSpeed`. The cause was the probe, not the code: a speed's contribution is a phase
    // wrapped into 0..1, and 3.5 s x 4.0 turns/s is exactly 14 turns, so the fast phase at the
    // slider's maximum is 0.0, bit-identical to the phase at speed zero. Any single fixed second
    // has some set of speeds it is blind to in exactly this way, and which ones depends on the
    // slider ranges -- so the blind spot would move every time somebody retuned a range, and would
    // read as "this parameter stopped working".
    //
    // The two seconds below are chosen to share no rational factor with each other, so no speed can
    // land on a whole number of turns at both.
    constexpr double kProbeA = 3.4271;
    constexpr double kProbeB = 7.90813;

    for (const auto& f : world::cosmicFloatFields()) {
        if (cpuOnly.contains(f.leaf)) continue;
        world::CosmicOcean lo = world::defaultCosmicOcean();
        world::CosmicOcean hi = lo;
        f.set(lo, f.slo);
        f.set(hi, f.shi == f.slo ? f.shi + 1.0f : f.shi);
        const bool movedA = lanes(world::packCosmicOcean(lo, 1.0f, kProbeA)) !=
                            lanes(world::packCosmicOcean(hi, 1.0f, kProbeA));
        const bool movedB = lanes(world::packCosmicOcean(lo, 1.0f, kProbeB)) !=
                            lanes(world::packCosmicOcean(hi, 1.0f, kProbeB));
        INFO("float leaf " << f.leaf << " does not reach the GPU block at either probe second");
        CHECK((movedA || movedB));
    }
    for (const auto& c : world::cosmicColorFields()) {
        world::CosmicOcean lo = world::defaultCosmicOcean();
        world::CosmicOcean hi = lo;
        c.set(lo, glm::vec3(0.0f));
        c.set(hi, glm::vec3(0.91f, 0.33f, 0.57f));
        INFO("colour leaf " << c.leaf << " does not reach the GPU block");
        CHECK(lanes(world::packCosmicOcean(lo, 1.0f, kProbeA)) !=
              lanes(world::packCosmicOcean(hi, 1.0f, kProbeA)));
    }
    for (const auto& b : world::cosmicBoolFields()) {
        world::CosmicOcean off = world::defaultCosmicOcean();
        world::CosmicOcean on = off;
        b.set(off, false);
        b.set(on, true);
        INFO("bool leaf " << b.leaf << " does not reach the GPU block");
        CHECK(lanes(world::packCosmicOcean(off, 1.0f, kProbeA)) !=
              lanes(world::packCosmicOcean(on, 1.0f, kProbeA)));
    }

    // The octave ceiling, checked the way it actually works: lower it below the authored detail and
    // the packed octave count follows.
    world::CosmicOcean o = world::defaultCosmicOcean();
    o.nebulaFar.detail = 6.0f;
    o.quality.nebulaOctaves = 6.0f;
    const float wide = world::packCosmicOcean(o, 1.0f, 0.0).nebFar1.x;
    o.quality.nebulaOctaves = 2.0f;
    const float narrow = world::packCosmicOcean(o, 1.0f, 0.0).nebFar1.x;
    CHECK(wide == 6.0f);
    CHECK(narrow == 2.0f);
}

TEST_CASE("Packing is a pure function of its arguments", "[cosmic][ocean]") {
    const world::CosmicOcean o = world::defaultCosmicOcean();
    // ADR-091: an offline render of second N must be a realtime frame at second N, so the same
    // second twice has to be the same bytes. Nothing here may read a frame counter or a clock.
    CHECK(lanes(world::packCosmicOcean(o, 1.0f, 12.25)) ==
          lanes(world::packCosmicOcean(o, 1.0f, 12.25)));
    CHECK(lanes(world::packCosmicOcean(o, 1.0f, 12.25)) !=
          lanes(world::packCosmicOcean(o, 1.0f, 12.30)));

    // §34: the seed is the arrangement. Same seed, same universe; different seed, different one.
    world::CosmicOcean other = o;
    other.seed = o.seed + 17.0f;
    CHECK(lanes(world::packCosmicOcean(o, 1.0f, 4.0)) != lanes(world::packCosmicOcean(other, 1.0f, 4.0)));

    // The lifecycle envelope reaches intensity and nothing else, so a fading ocean fades rather
    // than rearranging itself.
    const auto full = world::packCosmicOcean(o, 1.0f, 4.0);
    const auto half = world::packCosmicOcean(o, 0.5f, 4.0);
    CHECK_THAT(half.master.x, Catch::Matchers::WithinRel(full.master.x * 0.5f, 1.0e-5f));
    CHECK(half.nebFar0 == full.nebFar0);
    CHECK(half.planet4 == full.planet4);
}

TEST_CASE("Phases stay smooth an hour into a timeline", "[cosmic][ocean]") {
    // The reason `phase()` wraps. At t = 3600 s an unwrapped fast phase would be ~126000, where a
    // float's spacing is 0.0078 -- the shimmer would quantise into steps and then stop. Wrapped, a
    // 16 ms step at the end of a long render still moves every phase.
    world::CosmicOcean o = world::defaultCosmicOcean();
    o.color.fastSpeed = 8.0f;
    const auto a = world::packCosmicOcean(o, 1.0f, 3600.0);
    const auto b = world::packCosmicOcean(o, 1.0f, 3600.0 + 1.0 / 60.0);
    CHECK(a.colorAccent.w != b.colorAccent.w);
    CHECK(a.colorAccent.w >= 0.0f);
    CHECK(a.colorAccent.w < 1.0f);
}

TEST_CASE("Sanitising clamps every count a shader loops on", "[cosmic][ocean]") {
    // A modulation route drives a *final* anywhere inside the hard range, and four of these numbers
    // are loop bounds. A non-integral or out-of-range bound is a hang or a wrong picture, so the
    // clamp is the second line of defence after the hard ranges and this is its test.
    world::CosmicOcean o = world::defaultCosmicOcean();
    o.quality.nebulaOctaves = 99.0f;
    o.quality.starStrata = -4.0f;
    o.quality.planetCells = 3.4f;
    o.quality.dustCells = 4.6f;
    o.globalScale = 0.0f;
    o.nebulaFar.scale = 0.0f;
    o.dust.fadeDistance = 0.0f;
    o.mask.innerAngle = 50.0f;
    o.mask.outerAngle = 10.0f;
    world::sanitiseCosmicOcean(o);
    CHECK(o.quality.nebulaOctaves == 6.0f);
    CHECK(o.quality.starStrata == 1.0f);
    CHECK(o.quality.planetCells == 3.0f);
    CHECK(o.quality.dustCells == 5.0f);
    CHECK(o.globalScale > 0.0f);
    CHECK(o.nebulaFar.scale > 0.0f);
    CHECK(o.dust.fadeDistance > 0.0f);
    CHECK(o.mask.outerAngle > o.mask.innerAngle);

    // And the packer sanitises its own copy, so a caller that forgot cannot produce a bad block.
    world::CosmicOcean dirty = world::defaultCosmicOcean();
    dirty.quality.planetCells = 900.0f;
    CHECK(world::packCosmicOcean(dirty, 1.0f, 0.0).planet3.w <= 5.0f);
}

TEST_CASE("Validation refuses what would draw nothing or draw forever", "[cosmic][ocean]") {
    {
        world::CosmicOcean o = world::defaultCosmicOcean();
        o.nebulaFar.stratum.depth = 0.0f;
        CHECK_FALSE(o.validate());
    }
    {
        world::CosmicOcean o = world::defaultCosmicOcean();
        o.starsMid.stratum.parallax = 1.4f;
        CHECK_FALSE(o.validate());
    }
    {
        world::CosmicOcean o = world::defaultCosmicOcean();
        o.quality.planetCells = 9.0f;
        CHECK_FALSE(o.validate());
    }
    {
        world::CosmicOcean o = world::defaultCosmicOcean();
        o.planets.ringSize = 0.5f; // a ring inside its planet
        CHECK_FALSE(o.validate());
    }
    {
        world::CosmicOcean o = world::defaultCosmicOcean();
        o.mask.outerAngle = o.mask.innerAngle - 1.0f;
        CHECK_FALSE(o.validate());
    }
}

TEST_CASE("Every style is a palette and leaves the composition alone", "[cosmic][ocean]") {
    const auto styles = world::cosmicOceanStyleNames();
    REQUIRE(styles.size() >= 5); // §14 names five; "Deep Void" is the sixth
    CHECK_FALSE(world::applyCosmicOceanStyle(*(new world::CosmicOcean), "Not A Style"));

    std::set<std::string> distinct;
    for (const auto style : styles) {
        world::CosmicOcean o = world::defaultCosmicOcean();
        const glm::vec3 centre = o.center;
        const float planetDepth = o.planets.stratum.depth;
        const float planetDensity = o.planets.stratum.density;
        const float dustParallax = o.dust.stratum.parallax;

        REQUIRE(world::applyCosmicOceanStyle(o, style));
        REQUIRE(o.validate());
        // ADR-387's rule for the vortex's centre, applied here: a preset may not move where the
        // thing is, because that silently unanchors a composition the scene authored.
        CHECK(o.center == centre);
        CHECK(o.planets.stratum.depth == planetDepth);
        CHECK(o.planets.stratum.density == planetDensity);
        CHECK(o.dust.stratum.parallax == dustParallax);

        distinct.insert(std::to_string(o.color.primary.r) + "," + std::to_string(o.color.primary.g) +
                        "," + std::to_string(o.color.primary.b) + "/" +
                        std::to_string(o.color.secondary.r));
    }
    // §14 asks for palettes that are actually different, not one palette with six names.
    CHECK(distinct.size() == styles.size());
}

TEST_CASE("Every panel row names a field the tables carry", "[cosmic][ocean][ui]") {
    // ADR-382, and the whole reason `cosmicOceanRows()` is a table rather than a page of ImGui
    // calls: the panel computes `atmos/<name>/<leaf>` from these strings, so the test computes it
    // the same way. A wrong leaf neither fails to compile nor throws -- the section just draws an
    // empty box indistinguishable from "this scene has no such effect", which is exactly the defect
    // the owner found by eye.
    std::set<std::string> known;
    for (const auto& f : world::cosmicFloatFields()) known.insert(f.leaf);
    for (const auto& c : world::cosmicColorFields()) known.insert(c.leaf);
    for (const auto& b : world::cosmicBoolFields()) known.insert(b.leaf);

    std::set<std::string> shown;
    const auto walk = [&](std::span<const ui::EffectRow> rows) {
        for (const auto& row : rows) {
            INFO("panel row '" << row.label << "' names leaf '" << row.leaf << "'");
            CHECK(known.contains(std::string(row.leaf)));
            CHECK(shown.insert(std::string(row.leaf)).second); // and names it once
        }
    };
    walk(ui::cosmicOceanRows());
    walk(ui::cosmicOceanAdvancedRows());

    // Each of §28's twelve sections has to actually start somewhere, or the panel is one long list.
    std::set<std::string> sections;
    for (const auto& row : ui::cosmicOceanRows()) {
        if (!row.section.empty()) sections.insert(std::string(row.section));
    }
    for (const auto& row : ui::cosmicOceanAdvancedRows()) {
        if (!row.section.empty()) sections.insert(std::string(row.section));
    }
    CHECK(sections.size() >= 10);

    // The other direction, which is the one ADR-350 is about: a registered parameter no panel row
    // reaches is a control that exists and cannot be found. Not every leaf belongs in the World
    // Effects panel -- the Parameters panel is the exhaustive surface -- so this is a floor rather
    // than an equality, set high enough that forgetting a whole section fails it.
    CHECK(shown.size() * 10 >= known.size() * 9);
}
