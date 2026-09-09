#include "scene/spline_params.hpp"

#include <utility>

namespace avgen::scene {

namespace {

struct Reg {
    params::ParameterSet& params;
    SplineParameters& out;
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
    params::Parameter<int>* i(const char* rel, int def, int lo, int hi, int slo, int shi) {
        params::ParamDesc<int> d;
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = slo;
        d.softMax = shi;
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
};

template <typename T>
params::Parameter<T>* findRel(const SplineParameters& p, const std::string& rel) {
    for (params::IParameter* ip : p.all) {
        const std::string& path = ip->path();
        if (path.size() == p.prefix.size() + rel.size() && path.compare(p.prefix.size(), rel.size(), rel) == 0) {
            return dynamic_cast<params::Parameter<T>*>(ip);
        }
    }
    return nullptr;
}
template <typename T>
void copyValue(const SplineParameters& p, const std::string& rel, T& target) {
    if (auto* param = findRel<T>(p, rel)) {
        target = param->value();
    }
}

} // namespace

SplineParameters registerSplineParameters(params::ParameterSet& params, const spatial::Spline& rest,
                                          const std::string& prefix) {
    SplineParameters p;
    p.prefix = prefix;
    std::string group = prefix;
    while (!group.empty() && group.back() == '/') {
        group.pop_back();
    }
    Reg r{params, p, group};
    constexpr float kTwoPi = 6.2831853f;
    p.count = r.i("count", rest.count, 2, 4096, 2, 256);
    p.radius = r.f("radius", rest.radius, 0.0f, 1000.0f, 0.0f, 50.0f);
    r.f("radiusGrowth", rest.radiusGrowth, -1000.0f, 1000.0f, -20.0f, 20.0f);
    p.turns = r.f("turns", rest.turns, -100.0f, 100.0f, 0.0f, 10.0f);
    p.height = r.f("height", rest.height, -1000.0f, 1000.0f, -50.0f, 50.0f);
    r.f("startAngle", rest.startAngle, -100.0f, 100.0f, -kTwoPi, kTwoPi);
    p.noiseAmount = r.f("noiseAmount", rest.noiseAmount, 0.0f, 100.0f, 0.0f, 5.0f);
    r.f("noiseScale", rest.noiseScale, 0.001f, 100.0f, 0.01f, 5.0f);
    r.f("tension", rest.tension, 0.0f, 1.0f, 0.0f, 1.0f);
    r.v3("start", rest.start, -1e4f, 1e4f, -50.0f, 50.0f);
    r.v3("end", rest.end, -1e4f, 1e4f, -50.0f, 50.0f);
    r.v3("center", rest.center, -1e4f, 1e4f, -50.0f, 50.0f);
    r.v3("axis", rest.axis, -1.0f, 1.0f, -1.0f, 1.0f);
    r.v3("up", rest.up, -1.0f, 1.0f, -1.0f, 1.0f);
    r.v3("p0", rest.p0, -1e4f, 1e4f, -50.0f, 50.0f);
    r.v3("p1", rest.p1, -1e4f, 1e4f, -50.0f, 50.0f);
    r.v3("p2", rest.p2, -1e4f, 1e4f, -50.0f, 50.0f);
    r.v3("p3", rest.p3, -1e4f, 1e4f, -50.0f, 50.0f);
    return p;
}

void applySplineParameters(const SplineParameters& p, const spatial::Spline& rest, spatial::Spline& live) {
    live = rest;
    copyValue(p, "count", live.count);
    copyValue(p, "radius", live.radius);
    copyValue(p, "radiusGrowth", live.radiusGrowth);
    copyValue(p, "turns", live.turns);
    copyValue(p, "height", live.height);
    copyValue(p, "startAngle", live.startAngle);
    copyValue(p, "noiseAmount", live.noiseAmount);
    copyValue(p, "noiseScale", live.noiseScale);
    copyValue(p, "tension", live.tension);
    copyValue(p, "start", live.start);
    copyValue(p, "end", live.end);
    copyValue(p, "center", live.center);
    copyValue(p, "axis", live.axis);
    copyValue(p, "up", live.up);
    copyValue(p, "p0", live.p0);
    copyValue(p, "p1", live.p1);
    copyValue(p, "p2", live.p2);
    copyValue(p, "p3", live.p3);
}

void unregisterSplineParameters(params::ParameterSet& params, const SplineParameters& p) {
    for (params::IParameter* ip : p.all) {
        if (ip != nullptr) {
            const std::string path = ip->path();
            params.remove(path);
        }
    }
}

} // namespace avgen::scene
