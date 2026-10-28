#include "scene/mesh_wire.hpp"

#include "params/parameter_set.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>

namespace avgen::scene {

namespace {

using json = nlohmann::json;

struct FloatKey {
    const char* key;
    float MeshWire::*field;
    float lo, hi, softLo, softHi;
};

constexpr std::array<FloatKey, 15> kFloats{{
    {"intensity", &MeshWire::intensity, 0.0f, 1000.0f, 0.0f, 4.0f},
    {"opacity", &MeshWire::opacity, 0.0f, 1.0f, 0.0f, 1.0f},
    {"width", &MeshWire::width, 0.0f, 16.0f, 0.25f, 4.0f},
    {"creaseLow", &MeshWire::creaseLow, 0.0f, 180.0f, 0.0f, 45.0f},
    {"creaseHigh", &MeshWire::creaseHigh, 0.0f, 180.0f, 0.0f, 90.0f},
    {"reveal", &MeshWire::reveal, 0.0f, 1.0f, 0.0f, 1.0f},
    {"fadeNear", &MeshWire::fadeNear, 0.0f, 1.0e6f, 0.0f, 2000.0f},
    {"fadeFar", &MeshWire::fadeFar, 0.0f, 1.0e6f, 0.0f, 6000.0f},
    {"minPixels", &MeshWire::minPixels, 0.0f, 256.0f, 0.0f, 40.0f},
    {"wave", &MeshWire::wave, 0.0f, 1.0f, 0.0f, 1.0f},
    {"waveRadius", &MeshWire::waveRadius, 0.0f, 1.0e5f, 0.0f, 4000.0f},
    {"waveWidth", &MeshWire::waveWidth, 0.0f, 1.0e4f, 1.0f, 400.0f},
    {"waveGain", &MeshWire::waveGain, 0.0f, 1000.0f, 0.0f, 20.0f},
    {"waveAhead", &MeshWire::waveAhead, -1.0e5f, 1.0e5f, -500.0f, 3000.0f},
    {"clipBelow", &MeshWire::clipBelow, -1.0e9f, 1.0e9f, -100.0f, 100.0f},
}};
constexpr std::size_t kFloatCount = kFloats.size(); // and `color`, a vec3 parameter; `targets` is structural

} // namespace

bool meshWireTargets(const MeshWire& w, const std::string& name) {
    for (const std::string& t : w.targets) {
        if (!t.empty() && name.compare(0, t.size(), t) == 0) {
            return true;
        }
    }
    return false;
}

Result<MeshWire> meshWireFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("'meshWire' must be an object");
    }
    MeshWire w;
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& key = it.key();
        if (key == "targets") {
            if (!it->is_array()) {
                return fail("meshWire 'targets' must be an array of entity-name prefixes");
            }
            for (const json& t : *it) {
                if (!t.is_string()) {
                    return fail("meshWire 'targets' must be an array of strings");
                }
                w.targets.push_back(t.get<std::string>());
            }
            continue;
        }
        if (key == "color") {
            if (!it->is_array() || it->size() != 3) {
                return fail("meshWire 'color' must be an array of three numbers");
            }
            for (int i = 0; i < 3; ++i) {
                const json& e = (*it)[static_cast<std::size_t>(i)];
                if (!e.is_number() || !std::isfinite(e.get<double>())) {
                    return fail("meshWire 'color' must be three finite numbers");
                }
                w.color[i] = e.get<float>();
            }
            continue;
        }
        bool known = false;
        for (std::size_t k = 0; k < kFloatCount; ++k) {
            const FloatKey& f = kFloats[k];
            if (key != f.key) {
                continue;
            }
            known = true;
            if (!it->is_number() || !std::isfinite(it->get<double>())) {
                return fail("meshWire '{}' must be a finite number", key);
            }
            const float v = it->get<float>();
            if (v < f.lo || v > f.hi) {
                return fail("meshWire '{}' must be in [{}, {}]", key, f.lo, f.hi);
            }
            w.*(f.field) = v;
        }
        if (!known) {
            return fail("meshWire has no key '{}'", key);
        }
    }
    return w;
}

json meshWireToJson(const MeshWire& w) {
    json j = json::object();
    j["targets"] = w.targets;
    j["color"] = json::array({w.color.x, w.color.y, w.color.z});
    for (std::size_t k = 0; k < kFloatCount; ++k) {
        j[kFloats[k].key] = w.*(kFloats[k].field);
    }
    return j;
}

MeshWireParameters registerMeshWireParameters(params::ParameterSet& params, const MeshWire& rest,
                                              const std::string& prefix) {
    MeshWireParameters out;
    const std::string group = "Mesh Wire";
    for (std::size_t k = 0; k < kFloatCount; ++k) {
        const FloatKey& f = kFloats[k];
        params::ParamDesc<float> d;
        d.path = prefix + f.key;
        d.label = f.key;
        d.group = group;
        d.defaultValue = rest.*(f.field);
        d.hardMin = f.lo;
        d.hardMax = f.hi;
        d.softMin = f.softLo;
        d.softMax = f.softHi;
        out.all.push_back(&params.add(std::move(d)));
    }
    params::ParamDesc<glm::vec3> d;
    d.path = prefix + "color";
    d.label = "color";
    d.group = group;
    d.defaultValue = rest.color;
    d.hardMin = glm::vec3(0.0f);
    d.hardMax = glm::vec3(1.0f);
    d.softMin = glm::vec3(0.0f);
    d.softMax = glm::vec3(1.0f);
    d.isColor = true;
    out.all.push_back(&params.add(std::move(d)));
    return out;
}

void applyMeshWireParameters(const MeshWireParameters& p, const MeshWire& rest, MeshWire& live) {
    live = rest;
    for (std::size_t k = 0; k < kFloatCount && k < p.all.size(); ++k) {
        if (const auto* f = dynamic_cast<const params::Parameter<float>*>(p.all[k])) {
            live.*(kFloats[k].field) = f->value();
        }
    }
    if (p.all.size() == kFloatCount + 1) {
        if (const auto* c = dynamic_cast<const params::Parameter<glm::vec3>*>(p.all[kFloatCount])) {
            live.color = c->value();
        }
    }
}

} // namespace avgen::scene
