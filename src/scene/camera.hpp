#pragma once

// Physical camera: lens, exposure and focus (ADR-037). The lens turns focal length, sensor size
// and aperture into a field of view and a circle of confusion; the exposure block turns aperture,
// shutter and ISO (or a meter reading) into the linear scale the image formation chain applies
// before bloom (ADR-039); the focus block moves the focus distance toward a target over time.
//
// Everything here is GPU-free and deterministic: given the same inputs (including the metered
// luminance and the frame's delta time) the state advances identically, so an offline render
// reproduces a live one exactly.

#include "params/parameter_set.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace avgen::scene {

struct CompositionData;

// ---- exposure maths ---------------------------------------------------------------------------

// EV100 = log2(N^2 / t) - log2(S / 100) (the photographic triangle; Lagarde and de Rousiers 2014).
[[nodiscard]] float exposureValue100(float aperture, float shutterSeconds, float iso);
[[nodiscard]] float exposureValue100(const ExposureSettings& settings);

// The photometric linear scale for an EV: 1 / (1.2 * 2^EV100). Absolute, in nits.
[[nodiscard]] float exposureScaleFromEv100(float ev100);

// EV100 a reflected-light meter reads for `luminance` cd/m^2: log2(L * S / K), S = 100, K = 12.5.
[[nodiscard]] float meteredEv100(float luminance);

// The scale the chain applies for an EV, normalised so `settings.referenceEv100` yields exactly 1
// (scene-linear units here are not nits; see ExposureSettings). Equivalent to
// 2^(referenceEv100 - ev100 + compensation).
[[nodiscard]] float appliedExposureScale(float ev100, const ExposureSettings& settings);

// Manual mode: the scale from aperture, shutter, ISO and compensation.
[[nodiscard]] float manualExposureScale(const ExposureSettings& settings);

// Automatic mode: the EV that places a metered luminance on mid grey, in the same scene-referred
// EV space as `referenceEv100`. referenceEv100 + log2(L / 0.18).
[[nodiscard]] float autoTargetEv100(float luminance, const ExposureSettings& settings);

// The metering feedback loop's state. Part of render state: reset it when a render job seeks.
struct ExposureState {
    float ev100 = 0.0f;   // the EV currently in force (scene-referred, see appliedExposureScale)
    bool seeded = false;  // false until the first frame seeds it from the manual EV
    void reset() {
        ev100 = 0.0f;
        seeded = false;
    }
};

// Seeds `state` from the manual EV if it is not seeded yet. Called for both modes so switching to
// automatic starts from the manual exposure rather than from black.
void seedExposure(ExposureState& state, const ExposureSettings& settings);

// Advances `state` toward the EV `luminance` calls for at speedUp/speedDown EV per second and
// returns the resulting scale. `deltaSeconds <= 0` holds the state still.
float updateAutoExposure(ExposureState& state, float luminance, float deltaSeconds,
                         const ExposureSettings& settings);

// The scale for either mode. `hasMeasurement` is false on the first frame of an automatic run (no
// previous image to meter): the state stays at the manual EV instead of chasing black.
float updateExposure(ExposureState& state, float luminance, bool hasMeasurement, float deltaSeconds,
                     const ExposureSettings& settings);

// ---- focus ------------------------------------------------------------------------------------

// What the lens focuses on (ADR-037). Fixed keeps `LensSettings::focusDistance`; Point focuses on
// a world position; Focal focuses on a composition focal point (`name`, or the composition's own
// camera target when empty, or the heaviest point when that is empty too).
struct FocusSettings {
    enum class Mode : std::uint8_t { Fixed, Point, Focal };
    Mode mode = Mode::Fixed;
    glm::vec3 point{0.0f};
    std::string name;
    float speed = 0.0f; // metres per second the focus travels; 0 = instant
};

struct FocusState {
    float distance = 0.0f;
    bool seeded = false;
    void reset() {
        distance = 0.0f;
        seeded = false;
    }
};

// The distance the lens should focus at. `fallback` (normally lens.focusDistance) is returned when
// the mode is Fixed or when the named focal point does not exist, so a scene with no composition
// behaves exactly as before.
[[nodiscard]] float focusTargetDistance(const FocusSettings& focus, const Camera& camera,
                                        const CompositionData& composition, float fallback);

// Moves `state` toward `target` at `speed` metres per second (0 or a first frame = instant) and
// returns the new distance.
float updateFocus(FocusState& state, float target, float deltaSeconds, float speed);

// ---- parameters (camera/lens/*, camera/exposure/*, camera/focus/*) ------------------------------

struct CameraParameters {
    params::Parameter<float>* focalLength = nullptr;
    params::Parameter<float>* sensorWidth = nullptr;
    params::Parameter<float>* sensorHeight = nullptr;
    params::Parameter<float>* aperture = nullptr;
    params::Parameter<float>* focusDistance = nullptr;
    params::Parameter<float>* shutterAngle = nullptr;
    params::Parameter<bool>* useExplicitFov = nullptr;
    params::Parameter<int>* exposureMode = nullptr;
    params::Parameter<float>* exposureAperture = nullptr;
    params::Parameter<float>* shutterSeconds = nullptr;
    params::Parameter<float>* iso = nullptr;
    params::Parameter<float>* compensation = nullptr;
    params::Parameter<float>* minEv = nullptr;
    params::Parameter<float>* maxEv = nullptr;
    params::Parameter<float>* speedUp = nullptr;
    params::Parameter<float>* speedDown = nullptr;
    params::Parameter<float>* meterCenterWeight = nullptr;
    params::Parameter<int>* focusMode = nullptr;
    params::Parameter<glm::vec3>* focusPoint = nullptr;
    params::Parameter<float>* focusSpeed = nullptr;
};

CameraParameters registerCameraParameters(params::ParameterSet& params, const LensSettings& lens,
                                          const ExposureSettings& exposure, const FocusSettings& focus);
void applyCameraParameters(const CameraParameters& p, LensSettings& lens, ExposureSettings& exposure,
                           FocusSettings& focus);

[[nodiscard]] const char* exposureModeName(ExposureSettings::Mode mode);
[[nodiscard]] const char* focusModeName(FocusSettings::Mode mode);

} // namespace avgen::scene
