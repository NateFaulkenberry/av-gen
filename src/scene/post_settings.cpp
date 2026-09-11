#include "scene/post_settings.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
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
    };
    const std::pair<const char*, params::Parameter<bool>*> bools[] = {
        {"bloomEnabled", p.bloomEnabled}, {"halationEnabled", p.halationEnabled},
        {"tiltShiftEnabled", p.tiltShiftEnabled},
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
    s.motionBlurAmount = p.motionBlurAmount->value();
    s.antialias = p.antialias->value();
    s.sharpen = p.sharpen->value();
    s.sharpenId = static_cast<std::uint32_t>(std::max(p.sharpenId->value(), 0));
    s.tonemap = static_cast<TonemapOperator>(std::clamp(p.tonemap->value(), 0, 4));
    s.vignette = p.vignette->value();
    s.grain = p.grain->value();
    s.chromaRetention = p.chromaRetention->value();
}

} // namespace avgen::scene
