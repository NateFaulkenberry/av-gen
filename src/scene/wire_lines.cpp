#include "scene/wire_lines.hpp"

#include "params/parameter_set.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>

namespace avgen::scene {

namespace {

using json = nlohmann::json;

struct FloatKey {
    const char* key;
    float WireLines::*field;
    float lo, hi, softLo, softHi;
};

constexpr std::array<FloatKey, 7> kFloats{{
    {"mode", &WireLines::mode, 0.0f, 2.0f, 0.0f, 2.0f},
    {"crease", &WireLines::crease, 0.0f, 180.0f, 0.0f, 90.0f},
    {"intensity", &WireLines::intensity, 0.0f, 1000.0f, 0.0f, 20.0f},
    {"opacity", &WireLines::opacity, 0.0f, 1.0f, 0.0f, 1.0f},
    {"width", &WireLines::width, 0.0f, 64.0f, 0.25f, 8.0f},
    {"fill", &WireLines::fill, 0.0f, 1.0f, 0.0f, 1.0f},
    {"occlude", &WireLines::occlude, 0.0f, 1.0f, 0.0f, 1.0f},
}};

} // namespace

Result<void> readWireLines(const json& j, WireLines& out) {
    if (!j.is_object()) {
        return fail("'wire' must be an object");
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& key = it.key();
        if (key == "color") {
            if (!it->is_array() || it->size() != 3) {
                return fail("wire 'color' must be an array of three numbers");
            }
            glm::vec3 v{0.0f};
            for (int i = 0; i < 3; ++i) {
                const json& e = (*it)[static_cast<std::size_t>(i)];
                if (!e.is_number() || !std::isfinite(e.get<double>())) {
                    return fail("wire 'color' must be an array of three finite numbers");
                }
                v[i] = e.get<float>();
            }
            out.color = v;
            continue;
        }
        if (key == "mode" && it->is_string()) {
            const std::string m = it->get<std::string>();
            if (m == "off") {
                out.mode = 0.0f;
            } else if (m == "feature" || m == "crease") {
                out.mode = 1.0f;
            } else if (m == "all" || m == "triangles") {
                out.mode = 2.0f;
            } else {
                return fail("wire 'mode' '{}' is not off, feature or all", m);
            }
            continue;
        }
        bool known = false;
        for (const FloatKey& f : kFloats) {
            if (key != f.key) {
                continue;
            }
            known = true;
            if (!it->is_number() || !std::isfinite(it->get<double>())) {
                return fail("wire '{}' must be a finite number", key);
            }
            out.*(f.field) = it->get<float>();
        }
        if (!known) {
            return fail("wire has no key '{}' (mode, crease, color, intensity, opacity, width, fill, occlude)", key);
        }
    }
    return {};
}

json wireLinesToJson(const WireLines& w) {
    json j = json::object();
    for (const FloatKey& f : kFloats) {
        j[f.key] = w.*(f.field);
    }
    j["color"] = json::array({w.color.x, w.color.y, w.color.z});
    return j;
}

bool wireLinesIsDefault(const WireLines& w) {
    const WireLines d{};
    for (const FloatKey& f : kFloats) {
        if (w.*(f.field) != d.*(f.field)) {
            return false;
        }
    }
    return w.color == d.color;
}

WireParameters registerWireParameters(params::ParameterSet& params, const std::string& prefix,
                                      const std::string& group, const WireLines& rest,
                                      std::vector<params::IParameter*>* all) {
    WireParameters out;
    const auto addFloat = [&](const FloatKey& f) {
        params::ParamDesc<float> d;
        d.path = prefix + "wire/" + f.key;
        d.label = std::string("wire/") + f.key;
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
    out.mode = addFloat(kFloats[0]);
    out.crease = addFloat(kFloats[1]);
    {
        params::ParamDesc<glm::vec3> d;
        d.path = prefix + "wire/color";
        d.label = "wire/color";
        d.group = group;
        d.defaultValue = rest.color;
        d.hardMin = glm::vec3(0.0f);
        d.hardMax = glm::vec3(1.0f);
        d.softMin = glm::vec3(0.0f);
        d.softMax = glm::vec3(1.0f);
        d.isColor = true;
        auto& p = params.add(std::move(d));
        if (all != nullptr) {
            all->push_back(&p);
        }
        out.color = &p;
    }
    out.intensity = addFloat(kFloats[2]);
    out.opacity = addFloat(kFloats[3]);
    out.width = addFloat(kFloats[4]);
    out.fill = addFloat(kFloats[5]);
    out.occlude = addFloat(kFloats[6]);
    return out;
}

void applyWireParameters(const WireParameters& p, WireLines& live) {
    const auto copy = [](const auto* param, auto& target) {
        if (param != nullptr) {
            target = param->value();
        }
    };
    copy(p.mode, live.mode);
    copy(p.crease, live.crease);
    copy(p.color, live.color);
    copy(p.intensity, live.intensity);
    copy(p.opacity, live.opacity);
    copy(p.width, live.width);
    copy(p.fill, live.fill);
    copy(p.occlude, live.occlude);
}

} // namespace avgen::scene
