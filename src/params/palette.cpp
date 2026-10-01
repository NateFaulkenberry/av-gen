#include "params/palette.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace avgen::params {

namespace {

std::optional<glm::vec3> readColor(const nlohmann::json& a) {
    if (!a.is_array() || a.size() != 3 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number()) {
        return std::nullopt;
    }
    return glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
}

ParamDesc<float> desc(const char* path, float def, float lo, float hi, float softLo, float softHi, const char* label) {
    ParamDesc<float> d;
    d.path = path;
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = softLo;
    d.softMax = softHi;
    d.label = label;
    d.group = "palette";
    return d;
}

} // namespace

glm::vec3 Palette::toOklab(const glm::vec3& c) {
    const float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    const float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    const float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;
    const float l_ = std::cbrt(l);
    const float m_ = std::cbrt(m);
    const float s_ = std::cbrt(s);
    return {0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
            1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
            0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_};
}

glm::vec3 Palette::fromOklab(const glm::vec3& lab) {
    const float l_ = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z;
    const float m_ = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z;
    const float s_ = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z;
    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;
    return {4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
}

namespace {

// The state a role is read from: the state itself, or the nearest one that defines it.
template <typename Map>
const typename Map::mapped_type* nearest(const std::vector<PaletteState>& states, std::size_t i, const std::string& role,
                                         Map PaletteState::*member) {
    for (std::size_t d = 0; d < states.size(); ++d) {
        for (const std::ptrdiff_t sign : {-1, 1}) {
            const std::ptrdiff_t j = static_cast<std::ptrdiff_t>(i) + sign * static_cast<std::ptrdiff_t>(d);
            if (j < 0 || j >= static_cast<std::ptrdiff_t>(states.size())) {
                continue;
            }
            const Map& map = states[static_cast<std::size_t>(j)].*member;
            if (auto it = map.find(role); it != map.end()) {
                return &it->second;
            }
        }
    }
    return nullptr;
}

struct Pick {
    std::size_t a = 0;
    std::size_t b = 0;
    float f = 0.0f;
};

Pick pick(std::size_t count, float position) {
    Pick p;
    if (count == 0) {
        return p;
    }
    const float x = std::clamp(std::isfinite(position) ? position : 0.0f, 0.0f, static_cast<float>(count - 1));
    p.a = std::min(static_cast<std::size_t>(std::floor(x)), count - 1);
    p.b = std::min(p.a + 1, count - 1);
    p.f = x - static_cast<float>(p.a);
    return p;
}

} // namespace

glm::vec3 Palette::color(const std::string& role, float pos, float sat, float val) const {
    const Pick p = pick(states.size(), pos);
    if (states.empty()) {
        return glm::vec3(0.0f);
    }
    const glm::vec3* ca = nearest(states, p.a, role, &PaletteState::colors);
    const glm::vec3* cb = nearest(states, p.b, role, &PaletteState::colors);
    if (ca == nullptr || cb == nullptr) {
        return glm::vec3(0.0f);
    }
    glm::vec3 lab = glm::mix(toOklab(glm::max(*ca, glm::vec3(0.0f))), toOklab(glm::max(*cb, glm::vec3(0.0f))), p.f);
    lab.x *= std::max(val, 0.0f);
    lab.y *= std::max(sat, 0.0f);
    lab.z *= std::max(sat, 0.0f);
    return glm::max(fromOklab(lab), glm::vec3(0.0f));
}

float Palette::scalar(const std::string& role, float pos) const {
    const Pick p = pick(states.size(), pos);
    if (states.empty()) {
        return 0.0f;
    }
    const float* a = nearest(states, p.a, role, &PaletteState::scalars);
    const float* b = nearest(states, p.b, role, &PaletteState::scalars);
    if (a == nullptr || b == nullptr) {
        return 0.0f;
    }
    return *a + (*b - *a) * p.f;
}

void Palette::attach(ParameterSet& params) {
    positionParam = &params.add(desc("palette/position", position, -1.0f, 1000.0f, 0.0f,
                                     std::max(1.0f, static_cast<float>(states.size()) - 1.0f), "palette/position"));
    saturationParam = &params.add(desc("palette/saturation", saturation, 0.0f, 4.0f, 0.0f, 2.0f, "palette/saturation"));
    valueParam = &params.add(desc("palette/value", value, 0.0f, 4.0f, 0.0f, 2.0f, "palette/value"));
}

void Palette::detach(ParameterSet& params) {
    params.remove("palette/position");
    params.remove("palette/saturation");
    params.remove("palette/value");
    positionParam = nullptr;
    saturationParam = nullptr;
    valueParam = nullptr;
}

void Palette::apply(ParameterSet& params) {
    if (states.empty()) {
        return;
    }
    const float pos = positionParam != nullptr ? positionParam->value() : position;
    const float sat = saturationParam != nullptr ? saturationParam->value() : saturation;
    const float val = valueParam != nullptr ? valueParam->value() : value;
    for (const PaletteBinding& b : bindings) {
        IParameter* target = params.find(b.target);
        if (target == nullptr) {
            if (std::find(unresolved_.begin(), unresolved_.end(), b.target) == unresolved_.end()) {
                unresolved_.push_back(b.target);
            }
            continue;
        }
        const bool isColor = std::any_of(states.begin(), states.end(),
                                         [&](const PaletteState& s) { return s.colors.count(b.role) != 0; });
        const std::size_t n = target->componentCount();
        if (isColor) {
            const glm::vec3 c = color(b.role, pos, sat, val) * b.gain;
            const auto write = [&](std::size_t i, float v) {
                target->setFinalComponent(i, b.mode == PaletteMode::Multiply ? target->finalComponent(i) * v : v);
            };
            if (n == 1) {
                // A scalar target reads one channel, or the colour's luminance when none is named.
                write(0, b.component >= 0 && b.component < 3 ? c[b.component]
                                                             : glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f)));
            } else {
                for (std::size_t i = 0; i < std::min<std::size_t>(n, 3); ++i) {
                    if (b.component < 0 || static_cast<std::size_t>(b.component) == i) {
                        write(i, c[static_cast<glm::length_t>(i)]);
                    }
                }
            }
        } else {
            const float v = scalar(b.role, pos) * b.gain;
            for (std::size_t i = 0; i < n; ++i) {
                if (b.component >= 0 && static_cast<std::size_t>(b.component) != i) {
                    continue;
                }
                target->setFinalComponent(i, b.mode == PaletteMode::Multiply ? target->finalComponent(i) * v : v);
            }
        }
    }
}

nlohmann::json Palette::toJson() const {
    nlohmann::json j;
    nlohmann::json st = nlohmann::json::array();
    for (const PaletteState& s : states) {
        nlohmann::json o;
        o["name"] = s.name;
        nlohmann::json colors = nlohmann::json::object();
        for (const auto& [role, c] : s.colors) {
            colors[role] = {c.r, c.g, c.b};
        }
        o["colors"] = std::move(colors);
        if (!s.scalars.empty()) {
            nlohmann::json scalars = nlohmann::json::object();
            for (const auto& [role, v] : s.scalars) {
                scalars[role] = v;
            }
            o["scalars"] = std::move(scalars);
        }
        st.push_back(std::move(o));
    }
    j["states"] = std::move(st);
    nlohmann::json bs = nlohmann::json::array();
    for (const PaletteBinding& b : bindings) {
        nlohmann::json o{{"role", b.role}, {"target", b.target}};
        if (b.component >= 0) {
            o["component"] = b.component;
        }
        if (b.gain != 1.0f) {
            o["gain"] = b.gain;
        }
        if (b.mode == PaletteMode::Multiply) {
            o["mode"] = "multiply";
        }
        bs.push_back(std::move(o));
    }
    j["bindings"] = std::move(bs);
    j["position"] = positionParam != nullptr ? positionParam->base() : position;
    j["saturation"] = saturationParam != nullptr ? saturationParam->base() : saturation;
    j["value"] = valueParam != nullptr ? valueParam->base() : value;
    return j;
}

Result<Palette> Palette::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("palette: must be an object");
    }
    Palette p;
    if (!j.contains("states") || !j["states"].is_array() || j["states"].empty()) {
        return fail("palette: 'states' must be a non-empty array");
    }
    for (const auto& sj : j["states"]) {
        if (!sj.is_object()) {
            return fail("palette: every state must be an object");
        }
        PaletteState s;
        s.name = sj.value("name", std::string{});
        if (sj.contains("colors")) {
            if (!sj["colors"].is_object()) {
                return fail("palette state '{}': 'colors' must be an object of role: [r, g, b]", s.name);
            }
            for (auto it = sj["colors"].begin(); it != sj["colors"].end(); ++it) {
                auto c = readColor(it.value());
                if (!c) {
                    return fail("palette state '{}': colour '{}' must be [r, g, b]", s.name, it.key());
                }
                s.colors[it.key()] = *c;
            }
        }
        if (sj.contains("scalars")) {
            if (!sj["scalars"].is_object()) {
                return fail("palette state '{}': 'scalars' must be an object of role: number", s.name);
            }
            for (auto it = sj["scalars"].begin(); it != sj["scalars"].end(); ++it) {
                if (!it.value().is_number()) {
                    return fail("palette state '{}': scalar '{}' must be a number", s.name, it.key());
                }
                s.scalars[it.key()] = it.value().get<float>();
            }
        }
        p.states.push_back(std::move(s));
    }
    if (j.contains("bindings")) {
        if (!j["bindings"].is_array()) {
            return fail("palette: 'bindings' must be an array");
        }
        for (const auto& bj : j["bindings"]) {
            if (!bj.is_object() || !bj.contains("role") || !bj["role"].is_string() || !bj.contains("target") ||
                !bj["target"].is_string()) {
                return fail("palette: every binding needs a 'role' and a 'target'");
            }
            PaletteBinding b;
            b.role = bj["role"].get<std::string>();
            b.target = bj["target"].get<std::string>();
            b.component = bj.value("component", -1);
            b.gain = bj.value("gain", 1.0f);
            const std::string mode = bj.value("mode", std::string("replace"));
            if (mode == "multiply") {
                b.mode = PaletteMode::Multiply;
            } else if (mode != "replace") {
                return fail("palette binding '{}': mode must be 'replace' or 'multiply'", b.target);
            }
            p.bindings.push_back(std::move(b));
        }
    }
    p.position = j.value("position", 0.0f);
    p.saturation = j.value("saturation", 1.0f);
    p.value = j.value("value", 1.0f);
    return p;
}

} // namespace avgen::params
