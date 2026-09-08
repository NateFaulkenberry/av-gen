#include "scene/post_settings.hpp"

#include <algorithm>

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
params::ParamDesc<glm::vec3> v3(const char* path, glm::vec3 def, float lo, float hi) {
    params::ParamDesc<glm::vec3> d;
    d.path = path;
    d.defaultValue = def;
    d.hardMin = glm::vec3(lo);
    d.hardMax = glm::vec3(hi);
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
    p.bloomIntensity = &params.add(f("post/bloom/intensity", s.bloomIntensity, 0.0f, 10.0f, 0.0f, 2.0f));
    p.bloomThreshold = &params.add(f("post/bloom/threshold", s.bloomThreshold, 0.0f, 20.0f, 0.0f, 4.0f));
    p.bloomKnee = &params.add(f("post/bloom/knee", s.bloomKnee, 0.0f, 1.0f, 0.0f, 1.0f));
    p.bloomRadius = &params.add(f("post/bloom/radius", s.bloomRadius, 0.25f, 3.0f, 0.5f, 2.0f));
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
    p.motionBlurAmount = &params.add(f("post/motionBlur/amount", s.motionBlurAmount, 0.0f, 1.0f, 0.0f, 1.0f));
    {
        params::ParamDesc<int> d;
        d.path = "post/tonemap/operator";
        d.defaultValue = static_cast<int>(s.tonemap);
        d.hardMin = 0;
        d.hardMax = 4;
        p.tonemap = &params.add(std::move(d));
    }
    p.vignette = &params.add(f("post/output/vignette", s.vignette, 0.0f, 1.0f, 0.0f, 1.0f));
    p.grain = &params.add(f("post/output/grain", s.grain, 0.0f, 1.0f, 0.0f, 1.0f));
    return p;
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
    s.motionBlurAmount = p.motionBlurAmount->value();
    s.tonemap = static_cast<TonemapOperator>(std::clamp(p.tonemap->value(), 0, 4));
    s.vignette = p.vignette->value();
    s.grain = p.grain->value();
}

} // namespace avgen::scene
