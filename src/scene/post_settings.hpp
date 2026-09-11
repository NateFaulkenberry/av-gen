#pragma once

// Built-in post-processing settings (milestone 0.6, ADR-016; image formation ADR-039). Plain data
// owned by the Engine and copied into Scene::post each frame; every field is a
// "post/<effect>/<field>" parameter, except the blocks the camera fills in (exposure and the lens
// that drives depth of field), which come from "camera/*" (ADR-037).
//
// The chain's order is fixed (ADR-039) and documented in docs/image-formation.md:
//   scene HDR -> volumetrics -> exposure -> defocus (depth of field and the ADR-079 tilt-shift
//   band, which share one gather) -> motion blur -> lens distortion and
//   chromatic aberration -> bloom, halation, anamorphic -> colour grade -> sharpen -> tone map,
//   vignette, grain.

#include "params/parameter_set.hpp"
#include "scene/scene_types.hpp"

#include <nlohmann/json_fwd.hpp>

#include <glm/glm.hpp>

#include <cstdint>

namespace avgen::scene {

enum class TonemapOperator : std::uint8_t { AcesFitted, AgX, Reinhard, PbrNeutral, Clamp };

struct PostSettings {
    // ---- exposure (ADR-037), applied first so every threshold below is in exposed units -------
    ExposureSettings exposure;
    float exposureDeltaSeconds = 1.0f / 60.0f; // the frame's delta time, for the metering rates
    bool exposureReset = false;                // re-seed the meter (scene change, timeline seek)

    // ---- bloom (after the lens, before grading) ----------------------------------------------
    bool bloomEnabled = true;
    float bloomIntensity = 0.2f;   // subtler by default than pre-ADR-039 (was 0.35)
    float bloomThreshold = 1.0f;   // exposed luminance where bloom starts
    float bloomKnee = 0.6f;        // soft-knee width as a fraction of the threshold
    float bloomRadius = 1.0f;      // upsample spread (0.5..2)
    std::uint32_t bloomLevels = 6; // mip levels (fixed at creation of the chain)
    // Selective bloom (ADR-039): 0 = luminance only, 1 = the emission target only. Ignored, with
    // no visible change, when the renderer supplies no emission target (ADR-035).
    float bloomEmissionWeight = 0.0f;

    // ---- halation: a wide, red-weighted bloom tier, off by default ---------------------------
    bool halationEnabled = false;
    float halationIntensity = 0.5f;
    float halationThreshold = 2.0f;  // exposed luminance; higher than bloom's on purpose
    float halationRadius = 2.0f;     // multiplies the upsample spread of the halation pyramid
    float halationWarmth = 0.6f;     // 0 = any highlight, 1 = only highlights already warm
    glm::vec3 halationTint{1.0f, 0.30f, 0.12f};

    // ---- anamorphic: a horizontally stretched bloom tier with optional ghosts, off by default -
    bool anamorphicEnabled = false;
    float anamorphicIntensity = 0.35f;
    float anamorphicStretch = 8.0f;   // horizontal scale of the streak
    float anamorphicGhosts = 0.0f;    // 0 = none; ghost strength mirrored about the centre
    glm::vec3 anamorphicTint{0.35f, 0.55f, 1.0f};

    // ---- colour grading (scene-linear, after bloom) -------------------------------------------
    float contrast = 1.0f;
    float saturation = 1.0f;
    float temperature = 0.0f;      // -1 cool .. +1 warm
    float tint = 0.0f;             // -1 green .. +1 magenta
    float hueShift = 0.0f;         // radians
    glm::vec3 lift{0.0f};          // shadows offset
    glm::vec3 gamma{1.0f};         // midtones power
    glm::vec3 gain{1.0f};          // highlights multiplier

    // ---- lens (before bloom, after the depth-aware effects) ------------------------------------
    float chromaticAberration = 0.0f; // 0..1 (pixels scaled by resolution)
    float distortion = 0.0f;          // -1 pinch .. +1 barrel

    // ---- depth of field (needs depth) ----------------------------------------------------------
    bool dofEnabled = false;
    float focusDistance = 6.0f;    // metres (the tracked focus when the camera drives it)
    float focusRange = 2.0f;       // sharp zone half-width (only the non-physical fallback)
    float dofMaxRadius = 8.0f;     // pixels at the output resolution (scaled by height/720)
    // ADR-037: when true the blur radius is the lens's circle of confusion in pixels rather than
    // `dofMaxRadius`, which then only clamps it. `lens` is the camera's lens for that frame.
    bool dofPhysical = false;
    LensSettings lens;

    // ---- tilt-shift (ADR-079): the same defocus, driven by a screen band instead of a distance --
    // A tilt-shift lens swings its focal plane away from parallel with the sensor, so what is sharp
    // is a *band* across the frame at an arbitrary angle rather than a shell at one distance. That
    // is a different circle-of-confusion function, not a different filter, so it rides the depth of
    // field pass: when both are on the pixel takes the larger of the two circles, because the wider
    // blur is the one you can see. Off by default and costs nothing until `enabled`.
    bool tiltShiftEnabled = false;
    glm::vec2 tiltShiftCentre{0.5f, 0.5f}; // normalised screen position, (0,0) = top left
    float tiltShiftRotation = 0.0f;        // degrees; 0 = a horizontal band, positive = clockwise
    // Both distances are in fractions of the frame *height*, so the band keeps its shape and angle
    // when the aspect ratio changes rather than shearing with it.
    float tiltShiftBandWidth = 0.2f;  // full width of the fully sharp band
    float tiltShiftFalloff = 0.25f;   // distance past the band's edge to reach maximum defocus
    float tiltShiftMaxRadius = 8.0f;  // pixels at 720p (scaled by height / 720), as dofMaxRadius

    // ---- motion blur (ADR-040: tile-based reconstruction over the ADR-035 velocity target) -----
    // The blur length is the pixel's screen motion times `motionBlurAmount` times the shutter
    // fraction `lens.shutterAngle / 360` (ADR-037), so 1.0 with a 180 degree shutter is the
    // physically correct half-frame smear and a zero shutter angle is no blur at all. Camera,
    // object, instance, deformation and particle motion all blur, because they all write velocity.
    float motionBlurAmount = 0.0f;      // 0 off .. 1 = the physical length
    std::uint32_t motionBlurSamples = 16; // taps along the smear; fewer bands a long streak
    float motionBlurMaxRadius = 40.0f;  // pixels at 720p (scaled by height / 720)
    std::uint32_t motionBlurTileSize = 20; // velocity tile edge in pixels; also the reach in tiles

    // ---- output effects ------------------------------------------------------------------------
    // ADR-059: FXAA. This renderer has no MSAA and no TAA, so a scene of alpha-tested foliage
    // crawls at the edges under any camera movement; measured on Glowmere with the wind off, better
    // than a third of the frame-to-frame pixel churn is aliasing rather than geometry. 0 is off and
    // costs nothing; 0.75 is a good default for foliage, and above that the filter starts to soften
    // real detail as well as the edges.
    float antialias = 0.0f;        // 0..1 sub-pixel blend strength; 0 skips the pass entirely
    float sharpen = 0.0f;          // 0..1 contrast-adaptive sharpening, last in the post chain
    std::uint32_t sharpenId = 0;   // 0 = whole image; else only pixels with this identifier
                                   // (ADR-035 identifier target; ignored when absent)

    // ---- tone mapping and output (LDR) ----------------------------------------------------------
    TonemapOperator tonemap = TonemapOperator::AgX; // ADR-039: AgX is the default operator
    float vignette = 0.0f;         // 0..1
    float grain = 0.0f;            // 0..1
    float chromaRetention = 0.0f;  // 0..1 how much hue to hold in compressed highlights, so a
                                   // bright narrow-band light stays coloured instead of going
                                   // white. 0 leaves the operator's own rolloff untouched.
};

struct PostParameters {
    params::Parameter<bool>* bloomEnabled = nullptr;
    params::Parameter<float>* bloomIntensity = nullptr;
    params::Parameter<float>* bloomThreshold = nullptr;
    params::Parameter<float>* bloomKnee = nullptr;
    params::Parameter<float>* bloomRadius = nullptr;
    params::Parameter<float>* bloomEmissionWeight = nullptr;
    params::Parameter<bool>* halationEnabled = nullptr;
    params::Parameter<float>* halationIntensity = nullptr;
    params::Parameter<float>* halationThreshold = nullptr;
    params::Parameter<float>* halationRadius = nullptr;
    params::Parameter<float>* halationWarmth = nullptr;
    params::Parameter<glm::vec3>* halationTint = nullptr;
    params::Parameter<bool>* anamorphicEnabled = nullptr;
    params::Parameter<float>* anamorphicIntensity = nullptr;
    params::Parameter<float>* anamorphicStretch = nullptr;
    params::Parameter<float>* anamorphicGhosts = nullptr;
    params::Parameter<glm::vec3>* anamorphicTint = nullptr;
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
    params::Parameter<bool>* dofPhysical = nullptr;
    params::Parameter<bool>* tiltShiftEnabled = nullptr;
    params::Parameter<glm::vec2>* tiltShiftCentre = nullptr;
    params::Parameter<float>* tiltShiftRotation = nullptr;
    params::Parameter<float>* tiltShiftBandWidth = nullptr;
    params::Parameter<float>* tiltShiftFalloff = nullptr;
    params::Parameter<float>* tiltShiftMaxRadius = nullptr;
    params::Parameter<float>* motionBlurAmount = nullptr;
    params::Parameter<float>* antialias = nullptr;
    params::Parameter<float>* sharpen = nullptr;
    params::Parameter<int>* sharpenId = nullptr;
    params::Parameter<int>* tonemap = nullptr;
    params::Parameter<float>* vignette = nullptr;
    params::Parameter<float>* grain = nullptr;
    params::Parameter<float>* chromaRetention = nullptr;
};

PostParameters registerPostParameters(params::ParameterSet& params, const PostSettings& defaults);
// ADR-059: apply a composition's own `post` block as parameter base values. The project's
// `parameters` block is applied after the scene loads and therefore still wins.
Result<void> applyPostJson(const nlohmann::json& j, const PostParameters& p);
void applyPostParameters(const PostParameters& p, PostSettings& settings);
const char* tonemapOperatorName(TonemapOperator op);

// How defocused the tilt-shift band leaves a point, 0 (fully sharp) to 1 (the maximum radius).
// `uv` is a normalised screen position with (0,0) at the top left, matching the post chain's own
// convention; `aspect` is width / height, and distances are measured in fractions of the frame
// height so that a rotation is the same angle on screen whatever shape the frame is.
//
// This is the twin of `tiltShiftCoverage` in shaders/post.wgsl and the two must agree: the shader
// is what renders, this is what the unit tests can actually pin down. Keeping it here rather than
// inside the renderer also lets a future automated focus pull ask "is this point sharp?" without a
// device.
[[nodiscard]] float tiltShiftCoverage(const PostSettings& settings, glm::vec2 uv, float aspect);

} // namespace avgen::scene
