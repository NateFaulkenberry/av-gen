#include "scene/day_night.hpp"

#include <bit>
#include <cmath>
#include <cstdint>

namespace avgen::scene {

namespace {

// FNV-1a over the settings. `scene::SkyRuntime` has an identical helper, but it is a private class
// inside sky.cpp's anonymous namespace; duplicating eleven lines is cheaper than hoisting it into
// a shared header and touching a file this pass has no other reason to open.
class Hash64 {
public:
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h_ = (h_ ^ ((v >> (8 * i)) & 0xFFu)) * 0x100000001b3ULL;
        }
    }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v == 0.0f ? 0.0f : v)); }
    void v3(const glm::vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    [[nodiscard]] std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = 0xcbf29ce484222325ULL;
};

// The cycle's landmarks, as phases. 0 is midnight, 0.25 sunrise, 0.5 noon, 0.75 sunset -- the
// convention the brief asks for, written once so the curves below read as times of day.
constexpr float kMidnight = 0.00f;
constexpr float kPreDawn = 0.18f;
constexpr float kSunrise = 0.25f;
constexpr float kMorning = 0.33f;
constexpr float kNoon = 0.50f;
constexpr float kAfternoon = 0.64f;
constexpr float kSunset = 0.75f;
constexpr float kTwilight = 0.82f;
constexpr float kNight = 0.90f;

template <typename T>
void fill(std::vector<PhaseKey<T>>& keys, std::initializer_list<PhaseKey<T>> defaults) {
    if (keys.empty()) {
        keys.assign(defaults);
    }
}

} // namespace

void DayNightSettings::applyDefaults() {
    // Colours are scene-linear. The journey the brief asks for in §20 -- indigo, violet, pink,
    // cyan, gold, magenta, back to indigo -- lives in these four lists and nowhere else.
    fill<glm::vec3>(sunColor, {
        {kMidnight, {0.32f, 0.46f, 0.78f}},   // no sun; this tints the moonlight the rig carries
        {kPreDawn, {0.46f, 0.42f, 0.72f}},
        {kSunrise, {1.00f, 0.52f, 0.36f}},    // low warm pink-orange
        {kMorning, {1.00f, 0.86f, 0.70f}},
        {kNoon, {1.00f, 0.97f, 0.92f}},       // near neutral, slightly cool
        {kAfternoon, {1.00f, 0.90f, 0.74f}},
        {kSunset, {1.00f, 0.44f, 0.30f}},
        {kTwilight, {0.72f, 0.36f, 0.62f}},   // the violet the brief wants held onto
        {kNight, {0.34f, 0.44f, 0.80f}},
    });
    fill<float>(sunIntensity, {
        {kMidnight, 0.0f}, {kPreDawn, 0.0f}, {kSunrise, 0.55f}, {kMorning, 2.10f},
        {kNoon, 3.05f}, {kAfternoon, 2.35f}, {kSunset, 0.70f}, {kTwilight, 0.06f}, {kNight, 0.0f},
    });
    // The moon is the HDRI's; this is the *light* that goes with it, so the night has direction
    // and shadows rather than only ambient. It is deliberately an order of magnitude under the
    // sun: moonlight that competes with the Glowmere emission defeats the point of the night.
    fill<float>(moonIntensity, {
        {kMidnight, 1.60f}, {kPreDawn, 1.35f}, {kSunrise, 0.30f}, {kMorning, 0.0f},
        {kNoon, 0.0f}, {kAfternoon, 0.0f}, {kSunset, 0.18f}, {kTwilight, 0.85f}, {kNight, 1.45f},
    });
    fill<glm::vec3>(zenithColor, {
        {kMidnight, {0.0032f, 0.0044f, 0.0166f}},
        {kPreDawn, {0.0090f, 0.0090f, 0.0330f}},
        {kSunrise, {0.0400f, 0.0430f, 0.1100f}},
        {kMorning, {0.0700f, 0.1150f, 0.2400f}},
        {kNoon, {0.0850f, 0.1500f, 0.3200f}},
        {kAfternoon, {0.0780f, 0.1220f, 0.2600f}},
        {kSunset, {0.0480f, 0.0420f, 0.1250f}},
        {kTwilight, {0.0170f, 0.0140f, 0.0560f}},
        {kNight, {0.0055f, 0.0062f, 0.0230f}},
    });
    fill<glm::vec3>(horizonColor, {
        {kMidnight, {0.0094f, 0.0102f, 0.0286f}},
        {kPreDawn, {0.0330f, 0.0230f, 0.0580f}},   // violet lift before the sun
        {kSunrise, {0.4200f, 0.1700f, 0.1300f}},   // the warm band
        {kMorning, {0.2100f, 0.2400f, 0.3100f}},
        {kNoon, {0.2200f, 0.2900f, 0.4000f}},
        {kAfternoon, {0.3000f, 0.2500f, 0.2600f}},
        {kSunset, {0.5200f, 0.1900f, 0.1600f}},
        {kTwilight, {0.1300f, 0.0620f, 0.1500f}},  // magenta into violet
        {kNight, {0.0180f, 0.0170f, 0.0430f}},
    });
    fill<glm::vec3>(groundColor, {
        {kMidnight, {0.0052f, 0.0118f, 0.0212f}},
        {kSunrise, {0.0700f, 0.0520f, 0.0560f}},
        {kNoon, {0.1000f, 0.1250f, 0.1550f}},
        {kSunset, {0.0900f, 0.0520f, 0.0520f}},
        {kNight, {0.0070f, 0.0130f, 0.0250f}},
    });
    fill<float>(skyIntensity, {
        {kMidnight, 0.16f}, {kPreDawn, 0.20f}, {kSunrise, 0.62f}, {kMorning, 1.00f},
        {kNoon, 1.10f}, {kAfternoon, 1.00f}, {kSunset, 0.66f}, {kTwilight, 0.30f}, {kNight, 0.17f},
    });
    fill<float>(haze, {
        {kMidnight, 0.55f}, {kSunrise, 0.86f}, {kNoon, 0.45f}, {kSunset, 0.90f},
        {kTwilight, 0.78f}, {kNight, 0.60f},
    });
    // Stars do not toggle. They are gone by mid-morning and back by twilight, on a curve.
    fill<float>(starBrightness, {
        {kMidnight, 1.00f}, {kPreDawn, 0.85f}, {kSunrise, 0.28f}, {kMorning, 0.0f},
        {kNoon, 0.0f}, {kAfternoon, 0.0f}, {kSunset, 0.10f}, {kTwilight, 0.55f}, {kNight, 0.95f},
    });
    // The HDRI contribution. The brief's target: strong by day, reduced at sunset, near zero at
    // night -- but not *at* zero, because the night map is what gives the water something to
    // reflect and the island's underside something other than black.
    fill<float>(hdriIntensity, {
        {kMidnight, 0.16f}, {kPreDawn, 0.14f}, {kSunrise, 0.42f}, {kMorning, 0.92f},
        {kNoon, 1.00f}, {kAfternoon, 0.88f}, {kSunset, 0.46f}, {kTwilight, 0.22f}, {kNight, 0.16f},
    });
    // Which map. The crossfade the brief draws cannot be a texture blend here -- the renderer
    // holds one environment cube -- so this curve decides which map is bound, and it is shaped so
    // that it crosses 0.5 in late twilight and at pre-dawn, where `hdriIntensity` is at its
    // lowest. The swap therefore happens at the moment it is least visible, which is the honest
    // version of "do not perform a hard switch at sunset".
    fill<float>(hdriBlend, {
        {kMidnight, 1.0f}, {kPreDawn, 1.0f}, {kSunrise, 0.0f}, {kNoon, 0.0f},
        {kSunset, 0.0f}, {kTwilight, 0.35f}, {kNight, 1.0f},
    });
    // Glowmere: subtle by day, dominant at night. Accelerating toward night, as §33 asks.
    fill<float>(glowScale, {
        {kMidnight, 1.00f}, {kPreDawn, 0.92f}, {kSunrise, 0.55f}, {kMorning, 0.30f},
        {kNoon, 0.22f}, {kAfternoon, 0.32f}, {kSunset, 0.62f}, {kTwilight, 0.85f}, {kNight, 1.00f},
    });
    // Tuned against the ocean's actual extent, not guessed. Transmittance is exp(-(d*density)^2),
    // so the number that matters is the pair (island at ~257 u, water edge at 20,000 u): these
    // leave the island at 0.99+ and the far water at 0.00. An earlier set was 15x higher because
    // it was tuned when the plane edge was at 3,000 u, and at the enlarged extent it washed the
    // whole frame into flat dust -- the island, the water and the sky all one colour at sunset.
    // The water is part of the world changing state, not a static surface the sky happens to fall
    // on. `reflection` multiplies its environment sample: a night sea that mirrors the sky as hard
    // as a noon sea does comes out a pale sheet, which is what it was doing.
    //
    // The reflection is deliberately LOW at dawn and sunset, and the deep colour correspondingly
    // warm. That is not an aesthetic preference, it is working around a real limitation: the water
    // shader samples the environment *cube*, which is the HDRI, and the HDRI is a fixed blue-sky
    // map. With the visible sky now procedural (ADR-345) the two disagree at exactly the moments
    // the brief cares most about -- "sky says sunset, water still looks like noon" is its own §28
    // failure mode. Turning the reflection down at the warm ends and carrying the colour in the
    // water's own body is the half of that which does not need another renderer change.
    fill<float>(waterReflection, {
        {kMidnight, 0.55f}, {kPreDawn, 0.60f}, {kSunrise, 0.80f}, {kMorning, 2.40f},
        {kNoon, 3.10f}, {kAfternoon, 2.50f}, {kSunset, 0.85f}, {kTwilight, 0.62f}, {kNight, 0.58f},
    });
    fill<glm::vec3>(waterDeepColor, {
        {kMidnight, {0.0016f, 0.0060f, 0.0150f}},
        {kSunrise, {0.1450f, 0.0560f, 0.0520f}},   // the warm horizon reaching into the water
        {kNoon, {0.0090f, 0.0330f, 0.0620f}},
        {kSunset, {0.1750f, 0.0600f, 0.0540f}},
        {kTwilight, {0.0520f, 0.0260f, 0.0700f}},  // violet
        {kNight, {0.0018f, 0.0064f, 0.0162f}},
    });
    // ADR-705: converted from the exp-squared curve this used to be (0.000135 at midnight, ...) by
    // matching the distance at which half the light gets through: sigma = sqrt(ln 2) * density,
    // over the default absorption 0.5.
    fill<float>(volumeDensity, {
        {kMidnight, 0.000225f}, {kSunrise, 0.000358f}, {kNoon, 0.000158f}, {kSunset, 0.000341f},
        {kTwilight, 0.000275f}, {kNight, 0.000233f},
    });
    fill<glm::vec3>(fogColor, {
        {kMidnight, {0.0125f, 0.0165f, 0.0335f}},
        {kSunrise, {0.1600f, 0.0900f, 0.0950f}},
        {kNoon, {0.1400f, 0.1900f, 0.2700f}},
        {kSunset, {0.2000f, 0.0850f, 0.0900f}},
        {kTwilight, {0.0600f, 0.0330f, 0.0760f}},
        {kNight, {0.0140f, 0.0170f, 0.0380f}},
    });
}

std::uint64_t DayNightSettings::hash() const {
    Hash64 h;
    h.u32(enabled ? 1u : 0u);
    h.f32(cycleSeconds);
    h.f32(phaseOffset);
    h.u32(paused ? 1u : 0u);
    h.f32(manualPhase);
    h.f32(sunPeakElevation);
    h.f32(sunAzimuthAtDawn);
    h.f32(sunAzimuthSweep);
    h.f32(sunIntensityScale);
    h.f32(moonIntensityScale);
    h.f32(starBrightnessScale);
    h.f32(hdriIntensityScale);
    h.f32(glowInfluence);
    h.f32(fogHorizonBlend);
    const auto hs = [&h](const std::string& v) {
        for (const char ch : v) {
            h.u32(static_cast<std::uint32_t>(static_cast<unsigned char>(ch)));
        }
        h.u32(0xFFFFFFFFu);
    };
    hs(sunLight);
    hs(moonLight);
    hs(dayMap);
    hs(nightMap);
    for (const std::string& n : starNodes) hs(n);
    for (const std::string& n : glowNodes) hs(n);
    const auto hf = [&h](const std::vector<PhaseKey<float>>& keys) {
        for (const auto& k : keys) {
            h.f32(k.phase);
            h.f32(k.value);
        }
    };
    const auto hv = [&h](const std::vector<PhaseKey<glm::vec3>>& keys) {
        for (const auto& k : keys) {
            h.f32(k.phase);
            h.v3(k.value);
        }
    };
    hv(sunColor);
    hf(sunIntensity);
    hf(moonIntensity);
    hv(zenithColor);
    hv(horizonColor);
    hv(groundColor);
    hf(skyIntensity);
    hf(haze);
    hf(starBrightness);
    hf(hdriIntensity);
    hf(hdriBlend);
    hf(glowScale);
    hf(waterReflection);
    hv(waterDeepColor);
    hf(volumeDensity);
    hv(fogColor);
    return h.value();
}

float phaseAt(const DayNightSettings& settings, double seconds) {
    if (settings.paused) {
        const float p = settings.manualPhase;
        return p - std::floor(p);
    }
    const double period = settings.cycleSeconds > 1e-3f ? static_cast<double>(settings.cycleSeconds)
                                                        : 1.0;
    // Pure function of the clock: no accumulator, so a dropped frame costs nothing and a scrub
    // lands exactly where a playback would have been.
    const double raw = seconds / period + static_cast<double>(settings.phaseOffset);
    const double frac = raw - std::floor(raw);
    return static_cast<float>(frac);
}

DayNightState resolveDayNight(const DayNightSettings& settings, float phase) {
    DayNightState s;
    s.phase = phase - std::floor(phase);

    // The sun's arc. `elevationT` is a cosine of the phase so it is smooth and periodic by
    // construction: -1 at midnight, +1 at noon, and crossing zero exactly at sunrise and sunset.
    const float twoPi = 6.2831853071795864f;
    const float elevationT = -std::cos(s.phase * twoPi);
    const float elevation = settings.sunPeakElevation * elevationT;
    // Azimuth sweeps across the day so that dawn and dusk are on opposite horizons. Using the
    // same phase keeps it continuous across midnight.
    const float azimuth = settings.sunAzimuthAtDawn + settings.sunAzimuthSweep * (s.phase - 0.25f) * 2.0f;
    const glm::vec3 towardsSun(std::cos(elevation) * std::sin(azimuth),
                               std::sin(elevation),
                               std::cos(elevation) * std::cos(azimuth));
    s.sunDirection = -glm::normalize(towardsSun); // lights carry the direction of travel

    s.sunColor = samplePhase(settings.sunColor, s.phase);
    s.sunIntensity = std::max(0.0f, samplePhase(settings.sunIntensity, s.phase) * settings.sunIntensityScale);
    s.moonIntensity = std::max(0.0f, samplePhase(settings.moonIntensity, s.phase) * settings.moonIntensityScale);
    s.zenithColor = glm::max(samplePhase(settings.zenithColor, s.phase), glm::vec3(0.0f));
    s.horizonColor = glm::max(samplePhase(settings.horizonColor, s.phase), glm::vec3(0.0f));
    s.groundColor = glm::max(samplePhase(settings.groundColor, s.phase), glm::vec3(0.0f));
    s.skyIntensity = std::max(0.0f, samplePhase(settings.skyIntensity, s.phase));
    s.haze = std::max(1e-3f, samplePhase(settings.haze, s.phase));
    s.starBrightness = std::max(0.0f, samplePhase(settings.starBrightness, s.phase) * settings.starBrightnessScale);
    s.hdriIntensity = std::max(0.0f, samplePhase(settings.hdriIntensity, s.phase) * settings.hdriIntensityScale);
    s.hdriBlend = std::clamp(samplePhase(settings.hdriBlend, s.phase), 0.0f, 1.0f);
    const float glow = samplePhase(settings.glowScale, s.phase);
    // `glowInfluence` mixes between "ignore the cycle" and "follow it fully", so the manual
    // Glowmere intensities stay independently controllable as the brief requires.
    s.glowScale = std::max(0.0f, 1.0f + (glow - 1.0f) * settings.glowInfluence);
    s.waterReflection = std::max(0.0f, samplePhase(settings.waterReflection, s.phase));
    s.waterDeepColor = glm::max(samplePhase(settings.waterDeepColor, s.phase), glm::vec3(0.0f));
    s.volumeDensity = std::max(0.0f, samplePhase(settings.volumeDensity, s.phase));
    const glm::vec3 authoredFog = glm::max(samplePhase(settings.fogColor, s.phase), glm::vec3(0.0f));
    // The colour the sky actually is at the horizon, scaled the way the visible sky is scaled, so
    // water receding into fog and sky meeting the horizon arrive at the same value.
    const glm::vec3 horizonFog = s.horizonColor * s.skyIntensity;
    const float blend = std::clamp(settings.fogHorizonBlend, 0.0f, 1.0f);
    s.fogColor = glm::max(authoredFog * (1.0f - blend) + horizonFog * blend, glm::vec3(0.0f));
    s.useNightMap = s.hdriBlend >= 0.5f;
    return s;
}

} // namespace avgen::scene
