#include "scene/post_outline.hpp"

#include "params/parameter_set.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

namespace avgen::scene {

namespace {

struct Spec {
    const char* path;
    const char* key;
    float PostOutlineSettings::*field;
    float lo, hi, softLo, softHi;
};

const Spec kSpecs[] = {
    {"post/outline/amount", "outlineAmount", &PostOutlineSettings::amount, 0.0f, 1.0f, 0.0f, 1.0f},
    {"post/outline/intensity", "outlineIntensity", &PostOutlineSettings::intensity, 0.0f, 1000.0f, 0.0f, 20.0f},
    {"post/outline/width", "outlineWidth", &PostOutlineSettings::width, 0.0f, 64.0f, 0.5f, 8.0f},
    {"post/outline/depthThreshold", "outlineDepthThreshold", &PostOutlineSettings::depthThreshold, 0.001f, 10.0f,
     0.01f, 0.5f},
    {"post/outline/normalThreshold", "outlineNormalThreshold", &PostOutlineSettings::normalThreshold, 0.001f, 2.0f,
     0.02f, 1.0f},
    {"post/outline/silhouette", "outlineSilhouette", &PostOutlineSettings::silhouette, 0.0f, 1.0f, 0.0f, 1.0f},
    {"post/outline/objectEdges", "outlineObjectEdges", &PostOutlineSettings::objectEdges, 0.0f, 1.0f, 0.0f, 1.0f},
    {"post/outline/fadeStart", "outlineFadeStart", &PostOutlineSettings::fadeStart, 0.0f, 100000.0f, 0.0f, 200.0f},
    {"post/outline/fadeEnd", "outlineFadeEnd", &PostOutlineSettings::fadeEnd, 0.0f, 100000.0f, 0.0f, 400.0f},
};

} // namespace

PostOutlineParameters registerPostOutlineParameters(params::ParameterSet& params, const PostOutlineSettings& defaults) {
    PostOutlineParameters out;
    for (const Spec& s : kSpecs) {
        params::ParamDesc<float> d;
        d.path = s.path;
        d.defaultValue = defaults.*(s.field);
        d.hardMin = s.lo;
        d.hardMax = s.hi;
        d.softMin = s.softLo;
        d.softMax = s.softHi;
        out.rows.push_back({&params.add(std::move(d)), s.field, s.key});
    }
    params::ParamDesc<glm::vec3> c;
    c.path = "post/outline/color";
    c.defaultValue = defaults.color;
    c.hardMin = glm::vec3(0.0f);
    c.hardMax = glm::vec3(1.0f);
    c.softMin = glm::vec3(0.0f);
    c.softMax = glm::vec3(1.0f);
    c.isColor = true;
    out.color = &params.add(std::move(c));
    return out;
}

void applyPostOutlineParameters(const PostOutlineParameters& p, PostOutlineSettings& settings) {
    for (const auto& row : p.rows) {
        if (row.param != nullptr) {
            settings.*(row.field) = row.param->value();
        }
    }
    if (p.color != nullptr) {
        settings.color = p.color->value();
    }
}

bool applyPostOutlineJsonKey(const PostOutlineParameters& p, const std::string& key, const nlohmann::json& value,
                             std::string& error) {
    for (const auto& row : p.rows) {
        if (key == row.key && row.param != nullptr) {
            if (!value.is_number()) {
                error = "post." + key + " must be a number";
            } else {
                row.param->setBase(value.get<float>());
            }
            return true;
        }
    }
    if (key == "outlineColor" && p.color != nullptr) {
        if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() ||
            !value[2].is_number()) {
            error = "post.outlineColor must be an array of three numbers";
        } else {
            p.color->setBase(glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));
        }
        return true;
    }
    return false;
}

} // namespace avgen::scene
