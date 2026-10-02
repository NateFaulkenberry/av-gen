#include "scene/toon_shading.hpp"

#include "params/parameter_set.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <string_view>

namespace avgen::scene {

namespace {

using json = nlohmann::json;

struct FloatKey {
    const char* key;
    float ToonShading::*field;
    float lo, hi, softLo, softHi;
};
struct ColorKey {
    const char* key;
    glm::vec3 ToonShading::*field;
};

// One row per scalar: its key (the file's and the parameter's), its field and its ranges.
constexpr std::array<FloatKey, 8> kFloats{{
    {"bands", &ToonShading::bands, 0.0f, 16.0f, 0.0f, 8.0f},
    {"softness", &ToonShading::softness, 0.001f, 1.0f, 0.001f, 0.3f},
    {"terminator", &ToonShading::terminator, -1.0f, 0.99f, -0.6f, 0.6f},
    {"ambient", &ToonShading::ambient, 0.0f, 8.0f, 0.0f, 1.5f},
    {"rimWidth", &ToonShading::rimWidth, 0.0f, 1.0f, 0.0f, 0.6f},
    {"rimIntensity", &ToonShading::rimIntensity, 0.0f, 100.0f, 0.0f, 8.0f},
    {"specular", &ToonShading::specular, 0.0f, 100.0f, 0.0f, 4.0f},
    {"specularSize", &ToonShading::specularSize, 0.0f, 1.0f, 0.0f, 0.5f},
}};
constexpr std::array<ColorKey, 2> kColors{{
    {"shadowColor", &ToonShading::shadowColor},
    {"rimColor", &ToonShading::rimColor},
}};

} // namespace

Result<void> readToonShading(const json& j, ToonShading& out) {
    if (!j.is_object()) {
        return fail("'toon' must be an object");
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& key = it.key();
        bool known = false;
        for (const FloatKey& f : kFloats) {
            if (key != f.key) {
                continue;
            }
            known = true;
            if (!it->is_number() || !std::isfinite(it->get<double>())) {
                return fail("toon '{}' must be a finite number", key);
            }
            out.*(f.field) = it->get<float>();
        }
        for (const ColorKey& c : kColors) {
            if (key != c.key) {
                continue;
            }
            known = true;
            if (!it->is_array() || it->size() != 3) {
                return fail("toon '{}' must be an array of three numbers", key);
            }
            glm::vec3 v{0.0f};
            for (int i = 0; i < 3; ++i) {
                const json& e = (*it)[static_cast<std::size_t>(i)];
                if (!e.is_number() || !std::isfinite(e.get<double>())) {
                    return fail("toon '{}' must be an array of three finite numbers", key);
                }
                v[i] = e.get<float>();
            }
            out.*(c.field) = v;
        }
        if (!known) {
            return fail("toon has no key '{}' (bands, softness, terminator, shadowColor, ambient, rimWidth, "
                        "rimColor, rimIntensity, specular, specularSize)",
                        key);
        }
    }
    return {};
}

json toonShadingToJson(const ToonShading& t) {
    json j = json::object();
    for (const FloatKey& f : kFloats) {
        j[f.key] = t.*(f.field);
    }
    for (const ColorKey& c : kColors) {
        const glm::vec3& v = t.*(c.field);
        j[c.key] = json::array({v.x, v.y, v.z});
    }
    return j;
}

bool toonShadingIsDefault(const ToonShading& t) {
    const ToonShading d{};
    for (const FloatKey& f : kFloats) {
        if (t.*(f.field) != d.*(f.field)) {
            return false;
        }
    }
    for (const ColorKey& c : kColors) {
        if (t.*(c.field) != d.*(c.field)) {
            return false;
        }
    }
    return true;
}

ToonParameters registerToonParameters(params::ParameterSet& params, const std::string& prefix,
                                      const std::string& group, const ToonShading& rest,
                                      std::vector<params::IParameter*>* all) {
    ToonParameters out;
    const auto addFloat = [&](const FloatKey& f) {
        params::ParamDesc<float> d;
        d.path = prefix + "toon/" + f.key;
        d.label = std::string("toon/") + f.key;
        d.group = group;
        d.defaultValue = rest.*(f.field);
        d.hardMin = f.lo;
        d.hardMax = f.hi;
        d.softMin = f.softLo;
        d.softMax = f.softHi;
        auto& p = params.add(std::move(d));
        if (all != nullptr) {
            all->push_back(&p);
        }
        return &p;
    };
    const auto addColor = [&](const ColorKey& c) {
        params::ParamDesc<glm::vec3> d;
        d.path = prefix + "toon/" + c.key;
        d.label = std::string("toon/") + c.key;
        d.group = group;
        d.defaultValue = rest.*(c.field);
        d.hardMin = glm::vec3(0.0f);
        d.hardMax = glm::vec3(1.0f);
        d.softMin = glm::vec3(0.0f);
        d.softMax = glm::vec3(1.0f);
        d.isColor = true;
        auto& p = params.add(std::move(d));
        if (all != nullptr) {
            all->push_back(&p);
        }
        return &p;
    };
    out.bands = addFloat(kFloats[0]);
    out.softness = addFloat(kFloats[1]);
    out.terminator = addFloat(kFloats[2]);
    out.shadowColor = addColor(kColors[0]);
    out.ambient = addFloat(kFloats[3]);
    out.rimWidth = addFloat(kFloats[4]);
    out.rimColor = addColor(kColors[1]);
    out.rimIntensity = addFloat(kFloats[5]);
    out.specular = addFloat(kFloats[6]);
    out.specularSize = addFloat(kFloats[7]);
    return out;
}

void applyToonParameters(const ToonParameters& p, ToonShading& live) {
    const auto copy = [](const auto* param, auto& target) {
        if (param != nullptr) {
            target = param->value();
        }
    };
    copy(p.bands, live.bands);
    copy(p.softness, live.softness);
    copy(p.terminator, live.terminator);
    copy(p.shadowColor, live.shadowColor);
    copy(p.ambient, live.ambient);
    copy(p.rimWidth, live.rimWidth);
    copy(p.rimColor, live.rimColor);
    copy(p.rimIntensity, live.rimIntensity);
    copy(p.specular, live.specular);
    copy(p.specularSize, live.specularSize);
}

} // namespace avgen::scene
