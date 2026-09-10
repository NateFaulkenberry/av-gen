// The wind field and Tier 0 vegetation motion (ADR-055). This is pure maths with no GPU in it, so
// the properties that make it *look* like wind -- gusts that travel, regions that agree, neighbours
// that do not move in lockstep, roots that stay put -- are all checkable here rather than by
// squinting at a render.

#include "core/wind.hpp"

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
    return m;
}

wind::VegetationMotion mushroom() {
    wind::VegetationMotion m;
    m.stiffness = 3.0f;
    m.mass = 0.9f;
    m.damping = 0.8f;
    m.windSensitivity = 0.5f;
    m.bendLimit = 0.12f;
    m.tipAmplitude = 0.10f;
    m.gustResponse = 0.35f;
    m.bendCurve = 2.6f;
    m.amplitudeVariance = 0.25f;
    return m;
}

wind::VegetationMotion tree() {
    wind::VegetationMotion m;
    m.stiffness = 9.0f;
    m.mass = 40.0f;
    m.damping = 0.5f;
    m.windSensitivity = 1.4f;
    m.bendLimit = 0.12f;
    m.tipAmplitude = 0.30f;
    m.gustResponse = 0.8f;
    m.bendCurve = 3.2f;
    m.amplitudeVariance = 0.28f;
    return m;
}

} // namespace

TEST_CASE("wind field is deterministic", "[wind]") {
    const wind::WindUniforms u = wind::packWind(breezyValley());
    const glm::vec3 p(13.5f, 2.0f, -47.25f);
    for (float t : {0.0f, 1.75f, 91.5f}) {
        const wind::WindSample a = wind::sampleWind(u, p, t);
        const wind::WindSample b = wind::sampleWind(u, p, t);
        // Bit-identical, not merely close: the field must be reproducible frame for frame, because
        // an offline render of the same frame index has to match the live one exactly.
        REQUIRE(a.strength == b.strength);
        REQUIRE(a.gust == b.gust);
        REQUIRE(a.phase == b.phase);
        REQUIRE(a.direction.x == b.direction.x);
        REQUIRE(a.direction.y == b.direction.y);
    }
    // The same parameters pack to the same bytes, so the CPU and the GPU start from the same place.
    const wind::WindUniforms again = wind::packWind(breezyValley());
    REQUIRE(u.dir == again.dir);
    REQUIRE(u.region == again.region);
    REQUIRE(u.gust == again.gust);
    REQUIRE(u.turbulence == again.turbulence);
}

TEST_CASE("gusts propagate downwind at the speed they are given", "[wind]") {
    const wind::WindParams w = breezyValley();
    const wind::WindUniforms u = wind::packWind(w);
    const glm::vec2 dir(std::cos(w.direction), std::sin(w.direction));
    const float dt = 0.7f;
    const glm::vec3 step(dir.x * w.gustSpeed * dt, 0.0f, dir.y * w.gustSpeed * dt);

    // A front seen here now is the same front seen gustSpeed * dt metres downwind, dt later. This
    // is the whole difference between a field and a global clock: the phase is a function of
    // position as well as time, so a meadow cannot move in lockstep.
    for (const glm::vec3 p : {glm::vec3(0.0f), glm::vec3(-31.0f, 0.0f, 12.0f), glm::vec3(88.0f, 4.0f, -5.0f)}) {
        const wind::WindSample now = wind::sampleWind(u, p, 3.0f);
        const wind::WindSample later = wind::sampleWind(u, p + step, 3.0f + dt);
        REQUIRE(later.gust == Approx(now.gust).margin(1e-4));
    }

    // And it genuinely moves: the same point does not see the same gust a moment later.
    const wind::WindSample here = wind::sampleWind(u, glm::vec3(0.0f), 3.0f);
    const wind::WindSample soon = wind::sampleWind(u, glm::vec3(0.0f), 3.0f + dt);
    REQUIRE(std::abs(here.gust - soon.gust) > 0.05f);
}

TEST_CASE("the field is coherent near by and decorrelated far away", "[wind]") {
    const wind::WindUniforms u = wind::packWind(breezyValley());
    const float t = 12.0f;
    const wind::WindSample origin = wind::sampleWind(u, glm::vec3(0.0f), t);

    // Two plants a metre apart must agree, or a meadow reads as noise rather than as a population
    // under one sky.
    for (float d : {0.25f, 0.5f, 1.0f}) {
        const wind::WindSample near = wind::sampleWind(u, glm::vec3(d, 0.0f, d * 0.5f), t);
        REQUIRE(std::abs(near.strength - origin.strength) < 0.05f);
        REQUIRE(std::abs(near.gust - origin.gust) < 0.12f);
    }

    // Over the length of the world they must not: sample a long transect and require the strength
    // to actually swing, rather than sitting at one value the author cannot see.
    float lo = 1e9f;
    float hi = -1e9f;
    for (int i = 0; i < 400; ++i) {
        const float x = static_cast<float>(i) * 1.6f;
        const wind::WindSample s = wind::sampleWind(u, glm::vec3(x, 0.0f, x * 0.3f), t);
        lo = std::min(lo, s.strength);
        hi = std::max(hi, s.strength);
    }
    REQUIRE(hi - lo > 0.4f * 0.85f); // at least 40% of the base speed of swing across the transect
}

TEST_CASE("wind amplitude is what the author asked for", "[wind]") {
    wind::WindParams w = breezyValley();
    const wind::WindUniforms u = wind::packWind(w);
    float sum = 0.0f;
    float lo = 1e9f;
    float hi = -1e9f;
    int n = 0;
    for (int i = 0; i < 120; ++i) {
        for (int j = 0; j < 120; ++j) {
            const glm::vec3 p(static_cast<float>(i) * 2.7f, 0.0f, static_cast<float>(j) * 3.1f);
            const wind::WindSample s = wind::sampleWind(u, p, 5.0f);
            sum += s.strength;
            lo = std::min(lo, s.strength);
            hi = std::max(hi, s.strength);
            REQUIRE(s.gust >= 0.0f);
            REQUIRE(s.gust <= w.gustAmount + 1e-4f);
            REQUIRE(glm::length(s.direction) == Approx(1.0f).margin(1e-4));
            ++n;
        }
    }
    const float mean = sum / static_cast<float>(n);
    // regionAmount is the fraction of the base speed the strength swings by, in both directions.
    REQUIRE(mean == Approx(w.speed).margin(0.05f));
    REQUIRE(lo == Approx(w.speed * (1.0f - w.regionAmount)).margin(0.05f));
    REQUIRE(hi == Approx(w.speed * (1.0f + w.regionAmount)).margin(0.05f));

    // Doubling the speed doubles the strength everywhere and changes nothing else.
    w.speed *= 2.0f;
    const wind::WindUniforms fast = wind::packWind(w);
    const glm::vec3 probe(21.0f, 0.0f, -8.0f);
    const wind::WindSample a = wind::sampleWind(u, probe, 5.0f);
    const wind::WindSample b = wind::sampleWind(fast, probe, 5.0f);
    REQUIRE(b.strength == Approx(a.strength * 2.0f).margin(1e-4));
    REQUIRE(b.gust == Approx(a.gust).margin(1e-6));
}

TEST_CASE("calm is calm", "[wind]") {
    wind::WindParams w = breezyValley();
    w.enabled = false;
    REQUIRE_FALSE(w.active());
    const wind::WindSample off = wind::sampleWind(wind::packWind(w), glm::vec3(4.0f, 0.0f, 9.0f), 2.0f);
    REQUIRE(off.strength == Approx(0.0f).margin(1e-6));

    w.enabled = true;
    w.speed = 0.0f;
    REQUIRE_FALSE(w.active());
    const wind::WindSample still = wind::sampleWind(wind::packWind(w), glm::vec3(4.0f, 0.0f, 9.0f), 2.0f);
    REQUIRE(still.strength == Approx(0.0f).margin(1e-6));
}

TEST_CASE("turbulence turns the direction, within its stated bound", "[wind]") {
    wind::WindParams w = breezyValley();
    const glm::vec2 base(std::cos(w.direction), std::sin(w.direction));
    const wind::WindUniforms u = wind::packWind(w);
    float maxTurn = 0.0f;
    for (int i = 0; i < 500; ++i) {
        const glm::vec3 p(static_cast<float>(i) * 1.3f, 0.0f, static_cast<float>(i) * -0.7f);
        const wind::WindSample s = wind::sampleWind(u, p, 4.0f);
        maxTurn = std::max(maxTurn, std::acos(std::clamp(glm::dot(s.direction, base), -1.0f, 1.0f)));
    }
    REQUIRE(maxTurn > 0.05f);              // it does turn
    REQUIRE(maxTurn <= w.turbulence + 1e-3f); // and never by more than it was told to

    w.turbulence = 0.0f;
    const wind::WindSample straight = wind::sampleWind(wind::packWind(w), glm::vec3(17.0f, 0.0f, 3.0f), 4.0f);
    REQUIRE(straight.direction.x == Approx(base.x).margin(1e-5));
    REQUIRE(straight.direction.y == Approx(base.y).margin(1e-5));
}

TEST_CASE("the oscillator model is the textbook one", "[wind]") {
    // Gain 1 well below resonance, 1/(2 zeta) at it, falling away above it; lag 0, pi/2, then pi.
    REQUIRE(wind::oscillatorGain(0.0f, 10.0f, 0.3f) == Approx(1.0f).margin(1e-4));
    REQUIRE(wind::oscillatorGain(10.0f, 10.0f, 0.3f) == Approx(1.0f / 0.6f).margin(1e-3));
    REQUIRE(wind::oscillatorGain(100.0f, 10.0f, 0.3f) < 0.02f);
    REQUIRE(wind::oscillatorLag(0.0f, 10.0f, 0.3f) == Approx(0.0f).margin(1e-4));
    REQUIRE(wind::oscillatorLag(10.0f, 10.0f, 0.3f) == Approx(1.5707963f).margin(1e-3));
    REQUIRE(wind::oscillatorLag(100.0f, 10.0f, 0.3f) > 3.0f);
}

TEST_CASE("species differ the way the physics says they should", "[wind]") {
    const wind::WindParams w = breezyValley();
    const wind::MotionResponse g = wind::motionResponse(w, grass());
    const wind::MotionResponse f = wind::motionResponse(w, mushroom());
    const wind::MotionResponse c = wind::motionResponse(w, tree());

    // Soft and light beats stiff and heavy, everywhere.
    REQUIRE(g.steadyGain > f.steadyGain * 3.0f);
    REQUIRE(g.gustGain > f.gustGain * 5.0f);
    REQUIRE(g.flutterGain > f.flutterGain * 5.0f);

    // A mushroom lags the field by most of a second; grass does not lag at all. That lag is the
    // reason a stiff, heavy thing reads as slow rather than merely small.
    REQUIRE(g.swayDelay < 0.1f);
    REQUIRE(f.swayDelay > 0.5f);
    REQUIRE(c.swayDelay > 1.5f);

    // A tree is the extreme case: its resonance is far below the gust band, so a passing front is
    // filtered out almost entirely and only the slow regional swing survives. That is what makes
    // "a low-frequency sway of the canopy" fall out of the model rather than being hand-authored.
    REQUIRE(c.gustGain < c.steadyGain * 0.2f);
    REQUIRE(c.flutterOmega < 1.0f);  // it rings below a hertz
    REQUIRE(g.flutterOmega > 10.0f); // grass rings at a couple of hertz
    // Grass, by contrast, follows a gust essentially whole.
    REQUIRE(g.gustGain > g.steadyGain * 0.9f);

    // The two authoring dials are linear and independent of the physics.
    wind::VegetationMotion twice = grass();
    twice.tipAmplitude *= 2.0f;
    REQUIRE(wind::motionResponse(w, twice).steadyGain == Approx(g.steadyGain * 2.0f).margin(1e-5));
    twice = grass();
    twice.windSensitivity *= 2.0f;
    REQUIRE(wind::motionResponse(w, twice).steadyGain == Approx(g.steadyGain * 2.0f).margin(1e-5));
    // And stiffness divides, as a static deflection does.
    twice = grass();
    twice.stiffness *= 2.0f;
    REQUIRE(wind::motionResponse(w, twice).steadyGain < g.steadyGain * 0.6f);

    // Nothing that does not catch the wind moves at all.
    wind::VegetationMotion rock = grass();
    rock.windSensitivity = 0.0f;
    REQUIRE_FALSE(rock.active());
    const wind::MotionResponse none = wind::motionResponse(w, rock);
    REQUIRE(none.steadyGain == 0.0f);
    REQUIRE(none.gustGain == 0.0f);
    REQUIRE(none.flutterGain == 0.0f);
}

TEST_CASE("the deformation anchors the root and grows with height", "[wind]") {
    const wind::WindParams w = breezyValley();
    const wind::WindUniforms u = wind::packWind(w);
    const wind::MotionResponse r = wind::motionResponse(w, grass());
    const wind::WindSample s = wind::sampleWind(u, glm::vec3(11.0f, 0.0f, -3.0f), 6.0f);
    const glm::vec4 random(0.31f, 0.72f, 0.58f, 0.14f);
    const float baseY = -0.031f;
    const float extentY = 1.872f;
    const float scaleY = 1.1f;
    const float height = extentY * scaleY;

    // Exactly zero at the root. Not small: zero. A stalk that slides at the soil line is the tell
    // that the whole mesh is being moved rather than bent.
    const glm::vec3 atRoot = wind::vegetationDisplacement(baseY, baseY, extentY, scaleY, s, r, random, 6.0f);
    REQUIRE(glm::length(atRoot) == Approx(0.0f).margin(1e-9));
    // And below the root (a mesh whose bounds dip under the origin) too.
    const glm::vec3 below =
        wind::vegetationDisplacement(baseY - 0.2f, baseY, extentY, scaleY, s, r, random, 6.0f);
    REQUIRE(glm::length(below) == Approx(0.0f).margin(1e-9));

    // Monotonically increasing with height, and the tip carries the most.
    float previous = -1.0f;
    for (int i = 0; i <= 10; ++i) {
        const float h = static_cast<float>(i) / 10.0f;
        const glm::vec3 d =
            wind::vegetationDisplacement(baseY + h * extentY, baseY, extentY, scaleY, s, r, random, 6.0f);
        const float lateral = glm::length(glm::vec2(d.x, d.z));
        REQUIRE(lateral >= previous - 1e-6f);
        previous = lateral;
        // Never past the ceiling the species was given, at any height.
        REQUIRE(lateral <= r.bendLimit * height + 1e-4f);
    }
    REQUIRE(previous > 0.01f); // the tip did move

    // The tip drops as it leans: a bent stem keeps its length, it does not shear sideways.
    const glm::vec3 tip = wind::vegetationDisplacement(baseY + extentY, baseY, extentY, scaleY, s, r, random, 6.0f);
    REQUIRE(tip.y < 0.0f);
}

TEST_CASE("neighbours differ but a region still agrees", "[wind]") {
    const wind::WindParams w = breezyValley();
    const wind::WindUniforms u = wind::packWind(w);
    const wind::MotionResponse r = wind::motionResponse(w, grass());
    const float baseY = 0.0f;
    const float extentY = 1.0f;

    // Twenty specimens within two metres of each other, each with its own hashed randoms.
    std::vector<glm::vec3> tips;
    for (int i = 0; i < 20; ++i) {
        const float fi = static_cast<float>(i);
        const glm::vec3 root(std::fmod(fi * 0.37f, 2.0f), 0.0f, std::fmod(fi * 0.91f, 2.0f));
        const glm::vec4 random(std::fmod(fi * 0.6180339f, 1.0f), std::fmod(fi * 0.2442f, 1.0f),
                               std::fmod(fi * 0.7548f, 1.0f), 0.0f);
        const wind::WindSample s = wind::sampleWind(u, root, 6.0f);
        tips.push_back(wind::vegetationDisplacement(extentY, baseY, extentY, 1.0f, s, r, random, 6.0f));
    }
    // They all lean the same way -- no two neighbours pull in opposite directions ...
    glm::vec2 mean(0.0f);
    for (const glm::vec3& t : tips) {
        mean += glm::vec2(t.x, t.z);
    }
    mean /= static_cast<float>(tips.size());
    REQUIRE(glm::length(mean) > 0.01f);
    for (const glm::vec3& t : tips) {
        REQUIRE(glm::dot(glm::normalize(glm::vec2(t.x, t.z)), glm::normalize(mean)) > 0.0f);
    }
    // ... but they are not the same plant repeated: amplitudes and flutter phases spread.
    float lo = 1e9f;
    float hi = -1e9f;
    for (const glm::vec3& t : tips) {
        const float len = glm::length(glm::vec2(t.x, t.z));
        lo = std::min(lo, len);
        hi = std::max(hi, len);
    }
    REQUIRE(hi > lo * 1.25f);
}

TEST_CASE("wind and motion survive a JSON round trip", "[wind]") {
    const wind::WindParams w = breezyValley();
    const wind::WindParams back = wind::windFromJson(wind::windToJson(w));
    REQUIRE(back.enabled == w.enabled);
    REQUIRE(back.direction == Approx(w.direction));
    REQUIRE(back.speed == Approx(w.speed));
    REQUIRE(back.gustSpeed == Approx(w.gustSpeed));
    REQUIRE(back.gustSharpness == Approx(w.gustSharpness));
    REQUIRE(back.flutterScale == Approx(w.flutterScale));

    const wind::VegetationMotion m = mushroom();
    const wind::VegetationMotion m2 = wind::motionFromJson(wind::motionToJson(m));
    REQUIRE(m2.stiffness == Approx(m.stiffness));
    REQUIRE(m2.mass == Approx(m.mass));
    REQUIRE(m2.damping == Approx(m.damping));
    REQUIRE(m2.windSensitivity == Approx(m.windSensitivity));
    REQUIRE(m2.bendLimit == Approx(m.bendLimit));
    REQUIRE(m2.tipAmplitude == Approx(m.tipAmplitude));
    REQUIRE(m2.gustResponse == Approx(m.gustResponse));
    REQUIRE(m2.bendCurve == Approx(m.bendCurve));
    REQUIRE(m2.amplitudeVariance == Approx(m.amplitudeVariance));

    // A layer that says nothing about motion does not move: the default is a genuine no-op, so no
    // scene written before this existed starts swaying when it is loaded.
    const wind::VegetationMotion silent;
    REQUIRE_FALSE(silent.active());
}
