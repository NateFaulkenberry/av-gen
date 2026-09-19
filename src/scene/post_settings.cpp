#include "scene/post_settings.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace avgen::scene {

namespace {
params::ParamDesc<float> f(const char* path, float def, float lo, float hi, float slo, float shi) {
    params::ParamDesc<float> d;
    d.path = path;
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = slo;
    d.softMax = shi;
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
params::ParamDesc<glm::vec2> v2(const char* path, glm::vec2 def, float lo, float hi, float slo, float shi) {
    params::ParamDesc<glm::vec2> d;
    d.path = path;
    d.defaultValue = def;
    d.hardMin = glm::vec2(lo);
    d.hardMax = glm::vec2(hi);
    d.softMin = glm::vec2(slo);
    d.softMax = glm::vec2(shi);
    return d;
}
params::ParamDesc<glm::vec3> v3(const char* path, glm::vec3 def, float lo, float hi, bool isColor = false) {
    params::ParamDesc<glm::vec3> d;
    d.path = path;
    d.defaultValue = def;
    d.hardMin = glm::vec3(lo);
    d.hardMax = glm::vec3(hi);
    d.isColor = isColor;
    return d;
}
} // namespace

const char* tonemapOperatorName(TonemapOperator op) {
    switch (op) {
    case TonemapOperator::AgX: return "agx";
    case TonemapOperator::Reinhard: return "reinhard";
    case TonemapOperator::PbrNeutral: return "pbr-neutral";
    case TonemapOperator::Clamp: return "clamp";
    default: return "aces";
    }
}

PostParameters registerPostParameters(params::ParameterSet& params, const PostSettings& s) {
    PostParameters p;
    p.bloomEnabled = &params.add(b("post/bloom/enabled", s.bloomEnabled));
    p.bloomIntensity = &params.add(f("post/bloom/intensity", s.bloomIntensity, 0.0f, 10.0f, 0.0f, 1.0f));
    p.bloomThreshold = &params.add(f("post/bloom/threshold", s.bloomThreshold, 0.0f, 20.0f, 0.0f, 4.0f));
    p.bloomKnee = &params.add(f("post/bloom/knee", s.bloomKnee, 0.0f, 1.0f, 0.0f, 1.0f));
    p.bloomRadius = &params.add(f("post/bloom/radius", s.bloomRadius, 0.25f, 3.0f, 0.5f, 2.0f));
    p.bloomEmissionWeight =
        &params.add(f("post/bloom/emissionWeight", s.bloomEmissionWeight, 0.0f, 1.0f, 0.0f, 1.0f));
    p.halationEnabled = &params.add(b("post/halation/enabled", s.halationEnabled));
    p.halationIntensity = &params.add(f("post/halation/intensity", s.halationIntensity, 0.0f, 4.0f, 0.0f, 1.0f));
    p.halationThreshold = &params.add(f("post/halation/threshold", s.halationThreshold, 0.0f, 40.0f, 0.5f, 8.0f));
    p.halationRadius = &params.add(f("post/halation/radius", s.halationRadius, 0.5f, 6.0f, 1.0f, 4.0f));
    p.halationWarmth = &params.add(f("post/halation/warmth", s.halationWarmth, 0.0f, 1.0f, 0.0f, 1.0f));
    p.halationTint = &params.add(v3("post/halation/tint", s.halationTint, 0.0f, 4.0f, true));
    p.anamorphicEnabled = &params.add(b("post/anamorphic/enabled", s.anamorphicEnabled));
    p.anamorphicIntensity =
        &params.add(f("post/anamorphic/intensity", s.anamorphicIntensity, 0.0f, 4.0f, 0.0f, 1.0f));
    p.anamorphicStretch = &params.add(f("post/anamorphic/stretch", s.anamorphicStretch, 1.0f, 40.0f, 2.0f, 20.0f));
    p.anamorphicGhosts = &params.add(f("post/anamorphic/ghosts", s.anamorphicGhosts, 0.0f, 1.0f, 0.0f, 1.0f));
    p.anamorphicTint = &params.add(v3("post/anamorphic/tint", s.anamorphicTint, 0.0f, 4.0f, true));
    p.contrast = &params.add(f("post/grade/contrast", s.contrast, 0.0f, 4.0f, 0.5f, 2.0f));
    p.saturation = &params.add(f("post/grade/saturation", s.saturation, 0.0f, 4.0f, 0.0f, 2.0f));
    p.temperature = &params.add(f("post/grade/temperature", s.temperature, -1.0f, 1.0f, -1.0f, 1.0f));
    p.tint = &params.add(f("post/grade/tint", s.tint, -1.0f, 1.0f, -1.0f, 1.0f));
    p.hueShift = &params.add(f("post/grade/hueShift", s.hueShift, -3.1416f, 3.1416f, -3.1416f, 3.1416f));
    p.lift = &params.add(v3("post/grade/lift", s.lift, -1.0f, 1.0f));
    p.gamma = &params.add(v3("post/grade/gamma", s.gamma, 0.2f, 4.0f));
    p.gain = &params.add(v3("post/grade/gain", s.gain, 0.0f, 4.0f));
    p.chromaticAberration = &params.add(f("post/lens/chromaticAberration", s.chromaticAberration, 0.0f, 1.0f, 0.0f, 1.0f));
    p.distortion = &params.add(f("post/lens/distortion", s.distortion, -1.0f, 1.0f, -1.0f, 1.0f));
    p.dofEnabled = &params.add(b("post/dof/enabled", s.dofEnabled));
    p.focusDistance = &params.add(f("post/dof/focusDistance", s.focusDistance, 0.01f, 1000.0f, 0.5f, 30.0f));
    p.focusRange = &params.add(f("post/dof/focusRange", s.focusRange, 0.01f, 1000.0f, 0.1f, 20.0f));
    p.dofMaxRadius = &params.add(f("post/dof/maxRadius", s.dofMaxRadius, 0.0f, 32.0f, 0.0f, 16.0f));
    p.dofPhysical = &params.add(b("post/dof/physical", s.dofPhysical));
    p.tiltShiftEnabled = &params.add(b("post/tiltShift/enabled", s.tiltShiftEnabled));
    // The centre may sit outside the frame on purpose -- a band running off the top of the image is
    // a normal framing -- so the hard range is wider than the slider's.
    p.tiltShiftCentre = &params.add(v2("post/tiltShift/centre", s.tiltShiftCentre, -1.0f, 2.0f, 0.0f, 1.0f));
    p.tiltShiftRotation =
        &params.add(f("post/tiltShift/rotation", s.tiltShiftRotation, -180.0f, 180.0f, -90.0f, 90.0f));
    p.tiltShiftBandWidth =
        &params.add(f("post/tiltShift/bandWidth", s.tiltShiftBandWidth, 0.0f, 2.0f, 0.0f, 0.8f));
    // Not zero at the bottom: a zero falloff is a visible hard line across the image rather than a
    // lens, and the shader would have to guard the division anyway.
    p.tiltShiftFalloff = &params.add(f("post/tiltShift/falloff", s.tiltShiftFalloff, 0.001f, 2.0f, 0.01f, 1.0f));
    p.tiltShiftMaxRadius =
        &params.add(f("post/tiltShift/maxRadius", s.tiltShiftMaxRadius, 0.0f, 32.0f, 0.0f, 16.0f));
    p.motionBlurAmount = &params.add(f("post/motionBlur/amount", s.motionBlurAmount, 0.0f, 1.0f, 0.0f, 1.0f));
    // ADR-372. These three were read by `fs_motion_blur` on every frame that blurs and could not be
    // set from a scene or a project: `amount` was the only registered motion-blur control. Their
    // ranges are the clamps `PostProcessor::run` already applies, so a parameter cannot now express
    // a value the chain would silently alter -- a slider whose top half does nothing is the same
    // defect wearing a different hat.
    {
        params::ParamDesc<int> d;
        d.path = "post/motionBlur/samples";
        d.defaultValue = static_cast<int>(s.motionBlurSamples);
        d.hardMin = 2;   // PostProcessor clamps to [2, 32]
        d.hardMax = 32;
        d.softMin = 4;
        d.softMax = 32;
        d.label = "post/motionBlur/samples (taps along the smear; fewer bands a long streak)";
        p.motionBlurSamples = &params.add(std::move(d));
    }
    p.motionBlurMaxRadius =
        &params.add(f("post/motionBlur/maxRadius", s.motionBlurMaxRadius, 0.0f, 200.0f, 8.0f, 80.0f));
    {
        params::ParamDesc<int> d;
        d.path = "post/motionBlur/tileSize";
        d.defaultValue = static_cast<int>(s.motionBlurTileSize);
        d.hardMin = 4;   // PostProcessor clamps to [4, 40]
        d.hardMax = 40;
        d.softMin = 8;
        d.softMax = 40;
        d.label = "post/motionBlur/tileSize (velocity tile edge in pixels; also the reach in tiles)";
        p.motionBlurTileSize = &params.add(std::move(d));
    }
    p.antialias = &params.add(f("post/output/antialias", s.antialias, 0.0f, 1.0f, 0.0f, 1.0f));
    p.sharpen = &params.add(f("post/output/sharpen", s.sharpen, 0.0f, 1.0f, 0.0f, 1.0f));
    {
        params::ParamDesc<int> d;
        d.path = "post/output/sharpenId";
        d.defaultValue = static_cast<int>(s.sharpenId);
        d.hardMin = 0;
        d.hardMax = 65535;
        d.softMin = 0;
        d.softMax = 64;
        d.label = "post/output/sharpenId (0 = the whole image)";
        p.sharpenId = &params.add(std::move(d));
    }
    {
        params::ParamDesc<int> d;
        d.path = "post/tonemap/operator";
        d.defaultValue = static_cast<int>(s.tonemap);
        d.hardMin = 0;
        d.hardMax = 4;
        d.label = "post/tonemap/operator (0 aces, 1 agx, 2 reinhard, 3 pbr-neutral, 4 clamp)";
        p.tonemap = &params.add(std::move(d));
    }
    p.vignette = &params.add(f("post/output/vignette", s.vignette, 0.0f, 1.0f, 0.0f, 1.0f));
    p.grain = &params.add(f("post/output/grain", s.grain, 0.0f, 1.0f, 0.0f, 1.0f));
    p.chromaRetention =
        &params.add(f("post/tonemap/chroma-retention", s.chromaRetention, 0.0f, 1.0f, 0.0f, 1.0f));

    // ---- cinematic integration (§52.1, §68). §54: a new namespace, `post/look/*`, so no existing
    // parameter ID is renamed and nothing has to migrate. Every amount here is zero by default,
    // which is §60 -- and the two *shape* parameters below have non-zero defaults only because a
    // shape has no meaningful zero; they are inert while their amount is zero.
    p.lookAtmospheric = &params.add(f("post/look/atmospheric", s.look.atmospheric, 0.0f, 1.0f, 0.0f, 1.0f));
    // The hard maximum is generous because a scene's length unit is not metres by decree -- this
    // engine's scene-linear *intensity* unit is explicitly uncalibrated (docs/image-formation.md:87)
    // and its distances are whatever the author built in.
    p.lookAtmosphericDistance =
        &params.add(f("post/look/atmosphericDistance", s.look.atmosphericDistance, 1.0f, 100000.0f, 10.0f, 2000.0f));
    p.lookAtmosphericTint = &params.add(v3("post/look/atmosphericTint", s.look.atmosphericTint, 0.0f, 4.0f, true));
    p.lookColour = &params.add(f("post/look/colour", s.look.colour, 0.0f, 1.0f, 0.0f, 1.0f));
    p.lookLocalContrast = &params.add(f("post/look/localContrast", s.look.localContrast, 0.0f, 1.0f, 0.0f, 1.0f));
    // Not zero at the bottom: a zero radius makes the local mean equal the pixel, so the unsharp
    // subtracts a value from itself and the amount slider moves with no effect at all. That is
    // ADR-182's "a probe that cannot fail" applied to a control, and the hard minimum is what stops
    // the parameter set from being able to express it.
    p.lookLocalContrastRadius =
        &params.add(f("post/look/localContrastRadius", s.look.localContrastRadius, 1.0f, 128.0f, 4.0f, 64.0f));
    p.lookLightWrap = &params.add(f("post/look/lightWrap", s.look.lightWrap, 0.0f, 1.0f, 0.0f, 1.0f));
    return p;
}

// A composition's own `post` block, applied as parameter *base* values (ADR-059).
//
// Ten scene files in this repository carried one of these and none of them did anything: nothing
// read the block, so `chromaRetention` and `bloomEmissionWeight` -- the two controls that make a
// scene's own glow read -- sat at their defaults of zero in every scene that asked for them. The
// block is authored intent about a shot's look and belongs with the shot.
//
// Base values rather than a separate settings path, so the result behaves exactly as though an
// author had moved those sliders: the project's `parameters` block is applied after the scene
// loads and still wins, presets and automation still work, and a round trip writes back what was
// read. Unknown keys are reported, because a misspelt one is otherwise silent.
Result<void> applyPostJson(const nlohmann::json& j, const PostParameters& p) {
    if (j.is_null()) {
        return {};
    }
    if (!j.is_object()) {
        return fail("'post' must be an object");
    }
    const std::pair<const char*, params::Parameter<float>*> floats[] = {
        {"bloomIntensity", p.bloomIntensity}, {"bloomThreshold", p.bloomThreshold},
        {"bloomKnee", p.bloomKnee},           {"bloomRadius", p.bloomRadius},
        {"bloomEmissionWeight", p.bloomEmissionWeight},
        {"halationIntensity", p.halationIntensity}, {"halationThreshold", p.halationThreshold},
        {"halationRadius", p.halationRadius}, {"halationWarmth", p.halationWarmth},
        {"anamorphicIntensity", p.anamorphicIntensity},
        {"chromaticAberration", p.chromaticAberration},
        {"distortion", p.distortion},         {"antialias", p.antialias},
        {"sharpen", p.sharpen},               {"vignette", p.vignette},
        {"grain", p.grain},                   {"chromaRetention", p.chromaRetention},
        {"contrast", p.contrast},             {"saturation", p.saturation},
        {"tiltShiftRotation", p.tiltShiftRotation},
        {"tiltShiftBandWidth", p.tiltShiftBandWidth},
        {"tiltShiftFalloff", p.tiltShiftFalloff},
        {"tiltShiftMaxRadius", p.tiltShiftMaxRadius},
        // Cinematic integration (§52.1). Read here *and* written back, because ADR-350's failure was
        // a block with a reader and no writer, and this block's writer is the echo of what was read
        // -- so a key this table does not name is a key a scene cannot carry.
        {"motionBlurAmount", p.motionBlurAmount},
        {"motionBlurMaxRadius", p.motionBlurMaxRadius},
        {"lookAtmospheric", p.lookAtmospheric},
        {"lookAtmosphericDistance", p.lookAtmosphericDistance},
        {"lookColour", p.lookColour},
        {"lookLocalContrast", p.lookLocalContrast},
        {"lookLocalContrastRadius", p.lookLocalContrastRadius},
        {"lookLightWrap", p.lookLightWrap},
    };
    const std::pair<const char*, params::Parameter<bool>*> bools[] = {
        {"bloomEnabled", p.bloomEnabled}, {"halationEnabled", p.halationEnabled},
        {"tiltShiftEnabled", p.tiltShiftEnabled},
    };
    // This table used to not exist: `applyPostJson` handled no vec3 at all, so `lift`, `gamma`,
    // `gain` and both tints were registered parameters that a scene's own `post` block could not
    // set -- it got `post.lift: unknown key, ignored`. Adding the type closes that gap, and it is
    // safe to close now rather than never: no scene file in `examples/` names any of these five
    // keys (surveyed 2026-09-19), so nothing changes behaviour today and the next scene that wants
    // a tint gets one. `lookAtmosphericTint` is the new one the cinematic integration needs.
    const std::pair<const char*, params::Parameter<glm::vec3>*> vec3s[] = {
        {"lift", p.lift},
        {"gamma", p.gamma},
        {"gain", p.gain},
        {"halationTint", p.halationTint},
        {"anamorphicTint", p.anamorphicTint},
        {"lookAtmosphericTint", p.lookAtmosphericTint},
    };
    for (const auto& [key, value] : j.items()) {
        bool handled = false;
        for (const auto& [name, param] : floats) {
            if (key == name && param != nullptr) {
                if (!value.is_number()) {
                    return fail("post.{} must be a number", key);
                }
                param->setBase(value.get<float>());
                handled = true;
                break;
            }
        }
        if (handled) {
            continue;
        }
        for (const auto& [name, param] : bools) {
            if (key == name && param != nullptr) {
                if (!value.is_boolean()) {
                    return fail("post.{} must be a boolean", key);
                }
                param->setBase(value.get<bool>());
                handled = true;
                break;
            }
        }
        if (handled) {
            continue;
        }
        for (const auto& [name, param] : vec3s) {
            if (key == name && param != nullptr) {
                if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() ||
                    !value[2].is_number()) {
                    return fail("post.{} must be an array of three numbers", key);
                }
                param->setBase(glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));
                handled = true;
                break;
            }
        }
        if (handled) {
            continue;
        }
        if (key == "tiltShiftCentre" && p.tiltShiftCentre != nullptr) {
            if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) {
                return fail("post.tiltShiftCentre must be an array of two numbers");
            }
            p.tiltShiftCentre->setBase(glm::vec2(value[0].get<float>(), value[1].get<float>()));
            continue;
        }
        if (key == "tonemap" && p.tonemap != nullptr) {
            if (!value.is_number_unsigned()) {
                return fail("post.tonemap must be an unsigned integer (0 aces, 1 agx, 2 reinhard, "
                            "3 pbr-neutral, 4 clamp)");
            }
            p.tonemap->setBase(static_cast<int>(std::min(value.get<std::uint32_t>(), 4u)));
            continue;
        }
        if (key == "bloomLevels") {
            // Fixed when the bloom pyramid is created, so there is no parameter to move. Accepted
            // and ignored rather than warned about: the scenes that name it are not wrong to.
            continue;
        }
        log::warn("post.{}: unknown key, ignored", key);
    }
    return {};
}

void applyPostParameters(const PostParameters& p, PostSettings& s) {
    if (p.bloomEnabled == nullptr) {
        return;
    }
    s.bloomEnabled = p.bloomEnabled->value();
    s.bloomIntensity = p.bloomIntensity->value();
    s.bloomThreshold = p.bloomThreshold->value();
    s.bloomKnee = p.bloomKnee->value();
    s.bloomRadius = p.bloomRadius->value();
    s.bloomEmissionWeight = p.bloomEmissionWeight->value();
    s.halationEnabled = p.halationEnabled->value();
    s.halationIntensity = p.halationIntensity->value();
    s.halationThreshold = p.halationThreshold->value();
    s.halationRadius = p.halationRadius->value();
    s.halationWarmth = p.halationWarmth->value();
    s.halationTint = p.halationTint->value();
    s.anamorphicEnabled = p.anamorphicEnabled->value();
    s.anamorphicIntensity = p.anamorphicIntensity->value();
    s.anamorphicStretch = p.anamorphicStretch->value();
    s.anamorphicGhosts = p.anamorphicGhosts->value();
    s.anamorphicTint = p.anamorphicTint->value();
    s.contrast = p.contrast->value();
    s.saturation = p.saturation->value();
    s.temperature = p.temperature->value();
    s.tint = p.tint->value();
    s.hueShift = p.hueShift->value();
    s.lift = p.lift->value();
    s.gamma = p.gamma->value();
    s.gain = p.gain->value();
    s.chromaticAberration = p.chromaticAberration->value();
    s.distortion = p.distortion->value();
    s.dofEnabled = p.dofEnabled->value();
    s.focusDistance = p.focusDistance->value();
    s.focusRange = p.focusRange->value();
    s.dofMaxRadius = p.dofMaxRadius->value();
    s.dofPhysical = p.dofPhysical->value();
    s.tiltShiftEnabled = p.tiltShiftEnabled->value();
    s.tiltShiftCentre = p.tiltShiftCentre->value();
    s.tiltShiftRotation = p.tiltShiftRotation->value();
    s.tiltShiftBandWidth = p.tiltShiftBandWidth->value();
    s.tiltShiftFalloff = p.tiltShiftFalloff->value();
    s.tiltShiftMaxRadius = p.tiltShiftMaxRadius->value();
    s.motionBlurAmount = p.motionBlurAmount->value();
    if (p.motionBlurSamples != nullptr) {
        s.motionBlurSamples = static_cast<std::uint32_t>(std::max(p.motionBlurSamples->value(), 2));
        s.motionBlurMaxRadius = p.motionBlurMaxRadius->value();
        s.motionBlurTileSize = static_cast<std::uint32_t>(std::max(p.motionBlurTileSize->value(), 4));
    }
    s.antialias = p.antialias->value();
    s.sharpen = p.sharpen->value();
    s.sharpenId = static_cast<std::uint32_t>(std::max(p.sharpenId->value(), 0));
    s.tonemap = static_cast<TonemapOperator>(std::clamp(p.tonemap->value(), 0, 4));
    s.vignette = p.vignette->value();
    s.grain = p.grain->value();
    s.chromaRetention = p.chromaRetention->value();
    // Cinematic integration (§52.1). Guarded because `registerPostParameters` is what creates these
    // and an older caller may hold a `PostParameters` from before they existed; without the guard
    // that is a null dereference rather than a default.
    if (p.lookAtmospheric != nullptr) {
        s.look.atmospheric = p.lookAtmospheric->value();
        s.look.atmosphericDistance = p.lookAtmosphericDistance->value();
        s.look.atmosphericTint = p.lookAtmosphericTint->value();
        s.look.colour = p.lookColour->value();
        s.look.localContrast = p.lookLocalContrast->value();
        s.look.localContrastRadius = p.lookLocalContrastRadius->value();
        s.look.lightWrap = p.lookLightWrap->value();
    }
}

float tiltShiftCoverage(const PostSettings& s, glm::vec2 uv, float aspect) {
    // Scaling x by the aspect ratio puts both axes in units of frame height, which is what makes
    // the rotation an angle on *screen*. Measured in raw uv, a 45 degree band on a 16:9 frame comes
    // out at 28 degrees, and its width changes as it turns.
    const glm::vec2 p = (uv - s.tiltShiftCentre) * glm::vec2(std::max(aspect, 1e-4f), 1.0f);
    const float theta = glm::radians(s.tiltShiftRotation);
    // The normal to the band's axis. uv.y runs downwards, so a positive rotation reads clockwise.
    const glm::vec2 normal{-std::sin(theta), std::cos(theta)};
    const float distance = std::abs(p.x * normal.x + p.y * normal.y);
    const float halfWidth = std::max(s.tiltShiftBandWidth, 0.0f) * 0.5f;
    const float t = std::clamp((distance - halfWidth) / std::max(s.tiltShiftFalloff, 1e-4f), 0.0f, 1.0f);
    // Smoothstep rather than a linear ramp: the band's edge is where the eye looks for a seam, and
    // a linear ramp leaves a visible crease there because its slope jumps.
    return t * t * (3.0f - 2.0f * t);
}

} // namespace avgen::scene
