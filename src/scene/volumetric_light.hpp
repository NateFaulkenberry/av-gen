#pragma once

// Volumetric lighting's scene block, `environment.volumetricLight` (ADR-2210/2211).
//
// It switches the froxel lighting on and says how far it carries the air. It does NOT redefine the air:
// density, height layer, colour, anisotropy, absorption and noise stay the environment's existing
// `volume*` / `fog*` controls -- ONE medium, read by the froxel, the march and the surface fog alike
// (ADR-2211). With it on, the froxel carries the environment's air from the eye out to `distance`, the
// march keeps only the placed media, and the surface fog takes over past `distance` under the same law.
//
// Every field is an ordinary parameter (`scene/volumetric/*`), so routes, the timeline, OSC and MIDI reach
// it. Quality (grid, slices, shadow taps, subsamples) is the tier's, never the scene's (ADR-2213).

namespace avgen::scene {

struct VolumetricLight {
    bool enabled = false;
    // View distance in metres the froxel grid reaches; the surface fog carries the air beyond it.
    float distance = 200.0f;
    // A multiplier on the light the air scatters toward the eye (not on its density or extinction).
    float intensity = 1.0f;

    [[nodiscard]] bool operator==(const VolumetricLight&) const = default;
};

inline constexpr float kVolumetricLightMinDistance = 1.0f;
inline constexpr float kVolumetricLightMaxDistance = 20000.0f;

} // namespace avgen::scene
