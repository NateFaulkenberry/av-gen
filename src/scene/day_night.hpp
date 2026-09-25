#pragma once

// One authoritative time of day, and everything the environment derives from it (ADR-343).
//
// The brief's requirement is that a single normalised `dayPhase` drives the sky, the sun, the
// moonlight, the stars, the atmosphere, the environment maps, the water and the tree's
// bioluminescence -- so that the scene reads as one world changing state rather than as eight
// animations that happen to be running at once.
//
// Two properties are load-bearing and everything here is shaped by them:
//
//   * **`dayPhase` is a pure function of time.** `phaseAt` is `fract(seconds / cycleSeconds +
//     offset)`; there is no accumulator anywhere. This is the `LfoSource::update` precedent
//     (`renderTime * rate + phase`), and it is why scrubbing equals playing, why an offline render
//     of frame N matches a live playback at second N, and why nothing drifts over a long run.
//     An integrated cycle would fail all three the first time a frame was dropped.
//
//   * **`resolve` is a pure function of phase.** Same phase in, same state out, with no history.
//     That is what makes the eight showcase frames reproducible and the A/B arms meaningful.
//
// Wrapping is by construction rather than by care: every curve here is evaluated on a circular
// keyframe list, so phase 0.99 and phase 0.01 are 0.02 apart and the seam at midnight is not a
// special case anybody has to remember.

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

// A value on the cycle. `phase` is in [0, 1); the list is kept sorted and read circularly.
template <typename T>
struct PhaseKey {
    float phase = 0.0f;
    T value{};
};

// Interpolate a circular keyframe list at `phase`. `smooth` applies a smoothstep to the segment
// parameter, which is the brief's "do not assume every parameter should change linearly" -- a
// linear ramp through sunrise reads mechanical, and the fix belongs in the evaluator rather than
// in every caller.
template <typename T>
[[nodiscard]] T samplePhase(const std::vector<PhaseKey<T>>& keys, float phase, bool smooth = true) {
    if (keys.empty()) {
        return T{};
    }
    if (keys.size() == 1) {
        return keys.front().value;
    }
    phase = phase - std::floor(phase);
    // The segment containing `phase`, treating the list as a ring: the last key wraps to the first
    // across the midnight seam.
    std::size_t hi = 0;
    while (hi < keys.size() && keys[hi].phase <= phase) {
        ++hi;
    }
    const std::size_t i1 = hi % keys.size();
    const std::size_t i0 = (i1 + keys.size() - 1) % keys.size();
    float p0 = keys[i0].phase;
    float p1 = keys[i1].phase;
    float x = phase;
    if (i1 == 0) {          // wrapping segment: last -> first, across 1.0
        p1 += 1.0f;
        if (x < p0) {
            x += 1.0f;
        }
    }
    const float span = p1 - p0;
    float t = span > 1e-6f ? (x - p0) / span : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    if (smooth) {
        t = t * t * (3.0f - 2.0f * t);
    }
    return keys[i0].value * (1.0f - t) + keys[i1].value * t;
}

// What the author writes. Defaults describe the Tree of Life world: a 240-second cycle, a sun that
// rises in the east and sets in the west, and a Glowmere night.
struct DayNightSettings {
    bool enabled = false;
    // Seconds of timeline for one full midnight-to-midnight cycle. The brief's `cycleSpeed` is
    // expressed as a duration because a duration is what an author can reason about, and because
    // a speed of zero is a division rather than a pause.
    float cycleSeconds = 240.0f;
    float phaseOffset = 0.0f;   // added before the fract; 0 starts the timeline at midnight
    bool paused = false;        // hold `manualPhase` instead of following the clock
    float manualPhase = 0.0f;   // the scrub target, and what `paused` holds

    // Where the sun goes. Elevation runs -1 (straight down, midnight) to +1 (overhead, noon); the
    // azimuth sweeps so that sunrise and sunset are on opposite sides. Both are shaped rather than
    // linear so the sun lingers near the horizon, which is where all the colour is.
    float sunPeakElevation = 0.78f;   // radians at noon
    float sunAzimuthAtDawn = 1.62f;   // radians; +Z is 0
    float sunAzimuthSweep = 3.14159f; // radians swept from dawn to dusk

    std::vector<PhaseKey<glm::vec3>> sunColor;
    std::vector<PhaseKey<float>> sunIntensity;
    std::vector<PhaseKey<float>> moonIntensity;
    std::vector<PhaseKey<glm::vec3>> zenithColor;
    std::vector<PhaseKey<glm::vec3>> horizonColor;
    std::vector<PhaseKey<glm::vec3>> groundColor;
    std::vector<PhaseKey<float>> skyIntensity;
    std::vector<PhaseKey<float>> haze;
    std::vector<PhaseKey<float>> starBrightness;
    std::vector<PhaseKey<float>> hdriIntensity;   // the IBL contribution, both maps
    std::vector<PhaseKey<float>> hdriBlend;       // 0 = day map, 1 = night map
    std::vector<PhaseKey<float>> glowScale;       // multiplies the tree's Glowmere channels
    std::vector<PhaseKey<float>> waterReflection;  // scales the surface's environment sample
    std::vector<PhaseKey<glm::vec3>> waterDeepColor;
    // ADR-705: the air's ONE density (`Environment::volumeDensity`), at the default absorption 0.5.
    // Was `fogDensity`, the surface pass's own exp-squared density, which no longer exists; the
    // defaults were converted so the fog is as thick at the distance it reaches half (ADR-705).
    std::vector<PhaseKey<float>> volumeDensity;
    std::vector<PhaseKey<glm::vec3>> fogColor;

    // Scalars the UI exposes on top of the curves, so an author can push the whole night brighter
    // without editing eight keyframes.
    float sunIntensityScale = 1.0f;
    float moonIntensityScale = 1.0f;
    float starBrightnessScale = 1.0f;
    float hdriIntensityScale = 1.0f;
    float glowInfluence = 1.0f;   // 0 = Glowmere ignores the cycle; 1 = full curve
    // How much the fog colour is taken from the sky's own horizon rather than from the `fogColor`
    // curve. 1 (the default) is what atmospheric perspective actually is: something receding fades
    // into the colour the sky has where it meets the ground. Authoring the two separately is how
    // the ocean world got a visible band -- distant water fully fogged to one colour, meeting a
    // horizon of another, reading as a second horizon. 0 restores the hand-authored curve.
    float fogHorizonBlend = 1.0f;

    // What the cycle drives, named rather than guessed. A system that looked for a light called
    // "sun" would work on this scene and quietly do nothing on the next one; naming the bindings
    // in the scene file makes the coupling visible where an author can see and change it.
    std::string sunLight;               // the directional the sun curve drives; "" = none
    std::string moonLight;              // the directional the moon curve drives; "" = none
    std::vector<std::string> starNodes; // procedural nodes whose emission is the starfield
    std::vector<std::string> glowNodes; // glTF nodes carrying the tree's Glowmere layers
    std::string dayMap;                 // equirect used while `useNightMap` is false
    std::string nightMap;               // equirect used while it is true

    // Fills every empty curve with the Tree of Life defaults. Called on load so a scene that says
    // only `"dayNight": { "enabled": true }` gets a complete, cinematic cycle.
    void applyDefaults();
    [[nodiscard]] std::uint64_t hash() const;
};

// What one phase resolves to. Everything the renderer and the composition need, and nothing that
// has to be remembered between frames.
struct DayNightState {
    float phase = 0.0f;
    glm::vec3 sunDirection{0.0f, -1.0f, 0.0f}; // the direction light TRAVELS, as a light wants it
    glm::vec3 sunColor{1.0f};
    float sunIntensity = 0.0f;
    float moonIntensity = 0.0f;
    glm::vec3 zenithColor{0.0f};
    glm::vec3 horizonColor{0.0f};
    glm::vec3 groundColor{0.0f};
    float skyIntensity = 1.0f;
    float haze = 0.5f;
    float starBrightness = 1.0f;
    float hdriIntensity = 1.0f;
    float hdriBlend = 0.0f;
    float glowScale = 1.0f;
    float waterReflection = 1.0f;
    glm::vec3 waterDeepColor{0.0f};
    float volumeDensity = 0.0f;
    glm::vec3 fogColor{0.0f};
    // True when the night map should be bound. The renderer holds one environment cube, so the
    // two maps cannot be blended in shading; the swap is made where `hdriBlend` crosses 0.5, which
    // the default curves place at the cycle's darkest-contribution point so nothing jumps.
    bool useNightMap = false;
};

// `fract(seconds / cycleSeconds + offset)`, or the held phase when paused. No accumulator.
[[nodiscard]] float phaseAt(const DayNightSettings& settings, double seconds);

// Pure: same phase in, same state out.
[[nodiscard]] DayNightState resolveDayNight(const DayNightSettings& settings, float phase);

} // namespace avgen::scene
