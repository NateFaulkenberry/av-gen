#include "scene/field_params.hpp"

#include <algorithm>
#include <utility>

namespace avgen::scene {

namespace {

struct FieldRegistrar {
    params::ParameterSet& params;
    FieldParameters& out;
    std::string group;

    template <typename T>
    params::Parameter<T>* add(params::ParamDesc<T> d, const char* rel) {
        d.path = out.prefix + rel;
        d.label = rel;
        d.group = group;
        auto& p = params.add(std::move(d));
        out.all.push_back(&p);
        return &p;
    }
    params::Parameter<float>* f(const char* rel, float def, float lo, float hi, float slo, float shi) {
        params::ParamDesc<float> d;
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = slo;
        d.softMax = shi;
        return add(std::move(d), rel);
    }
    params::Parameter<bool>* b(const char* rel, bool def) {
        params::ParamDesc<bool> d;
        d.defaultValue = def;
        d.hardMin = false;
        d.hardMax = true;
        return add(std::move(d), rel);
    }
    params::Parameter<glm::vec3>* v3(const char* rel, glm::vec3 def, float lo, float hi, float slo, float shi) {
        params::ParamDesc<glm::vec3> d;
        d.defaultValue = def;
        d.hardMin = glm::vec3(lo);
        d.hardMax = glm::vec3(hi);
        d.softMin = glm::vec3(slo);
        d.softMax = glm::vec3(shi);
        return add(std::move(d), rel);
    }
    params::Parameter<glm::vec4>* c4(const char* rel, glm::vec4 def) {
        params::ParamDesc<glm::vec4> d;
        d.defaultValue = def;
        d.hardMin = glm::vec4(0.0f);
        d.hardMax = glm::vec4(100.0f);
        d.softMin = glm::vec4(0.0f);
        d.softMax = glm::vec4(1.0f);
        d.isColor = true;
        return add(std::move(d), rel);
    }
};

template <typename T>
params::Parameter<T>* findRel(const FieldParameters& p, const std::string& rel) {
    for (params::IParameter* ip : p.all) {
        const std::string& path = ip->path();
        if (path.size() == p.prefix.size() + rel.size() && path.compare(p.prefix.size(), rel.size(), rel) == 0) {
            return dynamic_cast<params::Parameter<T>*>(ip);
        }
    }
    return nullptr;
}

template <typename T>
void copyValue(const FieldParameters& p, const std::string& rel, T& target) {
    if (auto* param = findRel<T>(p, rel)) {
        target = param->value();
    }
}

} // namespace

FieldParameters registerFieldParameters(params::ParameterSet& params, const spatial::FieldSpec& rest,
                                        const std::string& prefix) {
    FieldParameters p;
    p.prefix = prefix;
    std::string group = prefix;
    while (!group.empty() && group.back() == '/') {
        group.pop_back();
    }
    FieldRegistrar r{params, p, group};
    constexpr float kTwoPi = 6.2831853f;

    p.enabled = r.b("enabled", rest.enabled);
    p.position = r.v3("position", rest.position, -1e4f, 1e4f, -20.0f, 20.0f);
    p.rotation = r.v3("rotation", rest.rotationDegrees, -360.0f, 360.0f, -360.0f, 360.0f);
    p.scale = r.v3("scale", rest.scale, 0.001f, 100.0f, 0.01f, 5.0f);
    p.strength = r.f("strength", rest.strength, -1000.0f, 1000.0f, -5.0f, 5.0f);
    r.b("invert", rest.invert);
    p.speed = r.f("speed", rest.speed, -100.0f, 100.0f, -5.0f, 5.0f);
    r.f("phase", rest.phase, -100.0f, 100.0f, -kTwoPi, kTwoPi);
    r.v3("axis", rest.axis, -1.0f, 1.0f, -1.0f, 1.0f);
    r.v3("point", rest.point, -1e4f, 1e4f, -20.0f, 20.0f);
    p.radius = r.f("radius", rest.radius, 0.0f, 1000.0f, 0.0f, 50.0f);
    r.f("length", rest.length, 0.0f, 1000.0f, 0.0f, 50.0f);
    r.v3("size", rest.size, 0.0f, 1000.0f, 0.0f, 20.0f);
    r.f("softness", rest.softness, 0.0f, 1000.0f, 0.0f, 5.0f);
    p.frequency = r.f("frequency", rest.frequency, 0.0f, 100.0f, 0.0f, 2.0f);
    r.f("spiralBias", rest.spiralBias, -10.0f, 10.0f, -2.0f, 2.0f);
    p.amplitude = r.f("amplitude", rest.amplitude, -1000.0f, 1000.0f, -5.0f, 5.0f);
    r.f("wavelength", rest.wavelength, 0.001f, 1000.0f, 0.1f, 20.0f);
    r.f("waveSpeed", rest.waveSpeed, -1000.0f, 1000.0f, -20.0f, 20.0f);
    r.f("waveWidth", rest.waveWidth, 0.0f, 1000.0f, 0.0f, 20.0f);
    r.f("waveOrigin", rest.waveOrigin, -1000.0f, 1000.0f, -20.0f, 20.0f);
    p.colorA = r.c4("colorA", rest.colorA);
    p.colorB = r.c4("colorB", rest.colorB);
    r.f("mix", rest.mix, 0.0f, 1.0f, 0.0f, 1.0f);
    r.f("falloff/inner", rest.falloff.inner, 0.0f, 1000.0f, 0.0f, 50.0f);
    p.falloffOuter = r.f("falloff/outer", rest.falloff.outer, 0.0f, 1000.0f, 0.0f, 50.0f);
    r.f("falloff/exponent", rest.falloff.exponent, 0.01f, 32.0f, 0.1f, 8.0f);
    r.f("falloff/noiseAmount", rest.falloff.noiseAmount, 0.0f, 10.0f, 0.0f, 1.0f);
    r.f("falloff/noiseScale", rest.falloff.noiseScale, 0.001f, 100.0f, 0.01f, 5.0f);
    return p;
}

void applyFieldParameters(const FieldParameters& p, const spatial::FieldSpec& rest, spatial::FieldSpec& live) {
    live = rest;
    copyValue(p, "enabled", live.enabled);
    copyValue(p, "position", live.position);
    copyValue(p, "rotation", live.rotationDegrees);
    copyValue(p, "scale", live.scale);
    copyValue(p, "strength", live.strength);
    copyValue(p, "invert", live.invert);
    copyValue(p, "speed", live.speed);
    copyValue(p, "phase", live.phase);
    copyValue(p, "axis", live.axis);
    copyValue(p, "point", live.point);
    copyValue(p, "radius", live.radius);
    copyValue(p, "length", live.length);
    copyValue(p, "size", live.size);
    copyValue(p, "softness", live.softness);
    copyValue(p, "frequency", live.frequency);
    copyValue(p, "spiralBias", live.spiralBias);
    copyValue(p, "amplitude", live.amplitude);
    copyValue(p, "wavelength", live.wavelength);
    copyValue(p, "waveSpeed", live.waveSpeed);
    copyValue(p, "waveWidth", live.waveWidth);
    copyValue(p, "waveOrigin", live.waveOrigin);
    copyValue(p, "colorA", live.colorA);
    copyValue(p, "colorB", live.colorB);
    copyValue(p, "mix", live.mix);
    copyValue(p, "falloff/inner", live.falloff.inner);
    copyValue(p, "falloff/outer", live.falloff.outer);
    copyValue(p, "falloff/exponent", live.falloff.exponent);
    copyValue(p, "falloff/noiseAmount", live.falloff.noiseAmount);
    copyValue(p, "falloff/noiseScale", live.falloff.noiseScale);
}

void unregisterFieldParameters(params::ParameterSet& params, const FieldParameters& p) {
    for (params::IParameter* ip : p.all) {
        if (ip != nullptr) {
            const std::string path = ip->path(); // remove() destroys the parameter that owns it
            params.remove(path);
        }
    }
}

} // namespace avgen::scene
