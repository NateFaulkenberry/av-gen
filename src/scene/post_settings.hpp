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

#include <array>
#include <cstdint>
#include <string>

namespace avgen::scene {

enum class TonemapOperator : std::uint8_t { AcesFitted, AgX, Reinhard, PbrNeutral, Clamp };

// ---- cinematic integration (Image/Look §52.1, §68) -----------------------------------------------
//
// The four controls that make the elements of a frame look like they were photographed together
// rather than composited. They are the only genuinely new state in the Image/Look work; everything
// else §52 asks for already exists above and below this struct under names that are kept.
//
// **Every field defaults to the value that means "do nothing", and a field at that value costs
// nothing and changes nothing.** That is §60/§87 and it is the milestone, so it is enforced
// structurally rather than by care: the two passes that carry these controls are *not encoded at
// all* unless a control is off its default, so with integration at zero the command stream, the
// shaders that run and therefore the float buffer handed to the tone map are bit-identical to a
// build without this struct. Nothing here is folded into `fs_composite` -- which always runs -- for
// exactly that reason. See docs/image-look-audit.md §1.4 and §5.
//
// The order is §68's: atmospheric, colour, local contrast, light wrap.
struct ImageLookIntegration {
    // §68.1 Atmospheric. Depth-driven aerial perspective *in the image*, as a fraction: 1 pushes a
    // pixel at `atmosphericDistance` fully toward the scene's own horizon colour.
    //
    // ADR-347 already gives the scene real fog, taking its colour from the sky's horizon, at a
    // density it also corrected (it was 15x too high). This control is deliberately NOT a second
    // fog and must not be sold as one.
    //
    // **Prefer the scene's fog (`environment.volumeDensity`, ADR-705; this was measured against the
    // exp-squared `fogDensity` it replaced).** That comparison has now been measured rather than
    // assumed (docs/image-look-audit.md §8): on a scene that already has ADR-347's fog, taking this
    // control to 0.6 moves 1.6% of pixels by at most three code values, where the fog alone moves
    // 32% by up to 109. Scene fog is the stronger and the more physical of the two -- per surface,
    // at shading time, in the sky's own colour.
    //
    // What is left for this control is narrow and real: it is applied to the *composed* frame, so
    // it reaches everything composited after shading -- volumetrics, user post layers, the bloom
    // and wide tiers -- which scene fog structurally cannot, and it needs no volumetric march. That
    // is the case to use it for. It is not the way to get aerial perspective.
    float atmospheric = 0.0f;         // 0..1
    float atmosphericDistance = 200.0f; // metres at which `atmospheric` is reached in full
    glm::vec3 atmosphericTint{0.55f, 0.68f, 0.85f}; // the colour distance tends toward

    // §68.2 Colour. Pulls the frame's chroma toward a common axis, which is what a single film
    // stock does to a scene lit by mixed sources. 0 is off.
    float colour = 0.0f; // 0..1

    // §68.3 Local contrast. A large-radius unsharp mask about the local mean -- "clarity", not
    // sharpening, which is `post/output/sharpen` and is a one-pixel neighbourhood.
    //
    // ADR-352: the glTF BRDF gains up to 68% of its energy at grazing angles and compounds per
    // bounce, so scene-referred values here may be hot. The gain is therefore applied about the
    // local mean in *log* space and the result is clamped to a bounded multiple of the input, so a
    // hot pixel cannot be amplified without limit. Verified against the `--pt-probe` diagnostic's
    // worst measured directional albedo rather than against an assumed ceiling.
    float localContrast = 0.0f;        // 0..1
    float localContrastRadius = 24.0f; // pixels at `PostSettings::referenceHeight`, scaled like
                                       // every other radius in that struct (ADR-917)

    // §68.4 Light wrap. A bright background bleeding around a foreground edge -- the single
    // strongest cue that a subject is *in* a scene rather than in front of it. Reuses the bloom
    // pyramid rather than building a third one, so it costs one texture fetch.
    float lightWrap = 0.0f; // 0..1

    // True when anything here is off its default, i.e. when the chain must encode the look passes.
    // The single place that decision is made; the renderer and the tests both ask this and so
    // cannot disagree about what "at zero" means.
    [[nodiscard]] bool active() const {
        return atmospheric > 0.0f || colour > 0.0f || localContrast > 0.0f || lightWrap > 0.0f;
    }
    // Split the same way the chain is: one pass needs depth and runs early, one runs late.
    [[nodiscard]] bool atmosphericActive() const { return atmospheric > 0.0f; }
    [[nodiscard]] bool lookActive() const { return colour > 0.0f || localContrast > 0.0f || lightWrap > 0.0f; }
};

struct PostSettings {
    // ---- exposure (ADR-037), applied first so every threshold below is in exposed units -------
    ExposureSettings exposure;
    float exposureDeltaSeconds = 1.0f / 60.0f; // the frame's delta time, for the metering rates
    bool exposureReset = false;                // re-seed the meter (scene change, timeline seek)

    // ---- resolution (ADR-917) --------------------------------------------------------------------
    // The frame height, in the post chain's own pixels, that every pixel-sized value below was
    // tuned at. The chain runs at the scene target, so a supersampled frame counts its supersampled
    // height: a 960x540 preview at supersample 2 is a 1080-line chain.
    //
    // At any other height every pixel-sized value scales by `height / referenceHeight` -- the
    // bloom and halation pyramids gain or lose levels at their fine end, the anamorphic streak's
    // reach, the motion-blur tiles and radius, the defocus radii and the look stage's local-contrast
    // radius -- so each covers the same fraction of the picture at 540 lines as at 4320. Before
    // ADR-917 the pyramids, the streak and the tiles were counted in pixels of whatever frame the
    // chain was handed (ADR-279 measured the bloom), so a preview and a final of one project showed
    // two different looks.
    //
    // 720 is the height the older radii (defocus, motion-blur radius, local contrast) were already
    // expressed at, so a scene that never sets this keeps their meaning exactly.
    float referenceHeight = 720.0f;

    // ---- bloom (after the lens, before grading) ----------------------------------------------
    bool bloomEnabled = true;
    float bloomIntensity = 0.2f;   // subtler by default than pre-ADR-039 (was 0.35)
    float bloomThreshold = 1.0f;   // exposed luminance where bloom starts
    float bloomKnee = 0.6f;        // soft-knee width as a fraction of the threshold
    float bloomRadius = 1.0f;      // upsample spread (0.5..2)
    // Pyramid depth AT `referenceHeight`. NOT fixed at creation, whatever this comment used to say:
    // `PostProcessor::run` reads it every frame (ADR-385), and since ADR-917 builds this many levels
    // plus log2(height / referenceHeight) more at the fine end, so the coarsest level -- which is
    // what sets the glow's reach -- is the same fraction of the frame at any size. Fewer makes a
    // tighter, harder glow. The halation pyramid follows the same count.
    std::uint32_t bloomLevels = 6;
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
    float anamorphicStretch = 8.0f;   // horizontal scale of the streak: its reach is 8 * stretch
                                      // quarter-resolution texels at `referenceHeight` (ADR-917)
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
    float dofMaxRadius = 8.0f;     // pixels at `referenceHeight` (scaled by height / referenceHeight)
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
    float tiltShiftMaxRadius = 8.0f;  // pixels at `referenceHeight`, as dofMaxRadius

    // ---- motion blur (ADR-040: tile-based reconstruction over the ADR-035 velocity target) -----
    // The blur length is the pixel's screen motion times `motionBlurAmount` times the shutter
    // fraction `lens.shutterAngle / 360` (ADR-037), so 1.0 with a 180 degree shutter is the
    // physically correct half-frame smear and a zero shutter angle is no blur at all. Camera,
    // object, instance, deformation and particle motion all blur, because they all write velocity.
    float motionBlurAmount = 0.0f;      // 0 off .. 1 = the physical length
    std::uint32_t motionBlurSamples = 16; // taps along the smear; fewer bands a long streak
    float motionBlurMaxRadius = 40.0f;  // pixels at `referenceHeight` (scaled by height / referenceHeight)
    // Velocity tile edge in pixels at `referenceHeight`, scaled like the radius since ADR-917. The
    // reconstruction only gathers from the 3x3 tiles around a pixel, so a smear longer than a tile
    // is cut off at the neighbourhood's edge: scaling the radius without the tile (the chain before
    // ADR-917) shortened every long smear at high resolution.
    std::uint32_t motionBlurTileSize = 20;

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

    // ---- cinematic integration (Image/Look §52.1, §68) --------------------------------------------
    // A member of this object rather than a rival to it (§52). Zero throughout by default; see the
    // struct's own comment for why that zero is enforced structurally.
    ImageLookIntegration look;

    // ---- ADR-1050: the spectrum sweep ------------------------------------------------------------
    // A band of saturated hues that travels across the frame as `sweepProgress` goes 0 -> 1: a
    // high-pass filter sweep translated into colour and light (All You Got, bridge 3 bar 8). Off while
    // both `sweepIntensity` and `sweepWash` are 0 (the defaults), so no existing picture changes.
    float sweepProgress = 0.0f;  // 0 = the band waits off the leading edge, 1 = it has left the far edge
    float sweepWidth = 0.35f;    // half-width of the band, as a fraction of the frame along the sweep
    float sweepIntensity = 0.0f; // additive light of the band (HDR, so it blooms)
    float sweepWash = 0.0f;      // 0..1: how far the band tints what is under it
    float sweepAngle = 0.0f;     // degrees; 0 travels left to right, 90 top to bottom
    float sweepSpan = 1.0f;      // hue cycles across the band (1 = one rainbow)
    float sweepHue = 0.0f;       // hue offset in turns
    float sweepTrail = 0.0f;     // 0..1: wash left behind the band once it has passed

    // ---- ADR-1055: the world wave ------------------------------------------------------------------
    // A band of coloured light that travels THROUGH the world (unlike the sweep, which crosses the
    // frame): a plane front at `waveProgress` metres from `waveOrigin` along `waveDirection`, `waveWidth`
    // metres half-wide. Every ray-marched SDF surface it crosses gains `waveIntensity` of light in the
    // band's colour, its edge lines and emission are recoloured toward the band (`waveEdgeTint`), and
    // behind the front they are pulled toward `waveTrailColor` by `waveTrail` -- the new palette left
    // behind. Shared by every SDF object, so one wave sweeps the whole house. Off (byte-identical)
    // while intensity, edge tint and trail are all 0.
    glm::vec3 waveOrigin{0.0f};
    glm::vec3 waveDirection{0.0f, 0.0f, -1.0f};
    float waveProgress = 0.0f;   // metres the front has travelled from the origin
    float waveWidth = 0.6f;      // half-width of the band, metres
    float waveIntensity = 0.0f;  // light added in the band (HDR; it blooms)
    glm::vec3 waveColor{1.0f, 0.35f, 0.85f}; // the band's colour when waveHueSpan is 0
    float waveHue = 0.0f;        // hue offset (turns) of the rainbow when waveHueSpan > 0
    float waveHueSpan = 0.0f;    // hue cycles across the band (0 = one colour, waveColor)
    float waveEdgeTint = 0.0f;   // 0..1: how far edges and emission take the band's colour inside it
    float waveTrail = 0.0f;      // 0..1: how far edges and emission behind the front take waveTrailColor
    glm::vec3 waveTrailColor{1.0f};
};

struct PostParameters {
    params::Parameter<float>* referenceHeight = nullptr; // ADR-917
    params::Parameter<bool>* bloomEnabled = nullptr;
    params::Parameter<float>* bloomIntensity = nullptr;
    params::Parameter<float>* bloomThreshold = nullptr;
    params::Parameter<float>* bloomKnee = nullptr;
    params::Parameter<float>* bloomRadius = nullptr;
    params::Parameter<int>* bloomLevels = nullptr; // ADR-385: live per frame, never registered
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
    // ADR-372: read by the shader every frame and registered nowhere, so no scene and no project
    // could reach them. Same family as the unwired identifier target, one layer along.
    params::Parameter<int>* motionBlurSamples = nullptr;
    params::Parameter<float>* motionBlurMaxRadius = nullptr;
    params::Parameter<int>* motionBlurTileSize = nullptr;
    params::Parameter<float>* antialias = nullptr;
    params::Parameter<float>* sharpen = nullptr;
    params::Parameter<int>* sharpenId = nullptr;
    params::Parameter<int>* tonemap = nullptr;
    params::Parameter<float>* vignette = nullptr;
    params::Parameter<float>* grain = nullptr;
    params::Parameter<float>* chromaRetention = nullptr;
    // ---- cinematic integration (§52.1, §54's namespaced IDs: post/look/*) -------------------------
    params::Parameter<float>* lookAtmospheric = nullptr;
    params::Parameter<float>* lookAtmosphericDistance = nullptr;
    params::Parameter<glm::vec3>* lookAtmosphericTint = nullptr;
    params::Parameter<float>* lookColour = nullptr;
    params::Parameter<float>* lookLocalContrast = nullptr;
    params::Parameter<float>* lookLocalContrastRadius = nullptr;
    params::Parameter<float>* lookLightWrap = nullptr;
    // ADR-1050: the spectrum sweep (post/sweep/*).
    params::Parameter<float>* sweepProgress = nullptr;
    params::Parameter<float>* sweepWidth = nullptr;
    params::Parameter<float>* sweepIntensity = nullptr;
    params::Parameter<float>* sweepWash = nullptr;
    params::Parameter<float>* sweepAngle = nullptr;
    params::Parameter<float>* sweepSpan = nullptr;
    params::Parameter<float>* sweepHue = nullptr;
    params::Parameter<float>* sweepTrail = nullptr;
    // ADR-1055: the world wave (post/wave/*).
    params::Parameter<glm::vec3>* waveOrigin = nullptr;
    params::Parameter<glm::vec3>* waveDirection = nullptr;
    params::Parameter<float>* waveProgress = nullptr;
    params::Parameter<float>* waveWidth = nullptr;
    params::Parameter<float>* waveIntensity = nullptr;
    params::Parameter<glm::vec3>* waveColor = nullptr;
    params::Parameter<float>* waveHue = nullptr;
    params::Parameter<float>* waveHueSpan = nullptr;
    params::Parameter<float>* waveEdgeTint = nullptr;
    params::Parameter<float>* waveTrail = nullptr;
    params::Parameter<glm::vec3>* waveTrailColor = nullptr;
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

// ---- resolution (ADR-917) -------------------------------------------------------------------------
//
// How far the post chain's pixel-sized values are scaled for a chain `frameHeight` pixels tall:
// `frameHeight / referenceHeight`. The chain's own pixels, so a supersampled frame counts its
// supersampled height. 1 at the reference, where every value means exactly what it says.
[[nodiscard]] float postPixelScale(const PostSettings& settings, std::uint32_t frameHeight);

// A one-line account of what that scale does to this scene's values, for the log of a render --
// "every glow, streak and blur is 4.00x its authored pixel size" is the sentence that tells a
// person why a final and a preview agree, or why they would not have.
[[nodiscard]] std::string describePostScale(const PostSettings& settings, std::uint32_t frameHeight);

// The energy-conserving pyramid (ADR-039), planned for a frame `octaves` = log2(pixel scale) away
// from the reference. Shared by the bloom and the halation pyramids, and here rather than in the
// renderer so the arithmetic is testable without a device.
//
// At the reference the authored pyramid gives level k the weight (1 - b) b^k, and the coarsest
// level what is left, b^(L-1) -- `b` being the upsample blend. A frame that is `octaves` finer has
// levels that are each that many octaves smaller, so the authored weights move that many levels
// towards the coarse end: level k's weight lands on level k + octaves, and a fractional shift
// shares it between the two levels that bracket its size, which keeps both the total (1) and the
// weighted mean octave exact. The levels finer than the reference's first carry no weight; they
// exist only as steps of the downsample, and their upsample passes are a plain tent. A frame
// coarser than the reference folds the weight of the levels it is too small to have into its first.
//
// At `octaves` 0 this is exactly the pre-ADR-917 pyramid: the same level count and, to the bit,
// the same blend on every upsample step.
inline constexpr std::uint32_t kMaxAuthoredPyramidLevels = 8;
inline constexpr std::uint32_t kMaxPyramidLevels = 12;
struct PyramidPlan {
    std::uint32_t levels = 0;                        // levels to build, finest first
    std::array<float, kMaxPyramidLevels> weight{};   // each level's share of the result; sums to 1
    // The upsample blend for each step: level j is `mix(level j, tent(coarser result), blend[j])`,
    // as fs_upsample computes it. The coarsest level has no step; its entry is 0.
    std::array<float, kMaxPyramidLevels> blend{};
};
// `baseWidth`/`baseHeight` are the pyramid's first level (half the frame for bloom, a quarter for
// halation): the plan stops, as the chain does, before a level would be narrower than two texels,
// and folds whatever weight lay beyond into the last level it has.
[[nodiscard]] PyramidPlan planPyramid(std::uint32_t authoredLevels, float blend, float octaves,
                                      std::uint32_t baseWidth, std::uint32_t baseHeight);

} // namespace avgen::scene
