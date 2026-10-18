// Generative R&D (docs/prototypes/generative-rd/): the CPU references of the two grid modes it added.
//   ADR-1450, the oscillator grid: validation and JSON, free running at the natural frequency, synchronisation
//   (the Kuramoto transition), two populations that lock separately, kicks that stay where they land, a coupling
//   mask, unit phases and bounded readouts under extreme settings, determinism.
//   ADR-1451, the reaction-diffusion plane: validation, defaults that leave the old grid's JSON alone, `iterations`
//   as an exact split of `speed`, the isotropic 9-point stencil, and the regime field.

#include "spatial/field.hpp"
#include "spatial/grid_field.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <complex>

using namespace avgen;
using Catch::Approx;

namespace {

spatial::GridField oscillators(int n, float half = 8.0f) {
    spatial::GridField g;
    g.name = "osc";
    g.mode = spatial::GridMode::Oscillator;
    g.wrap = spatial::GridWrap::Wrap;
    g.resolution = {n, 1, n};
    g.boundsMin = {-half, -1.0f, -half};
    g.boundsMax = {half, 1.0f, half};
    g.simRate = 60.0f;
    return g;
}

// Steps `seconds` of sub-steps, each at its own second (ADR-1119).
void run(spatial::GridField& g, double seconds, const spatial::FieldSet* set = nullptr, long first = 0) {
    const long steps = std::lround(seconds * g.simRate);
    for (long s = first; s < first + steps; ++s) {
        g.step(1.0f / g.simRate, static_cast<double>(s + 1) / g.simRate, set);
    }
}

// |mean e^(i theta)| over the cells for which `take(cell)` holds: the Kuramoto order parameter.
template <typename F>
float order(const spatial::GridField& g, F take) {
    std::complex<double> sum = 0.0;
    int count = 0;
    for (int k = 0; k < g.resolution.z; ++k) {
        for (int i = 0; i < g.resolution.x; ++i) {
            if (!take(k * g.resolution.x + i)) {
                continue;
            }
            const std::size_t b = g.index(i, 0, k);
            sum += std::complex<double>(g.data[b], g.data[b + 1]);
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(std::abs(sum) / count) : 0.0f;
}

float order(const spatial::GridField& g) {
    return order(g, [](int) { return true; });
}

} // namespace

TEST_CASE("An oscillator grid is a plane with its settings in range and round-trips", "[grid][oscillator]") {
    spatial::GridField g = oscillators(1024);
    CHECK(g.validate().has_value());
    CHECK(g.plane());
    CHECK(g.components() == 4);
    auto broken = [](auto edit) {
        spatial::GridField o = oscillators(32);
        edit(o);
        return !o.validate().has_value();
    };
    CHECK(broken([](spatial::GridField& o) { o.resolution = {32, 2, 32}; }));
    CHECK(broken([](spatial::GridField& o) { o.resolution = {2048, 1, 32}; }));
    CHECK(broken([](spatial::GridField& o) { o.couplingRadius = 0; }));
    CHECK(broken([](spatial::GridField& o) { o.couplingRadius = 5; }));
    CHECK(broken([](spatial::GridField& o) { o.links = 5; }));
    CHECK(broken([](spatial::GridField& o) { o.linkRange = 0.5f; }));
    CHECK(broken([](spatial::GridField& o) { o.coupling = -1.0f; }));
    CHECK(broken([](spatial::GridField& o) { o.spread = -0.1f; }));
    CHECK(broken([](spatial::GridField& o) { o.populationMix = 1.5f; }));
    CHECK(broken([](spatial::GridField& o) { o.seedAmount = 2.0f; }));
    CHECK(broken([](spatial::GridField& o) { o.persistence = 0.0f; }));
    CHECK(broken([](spatial::GridField& o) { o.iterations = 0; }));
    CHECK(broken([](spatial::GridField& o) { o.speed = 0.0f; }));
    CHECK(broken([](spatial::GridField& o) { o.frequency = std::nanf(""); }));

    g.coupling = 3.5f;
    g.phaseLag = 1.2f;
    g.couplingRadius = 3;
    g.links = 2;
    g.linkRange = 40.0f;
    g.linkWeight = 0.5f;
    g.populationMix = 0.4f;
    g.detune = 0.1f;
    g.crossCoupling = 0.2f;
    g.persistence = 2.0f;
    g.noise = 0.3f;
    g.frequencyField = "tune";
    g.conductivityField = "guide";
    g.injectField = "kick";
    g.speed = 1.5f;
    g.iterations = 2;
    auto back = spatial::GridField::fromJson(g.toJson());
    REQUIRE(back.has_value());
    CHECK(back->mode == spatial::GridMode::Oscillator);
    CHECK(back->structuralHash() == g.structuralHash());
    CHECK(back->layoutHash() == g.layoutHash());
    CHECK(back->phaseLag == 1.2f);
    CHECK(back->couplingRadius == 3);
    CHECK(back->links == 2);
    CHECK(back->frequencyField == "tune");
    CHECK(back->conductivityField == "guide");
    // A behaviour setting is not layout; a field it reads is.
    spatial::GridField b = g;
    b.coupling = 9.0f;
    CHECK(b.layoutHash() == g.layoutHash());
    CHECK(b.structuralHash() != g.structuralHash());
    b.frequencyField = "other";
    CHECK(b.layoutHash() != g.layoutHash());
}

TEST_CASE("An uncoupled oscillator turns at its natural frequency", "[grid][oscillator]") {
    spatial::GridField g = oscillators(8);
    g.coupling = 0.0f;
    g.spread = 0.0f;
    g.frequency = 0.25f; // a quarter turn a second
    g.seedAmount = 0.0f;
    g.reset();
    run(g, 1.0);
    for (int k = 0; k < 8; ++k) {
        for (int i = 0; i < 8; ++i) {
            const std::size_t b = g.index(i, 0, k);
            CHECK(g.data[b] == Approx(0.0f).margin(2e-4));    // cos(pi / 2)
            CHECK(g.data[b + 1] == Approx(1.0f).margin(2e-4)); // sin(pi / 2)
        }
    }
    // speed doubles model time; iterations only split it
    spatial::GridField fast = oscillators(8);
    fast.coupling = 0.0f;
    fast.spread = 0.0f;
    fast.seedAmount = 0.0f;
    fast.speed = 2.0f;
    fast.iterations = 4;
    fast.reset();
    run(fast, 1.0);
    CHECK(fast.data[fast.index(3, 0, 3)] == Approx(-1.0f).margin(2e-4)); // half a turn
}

TEST_CASE("Coupling synchronises a lattice of detuned oscillators; without it they stay incoherent",
          "[grid][oscillator]") {
    auto make = [](float coupling) {
        spatial::GridField g = oscillators(32);
        // Phases spread over half the circle: a random start on a local lattice freezes in phase vortices (the 2D XY
        // model's defects, which the scenes use), and their global order stays near 0.7 however long it runs.
        g.seedAmount = 0.5f;
        g.spread = 0.08f;
        g.frequency = 0.3f;
        g.couplingRadius = 2;
        g.coupling = coupling;
        g.seed = 7;
        g.reset();
        return g;
    };
    spatial::GridField free = make(0.0f);
    spatial::GridField locked = make(12.0f);
    const float start = order(locked);
    run(free, 20.0);
    run(locked, 20.0);
    CHECK(order(free) < 0.2f); // the detuned free runners dephase
    CHECK(start < 0.7f);
    CHECK(order(locked) > 0.9f);
    // R, the local order, agrees: high where locked, low where free.
    float meanR = 0.0f;
    for (int k = 0; k < 32; ++k) {
        for (int i = 0; i < 32; ++i) {
            meanR += locked.data[locked.index(i, 0, k) + 2];
        }
    }
    CHECK(meanR / 1024.0f > 0.9f);
}

TEST_CASE("Two populations with no cross-coupling lock separately and drift apart", "[grid][oscillator]") {
    spatial::GridField g = oscillators(32);
    g.seedAmount = 1.0f;
    g.spread = 0.0f;
    g.frequency = 0.3f;
    g.detune = 0.17f; // population B runs faster
    g.populationMix = 0.5f;
    g.crossCoupling = 0.0f;
    g.couplingRadius = 4; // each population needs reach: its own members are half the disk
    g.links = 4;
    g.linkRange = 12.0f;
    g.coupling = 15.0f;
    g.seed = 3;
    g.reset();
    run(g, 25.0);
    // Which cells are population B, recovered the way the kernel does: compare a free-running copy's phases.
    spatial::GridField probe = oscillators(32);
    probe.seedAmount = 0.0f;
    probe.spread = 0.0f;
    probe.frequency = 0.0f;
    probe.detune = 0.25f;
    probe.populationMix = 0.5f;
    probe.coupling = 0.0f;
    probe.seed = 3;
    probe.reset();
    run(probe, 1.0); // population B turned a quarter, A did not move
    auto isB = [&](int cell) {
        const int i = cell % 32;
        const int k = cell / 32;
        return probe.data[probe.index(i, 0, k) + 1] > 0.5f;
    };
    auto isA = [&](int cell) { return !isB(cell); };
    CHECK(order(g, isA) > 0.85f);
    CHECK(order(g, isB) > 0.85f);
    // Each population is coherent; together they are not always in step (they beat at the detune).
    float lowest = 1.0f;
    for (int s = 0; s < 6; ++s) {
        run(g, 0.5, nullptr, std::lround((25.0 + 0.5 * s) * 60.0));
        lowest = std::min(lowest, order(g));
    }
    CHECK(lowest < 0.6f);
}

TEST_CASE("A kick moves only the phases it reaches, and a zero coupling mask decouples", "[grid][oscillator]") {
    spatial::FieldSet set;
    spatial::FieldSpec kick;
    kick.name = "kick";
    kick.kind = spatial::FieldKind::Box;
    kick.position = {-6.0f, 0.0f, -6.0f};
    kick.size = {1.0f, 2.0f, 1.0f};
    kick.softness = 0.01f;
    set.fields.push_back(kick);
    spatial::FieldSpec none;
    none.name = "none";
    none.kind = spatial::FieldKind::Constant;
    none.strength = 0.0f;
    set.fields.push_back(none);

    auto make = [](const char* inject) {
        spatial::GridField g = oscillators(32);
        g.seedAmount = 0.0f;
        g.spread = 0.0f;
        g.coupling = 0.0f; // nothing spreads the kick: it stays where it landed
        g.injectField = inject;
        g.injectRate = 30.0f;
        g.reset();
        return g;
    };
    spatial::GridField kicked = make("kick");
    spatial::GridField control = make("");
    run(kicked, 0.5, &set);
    run(control, 0.5, &set);
    int moved = 0;
    int movedOutside = 0;
    for (int k = 0; k < 32; ++k) {
        for (int i = 0; i < 32; ++i) {
            const std::size_t b = kicked.index(i, 0, k);
            const bool differs = std::abs(kicked.data[b] - control.data[b]) > 1e-3f;
            const glm::vec3 c = kicked.cellCenter(i, 0, k);
            const bool inside = std::abs(c.x + 6.0f) < 1.0f && std::abs(c.z + 6.0f) < 1.0f;
            moved += differs ? 1 : 0;
            movedOutside += (differs && !inside) ? 1 : 0;
            if (inside) {
                CHECK(kicked.data[b + 3] > 0.5f); // activity records the kick
            }
        }
    }
    CHECK(moved > 0);
    CHECK(movedOutside == 0);

    // A coupling mask of 0 is K = 0.
    spatial::GridField masked = oscillators(16);
    masked.seedAmount = 1.0f;
    masked.coupling = 10.0f;
    masked.conductivityField = "none";
    masked.reset();
    spatial::GridField freeRunning = masked;
    freeRunning.coupling = 0.0f;
    freeRunning.conductivityField.clear();
    run(masked, 2.0, &set);
    run(freeRunning, 2.0, &set);
    for (std::size_t c = 0; c < masked.data.size(); c += 4) {
        CHECK(masked.data[c] == Approx(freeRunning.data[c]).margin(1e-5));
    }
}

TEST_CASE("Oscillator phases stay unit and readouts bounded under extreme settings, and runs repeat",
          "[grid][oscillator]") {
    auto make = []() {
        spatial::GridField g = oscillators(24);
        g.seedAmount = 1.0f;
        g.coupling = 1000.0f;
        g.noise = 100.0f;
        g.phaseLag = 3.0f;
        g.spread = 50.0f;
        g.links = 4;
        g.linkRange = 200.0f;
        g.linkWeight = 100.0f;
        g.populationMix = 0.5f;
        g.crossCoupling = 10.0f;
        g.persistence = 0.001f;
        g.wrap = spatial::GridWrap::Clamp;
        g.reset();
        return g;
    };
    spatial::GridField a = make();
    spatial::GridField b = make();
    run(a, 3.0);
    run(b, 3.0);
    CHECK(a.data == b.data);
    for (std::size_t c = 0; c < a.data.size(); c += 4) {
        REQUIRE(std::isfinite(a.data[c]));
        CHECK(a.data[c] * a.data[c] + a.data[c + 1] * a.data[c + 1] == Approx(1.0f).margin(1e-4));
        CHECK(a.data[c + 2] >= 0.0f);
        CHECK(a.data[c + 2] <= 1.0f);
        CHECK(a.data[c + 3] >= 0.0f);
        CHECK(a.data[c + 3] <= 4.0f);
    }
    // Reset is idempotent and re-seeds.
    spatial::GridField c = make();
    c.reset();
    CHECK(c.data == make().data);
}

// ---- ADR-1451 --------------------------------------------------------------------------------------

namespace {

spatial::GridField rdPlane(int n) {
    spatial::GridField g;
    g.name = "rd";
    g.mode = spatial::GridMode::ReactionDiffusion;
    g.wrap = spatial::GridWrap::Wrap;
    g.resolution = {n, 1, n};
    g.boundsMin = {0.0f, -1.0f, 0.0f};
    g.boundsMax = {static_cast<float>(n), 1.0f, static_cast<float>(n)};
    g.diffusionA = 0.3f;
    g.diffusionB = 0.15f;
    g.feed = 0.055f;
    g.kill = 0.062f;
    return g;
}

void seedCentre(spatial::GridField& g) {
    g.reset();
    const int c = g.resolution.x / 2;
    for (int k = c - 6; k < c + 6; ++k) { // a 5 x 5 seed dies; 12 x 12 grows (measured)
        for (int i = c - 6; i < c + 6; ++i) {
            g.data[g.index(i, 0, k)] = 0.5f;
            g.data[g.index(i, 0, k) + 1] = 0.25f;
        }
    }
}

} // namespace

TEST_CASE("A reaction-diffusion grid may be a plane up to 1024 and keeps its old JSON by default", "[grid][rd]") {
    spatial::GridField g = rdPlane(1024);
    CHECK(g.validate().has_value());
    CHECK(g.reactionPlane());
    g.resolution = {1025, 1, 16};
    CHECK_FALSE(g.validate().has_value());
    g.resolution = {129, 2, 16}; // a volume keeps the volume limit
    CHECK_FALSE(g.validate().has_value());
    spatial::GridField d = rdPlane(64);
    const nlohmann::json j = d.toJson();
    CHECK_FALSE(j.contains("speed"));
    CHECK_FALSE(j.contains("iterations"));
    CHECK_FALSE(j.contains("regimeField"));
    spatial::GridField e = d;
    e.speed = 600.0f;
    e.iterations = 10;
    e.regimeField = "regime";
    e.feedB = 0.03f;
    e.killB = 0.057f;
    CHECK(e.structuralHash() != d.structuralHash());
    auto back = spatial::GridField::fromJson(e.toJson());
    REQUIRE(back.has_value());
    CHECK(back->structuralHash() == e.structuralHash());
    CHECK(back->iterations == 10);
    CHECK(back->regimeField == "regime");
    e.iterations = 65;
    CHECK_FALSE(e.validate().has_value());
    e.iterations = 4;
    e.speed = 0.0f;
    CHECK_FALSE(e.validate().has_value());
}

TEST_CASE("Reaction-diffusion iterations split a sub-step exactly, and the plane stencil is isotropic",
          "[grid][rd]") {
    spatial::GridField once = rdPlane(32);
    once.speed = 4.0f;
    once.iterations = 4;
    seedCentre(once);
    spatial::GridField four = rdPlane(32);
    seedCentre(four);
    once.step(1.0f, 1.0);
    for (int s = 0; s < 4; ++s) {
        four.step(1.0f, 1.0);
    }
    CHECK(once.data == four.data);

    // Pure diffusion of B from one cell, no reaction (A = 0): the 9-point stencil feeds a diagonal neighbour a
    // quarter of what it feeds an adjacent one (the 5-point stencil would feed it nothing).
    spatial::GridField g = rdPlane(9);
    g.diffusionA = 0.0f;
    g.diffusionB = 0.1f;
    g.feed = 0.0f;
    g.kill = 0.0f;
    g.reset();
    for (std::size_t c = 0; c < g.data.size(); c += 2) {
        g.data[c] = 0.0f;
    }
    g.data[g.index(4, 0, 4) + 1] = 1.0f;
    g.step(1.0f, 1.0);
    const float adjacent = g.data[g.index(5, 0, 4) + 1];
    const float diagonal = g.data[g.index(5, 0, 5) + 1];
    CHECK(adjacent == Approx(0.1f * 4.0f / 6.0f));
    CHECK(diagonal == Approx(0.25f * adjacent));
    CHECK(g.data[g.index(4, 0, 4) + 1] == Approx(1.0f - 0.1f * 20.0f / 6.0f));
}

TEST_CASE("A regime field of 1 is the B regime, and a plane pattern forms and stays in range", "[grid][rd]") {
    spatial::FieldSet set;
    spatial::FieldSpec one;
    one.name = "regime";
    one.kind = spatial::FieldKind::Constant;
    set.fields.push_back(one);
    spatial::GridField blended = rdPlane(32);
    blended.regimeField = "regime";
    blended.feedB = 0.03f;
    blended.killB = 0.057f;
    seedCentre(blended);
    spatial::GridField direct = rdPlane(32);
    direct.feed = 0.03f;
    direct.kill = 0.057f;
    seedCentre(direct);
    for (int s = 0; s < 50; ++s) {
        blended.step(1.0f, 1.0, &set);
        direct.step(1.0f, 1.0, &set);
    }
    CHECK(blended.data == direct.data);

    spatial::GridField g = rdPlane(64);
    g.speed = 60.0f * 16.0f;
    g.iterations = 16;
    seedCentre(g);
    run(g, 2.0); // ~2000 model steps
    float minB = 1.0f;
    float maxB = 0.0f;
    for (std::size_t c = 0; c < g.data.size(); c += 2) {
        REQUIRE(std::isfinite(g.data[c]));
        CHECK(g.data[c] >= 0.0f);
        CHECK(g.data[c] <= 1.0f);
        minB = std::min(minB, g.data[c + 1]);
        maxB = std::max(maxB, g.data[c + 1]);
    }
    CHECK(maxB - minB > 0.2f); // a pattern, not a uniform field
}

// ---- ADR-1452 --------------------------------------------------------------------------------------

TEST_CASE("A scalar or vector grid one cell thick may be a plane up to 1024, and steps as before", "[grid][rd]") {
    spatial::GridField g;
    g.name = "dye";
    g.mode = spatial::GridMode::Scalar;
    g.resolution = {1024, 1, 512};
    g.boundsMin = {-8.0f, -1.0f, -8.0f};
    g.boundsMax = {8.0f, 1.0f, 8.0f};
    CHECK(g.validate().has_value());
    g.mode = spatial::GridMode::Vector; // 1024 x 512 x 4 floats: inside the table
    CHECK(g.validate().has_value());
    g.resolution = {1025, 1, 16};
    CHECK_FALSE(g.validate().has_value());
    g.resolution = {256, 2, 16}; // not a plane: the volume limit
    CHECK_FALSE(g.validate().has_value());

    // A plane diffuses in x and z only, conserving mass (y wraps or clamps onto itself).
    spatial::GridField d;
    d.name = "dye";
    d.mode = spatial::GridMode::Scalar;
    d.wrap = spatial::GridWrap::Wrap;
    d.resolution = {200, 1, 200};
    d.boundsMin = {0.0f, -1.0f, 0.0f};
    d.boundsMax = {200.0f, 1.0f, 200.0f};
    d.diffusion = 2.0f;
    d.diffuseIterations = 4;
    d.reset();
    d.data[d.index(100, 0, 100)] = 1.0f;
    d.step(1.0f / 60.0f, 0.0);
    double mass = 0.0;
    for (const float v : d.data) {
        mass += v;
    }
    CHECK(mass == Approx(1.0).margin(1e-4));
    CHECK(d.data[d.index(101, 0, 100)] > 0.0f);
    CHECK(d.data[d.index(101, 0, 100)] == Approx(d.data[d.index(100, 0, 101)]));
}

// ---- ADR-1453 --------------------------------------------------------------------------------------

TEST_CASE("A reaction-diffusion grid may start from authored values; without them it starts as before", "[grid][rd]") {
    // Without authored values: the canonical seed, unchanged (A = 1 outside the fbm blobs, B = 0 there) and the same
    // hashes as a grid that never had the key.
    spatial::GridField plain = rdPlane(32);
    plain.seedAmount = 1.0f;
    plain.reset();
    int blobs = 0;
    for (std::size_t c = 0; c < plain.data.size(); c += 2) {
        const bool blob = plain.data[c + 1] > 0.0f;
        blobs += blob ? 1 : 0;
        CHECK(plain.data[c] == Approx(blob ? 0.5f : 1.0f));
        CHECK(plain.data[c + 1] == Approx(blob ? 0.25f : 0.0f));
    }
    CHECK(blobs > 0);
    CHECK_FALSE(plain.toJson().contains("initial"));

    // With them: a reset is exactly the authored state, before and after stepping, and the JSON round-trips.
    spatial::GridField authored = rdPlane(16);
    authored.initial.resize(authored.floatCount());
    for (std::size_t c = 0; c < authored.initial.size(); c += 2) {
        authored.initial[c] = 0.4f + 0.001f * static_cast<float>(c % 97);
        authored.initial[c + 1] = 0.3f - 0.001f * static_cast<float>(c % 89);
    }
    REQUIRE(authored.validate().has_value());
    authored.reset();
    CHECK(authored.data == authored.initial);
    for (int s = 0; s < 20; ++s) {
        authored.step(1.0f, 1.0);
    }
    CHECK(authored.data != authored.initial);
    authored.reset(); // what a seek with nothing to restore does (rendering::Simulation uploads reset())
    CHECK(authored.data == authored.initial);
    auto back = spatial::GridField::fromJson(authored.toJson());
    REQUIRE(back.has_value());
    CHECK(back->initial == authored.initial);
    CHECK(back->layoutHash() == authored.layoutHash());
    spatial::GridField other = authored;
    other.initial.clear();
    CHECK(other.layoutHash() != authored.layoutHash()); // a changed start re-seeds the grid

    // The wrong length is refused.
    authored.initial.pop_back();
    CHECK_FALSE(authored.validate().has_value());
}
