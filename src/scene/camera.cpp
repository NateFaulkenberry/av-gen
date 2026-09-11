#include "scene/camera.hpp"

#include "scene/composition_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace avgen::scene {

namespace {

constexpr float kMeterCalibration = 12.5f; // K in EV100 = log2(L S / K)
constexpr float kMidGrey = 0.18f;

params::ParamDesc<float> f(const char* path, float def, float lo, float hi, float slo, float shi,
                           const char* label = nullptr) {
    params::ParamDesc<float> d;
    d.path = path;
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = slo;
    d.softMax = shi;
    if (label != nullptr) {
        d.label = label;
    }
    return d;
}

params::ParamDesc<bool> b(const char* path, bool def) {
    params::ParamDesc<bool> d;
    d.path = path;
    d.defaultValue = def;
    d.hardMin = false;
    d.hardMax = true;
    return d;
}

params::ParamDesc<int> i(const char* path, int def, int lo, int hi, const char* label) {
    params::ParamDesc<int> d;
    d.path = path;
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = lo;
    d.softMax = hi;
    d.label = label;
    return d;
}

} // namespace

// ---- lens ---------------------------------------------------------------------------------------

float LensSettings::fovYRadians() const {
    return 2.0f * std::atan(std::max(sensorHeight, 1e-3f) / (2.0f * std::max(focalLength, 1e-3f)));
}

float LensSettings::fovXRadians() const {
    return 2.0f * std::atan(std::max(sensorWidth, 1e-3f) / (2.0f * std::max(focalLength, 1e-3f)));
}

float LensSettings::circleOfConfusion(float distance) const {
    // c = f^2 |d - s| / (N d (s - f)); f in millimetres, d and s converted from metres.
    const float lens = std::max(focalLength, 1e-3f);
    const float focus = std::max(focusDistance * 1000.0f, lens * 1.0001f + 1e-3f);
    const float subject = std::max(distance * 1000.0f, 1e-3f);
    const float n = std::max(aperture, 0.05f);
    return std::abs(lens * lens * (subject - focus) / (n * subject * (focus - lens)));
}

float LensSettings::circleOfConfusionPixels(float distance, float imageHeight) const {
    const float mmPerPixel = std::max(sensorHeight, 1e-3f) / std::max(imageHeight, 1.0f);
    return circleOfConfusion(distance) / mmPerPixel;
}

float Camera::effectiveFovY() const {
    return lens.useExplicitFov ? fovYRadians : lens.fovYRadians();
}

// ---- exposure -----------------------------------------------------------------------------------

float exposureValue100(float aperture, float shutterSeconds, float iso) {
    const float n = std::max(aperture, 1e-3f);
    const float t = std::max(shutterSeconds, 1e-9f);
    const float s = std::max(iso, 1e-3f);
    return std::log2(n * n / t) - std::log2(s / 100.0f);
}

float exposureValue100(const ExposureSettings& settings) {
    return exposureValue100(settings.aperture, settings.shutterSeconds, settings.iso);
}

float exposureScaleFromEv100(float ev100) { return 1.0f / (1.2f * std::exp2(ev100)); }

float meteredEv100(float luminance) {
    return std::log2(std::max(luminance, 1e-8f) * 100.0f / kMeterCalibration);
}

float appliedExposureScale(float ev100, const ExposureSettings& settings) {
    // exposureScaleFromEv100(ev - compensation) / exposureScaleFromEv100(referenceEv100).
    return std::exp2(settings.referenceEv100 - ev100 + settings.compensation);
}

float manualExposureScale(const ExposureSettings& settings) {
    return appliedExposureScale(exposureValue100(settings), settings);
}

float autoTargetEv100(float luminance, const ExposureSettings& settings) {
    return settings.referenceEv100 + std::log2(std::max(luminance, 1e-8f) / kMidGrey);
}

void seedExposure(ExposureState& state, const ExposureSettings& settings) {
    if (state.seeded) {
        return;
    }
    const float lo = std::min(settings.minEv, settings.maxEv);
    const float hi = std::max(settings.minEv, settings.maxEv);
    state.ev100 = std::clamp(exposureValue100(settings), lo, hi);
    state.seeded = true;
}

float updateAutoExposure(ExposureState& state, float luminance, float deltaSeconds,
                         const ExposureSettings& settings) {
    seedExposure(state, settings);
    const float lo = std::min(settings.minEv, settings.maxEv);
    const float hi = std::max(settings.minEv, settings.maxEv);
    const float target = std::clamp(autoTargetEv100(luminance, settings), lo, hi);
    // A brighter image needs a higher EV (a smaller scale); that direction is `speedUp`.
    const float rate = std::max(target > state.ev100 ? settings.speedUp : settings.speedDown, 0.0f);
    const float step = rate * std::max(deltaSeconds, 0.0f);
    state.ev100 += std::clamp(target - state.ev100, -step, step);
    state.ev100 = std::clamp(state.ev100, lo, hi);
    return appliedExposureScale(state.ev100, settings);
}

float updateExposure(ExposureState& state, float luminance, bool hasMeasurement, float deltaSeconds,
                     const ExposureSettings& settings) {
    if (settings.mode == ExposureSettings::Mode::Automatic && hasMeasurement) {
        return updateAutoExposure(state, luminance, deltaSeconds, settings);
    }
    // Manual, or automatic with nothing metered yet: hold the manual exposure and seed from it.
    state.ev100 = exposureValue100(settings);
    state.seeded = true;
    return appliedExposureScale(state.ev100, settings);
}

// ---- focus --------------------------------------------------------------------------------------

float focusTargetDistance(const FocusSettings& focus, const Camera& camera, const CompositionData& composition,
                          float fallback) {
    switch (focus.mode) {
    case FocusSettings::Mode::Point:
        return std::max(glm::length(focus.point - camera.position), 1e-3f);
    case FocusSettings::Mode::Focal: {
        const FocalPoint* point = nullptr;
        if (!focus.name.empty()) {
            point = composition.find(focus.name);
        } else if (!composition.cameraTarget.empty()) {
            point = composition.find(composition.cameraTarget);
        }
        if (point == nullptr) {
            for (const FocalPoint& candidate : composition.focalPoints) {
                if (point == nullptr || candidate.weight > point->weight) {
                    point = &candidate;
                }
            }
        }
        if (point == nullptr) {
            return fallback; // no composition: behave exactly as a fixed focus
        }
        return std::max(glm::length(point->position - camera.position), 1e-3f);
    }
    case FocusSettings::Mode::Fixed:
        break;
    }
    return fallback;
}

float updateFocus(FocusState& state, float target, float deltaSeconds, float speed) {
    if (!state.seeded || speed <= 0.0f || deltaSeconds <= 0.0f) {
        state.distance = target;
        state.seeded = true;
        return state.distance;
    }
    const float step = speed * deltaSeconds;
    state.distance += std::clamp(target - state.distance, -step, step);
    return state.distance;
}

// ---- names --------------------------------------------------------------------------------------

const char* exposureModeName(ExposureSettings::Mode mode) {
    return mode == ExposureSettings::Mode::Automatic ? "automatic" : "manual";
}

const char* focusModeName(FocusSettings::Mode mode) {
    switch (mode) {
    case FocusSettings::Mode::Point: return "point";
    case FocusSettings::Mode::Focal: return "focal";
    case FocusSettings::Mode::Fixed: break;
    }
    return "fixed";
}

// ---- parameters ----------------------------------------------------------------------------------

// ---- camera shake ------------------------------------------------------------------------------

namespace {

// A hash, not a generator. There is no state to seed, reset or get wrong on a seek: the value at a
// given integer step is a pure function of that step.
float shakeHash(int step, int channel) {
    auto h = static_cast<std::uint32_t>(step) * 0x9E3779B9u;
    h ^= static_cast<std::uint32_t>(channel + 1) * 0x85EBCA6Bu;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFF) * 2.0f - 1.0f;
}

// Smoothstep-interpolated value noise: continuous, band-limited at `frequency`, and zero-mean over
// any whole number of steps. White noise would read as a buzz and a sine would read as a wobble.
float shakeNoise(float t, int channel) {
    const float floored = std::floor(t);
    const auto step = static_cast<int>(floored);
    const float u = t - floored;
    const float s = u * u * (3.0f - 2.0f * u);
    return std::lerp(shakeHash(step, channel), shakeHash(step + 1, channel), s);
}

// Two octaves, normalised: the second gives the motion a jitter the first cannot, and without it a
// shake at 9 Hz reads as a slow sway.
float shakeChannel(float t, int channel) {
    return (shakeNoise(t, channel) + 0.5f * shakeNoise(t * 2.17f + 31.0f, channel + 8)) / 1.5f;
}

} // namespace

float cameraShakeEnvelope(const CameraShake& shake, double seconds) {
    if (!(shake.amplitude > 0.0f) && !(shake.rotationDegrees > 0.0f)) {
        return 0.0f;
    }
    const double elapsed = seconds - shake.startSeconds;
    if (elapsed < 0.0) {
        return 0.0f;
    }
    if (!(shake.decaySeconds > 0.0f)) {
        return 1.0f; // sustained: the amplitude is whatever a keyframe or a route says it is
    }
    const auto linear = static_cast<float>(1.0 - elapsed / shake.decaySeconds);
    if (linear <= 0.0f) {
        return 0.0f;
    }
    return linear * linear;
}

glm::vec3 cameraShakeOffset(const CameraShake& shake, double seconds) {
    const float envelope = cameraShakeEnvelope(shake, seconds);
    if (envelope <= 0.0f || !(shake.amplitude > 0.0f)) {
        return glm::vec3(0.0f);
    }
    const auto t = static_cast<float>(seconds * static_cast<double>(std::max(shake.frequency, 0.01f)));
    return glm::vec3(shakeChannel(t, 0), shakeChannel(t, 1), shakeChannel(t, 2)) *
           (shake.amplitude * envelope);
}

void applyCameraShake(const CameraShake& shake, double seconds, glm::vec3& position,
                      glm::vec3& target) {
    const float envelope = cameraShakeEnvelope(shake, seconds);
    if (envelope <= 0.0f) {
        return;
    }
    glm::vec3 forward = target - position;
    const float distance = glm::length(forward);
    if (distance < 1e-4f) {
        return;
    }
    forward /= distance;
    // The same world up every other camera in this engine uses; a shake must not invent a basis.
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::cross(forward, worldUp);
    if (glm::length(right) < 1e-4f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f); // straight down: any horizontal axis will do
    }
    right = glm::normalize(right);
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));

    const glm::vec3 offset = cameraShakeOffset(shake, seconds);
    position += right * offset.x + up * offset.y + forward * offset.z;
    // The aim moves with the body, so a positional shake alone does not swing the frame; the
    // angular term is what does, and it is deliberately separate so a handheld feel and a rumble
    // can be authored independently.
    glm::vec3 aim = position + forward * distance;
    if (shake.rotationDegrees > 0.0f) {
        const auto t = static_cast<float>(seconds *
                                          static_cast<double>(std::max(shake.frequency, 0.01f)));
        const float yaw = shakeChannel(t + 7.0f, 3) * shake.rotationDegrees * envelope;
        const float pitch = shakeChannel(t + 13.0f, 4) * shake.rotationDegrees * envelope;
        // Small angles: a degree of swing at distance d is d * tan(theta), and the linearisation is
        // exact enough for the amplitudes a shake is ever authored at.
        constexpr float kDegToRad = 0.01745329252f;
        aim += right * (std::tan(yaw * kDegToRad) * distance) +
               up * (std::tan(pitch * kDegToRad) * distance);
    }
    target = aim;
}

CameraParameters registerCameraParameters(params::ParameterSet& params, const LensSettings& lens,
                                          const ExposureSettings& exposure, const FocusSettings& focus) {
    CameraParameters p;
    p.focalLength = &params.add(f("camera/lens/focalLength", lens.focalLength, 4.0f, 800.0f, 12.0f, 200.0f));
    p.sensorWidth = &params.add(f("camera/lens/sensorWidth", lens.sensorWidth, 1.0f, 100.0f, 12.0f, 70.0f));
    p.sensorHeight = &params.add(f("camera/lens/sensorHeight", lens.sensorHeight, 1.0f, 100.0f, 8.0f, 50.0f));
    p.aperture = &params.add(f("camera/lens/aperture", lens.aperture, 0.7f, 45.0f, 1.0f, 22.0f));
    p.focusDistance = &params.add(f("camera/lens/focusDistance", lens.focusDistance, 0.02f, 5000.0f, 0.5f, 60.0f));
    p.shutterAngle = &params.add(f("camera/lens/shutterAngle", lens.shutterAngle, 0.0f, 360.0f, 0.0f, 360.0f));
    p.useExplicitFov = &params.add(b("camera/lens/useExplicitFov", lens.useExplicitFov));

    p.exposureMode = &params.add(i("camera/exposure/mode", static_cast<int>(exposure.mode), 0, 1,
                                   "camera/exposure/mode (0 manual, 1 automatic)"));
    p.exposureAperture = &params.add(f("camera/exposure/aperture", exposure.aperture, 0.7f, 45.0f, 1.0f, 22.0f));
    p.shutterSeconds =
        &params.add(f("camera/exposure/shutterSeconds", exposure.shutterSeconds, 1.0f / 8000.0f, 4.0f, 1.0f / 250.0f, 0.1f));
    p.iso = &params.add(f("camera/exposure/iso", exposure.iso, 25.0f, 204800.0f, 50.0f, 6400.0f));
    p.compensation = &params.add(f("camera/exposure/compensation", exposure.compensation, -8.0f, 8.0f, -3.0f, 3.0f));
    p.minEv = &params.add(f("camera/exposure/minEv", exposure.minEv, -16.0f, 24.0f, -8.0f, 8.0f));
    p.maxEv = &params.add(f("camera/exposure/maxEv", exposure.maxEv, -16.0f, 24.0f, 4.0f, 20.0f));
    p.speedUp = &params.add(f("camera/exposure/speedUp", exposure.speedUp, 0.0f, 30.0f, 0.0f, 8.0f));
    p.speedDown = &params.add(f("camera/exposure/speedDown", exposure.speedDown, 0.0f, 30.0f, 0.0f, 8.0f));
    p.meterCenterWeight =
        &params.add(f("camera/exposure/meterCenterWeight", exposure.meterCenterWeight, 0.0f, 1.0f, 0.0f, 1.0f));

    p.focusMode = &params.add(i("camera/focus/mode", static_cast<int>(focus.mode), 0, 2,
                                "camera/focus/mode (0 fixed, 1 point, 2 focal point)"));
    {
        params::ParamDesc<glm::vec3> d;
        d.path = "camera/focus/point";
        d.defaultValue = focus.point;
        d.hardMin = glm::vec3(-1e4f);
        d.hardMax = glm::vec3(1e4f);
        d.softMin = glm::vec3(-50.0f);
        d.softMax = glm::vec3(50.0f);
        p.focusPoint = &params.add(std::move(d));
    }
    p.focusSpeed = &params.add(f("camera/focus/speed", focus.speed, 0.0f, 500.0f, 0.0f, 30.0f));
    return p;
}

void applyCameraParameters(const CameraParameters& p, LensSettings& lens, ExposureSettings& exposure,
                           FocusSettings& focus) {
    if (p.focalLength == nullptr) {
        return;
    }
    lens.focalLength = p.focalLength->value();
    lens.sensorWidth = p.sensorWidth->value();
    lens.sensorHeight = p.sensorHeight->value();
    lens.aperture = p.aperture->value();
    lens.focusDistance = p.focusDistance->value();
    lens.shutterAngle = p.shutterAngle->value();
    lens.useExplicitFov = p.useExplicitFov->value();

    exposure.mode = static_cast<ExposureSettings::Mode>(std::clamp(p.exposureMode->value(), 0, 1));
    exposure.aperture = p.exposureAperture->value();
    exposure.shutterSeconds = p.shutterSeconds->value();
    exposure.iso = p.iso->value();
    exposure.compensation = p.compensation->value();
    exposure.minEv = p.minEv->value();
    exposure.maxEv = p.maxEv->value();
    exposure.speedUp = p.speedUp->value();
    exposure.speedDown = p.speedDown->value();
    exposure.meterCenterWeight = p.meterCenterWeight->value();

    focus.mode = static_cast<FocusSettings::Mode>(std::clamp(p.focusMode->value(), 0, 2));
    focus.point = p.focusPoint->value();
    focus.speed = p.focusSpeed->value();
}

} // namespace avgen::scene
