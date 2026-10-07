// ADR-1201: the excitable propagation grid, CPU side -- JSON round trip, validation, the field channel
// selector, and the reference step's behaviour: a calibrated wave speed (independent of cell size and
// simRate), refractory blocking, conductivity barriers, and energy / wake time constants.

#include "spatial/field.hpp"
#include "spatial/grid_field.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

// An n x 1 x n medium over [-half, half] on x and z, at rest.
spatial::GridField medium(int n, float half, float simRate = 60.0f) {
    spatial::GridField g;
    g.name = "prop";
    g.mode = spatial::GridMode::Excitable;
    g.resolution = {n, 1, n};
    g.boundsMin = {-half, -1.0f, -half};
    g.boundsMax = {half, 1.0f, half};
    g.simRate = simRate;
    g.reset();
    return g;
}

// Fires cell (i, k) at t = 0: r = 1, the moment of firing.
void impulse(spatial::GridField& g, int i, int k) { g.data[g.index(i, 0, k) + 1] = 1.0f; }

// Steps `seconds` of fixed steps, each at its own second (ADR-1119), from step `first` + 1.
void run(spatial::GridField& g, double seconds, const spatial::FieldSet* set = nullptr, long first = 0) {
    const long steps = std::lround(seconds * g.simRate);
    for (long s = first; s < first + steps; ++s) {
        g.step(1.0f / g.simRate, static_cast<double>(s + 1) / g.simRate, set);
    }
}

float channel(const spatial::GridField& g, int i, int k, int c) { return g.data[g.index(i, 0, k) + static_cast<std::size_t>(c)]; }

// Seconds since cell (i, k) last fired (infinite when it never has).
float ageOf(const spatial::GridField& g, int i, int k) {
    const float r = channel(g, i, k, 1);
    return r > 0.0f ? -g.refractoryTime * std::log(r) : 1e30f;
}

struct Front {
    float fittedSpeed = 0.0f; // least squares of distance against arrival time, through the origin
    float radius = 0.0f;      // the farthest fired cell
    float slowest = 1e30f;    // the slowest direction (distance / arrival over cells beyond 2 m)
    float fastest = 0.0f;
};

// The front from an impulse at cell (c, c) after `seconds`.
Front measure(const spatial::GridField& g, int c, float seconds) {
    Front f;
    double num = 0.0;
    double den = 0.0;
    const glm::vec3 origin = g.cellCenter(c, 0, c);
    for (int k = 0; k < g.resolution.z; ++k) {
        for (int i = 0; i < g.resolution.x; ++i) {
            if (channel(g, i, k, 1) <= 0.0f) {
                continue;
            }
            const glm::vec3 p = g.cellCenter(i, 0, k);
            const float d = glm::length(glm::vec2(p.x - origin.x, p.z - origin.z));
            const float arrival = seconds - ageOf(g, i, k);
            f.radius = std::max(f.radius, d);
            if (d > 2.0f && arrival > 0.0f) {
                num += static_cast<double>(d) * arrival;
                den += static_cast<double>(arrival) * arrival;
                f.slowest = std::min(f.slowest, d / arrival);
                f.fastest = std::max(f.fastest, d / arrival);
            }
        }
    }
    f.fittedSpeed = den > 0.0 ? static_cast<float>(num / den) : 0.0f;
    return f;
}

} // namespace

TEST_CASE("An excitable grid round-trips its mode and every behaviour through JSON", "[grid][excitable]") {
    spatial::GridField g = medium(512, 64.0f);
    g.resolution = {512, 1, 1024};
    g.wrap = spatial::GridWrap::Wrap;
    g.injectField = "kick";
    g.conductivityField = "banks";
    g.injectRate = 2.0f;
    g.threshold = 0.4f;
    g.coupling = 1.3f;
    g.waveSpeed = 6.5f;
    g.riseRate = 12.0f;
    g.excitationDecay = 0.8f;
    g.refractoryTime = 2.5f;
    g.refractoryStrength = 3.0f;
    g.energyTime = 4.0f;
    g.wakeTime = 45.0f;
    g.noise = 0.3f;
    g.ceiling = 0.9f;
    g.seed = 77;
    REQUIRE(g.validate().has_value());

    const nlohmann::json j = g.toJson();
    CHECK(j.at("mode") == "excitable");
    CHECK(j.at("conductivityField") == "banks");
    auto back = spatial::GridField::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->mode == spatial::GridMode::Excitable);
    CHECK(back->components() == 4);
    CHECK(back->conductivityField == "banks");
    CHECK(back->threshold == 0.4f);
    CHECK(back->coupling == 1.3f);
    CHECK(back->waveSpeed == 6.5f);
    CHECK(back->riseRate == 12.0f);
    CHECK(back->excitationDecay == 0.8f);
    CHECK(back->refractoryTime == 2.5f);
    CHECK(back->refractoryStrength == 3.0f);
    CHECK(back->energyTime == 4.0f);
    CHECK(back->wakeTime == 45.0f);
    CHECK(back->noise == 0.3f);
    CHECK(back->ceiling == 0.9f);
    CHECK(back->structuralHash() == g.structuralHash());
    CHECK(back->layoutHash() == g.layoutHash());

    // The excitable keys are an excitable grid's only: a scalar grid neither writes them nor hashes them.
    spatial::GridField scalar;
    const nlohmann::json sj = scalar.toJson();
    CHECK_FALSE(sj.contains("threshold"));
    CHECK_FALSE(sj.contains("waveSpeed"));
    spatial::GridField moved = scalar;
    moved.waveSpeed = 9.0f;
    CHECK(moved.structuralHash() == scalar.structuralHash());

    // Behaviour moves the settings hash (checkpoints drop) but not the layout (the state continues);
    // the conductivity field's name is layout, like every input field's.
    spatial::GridField faster = g;
    faster.waveSpeed = 9.0f;
    CHECK(faster.structuralHash() != g.structuralHash());
    CHECK(faster.layoutHash() == g.layoutHash());
    spatial::GridField rerouted = g;
    rerouted.conductivityField = "rock";
    CHECK(rerouted.layoutHash() != g.layoutHash());
}

TEST_CASE("An excitable grid is a plane with its behaviour in range", "[grid][excitable]") {
    CHECK(medium(1024, 64.0f).validate().has_value());
    auto broken = [](auto edit) {
        spatial::GridField g = medium(64, 8.0f);
        edit(g);
        return !g.validate().has_value();
    };
    CHECK(broken([](spatial::GridField& g) { g.resolution = {64, 2, 64}; }));
    CHECK(broken([](spatial::GridField& g) { g.resolution = {2048, 1, 64}; }));
    CHECK(broken([](spatial::GridField& g) { g.threshold = 0.0f; }));
    CHECK(broken([](spatial::GridField& g) { g.coupling = -1.0f; }));
    CHECK(broken([](spatial::GridField& g) { g.waveSpeed = 0.0f; }));
    CHECK(broken([](spatial::GridField& g) { g.riseRate = 0.0f; }));
    CHECK(broken([](spatial::GridField& g) { g.excitationDecay = 0.0f; }));
    CHECK(broken([](spatial::GridField& g) { g.refractoryTime = 0.0f; }));
    CHECK(broken([](spatial::GridField& g) { g.refractoryStrength = -0.5f; }));
    CHECK(broken([](spatial::GridField& g) { g.energyTime = 0.0f; }));
    CHECK(broken([](spatial::GridField& g) { g.wakeTime = -1.0f; }));
    CHECK(broken([](spatial::GridField& g) { g.noise = 1.5f; }));
    CHECK(broken([](spatial::GridField& g) { g.ceiling = -0.1f; }));
    CHECK(broken([](spatial::GridField& g) { g.seedAmount = 0.2f; }));
    // The JSON reader validates too.
    CHECK_FALSE(spatial::GridField::fromJson(nlohmann::json::parse(
                    R"({"name": "p", "mode": "excitable", "resolution": [64, 4, 64]})"))
                    .has_value());
    CHECK(spatial::GridField::fromJson(nlohmann::json::parse(
              R"({"name": "p", "mode": "excitable", "resolution": [1024, 1, 1024], "boundsMin": [-64, -1, -64],
                  "boundsMax": [64, 1, 64], "waveSpeed": 5})"))
              .has_value());
}

TEST_CASE("A front from one impulse travels at waveSpeed whatever the cell size and the sim rate",
          "[grid][excitable]") {
    struct Case {
        int n;
        float half;
        float simRate;
        float speed;
    };
    // 0.25 m cells at 60 Hz; 0.33 m cells at 120 Hz; 0.5 m cells at 30 Hz, and a fast front.
    for (const Case c : {Case{128, 16.0f, 60.0f, 3.0f}, Case{96, 16.0f, 120.0f, 3.0f}, Case{64, 16.0f, 30.0f, 3.0f},
                         Case{128, 32.0f, 60.0f, 7.0f}}) {
        spatial::GridField g = medium(c.n, c.half, c.simRate);
        g.waveSpeed = c.speed;
        const int centre = c.n / 2;
        impulse(g, centre, centre);
        const float seconds = 4.0f;
        run(g, seconds);
        const Front f = measure(g, centre, seconds);
        INFO("cells " << c.n << " over " << 2.0f * c.half << " m at " << c.simRate << " Hz, waveSpeed " << c.speed
                      << ": fitted " << f.fittedSpeed << " m/s, radius " << f.radius << " m (expected "
                      << c.speed * seconds << "), directions " << f.slowest << ".." << f.fastest);
        CHECK(std::abs(f.fittedSpeed - c.speed) < 0.10f * c.speed);
        CHECK(std::abs(f.radius - c.speed * seconds) < 0.10f * c.speed * seconds);
        // Isotropy: the 5 x 5 disk stencil keeps every direction within 4% (exact on the axes).
        CHECK(f.slowest > 0.96f * c.speed);
        CHECK(f.fastest < 1.01f * c.speed);
        // The front is a ring: the impulse fired once and the medium behind the front did not refire it.
        CHECK(ageOf(g, centre, centre) == Approx(seconds).margin(1e-3));
        CHECK(channel(g, centre, centre, 0) < 0.01f); // its excitation has long faded
    }
}

TEST_CASE("Refractory cells behind a front resist a second stimulus until they recover", "[grid][excitable]") {
    spatial::FieldSet set;
    spatial::FieldSpec poke;
    poke.name = "poke";
    poke.kind = spatial::FieldKind::Sphere;
    poke.radius = 0.2f;
    poke.softness = 0.05f;
    poke.falloff.kind = spatial::FalloffKind::None;
    spatial::GridField g = medium(128, 16.0f);
    g.waveSpeed = 4.0f;
    g.injectRate = 1.2f; // above threshold (0.5) at rest, below threshold * (1 + 4 r) while r > 0.35
    const int centre = 64;
    impulse(g, centre, centre);
    // The probe cell, 4 m along +x: the front reaches it at ~1 s.
    const int pi = centre + 16;
    poke.position = g.cellCenter(pi, 0, centre);
    set.fields.push_back(poke);

    run(g, 1.5);
    const float firedAgo = ageOf(g, pi, centre);
    REQUIRE(firedAgo < 0.6f); // the front passed
    REQUIRE(firedAgo > 0.4f);
    const float rBefore = channel(g, pi, centre, 1);
    // A stimulus 0.5 s after the front, inside the refractory window, for one step: no second firing.
    g.injectField = "poke";
    run(g, 1.0 / 60.0, &set, 90);
    g.injectField.clear();
    CHECK(ageOf(g, pi, centre) == Approx(firedAgo + 1.0f / 60.0f).margin(1e-4));
    CHECK(channel(g, pi, centre, 1) < rBefore);
    // ...and recovered, 6 s later, the same stimulus fires it (control: the stimulus is strong enough).
    run(g, 6.0, nullptr, 91);
    g.injectField = "poke";
    run(g, 1.0 / 60.0, &set, 91 + 360);
    CHECK(ageOf(g, pi, centre) < 1e-4f);
    CHECK(channel(g, pi, centre, 1) == Approx(1.0f));
}

TEST_CASE("Conductivity 0 stops a front where the medium is barren", "[grid][excitable]") {
    spatial::FieldSet set;
    spatial::FieldSpec banks; // 1 for x < 3, 0 for x > 3.25 (an inverted plane)
    banks.name = "banks";
    banks.kind = spatial::FieldKind::Plane;
    banks.position = {3.0f, 0.0f, 0.0f};
    banks.axis = {1.0f, 0.0f, 0.0f};
    banks.softness = 0.25f;
    banks.invert = true;
    banks.falloff.kind = spatial::FalloffKind::None;
    set.fields.push_back(banks);

    auto farthestX = [](const spatial::GridField& g) {
        float x = -1e30f;
        for (int k = 0; k < g.resolution.z; ++k) {
            for (int i = 0; i < g.resolution.x; ++i) {
                if (channel(g, i, k, 1) > 0.0f) {
                    x = std::max(x, g.cellCenter(i, 0, k).x);
                }
            }
        }
        return x;
    };
    spatial::GridField open = medium(128, 16.0f);
    open.waveSpeed = 4.0f;
    impulse(open, 64, 64);
    spatial::GridField barred = open;
    barred.conductivityField = "banks";
    run(open, 5.0, &set);
    run(barred, 5.0, &set);
    CHECK(farthestX(open) > 15.0f); // control: unbarred, the front crosses the whole grid
    CHECK(farthestX(barred) < 3.25f);
    // The rest of the medium still conducts: the front went everywhere else.
    CHECK(channel(barred, 64 - 40, 64, 1) > 0.0f);
    CHECK(channel(barred, 64, 64 + 40, 1) > 0.0f);
}

TEST_CASE("Energy and wake integrate the excitation and decay with their time constants", "[grid][excitable]") {
    spatial::GridField g = medium(4, 2.0f);
    g.energyTime = 2.0f;
    g.wakeTime = 10.0f;
    g.excitationDecay = 1e6f; // u holds at 1: no cell fires (r = 0) and nothing decays it
    for (std::size_t c = 0; c < g.data.size(); c += 4) {
        g.data[c] = 1.0f;
    }
    run(g, 2.0);
    CHECK(channel(g, 1, 1, 2) == Approx(1.0f - std::exp(-1.0f)).margin(2e-3));
    CHECK(channel(g, 1, 1, 3) == Approx(1.0f - std::exp(-0.2f)).margin(2e-3));
    run(g, 8.0, nullptr, 120);
    CHECK(channel(g, 1, 1, 3) == Approx(1.0f - std::exp(-1.0f)).margin(2e-3));
    CHECK(channel(g, 1, 1, 0) == Approx(1.0f).margin(1e-4));

    // Decay: the excitation gone, energy falls by e in energyTime and wake by e in wakeTime.
    spatial::GridField d = medium(4, 2.0f);
    d.energyTime = 2.0f;
    d.wakeTime = 10.0f;
    d.excitationDecay = 0.001f;
    for (std::size_t c = 0; c < d.data.size(); c += 4) {
        d.data[c + 2] = 1.0f;
        d.data[c + 3] = 1.0f;
    }
    run(d, 2.0);
    CHECK(channel(d, 2, 2, 2) == Approx(std::exp(-1.0f)).margin(2e-3));
    run(d, 8.0, nullptr, 120);
    CHECK(channel(d, 2, 2, 3) == Approx(std::exp(-1.0f)).margin(2e-3));
}

TEST_CASE("Noise roughens the front deterministically and never stops it", "[grid][excitable]") {
    auto ring = [](float noise) {
        spatial::GridField g = medium(96, 12.0f);
        g.noise = noise;
        g.seed = 5;
        impulse(g, 48, 48);
        run(g, 2.0);
        return g;
    };
    const spatial::GridField a = ring(0.4f);
    const spatial::GridField b = ring(0.4f);
    const spatial::GridField clean = ring(0.0f);
    CHECK(a.data == b.data);
    std::size_t differ = 0;
    std::size_t fired = 0;
    for (std::size_t c = 1; c < a.data.size(); c += 4) {
        differ += std::abs(a.data[c] - clean.data[c]) > 1e-3f ? 1u : 0u;
        fired += a.data[c] > 0.0f ? 1u : 0u;
    }
    CHECK(differ > a.cellCount() / 10);
    // Radius 8 m: about pi * 8^2 / 0.0625 = 3217 cells; a noisy front covers roughly the same area.
    CHECK(fired > 2500);
}

TEST_CASE("A grid field reads any channel of its grid", "[grid][excitable][fields]") {
    spatial::FieldSet set;
    spatial::GridField g = medium(4, 2.0f);
    for (std::size_t cell = 0; cell < g.cellCount(); ++cell) {
        for (int c = 0; c < 4; ++c) {
            g.data[cell * 4 + static_cast<std::size_t>(c)] = 0.1f * static_cast<float>(c + 1);
        }
    }
    set.grids.push_back(g);
    spatial::FieldSpec f;
    f.name = "read";
    f.kind = spatial::FieldKind::Grid;
    f.reference = "prop";
    f.falloff.kind = spatial::FalloffKind::None;
    const glm::vec3 p{0.3f, 0.0f, -0.7f};
    CHECK(spatial::sampleScalar(f, p, 0.0, &set) == Approx(0.1f)); // the default reading is u
    for (int c = 0; c < 4; ++c) {
        f.channel = c;
        CHECK(spatial::sampleScalar(f, p, 0.0, &set) == Approx(0.1f * static_cast<float>(c + 1)));
    }

    // JSON: written and hashed only when set, and in range on a grid field only.
    f.channel = 3;
    const nlohmann::json j = f.toJson();
    CHECK(j.at("channel") == 3);
    auto back = spatial::FieldSpec::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->channel == 3);
    CHECK(back->structuralHash() == f.structuralHash());
    spatial::FieldSpec plain = f;
    plain.channel = -1;
    CHECK_FALSE(plain.toJson().contains("channel"));
    CHECK(plain.structuralHash() != f.structuralHash());
    f.channel = 4;
    CHECK_FALSE(f.validate().has_value());
    spatial::FieldSpec sphere;
    sphere.kind = spatial::FieldKind::Sphere;
    sphere.channel = 0;
    CHECK_FALSE(sphere.validate().has_value());

    // Packed: the channel rides in gridRes.w above the bound and wrap bits; the default leaves it as it was.
    f.channel = 2;
    CHECK(spatial::packField(f, 0.0, &set).gridRes.w == 1.0f + 4.0f * 3.0f);
    f.channel = -1;
    CHECK(spatial::packField(f, 0.0, &set).gridRes.w == 1.0f);
    CHECK(spatial::packField(f, 0.0, &set).type == static_cast<std::uint32_t>(spatial::FieldType::Scalar));
}
