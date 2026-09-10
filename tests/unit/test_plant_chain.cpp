// Tier 1 vegetation: the chain integrator, the level-of-detail decision and localised disturbance
// (ADR-056). All of it is pure maths with no GPU in it, so the properties that decide whether a
// simulated plant looks like a plant -- that it does not explode, that its root stays in the soil,
// that it settles where the transfer function says it should, that it overshoots on the way there,
// that changing tier does not move it, that it goes to sleep and wakes again -- are checkable here
// rather than by squinting at a render.

#include "core/plant_chain.hpp"
#include "core/wind.hpp"
#include "spatial/vegetation_sim.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;
using namespace avgen;

namespace {

wind::WindParams breezyValley() {
    wind::WindParams w;
    w.enabled = true;
    w.direction = 0.62f;
    w.speed = 0.85f;
    w.regionScale = 70.0f;
    w.regionAmount = 0.5f;
    w.regionDrift = 0.045f;
    w.turbulence = 0.35f;
    w.turbulenceScale = 13.0f;
    w.turbulenceSpeed = 1.4f;
    w.gustAmount = 0.9f;
    w.gustScale = 38.0f;
    w.gustSpeed = 7.5f;
    w.gustSharpness = 3.0f;
    w.flutterScale = 2.4f;
    return w;
}

wind::VegetationMotion grass() {
    wind::VegetationMotion m;
    m.stiffness = 0.9f;
    m.mass = 0.004f;
    m.damping = 0.28f;
    m.windSensitivity = 1.0f;
    m.bendLimit = 0.5f;
    m.tipAmplitude = 0.17f;
    m.gustResponse = 1.3f;
    m.bendCurve = 1.5f;
    m.amplitudeVariance = 0.4f;
    m.simulate.enabled = true;
    m.simulate.maxDistance = 16.0f;
    m.simulate.minScreenRadius = 14.0f;
    m.simulate.budget = 64;
    return m;
}

wind::ChainParams speciesChain(const wind::VegetationMotion& m, float height) {
    wind::ChainParams p;
    p.omega0 = std::sqrt(m.stiffness / m.mass);
    p.zeta = m.damping;
    p.height = height;
    return p;
}

float segmentLength(const wind::PlantChain& c, int i) {
    const glm::vec3 prev = i == 0 ? glm::vec3(0.0f) : c.pos[static_cast<std::size_t>(i - 1)];
    return glm::length(c.pos[static_cast<std::size_t>(i)] - prev);
}

// A patch of instances on a grid, so the level-of-detail tests have something to choose from.
std::vector<spatial::InstanceRecord> patch(int side, float spacing) {
    std::vector<spatial::InstanceRecord> out;
    out.reserve(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
            spatial::InstanceRecord r{};
            r.position = glm::vec4(static_cast<float>(x) * spacing, 0.0f, static_cast<float>(z) * spacing, 1.0f);
            r.rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            r.scale = glm::vec4(1.0f);
            const auto i = static_cast<float>(z * side + x);
            r.random = glm::vec4(std::fmod(i * 0.371f, 1.0f), std::fmod(i * 0.117f, 1.0f),
                                 std::fmod(i * 0.733f, 1.0f), std::fmod(i * 0.529f, 1.0f));
            r.color = glm::vec4(1.0f);
            r.emissive = glm::vec4(0.0f);
            out.push_back(r);
        }
    }
    return out;
}

spatial::VegetationSim::Frame patchFrame(const wind::WindParams& w, const wind::VegetationMotion& m,
                                         const glm::vec3& camera) {
    spatial::VegetationSim::Frame f;
    f.cameraPosition = camera;
    f.projScale = 900.0f / (2.0f * std::tan(0.35f)); // a 40 degree vertical field over 900 pixels
    f.sourceRadius = 0.3f;
    f.extentY = 0.45f;
    f.deltaTime = 1.0f / 60.0f;
    f.budget = 256;
    f.wind = wind::packWind(w);
    f.response = wind::motionResponse(w, m);
    f.motion = m;
    return f;
}

} // namespace

TEST_CASE("the chain's calibration is the beam it claims to be", "[plant]") {
    const wind::ChainCalibration& cal = wind::chainCalibration();
    // A unit-length cantilever of four elements with a unit joint spring. The continuum values for
    // EI = k h^3 and a mass per unit length of 4 are a static tip of 32 under a unit uniform load
    // and a fundamental of 0.22 rad/s; a four-element discretisation is softer than the continuum,
    // which is the direction these must miss in.
    CHECK(cal.staticTip > 32.0f);
    CHECK(cal.staticTip < 120.0f);
    CHECK(cal.frequency > 0.10f);
    CHECK(cal.frequency < 0.22f);
    // The stiffest mode is what an explicit integrator has to survive, and it is an order of
    // magnitude above the fundamental. If this ever came back near 1 the substep would be chosen
    // from the wrong number and stiff species would explode.
    CHECK(cal.stiffRatio > 5.0f);
    CHECK(cal.stiffRatio < 200.0f);
    // Self weight topples a column at a load of the order of the joint spring's own scale.
    CHECK(cal.buckling > 0.0f);
    CHECK(cal.buckling < 1.0f);
}

TEST_CASE("a driven chain settles where the transfer function says it should", "[plant]") {
    // The whole reason Tier 1 can be a level of detail rather than a second look: the chain's DC
    // gain is Tier 0's. A promoted plant leans exactly as far as its unpromoted neighbours.
    struct Case {
        const char* name;
        float stiffness;
        float mass;
        float damping;
        float height;
    };
    const Case cases[] = {{"grass", 0.9f, 0.004f, 0.28f, 0.45f},
                          {"fern", 1.1f, 0.007f, 0.35f, 0.65f},
                          {"bush", 2.6f, 0.09f, 0.45f, 1.2f},
                          {"mushroom", 3.0f, 0.9f, 0.8f, 0.25f},
                          {"tree", 9.0f, 40.0f, 0.5f, 14.0f}};
    for (const Case& c : cases) {
        wind::ChainParams p;
        p.omega0 = std::sqrt(c.stiffness / c.mass);
        p.zeta = c.damping;
        p.height = c.height;
        const float dt = 1.0f / 60.0f;
        const wind::ChainTuning t = wind::tuneChain(p, dt);
        wind::PlantChain chain;
        chain.reset();
        const glm::vec2 target(0.2f, 0.0f);
        // Sixty periods of the fundamental is long enough for anything to have settled.
        const int steps = static_cast<int>(60.0f * wind::kTau / p.omega0 / dt);
        for (int i = 0; i < steps; ++i) {
            wind::stepChain(chain, t, target, glm::vec3(0.0f), c.height, 0.9f, dt);
        }
        INFO(c.name);
        CHECK(chain.bend().x == Approx(0.2f).margin(0.025f));
        CHECK(chain.bend().y == Approx(0.0f).margin(0.005f));
    }
}

TEST_CASE("the tip overshoots and rings back, which Tier 0 cannot do", "[plant]") {
    // The one property that makes Tier 1 worth its cost. A step in the wind drives a lightly damped
    // plant past its resting lean and back; the transfer function would have gone straight there.
    const wind::VegetationMotion m = grass();
    const wind::ChainParams p = speciesChain(m, 0.45f);
    const float dt = 1.0f / 60.0f;
    const wind::ChainTuning t = wind::tuneChain(p, dt);
    wind::PlantChain chain;
    chain.reset();
    float peak = 0.0f;
    float peakTime = 0.0f;
    for (int i = 0; i < 400; ++i) {
        wind::stepChain(chain, t, glm::vec2(0.2f, 0.0f), glm::vec3(0.0f), 0.45f, 0.9f, dt);
        if (chain.bend().x > peak) {
            peak = chain.bend().x;
            peakTime = static_cast<float>(i + 1) * dt;
        }
    }
    // Textbook overshoot for a damping ratio of 0.28 is 40%; a chain has higher modes in it too, so
    // this is a band rather than a number. Below 10% there is no inertia to see and above 90% the
    // plant is a spring toy.
    const float overshoot = (peak - 0.2f) / 0.2f;
    INFO("overshoot " << overshoot << " at t=" << peakTime);
    CHECK(overshoot > 0.10f);
    CHECK(overshoot < 0.90f);
    // ...and it happens within a quarter period or so of the resonance, not five seconds later.
    CHECK(peakTime > 0.05f);
    CHECK(peakTime < 2.0f * wind::kTau / p.omega0);
}

TEST_CASE("the root stays in the soil and the stem keeps its length", "[plant]") {
    const wind::VegetationMotion m = grass();
    const wind::ChainParams p = speciesChain(m, 0.45f);
    const float dt = 1.0f / 60.0f;
    const wind::ChainTuning t = wind::tuneChain(p, dt);
    wind::PlantChain chain;
    chain.reset();
    const float rest = 1.0f / static_cast<float>(wind::kChainPoints);
    for (int i = 0; i < 600; ++i) {
        // A violent, reversing drive plus a shove: nothing here may stretch the stalk.
        const float s = std::sin(static_cast<float>(i) * 0.21f);
        wind::stepChain(chain, t, glm::vec2(0.6f * s, -0.4f * s), glm::vec3(60.0f * s, 0.0f, -20.0f), 0.45f,
                        0.9f, dt);
        for (int k = 0; k < wind::kChainPoints; ++k) {
            INFO("step " << i << " segment " << k);
            CHECK(segmentLength(chain, k) == Approx(rest).margin(1e-3f));
        }
    }
    // The pinned root is not part of the state at all, so the first segment can only ever pivot
    // about the origin: this is the property Tier 0 got from a height profile that is exactly zero
    // at the base, and Tier 1 gets it from the topology instead.
    CHECK(glm::length(chain.pos[0]) == Approx(rest).margin(1e-3f));
}

TEST_CASE("the integrator does not explode", "[plant]") {
    // Absurd everything: a very stiff, barely damped species, a frame rate of fifteen, a drive far
    // past the bend limit and a disturbance two orders of magnitude too strong.
    wind::ChainParams p;
    p.omega0 = 55.0f;
    p.zeta = 0.03f;
    p.height = 0.2f;
    const float dt = 1.0f / 15.0f;
    const wind::ChainTuning t = wind::tuneChain(p, dt);
    wind::PlantChain chain;
    chain.reset();
    for (int i = 0; i < 4000; ++i) {
        wind::stepChain(chain, t, glm::vec2(5.0f, -4.0f), glm::vec3(500.0f, 0.0f, 250.0f), 0.2f, 0.9f, dt);
    }
    CHECK(std::isfinite(chain.bend().x));
    CHECK(std::isfinite(chain.bend().y));
    CHECK(glm::length(chain.bend()) <= Approx(0.9f).margin(1e-3f));
    // The stiff species was softened to what the step could carry rather than allowed to diverge.
    CHECK(t.omega < p.omega0);
}

TEST_CASE("energy decays and a plant comes to rest", "[plant]") {
    const wind::VegetationMotion m = grass();
    const wind::ChainParams p = speciesChain(m, 0.45f);
    const float dt = 1.0f / 60.0f;
    const wind::ChainTuning t = wind::tuneChain(p, dt);
    wind::PlantChain chain;
    chain.reset();
    // Bend it over and let go into perfectly still air.
    chain.setFromBend(glm::vec2(0.3f, 0.1f), glm::vec2(0.0f));
    float previous = glm::length(chain.bend());
    for (int i = 0; i < 600; ++i) {
        wind::stepChain(chain, t, glm::vec2(0.0f), glm::vec3(0.0f), 0.45f, 0.9f, dt);
    }
    CHECK(glm::length(chain.bend()) < 0.02f * previous);
    CHECK(chain.speed() < 0.01f);
    previous = 0.0f; // silence the unused-after-loop reading
    CHECK(previous == 0.0f);
}

TEST_CASE("Tier 1 tracks Tier 0 but is not Tier 0", "[plant]") {
    // In a real field, over ten seconds: the two tiers must agree on average -- otherwise a
    // promoted plant is visibly a different plant from its neighbours -- and must differ moment to
    // moment, or Tier 1 is an expensive way to draw Tier 0.
    const wind::WindParams w = breezyValley();
    const wind::VegetationMotion m = grass();
    const wind::WindUniforms uniforms = wind::packWind(w);
    const wind::MotionResponse r = wind::motionResponse(w, m);
    const wind::ChainParams p = speciesChain(m, 0.45f);
    const float dt = 1.0f / 60.0f;
    const wind::ChainTuning t = wind::tuneChain(p, dt);
    wind::PlantChain chain;
    chain.reset();
    const glm::vec4 random(0.3f, 0.1f, 0.5f, 0.7f);
    const glm::vec3 root(3.0f, 0.0f, -2.0f);
    float sum0 = 0.0f;
    float sum1 = 0.0f;
    float sumDelta = 0.0f;
    int samples = 0;
    for (int i = 0; i < 900; ++i) {
        const float now = static_cast<float>(i) * dt;
        const wind::WindSample s = wind::sampleWind(uniforms, root, now - r.swayDelay);
        const glm::vec2 tier0 = wind::vegetationBend(s, r, random, now);
        wind::stepChain(chain, t, tier0, glm::vec3(0.0f), 0.45f, 0.9f, dt);
        if (i > 120) { // past the start transient
            sum0 += glm::length(tier0);
            sum1 += glm::length(chain.bend());
            sumDelta += glm::length(chain.bend() - tier0);
            ++samples;
        }
    }
    const float mean0 = sum0 / static_cast<float>(samples);
    const float mean1 = sum1 / static_cast<float>(samples);
    const float meanDelta = sumDelta / static_cast<float>(samples);
    INFO("tier0 " << mean0 << " tier1 " << mean1 << " delta " << meanDelta);
    CHECK(mean1 == Approx(mean0).epsilon(0.35f));
    CHECK(meanDelta > 0.2f * mean0);
}

TEST_CASE("a chain built from a bend has exactly that bend", "[plant]") {
    // The continuity guarantee behind the level-of-detail transition. `setFromBend` is what a
    // promotion runs, and the shader reads `bend()` on the very next frame: if these two disagree
    // the plant jumps at the moment it is promoted, which is the one thing a transition may not do.
    const glm::vec2 bends[] = {{0.0f, 0.0f}, {0.21f, -0.04f}, {-0.35f, 0.18f}, {0.5f, 0.5f}};
    for (const glm::vec2& b : bends) {
        wind::PlantChain chain;
        chain.setFromBend(b, glm::vec2(0.1f, -0.2f));
        INFO("bend " << b.x << "," << b.y);
        CHECK(chain.bend().x == Approx(b.x).margin(1e-4f));
        CHECK(chain.bend().y == Approx(b.y).margin(1e-4f));
        const float rest = 1.0f / static_cast<float>(wind::kChainPoints);
        for (int k = 0; k < wind::kChainPoints; ++k) {
            CHECK(segmentLength(chain, k) == Approx(rest).margin(1e-3f));
        }
    }
}

TEST_CASE("a disturbance pushes, decays and lets go", "[plant]") {
    wind::DisturbanceField field;
    wind::Disturbance d;
    d.position = glm::vec3(0.0f, 0.0f, 0.0f);
    d.direction = glm::vec3(1.0f, 0.0f, 0.0f);
    d.radius = 2.0f;
    d.strength = 20.0f;
    d.duration = 0.5f;
    field.add(d);

    // Inside the radius it pushes the way it was pointed; outside it does nothing at all, and the
    // falloff is smooth at the rim rather than a step.
    CHECK(field.accelerationAt(glm::vec3(0.0f)).x == Approx(20.0f).margin(1e-3f));
    CHECK(glm::length(field.accelerationAt(glm::vec3(2.5f, 0.0f, 0.0f))) == Approx(0.0f).margin(1e-6f));
    CHECK(glm::length(field.accelerationAt(glm::vec3(1.98f, 0.0f, 0.0f))) < 0.02f);
    CHECK(field.reaches(glm::vec3(1.0f, 0.0f, 0.0f)));
    CHECK_FALSE(field.reaches(glm::vec3(3.0f, 0.0f, 0.0f)));
    const float near = glm::length(field.accelerationAt(glm::vec3(0.5f, 0.0f, 0.0f)));
    const float far = glm::length(field.accelerationAt(glm::vec3(1.5f, 0.0f, 0.0f)));
    CHECK(near > far);

    // It ages out, and once it is spent it is gone rather than merely weak.
    field.advance(0.25f);
    CHECK(field.accelerationAt(glm::vec3(0.0f)).x < 20.0f);
    CHECK(field.count() == 1);
    field.advance(0.3f);
    CHECK(field.count() == 0);
}

TEST_CASE("the disturbance set stays bounded", "[plant]") {
    wind::DisturbanceField field;
    for (int i = 0; i < 200; ++i) {
        wind::Disturbance d;
        d.position = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);
        d.radius = 1.0f;
        d.strength = 1.0f + static_cast<float>(i);
        d.duration = 10.0f;
        field.add(d);
    }
    CHECK(field.count() == static_cast<std::size_t>(wind::kMaxDisturbances));
    // The strongest survived: what is kept is what a viewer would notice.
    float weakest = 1e9f;
    for (const wind::Disturbance& d : field.items()) {
        weakest = std::min(weakest, d.strength);
    }
    CHECK(weakest > 100.0f);
}

TEST_CASE("a shoved plant recovers", "[plant]") {
    const wind::VegetationMotion m = grass();
    const wind::ChainParams p = speciesChain(m, 0.45f);
    const float dt = 1.0f / 60.0f;
    const wind::ChainTuning t = wind::tuneChain(p, dt);
    wind::PlantChain chain;
    chain.reset();
    wind::DisturbanceField field;
    wind::Disturbance d;
    d.position = glm::vec3(0.0f);
    d.direction = glm::vec3(1.0f, 0.0f, 0.0f);
    d.radius = 2.0f;
    d.strength = 30.0f;
    d.duration = 0.3f;
    field.add(d);
    float peak = 0.0f;
    glm::vec2 atPeak(0.0f);
    for (int i = 0; i < 30; ++i) {
        wind::stepChain(chain, t, glm::vec2(0.0f), field.accelerationAt(glm::vec3(0.5f, 0.0f, 0.0f)), 0.45f,
                        0.9f, dt);
        field.advance(dt);
        if (glm::length(chain.bend()) > peak) {
            peak = glm::length(chain.bend());
            atPeak = chain.bend();
        }
    }
    CHECK(peak > 0.02f);
    CHECK(atPeak.x > 0.0f); // shoved the way it was pushed
    // ...and by the time the shove is spent it has already swung back through upright, which is the
    // inertia Tier 0 cannot express: a transfer function would have returned monotonically.
    CHECK(chain.bend().x < atPeak.x);
    for (int i = 0; i < 600; ++i) {
        wind::stepChain(chain, t, glm::vec2(0.0f), glm::vec3(0.0f), 0.45f, 0.9f, dt);
    }
    CHECK(glm::length(chain.bend()) < 0.05f * peak);
}

TEST_CASE("the camera wake is one slot, not one per frame", "[plant]") {
    wind::DisturbanceField field;
    wind::CameraWake cfg;
    cfg.enabled = true;
    for (int i = 0; i < 100; ++i) {
        field.trackBody(glm::vec3(static_cast<float>(i) * 0.05f, 0.0f, 0.0f), glm::vec3(3.0f, 0.0f, 0.0f), cfg);
        field.advance(1.0f / 60.0f);
    }
    CHECK(field.count() == 1);
    // It follows the body and points the way it is going.
    CHECK(field.items()[0].position.x > 4.0f);
    CHECK(field.items()[0].direction.x == Approx(1.0f).margin(1e-4f));
    // ...and when the body stops it runs out rather than lingering forever.
    for (int i = 0; i < 60; ++i) {
        field.trackBody(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f), cfg);
        field.advance(1.0f / 60.0f);
    }
    CHECK(field.count() == 0);
}

TEST_CASE("level of detail picks the plants a viewer can resolve", "[plant]") {
    const wind::WindParams w = breezyValley();
    const wind::VegetationMotion m = grass();
    spatial::VegetationSim sim;
    const std::vector<spatial::InstanceRecord> records = patch(40, 0.6f); // 1600 clumps over 24 metres
    sim.setInstances(records);

    spatial::VegetationSim::Frame f = patchFrame(w, m, glm::vec3(0.0f, 1.0f, 0.0f));
    sim.update(f);
    CHECK(sim.activeCount() > 0);
    CHECK(sim.activeCount() <= static_cast<std::uint32_t>(m.simulate.budget));
    // The point of the grid: the work is proportional to what is near the camera, not to the layer.
    CHECK(sim.examinedCount() < records.size() / 2);

    // Everything simulated is within the distance limit, and everything is big enough on screen.
    std::uint32_t checked = 0;
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (sim.slots()[i] == 0u) {
            continue;
        }
        ++checked;
        const glm::vec3 p(records[i].position);
        CHECK(glm::length(p - f.cameraPosition) <= m.simulate.maxDistance);
    }
    CHECK(checked == sim.activeCount());

    // Move the camera to the far corner and the set follows it there.
    spatial::VegetationSim::Frame far = patchFrame(w, m, glm::vec3(23.4f, 1.0f, 23.4f));
    for (int i = 0; i < 240; ++i) { // long enough for every release to finish
        far.renderTime = static_cast<float>(i) / 60.0f;
        sim.update(far);
    }
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (sim.slots()[i] == 0u) {
            continue;
        }
        const glm::vec3 p(records[i].position);
        CHECK(glm::length(p - far.cameraPosition) <= m.simulate.maxDistance);
    }
}

TEST_CASE("a tier change does not move the plant", "[plant]") {
    // The pose the shader draws must be continuous through a promotion. The simulation hands back
    // `dynamics()[slot]`, so the test is that on the frame a plant is promoted that value equals the
    // Tier 0 bend the shader would otherwise have computed for it.
    const wind::WindParams w = breezyValley();
    const wind::VegetationMotion m = grass();
    spatial::VegetationSim sim;
    const std::vector<spatial::InstanceRecord> records = patch(20, 0.6f);
    sim.setInstances(records);
    spatial::VegetationSim::Frame f = patchFrame(w, m, glm::vec3(0.0f, 1.0f, 0.0f));
    f.renderTime = 4.25f;
    sim.update(f);
    REQUIRE(sim.activeCount() > 0);

    std::uint32_t compared = 0;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const std::uint32_t slot = sim.slots()[i];
        if (slot == 0u) {
            continue;
        }
        const glm::vec3 root(records[i].position);
        const wind::WindSample s = wind::sampleWind(f.wind, root, f.renderTime - f.response.swayDelay);
        const glm::vec2 tier0 = wind::vegetationBend(s, f.response, records[i].random, f.renderTime);
        const glm::vec4 dyn = sim.dynamics()[slot - 1u];
        INFO("record " << i);
        // One frame of integration has happened, so it is "did not jump", not "did not move": a
        // sixtieth of a second of a plant this stiff is a small fraction of its own travel.
        CHECK(glm::length(glm::vec2(dyn.x, dyn.y) - tier0) < 0.25f * std::max(glm::length(tier0), 0.02f) + 0.01f);
        ++compared;
    }
    CHECK(compared > 0);
}

TEST_CASE("a demoted plant hands its pose back", "[plant]") {
    const wind::WindParams w = breezyValley();
    wind::VegetationMotion m = grass();
    m.simulate.release = 0.25f;
    spatial::VegetationSim sim;
    const std::vector<spatial::InstanceRecord> records = patch(20, 0.6f);
    sim.setInstances(records);
    spatial::VegetationSim::Frame near = patchFrame(w, m, glm::vec3(5.7f, 1.0f, 5.7f));
    for (int i = 0; i < 30; ++i) {
        near.renderTime = static_cast<float>(i) / 60.0f;
        sim.update(near);
    }
    REQUIRE(sim.activeCount() > 0);
    // Find a plant that is about to be dropped, then walk away and watch its slot.
    std::size_t watched = records.size();
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (sim.slots()[i] != 0u) {
            watched = i;
            break;
        }
    }
    REQUIRE(watched < records.size());

    spatial::VegetationSim::Frame away = patchFrame(w, m, glm::vec3(200.0f, 1.0f, 200.0f));
    float last = -1.0f;
    float worstJump = 0.0f;
    for (int i = 0; i < 60; ++i) {
        away.renderTime = 0.5f + static_cast<float>(i) / 60.0f;
        const std::uint32_t before = sim.slots()[watched];
        sim.update(away);
        const std::uint32_t after = sim.slots()[watched];
        const glm::vec3 root(records[watched].position);
        const wind::WindSample s = wind::sampleWind(away.wind, root, away.renderTime - away.response.swayDelay);
        const glm::vec2 tier0 =
            wind::vegetationBend(s, away.response, records[watched].random, away.renderTime);
        // What the shader draws: the simulated pose while the slot is held, Tier 0 once it is not.
        const glm::vec2 drawn =
            after != 0u ? glm::vec2(sim.dynamics()[after - 1u]) : tier0;
        if (last >= 0.0f) {
            worstJump = std::max(worstJump, std::fabs(glm::length(drawn) - last));
        }
        last = glm::length(drawn);
        if (before != 0u && after == 0u) {
            // The frame the slot was freed. By then the blend had already reached Tier 0.
            CHECK(glm::length(drawn - tier0) < 1e-6f);
        }
    }
    CHECK(sim.slots()[watched] == 0u);
    // Nothing the shader would have drawn jumped by more than a plant moves in a frame anyway.
    CHECK(worstJump < 0.05f);
}

TEST_CASE("still plants sleep, and a gust or a shove wakes them", "[plant]") {
    // Calm air: nothing is moving, so nothing should be integrated after a moment.
    wind::WindParams calm = breezyValley();
    calm.speed = 0.0f;
    wind::VegetationMotion m = grass();
    m.simulate.sleepSeconds = 0.1f;
    spatial::VegetationSim sim;
    const std::vector<spatial::InstanceRecord> records = patch(16, 0.6f);
    sim.setInstances(records);
    spatial::VegetationSim::Frame f = patchFrame(calm, m, glm::vec3(0.0f, 1.0f, 0.0f));
    // A calm wind has no response at all, so drive the sim with a live field and a still one in
    // turn: the response is what the layer would use, the field is what it is asked to answer.
    f.response = wind::motionResponse(breezyValley(), m);
    for (int i = 0; i < 120; ++i) {
        f.renderTime = static_cast<float>(i) / 60.0f;
        sim.update(f);
    }
    REQUIRE(sim.activeCount() > 0);
    CHECK(sim.awakeCount() < sim.activeCount() / 4);

    // A shove wakes every plant it reaches, on the frame it arrives.
    wind::DisturbanceField field;
    wind::Disturbance d;
    d.position = glm::vec3(1.0f, 0.0f, 1.0f);
    d.direction = glm::vec3(1.0f, 0.0f, 0.0f);
    d.radius = 4.0f;
    d.strength = 25.0f;
    d.duration = 0.4f;
    field.add(d);
    f.disturbances = &field;
    f.renderTime = 2.0f;
    sim.update(f);
    CHECK(sim.awakeCount() > 0);

    // The wind picking up wakes them too, with nothing touching them.
    f.disturbances = nullptr;
    spatial::VegetationSim::Frame breezy = patchFrame(breezyValley(), m, glm::vec3(0.0f, 1.0f, 0.0f));
    breezy.renderTime = 3.0f;
    sim.update(breezy);
    CHECK(sim.awakeCount() > 0);
}

TEST_CASE("simulation cost tracks the active set, not the population", "[plant]") {
    const wind::WindParams w = breezyValley();
    const wind::VegetationMotion m = grass();
    // The same patch density over four times the area: sixteen times the plants, the same handful
    // near the camera. If the level-of-detail pass ever became a walk over the layer this would be
    // the test that noticed.
    spatial::VegetationSim small;
    spatial::VegetationSim large;
    small.setInstances(patch(30, 0.6f));
    large.setInstances(patch(120, 0.6f));
    spatial::VegetationSim::Frame f = patchFrame(w, m, glm::vec3(3.0f, 1.0f, 3.0f));
    small.update(f);
    large.update(f);
    CHECK(large.activeCount() == small.activeCount());
    CHECK(large.examinedCount() < 3 * small.examinedCount() + 32);
    CHECK(large.examinedCount() < 900u);
}

TEST_CASE("the simulation is off unless a scene asks for it", "[plant]") {
    const wind::WindParams w = breezyValley();
    wind::VegetationMotion m = grass();
    m.simulate = wind::SimLod{}; // the defaults
    CHECK_FALSE(m.simulate.enabled);
    spatial::VegetationSim sim;
    sim.setInstances(patch(20, 0.6f));
    spatial::VegetationSim::Frame f = patchFrame(w, m, glm::vec3(0.0f, 1.0f, 0.0f));
    sim.update(f);
    CHECK(sim.activeCount() == 0);
    CHECK(sim.awakeCount() == 0);
    for (std::uint32_t slot : sim.slots()) {
        CHECK(slot == 0u);
    }
}

TEST_CASE("simulation settings survive a JSON round trip", "[plant]") {
    wind::WindParams w = breezyValley();
    w.simBudget = 311;
    w.wake.enabled = true;
    w.wake.radius = 2.75f;
    w.wake.strength = 9.5f;
    wind::VegetationMotion m = grass();
    m.simulate.enabled = true;
    m.simulate.maxDistance = 17.5f;
    m.simulate.minScreenRadius = 33.0f;
    m.simulate.budget = 77;
    m.simulate.release = 0.42f;

    const wind::WindParams w2 = wind::windFromJson(wind::windToJson(w));
    CHECK(w2.simBudget == 311);
    CHECK(w2.wake.enabled);
    CHECK(w2.wake.radius == Approx(2.75f));
    CHECK(w2.wake.strength == Approx(9.5f));

    const wind::VegetationMotion m2 = wind::motionFromJson(wind::motionToJson(m));
    CHECK(m2.simulate.enabled);
    CHECK(m2.simulate.maxDistance == Approx(17.5f));
    CHECK(m2.simulate.minScreenRadius == Approx(33.0f));
    CHECK(m2.simulate.budget == 77);
    CHECK(m2.simulate.release == Approx(0.42f));

    // A scene written before any of this existed still loads with everything switched off.
    const wind::VegetationMotion old = wind::motionFromJson(nlohmann::json{{"stiffness", 2.0f}});
    CHECK_FALSE(old.simulate.enabled);
}

TEST_CASE("the budget is a ceiling, including plants on their way out", "[plant]") {
    // A slot is held through the hand-back, so a plant on its way down still costs budget. It has
    // to: the GPU buffer is sized to the budget, and a camera that turns quickly can put a whole
    // active set into release at once. Getting this wrong overran the buffer by two slots and Dawn
    // said so, which is the only reason it was ever noticed.
    const wind::WindParams w = breezyValley();
    wind::VegetationMotion m = grass();
    m.simulate.budget = 24;
    m.simulate.release = 0.5f;
    spatial::VegetationSim sim;
    const std::vector<spatial::InstanceRecord> records = patch(40, 0.5f);
    sim.setInstances(records);
    // Walk the camera across the patch and back, fast enough that plants are always entering and
    // leaving, and watch the two counts that must never exceed the budget.
    for (int i = 0; i < 400; ++i) {
        const float u = static_cast<float>(i) / 400.0f;
        const float x = 19.0f * (u < 0.5f ? 2.0f * u : 2.0f - 2.0f * u);
        spatial::VegetationSim::Frame f = patchFrame(w, m, glm::vec3(x, 1.0f, 9.5f));
        f.renderTime = static_cast<float>(i) / 60.0f;
        sim.update(f);
        INFO("step " << i << " slots " << sim.slotCount() << " active " << sim.activeCount());
        CHECK(sim.slotCount() <= static_cast<std::uint32_t>(m.simulate.budget));
        CHECK(sim.activeCount() <= static_cast<std::uint32_t>(m.simulate.budget));
        // Every slot the map points at is one this frame's dynamics array actually has, and every
        // dirty range the renderer would upload is inside the map. Aggregated rather than asserted
        // per record, because a per-record CHECK here is a hundred thousand assertions a run.
        std::uint32_t highest = 0;
        for (std::uint32_t slot : sim.slots()) {
            highest = std::max(highest, slot);
        }
        CHECK(highest <= sim.slotCount());
        bool spansFit = true;
        for (const auto& span : sim.dirtySlots()) {
            spansFit = spansFit && static_cast<std::size_t>(span.first) + span.count <= records.size();
        }
        CHECK(spansFit);
    }
    CHECK(sim.activeCount() > 0);
}
