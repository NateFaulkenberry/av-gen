#pragma once

// Built-in post-processing settings (milestone 0.6, ADR-016). Plain data owned by the Engine and
// copied into Scene::post each frame; every field is a "post/<effect>/<field>" parameter.

#include "params/parameter_set.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace avgen::scene {

enum class TonemapOperator : std::uint8_t { AcesFitted, AgX, Reinhard, PbrNeutral, Clamp };

struct PostSettings {
    // Bloom (before grading)
    bool bloomEnabled = true;
    float bloomIntensity = 0.35f;
    float bloomThreshold = 1.0f;   // scene-linear luminance where bloom starts
    float bloomKnee = 0.5f;        // soft threshold width
    float bloomRadius = 1.0f;      // upsample spread (0.5..2)
    std::uint32_t bloomLevels = 6; // mip levels (fixed at creation of the chain)

    // Colour grading (scene-linear, after bloom)
    float contrast = 1.0f;
    float saturation = 1.0f;
    float temperature = 0.0f;      // -1 cool .. +1 warm
    float tint = 0.0f;             // -1 green .. +1 magenta
    float hueShift = 0.0f;         // radians
    glm::vec3 lift{0.0f};          // shadows offset
    glm::vec3 gamma{1.0f};         // midtones power
    glm::vec3 gain{1.0f};          // highlights multiplier

    // Lens (before tone mapping)
    float chromaticAberration = 0.0f; // 0..1 (pixels scaled by resolution)
    float distortion = 0.0f;          // -1 pinch .. +1 barrel

    // Depth of field (needs depth)
    bool dofEnabled = false;
    float focusDistance = 6.0f;    // metres
    float focusRange = 2.0f;       // sharp zone half-width
    float dofMaxRadius = 8.0f;     // pixels at the output resolution (scaled by height/720)

    // Motion blur (camera, from depth reprojection)
    float motionBlurAmount = 0.0f; // 0 off .. 1 = full frame velocity
    std::uint32_t motionBlurSamples = 8;

    // Tone mapping and output (LDR)
    TonemapOperator tonemap = TonemapOperator::AcesFitted;
    float vignette = 0.0f;         // 0..1
    float grain = 0.0f;            // 0..1
};

struct PostParameters {
    params::Parameter<bool>* bloomEnabled = nullptr;
    params::Parameter<float>* bloomIntensity = nullptr;
    params::Parameter<float>* bloomThreshold = nullptr;
    params::Parameter<float>* bloomKnee = nullptr;
    params::Parameter<float>* bloomRadius = nullptr;
    params::Parameter<float>* contrast = nullptr;
    params::Parameter<float>* saturation = nullptr;
    params::Parameter<float>* temperature = nullptr;
    params::Parameter<float>* tint = nullptr;
    params::Parameter<float>* hueShift = nullptr;
    params::Parameter<glm::vec3>* lift = nullptr;
    params::Parameter<glm::vec3>* gamma = nullptr;
    params::Parameter<glm::vec3>* gain = nullptr;
    params::Parameter<float>* chromaticAberration = nullptr;
    params::Parameter<float>* distortion = nullptr;
    params::Parameter<bool>* dofEnabled = nullptr;
    params::Parameter<float>* focusDistance = nullptr;
    params::Parameter<float>* focusRange = nullptr;
    params::Parameter<float>* dofMaxRadius = nullptr;
    params::Parameter<float>* motionBlurAmount = nullptr;
    params::Parameter<int>* tonemap = nullptr;
    params::Parameter<float>* vignette = nullptr;
    params::Parameter<float>* grain = nullptr;
};

PostParameters registerPostParameters(params::ParameterSet& params, const PostSettings& defaults);
void applyPostParameters(const PostParameters& p, PostSettings& settings);
const char* tonemapOperatorName(TonemapOperator op);

} // namespace avgen::scene
