// The day/night cycle's three contracts (ADR-343), each of which the brief states as a
// requirement and each of which is cheap to break silently:
//
//   determinism   same phase in, same state out, with no history
//   purity        phase is a function of the clock, not an accumulator -- so scrub == play
//   continuity    the cycle wraps at midnight with no discontinuity
//
// None of these show up in a frame until they have been wrong for a while, which is exactly the
// kind of thing to assert against rather than look for.

#include "scene/day_night.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;
using avgen::scene::DayNightSettings;
using avgen::scene::DayNightState;
using avgen::scene::phaseAt;
using avgen::scene::resolveDayNight;

namespace {

DayNightSettings defaults() {
    DayNightSettings s;
    s.enabled = true;
    s.cycleSeconds = 240.0f;
    s.applyDefaults();
    return s;
}

// Everything the state carries, flattened, so "the environment moved" is one number.
std::vector<float> flatten(const DayNightState& s) {
    return {s.sunDirection.x, s.sunDirection.y, s.sunDirection.z,
            s.sunColor.r,     s.sunColor.g,     s.sunColor.b,
            s.sunIntensity,   s.moonIntensity,
            s.zenithColor.r,  s.zenithColor.g,  s.zenithColor.b,
            s.horizonColor.r, s.horizonColor.g, s.horizonColor.b,
            s.groundColor.r,  s.groundColor.g,  s.groundColor.b,
            s.skyIntensity,   s.haze,           s.starBrightness,
            s.hdriIntensity,  s.hdriBlend,      s.glowScale,
            s.fogDensity,     s.fogColor.r,     s.fogColor.g, s.fogColor.b};
}

float maxDelta(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

} // namespace

TEST_CASE("dayPhase is a pure function of the clock, not an accumulator", "[daynight][environment]") {
    const DayNightSettings s = defaults();

    // Evaluated at one second, then at the same second reached by a different route. An
    // integrating implementation would disagree; this one cannot.
    CHECK(phaseAt(s, 60.0) == Approx(0.25f));   // a quarter of a 240 s cycle: sunrise
    CHECK(phaseAt(s, 120.0) == Approx(0.50f));  // noon
    CHECK(phaseAt(s, 180.0) == Approx(0.75f));  // sunset

    // Whole cycles later is the same phase. This is the property that makes a long offline render
    // match a scrub, and the one an accumulated float would lose to drift.
    for (int cycle : {1, 7, 100, 5000}) {
        const double later = 60.0 + 240.0 * cycle;
        INFO("cycle " << cycle << " -> t = " << later);
        CHECK(phaseAt(s, later) == Approx(phaseAt(s, 60.0)).margin(1e-4));
    }

    // Negative time is still in [0, 1): a timeline can be scrubbed before zero.
    CHECK(phaseAt(s, -60.0) >= 0.0f);
    CHECK(phaseAt(s, -60.0) < 1.0f);
    CHECK(phaseAt(s, -60.0) == Approx(0.75f));

    // Paused holds the manual phase and ignores the clock entirely -- the scrub case.
    DayNightSettings held = s;
    held.paused = true;
    held.manualPhase = 0.375f;
    CHECK(phaseAt(held, 0.0) == Approx(0.375f));
    CHECK(phaseAt(held, 999999.0) == Approx(0.375f));
}

TEST_CASE("The environment is deterministic in phase", "[daynight][environment]") {
    const DayNightSettings s = defaults();
    for (float phase : {0.0f, 0.125f, 0.25f, 0.5f, 0.625f, 0.75f, 0.9f}) {
        INFO("phase " << phase);
        CHECK(maxDelta(flatten(resolveDayNight(s, phase)), flatten(resolveDayNight(s, phase))) == 0.0f);
    }
    // And reaching a phase from a different starting point changes nothing, because there is no
    // starting point to reach it from.
    DayNightState direct = resolveDayNight(s, 0.5f);
    for (float p : {0.1f, 0.9f, 0.3f}) {
        (void)resolveDayNight(s, p);
    }
    CHECK(maxDelta(flatten(direct), flatten(resolveDayNight(s, 0.5f))) == 0.0f);
}

TEST_CASE("The cycle wraps at midnight with no discontinuity", "[daynight][environment]") {
    const DayNightSettings s = defaults();

    // Either side of the seam. The step across 0.99 -> 0.01 must be no larger than a step of the
    // same size taken anywhere else in the cycle -- this is the assertion that catches a curve
    // evaluator that clamps at the ends instead of treating the list as a ring.
    const float seam = maxDelta(flatten(resolveDayNight(s, 0.99f)), flatten(resolveDayNight(s, 0.01f)));

    float worstElsewhere = 0.0f;
    for (int i = 0; i < 50; ++i) {
        const float a = 0.02f + static_cast<float>(i) * 0.019f;
        worstElsewhere = std::max(worstElsewhere,
                                  maxDelta(flatten(resolveDayNight(s, a)),
                                           flatten(resolveDayNight(s, a + 0.02f))));
    }
    INFO("seam step " << seam << " vs worst 0.02 step elsewhere " << worstElsewhere);
    CHECK(seam <= worstElsewhere);

    // Exactly 0 and exactly 1 are the same instant.
    CHECK(maxDelta(flatten(resolveDayNight(s, 0.0f)), flatten(resolveDayNight(s, 1.0f))) == Approx(0.0f).margin(1e-5));

    // The control: the cycle actually goes somewhere. Every assertion above would pass on a
    // constant environment, which is the ADR-182 failure this guards against. Midnight and noon
    // must differ by a lot -- a band, not a floor.
    const float day = maxDelta(flatten(resolveDayNight(s, 0.0f)), flatten(resolveDayNight(s, 0.5f)));
    INFO("midnight vs noon, largest channel difference " << day);
    CHECK(day > 0.5f);
    CHECK(day < 20.0f);
}

TEST_CASE("The sun is up by day and down by night", "[daynight][environment]") {
    const DayNightSettings s = defaults();
    // `sunDirection` is the direction light travels, so a sun overhead points down: y < 0.
    CHECK(resolveDayNight(s, 0.5f).sunDirection.y < -0.5f);   // noon, high
    CHECK(resolveDayNight(s, 0.0f).sunDirection.y > 0.5f);    // midnight, below the world
    // Sunrise and sunset are the horizon crossings, and they are on opposite sides.
    CHECK(std::abs(resolveDayNight(s, 0.25f).sunDirection.y) < 0.05f);
    CHECK(std::abs(resolveDayNight(s, 0.75f).sunDirection.y) < 0.05f);
    const glm::vec3 dawn = resolveDayNight(s, 0.25f).sunDirection;
    const glm::vec3 dusk = resolveDayNight(s, 0.75f).sunDirection;
    CHECK(glm::dot(glm::normalize(glm::vec3(dawn.x, 0.0f, dawn.z)),
                   glm::normalize(glm::vec3(dusk.x, 0.0f, dusk.z))) < -0.5f);

    // Light and dark behave as the brief describes: the sun carries the day, the moon the night,
    // and they do not both burn at once.
    CHECK(resolveDayNight(s, 0.5f).sunIntensity > 2.0f);
    CHECK(resolveDayNight(s, 0.5f).moonIntensity == Approx(0.0f));
    CHECK(resolveDayNight(s, 0.0f).sunIntensity == Approx(0.0f));
    CHECK(resolveDayNight(s, 0.0f).moonIntensity > 1.0f);

    // Stars fade rather than toggle, and are invisible at noon.
    CHECK(resolveDayNight(s, 0.5f).starBrightness == Approx(0.0f));
    CHECK(resolveDayNight(s, 0.0f).starBrightness > 0.9f);
    CHECK(resolveDayNight(s, 0.30f).starBrightness > 0.0f);
    CHECK(resolveDayNight(s, 0.30f).starBrightness < 0.9f);

    // Glowmere is subtle by day and dominant at night, and `glowInfluence` 0 disables the
    // coupling without touching the curve -- the brief wants the manual intensity to stay
    // independently controllable.
    CHECK(resolveDayNight(s, 0.5f).glowScale < 0.4f);
    CHECK(resolveDayNight(s, 0.0f).glowScale == Approx(1.0f));
    DayNightSettings ignore = s;
    ignore.glowInfluence = 0.0f;
    CHECK(resolveDayNight(ignore, 0.5f).glowScale == Approx(1.0f));
}

TEST_CASE("Fog fades into the colour the sky has at the horizon", "[daynight][environment]") {
    const DayNightSettings s = defaults();
    // Atmospheric perspective is "a thing receding fades into the sky at the horizon". Authoring
    // the fog colour and the horizon colour as two independent curves let them disagree, and on
    // the ocean world they did: distant water fully fogged to one colour met a sky of another and
    // the seam read as a SECOND HORIZON in the grazing view. Measured before the fix -- the band
    // sat at 24/32/55 sRGB against a fog colour of 29/35/51, which is how it was identified.
    for (float phase : {0.0f, 0.25f, 0.5f, 0.75f, 0.9f}) {
        const DayNightState st = resolveDayNight(s, phase);
        const glm::vec3 want = st.horizonColor * st.skyIntensity;
        INFO("phase " << phase);
        CHECK(st.fogColor.r == Approx(want.r).margin(1e-5));
        CHECK(st.fogColor.g == Approx(want.g).margin(1e-5));
        CHECK(st.fogColor.b == Approx(want.b).margin(1e-5));
    }

    // THE CONTROL. `fogHorizonBlend` 0 must give the hand-authored curve back, and that curve must
    // actually differ from the horizon -- otherwise the assertions above would hold for a system
    // that ignored the setting entirely.
    DayNightSettings authored = s;
    authored.fogHorizonBlend = 0.0f;
    const DayNightState tracked = resolveDayNight(s, 0.75f);
    const DayNightState hand = resolveDayNight(authored, 0.75f);
    const float apart = std::abs(tracked.fogColor.r - hand.fogColor.r) +
                        std::abs(tracked.fogColor.g - hand.fogColor.g) +
                        std::abs(tracked.fogColor.b - hand.fogColor.b);
    INFO("tracked vs authored fog at sunset, summed channel difference " << apart);
    CHECK(apart > 0.02f);
}

TEST_CASE("The HDRI contribution follows the cycle and swaps at its quietest", "[daynight][environment]") {
    const DayNightSettings s = defaults();
    // Strong by day, near zero at night: the brief's target behaviour.
    CHECK(resolveDayNight(s, 0.50f).hdriIntensity > 0.9f);
    CHECK(resolveDayNight(s, 0.75f).hdriIntensity < 0.6f);   // sunset, reduced
    CHECK(resolveDayNight(s, 0.00f).hdriIntensity < 0.25f);  // night, near zero
    CHECK(resolveDayNight(s, 0.00f).hdriIntensity > 0.0f);   // but not zero: the water needs it

    CHECK_FALSE(resolveDayNight(s, 0.5f).useNightMap);
    CHECK(resolveDayNight(s, 0.0f).useNightMap);

    // The map swap happens where the contribution is low. This is what makes a binary swap read
    // as a crossfade, and it is the claim most worth pinning: find the phase where `useNightMap`
    // flips and check the contribution there is a fraction of the daytime peak.
    const float noonIntensity = resolveDayNight(s, 0.5f).hdriIntensity;
    bool previous = resolveDayNight(s, 0.0f).useNightMap;
    int flips = 0;
    for (int i = 1; i <= 2000; ++i) {
        const float p = static_cast<float>(i) / 2000.0f;
        const DayNightState st = resolveDayNight(s, p);
        if (st.useNightMap != previous) {
            ++flips;
            INFO("swap at phase " << p << " with hdriIntensity " << st.hdriIntensity);
            CHECK(st.hdriIntensity < noonIntensity * 0.45f);
            previous = st.useNightMap;
        }
    }
    // Exactly two swaps in a cycle: one into night, one back out. More would mean the curve
    // chatters across the threshold, which would be a visible flicker.
    CHECK(flips == 2);
}
