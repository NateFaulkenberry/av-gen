#include "scene/reflection_bands.hpp"

#include "params/parameter_set.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

namespace {

using json = nlohmann::json;

constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530718f;

Result<float> number(const json& j, const std::string& where, const char* key) {
    const json& v = j.at(key);
    if (!v.is_number() || !std::isfinite(v.get<double>())) {
        return fail("{} '{}' must be a finite number", where, key);
    }
    return v.get<float>();
}

Result<void> checkKeys(const json& j, const std::string& where, std::initializer_list<const char*> keys) {
    if (!j.is_object()) {
        return fail("{} must be an object", where);
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (std::find_if(keys.begin(), keys.end(), [&](const char* k) { return it.key() == k; }) == keys.end()) {
            std::string names;
            for (const char* k : keys) {
                names += names.empty() ? k : std::string(", ") + k;
            }
            return fail("{} has no key '{}' ({})", where, it.key(), names);
        }
    }
    return {};
}

// Reads `key` into `out` when present.
#define AVGEN_BAND_FLOAT(J, WHERE, KEY, OUT)                                                                    \
    if ((J).contains(KEY)) {                                                                                    \
        auto v_ = number((J), (WHERE), (KEY));                                                                  \
        if (!v_) {                                                                                              \
            return std::unexpected(v_.error());                                                                 \
        }                                                                                                       \
        (OUT) = *v_;                                                                                            \
    }

float smoothstep01(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float safeAtan2(float y, float x) { return std::abs(x) + std::abs(y) < 1e-12f ? 0.0f : std::atan2(y, x); }

glm::vec3 bandColor(float warmth) {
    return glm::mix(glm::vec3(0.80f, 0.90f, 1.08f), glm::vec3(1.08f, 0.93f, 0.78f), warmth);
}

} // namespace

Result<ReflectionBands> readReflectionBands(const json& j) {
    ReflectionBands b;
    b.enabled = true;
    const std::string where = "environment.bands";
    if (auto r = checkKeys(j, where, {"phase", "rotation", "gain", "softbox", "strips"}); !r) {
        return std::unexpected(r.error());
    }
    AVGEN_BAND_FLOAT(j, where, "phase", b.phase);
    AVGEN_BAND_FLOAT(j, where, "rotation", b.rotation);
    AVGEN_BAND_FLOAT(j, where, "gain", b.gain);
    if (b.gain < 0.0f) {
        return fail("{} 'gain' must be >= 0 (got {})", where, b.gain);
    }
    if (j.contains("softbox")) {
        const json& s = j.at("softbox");
        const std::string sw = where + ".softbox";
        if (auto r = checkKeys(s, sw, {"intensity", "azimuth", "elevation", "falloff", "skyFill"}); !r) {
            return std::unexpected(r.error());
        }
        AVGEN_BAND_FLOAT(s, sw, "intensity", b.softbox.intensity);
        AVGEN_BAND_FLOAT(s, sw, "azimuth", b.softbox.azimuth);
        AVGEN_BAND_FLOAT(s, sw, "elevation", b.softbox.elevation);
        AVGEN_BAND_FLOAT(s, sw, "falloff", b.softbox.falloff);
        AVGEN_BAND_FLOAT(s, sw, "skyFill", b.softbox.skyFill);
        if (b.softbox.intensity < 0.0f || b.softbox.skyFill < 0.0f || !(b.softbox.falloff > 0.0f)) {
            return fail("{}: intensity and skyFill must be >= 0 and falloff > 0", sw);
        }
    }
    if (j.contains("strips")) {
        const json& arr = j.at("strips");
        if (!arr.is_array()) {
            return fail("{} 'strips' must be an array", where);
        }
        if (arr.size() > static_cast<std::size_t>(kMaxReflectionBands)) {
            return fail("{} has {} strips; at most {} are drawn", where, arr.size(), kMaxReflectionBands);
        }
        for (std::size_t k = 0; k < arr.size(); ++k) {
            const json& s = arr[k];
            const std::string sw = where + ".strips[" + std::to_string(k) + "]";
            if (auto r = checkKeys(s, sw, {"axis", "offset", "width", "intensity", "segments", "warmth", "rate"}); !r) {
                return std::unexpected(r.error());
            }
            ReflectionBand band;
            if (s.contains("axis")) {
                const json& a = s.at("axis");
                if (!a.is_array() || a.size() != 3 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number()) {
                    return fail("{} 'axis' must be [x, y, z]", sw);
                }
                band.axis = glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
                if (!std::isfinite(band.axis.x) || !std::isfinite(band.axis.y) || !std::isfinite(band.axis.z) ||
                    glm::length(band.axis) < 1e-6f) {
                    return fail("{} 'axis' must be a finite, non-zero direction", sw);
                }
            }
            AVGEN_BAND_FLOAT(s, sw, "offset", band.offset);
            AVGEN_BAND_FLOAT(s, sw, "width", band.width);
            AVGEN_BAND_FLOAT(s, sw, "intensity", band.intensity);
            AVGEN_BAND_FLOAT(s, sw, "warmth", band.warmth);
            AVGEN_BAND_FLOAT(s, sw, "rate", band.rate);
            if (s.contains("segments")) {
                const json& v = s.at("segments");
                if (!v.is_number_integer() || v.get<int>() < 0 || v.get<int>() > 64) {
                    return fail("{} 'segments' must be an integer in 0..64", sw);
                }
                band.segments = v.get<int>();
            }
            if (!(band.width > 0.0f) || band.intensity < 0.0f || band.offset < -1.0f || band.offset > 1.0f) {
                return fail("{}: width must be > 0, intensity >= 0 and offset in -1..1", sw);
            }
            b.strips.push_back(band);
        }
    }
    return b;
}

#undef AVGEN_BAND_FLOAT

json reflectionBandsToJson(const ReflectionBands& b) {
    json j = json::object();
    j["phase"] = b.phase;
    j["rotation"] = b.rotation;
    j["gain"] = b.gain;
    j["softbox"] = json{{"intensity", b.softbox.intensity},
                        {"azimuth", b.softbox.azimuth},
                        {"elevation", b.softbox.elevation},
                        {"falloff", b.softbox.falloff},
                        {"skyFill", b.softbox.skyFill}};
    json strips = json::array();
    for (const ReflectionBand& s : b.strips) {
        strips.push_back(json{{"axis", {s.axis.x, s.axis.y, s.axis.z}},
                              {"offset", s.offset},
                              {"width", s.width},
                              {"intensity", s.intensity},
                              {"segments", s.segments},
                              {"warmth", s.warmth},
                              {"rate", s.rate}});
    }
    j["strips"] = std::move(strips);
    return j;
}

ReflectionBandParameters registerReflectionBandParameters(params::ParameterSet& params, const std::string& prefix,
                                                          const ReflectionBands& rest,
                                                          std::vector<params::IParameter*>* all) {
    ReflectionBandParameters out;
    if (!rest.enabled) {
        return out;
    }
    const auto add = [&](const std::string& key, float value, float lo, float hi, float softLo, float softHi) {
        params::ParamDesc<float> d;
        d.path = prefix + "bands/" + key;
        d.label = "bands/" + key;
        d.group = "Bands";
        d.defaultValue = value;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = softLo;
        d.softMax = softHi;
        auto& p = params.add(std::move(d));
        if (all != nullptr) {
            all->push_back(&p);
        }
        return &p;
    };
    out.phase = add("phase", rest.phase, -1.0e6f, 1.0e6f, 0.0f, 100.0f);
    out.rotation = add("rotation", rest.rotation, -1.0e4f, 1.0e4f, -kPi, kPi);
    out.gain = add("gain", rest.gain, 0.0f, 100.0f, 0.0f, 4.0f);
    out.softboxIntensity = add("softbox/intensity", rest.softbox.intensity, 0.0f, 100.0f, 0.0f, 2.0f);
    out.softboxAzimuth = add("softbox/azimuth", rest.softbox.azimuth, -1.0e4f, 1.0e4f, -kPi, kPi);
    out.softboxElevation = add("softbox/elevation", rest.softbox.elevation, -0.5f * kPi, 0.5f * kPi, -0.5f * kPi,
                               0.5f * kPi);
    for (std::size_t k = 0; k < rest.strips.size(); ++k) {
        const ReflectionBand& s = rest.strips[k];
        const std::string base = std::to_string(k) + "/";
        ReflectionBandParameters::Strip p;
        p.intensity = add(base + "intensity", s.intensity, 0.0f, 1000.0f, 0.0f, 8.0f);
        p.offset = add(base + "offset", s.offset, -1.0f, 1.0f, -1.0f, 1.0f);
        p.width = add(base + "width", s.width, 1e-4f, 1.0f, 0.005f, 0.2f);
        p.warmth = add(base + "warmth", s.warmth, 0.0f, 1.0f, 0.0f, 1.0f);
        out.strips.push_back(p);
    }
    return out;
}

void applyReflectionBandParameters(const ReflectionBandParameters& p, ReflectionBands& live) {
    const auto copy = [](const params::Parameter<float>* param, float& target) {
        if (param != nullptr) {
            target = param->value();
        }
    };
    copy(p.phase, live.phase);
    copy(p.rotation, live.rotation);
    copy(p.gain, live.gain);
    copy(p.softboxIntensity, live.softbox.intensity);
    copy(p.softboxAzimuth, live.softbox.azimuth);
    copy(p.softboxElevation, live.softbox.elevation);
    for (std::size_t k = 0; k < p.strips.size() && k < live.strips.size(); ++k) {
        copy(p.strips[k].intensity, live.strips[k].intensity);
        copy(p.strips[k].offset, live.strips[k].offset);
        copy(p.strips[k].width, live.strips[k].width);
        copy(p.strips[k].warmth, live.strips[k].warmth);
    }
}

ReflectionBandLanes packReflectionBands(const ReflectionBands& b) {
    ReflectionBandLanes l;
    if (!b.enabled) {
        return l;
    }
    const int count = std::min(static_cast<int>(b.strips.size()), kMaxReflectionBands);
    l.info = glm::vec4(static_cast<float>(count), b.phase, std::max(b.gain, 0.0f), b.rotation);
    const float ce = std::cos(b.softbox.elevation);
    const glm::vec3 key(ce * std::cos(b.softbox.azimuth), std::sin(b.softbox.elevation),
                        ce * std::sin(b.softbox.azimuth));
    l.soft = glm::vec4(key, std::max(b.softbox.intensity, 0.0f));
    l.soft2 = glm::vec4(std::max(b.softbox.falloff, 1e-3f), std::max(b.softbox.skyFill, 0.0f), 0.0f, 1.0f);
    for (int k = 0; k < count; ++k) {
        const ReflectionBand& s = b.strips[static_cast<std::size_t>(k)];
        const float len = glm::length(s.axis);
        const glm::vec3 axis = len > 1e-6f ? s.axis / len : glm::vec3(0.0f, 1.0f, 0.0f);
        l.bands[2 * k] = glm::vec4(axis, std::clamp(s.offset, -1.0f, 1.0f));
        l.bands[2 * k + 1] = glm::vec4(std::max(s.width, 1e-4f), std::max(s.intensity, 0.0f),
                                       static_cast<float>(std::max(s.segments, 0)), std::clamp(s.warmth, 0.0f, 1.0f));
        l.rate[k] = s.rate;
    }
    return l;
}

glm::vec3 reflectionBandRadiance(const ReflectionBandLanes& lanes, glm::vec3 dirIn, float alpha, bool softbox) {
    if (lanes.soft2.w < 0.5f) {
        return glm::vec3(0.0f);
    }
    // the rig turns by +rotation about Y, so the direction turns by -rotation into the rig's frame
    const float c = std::cos(lanes.info.w);
    const float s = std::sin(lanes.info.w);
    const glm::vec3 d(c * dirIn.x + s * dirIn.z, dirIn.y, -s * dirIn.x + c * dirIn.z);
    glm::vec3 sum(0.0f);
    const int count = static_cast<int>(lanes.info.x + 0.5f);
    for (int k = 0; k < count && k < kMaxReflectionBands; ++k) {
        const glm::vec4 a0 = lanes.bands[static_cast<std::size_t>(2 * k)];
        const glm::vec4 a1 = lanes.bands[static_cast<std::size_t>(2 * k + 1)];
        if (a1.y <= 0.0f) {
            continue;
        }
        const float x = glm::dot(d, glm::vec3(a0)) - a0.w;
        const float ww = a1.x * a1.x + alpha * alpha;
        float g = std::exp(-x * x / (2.0f * ww)) * a1.x / std::sqrt(ww);
        if (a1.z > 0.5f) {
            const glm::vec3 axis(a0);
            const glm::vec3 up = std::abs(axis.y) > 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 e1 = glm::normalize(glm::cross(up, axis));
            const glm::vec3 e2 = glm::cross(axis, e1);
            const float ph = safeAtan2(glm::dot(d, e2), glm::dot(d, e1));
            const float u = ph * a1.z / kTau + lanes.info.y * lanes.rate[k];
            const float sf = u - std::floor(u);
            const float soft = 0.04f + alpha;
            g *= smoothstep01(0.0f, soft, sf) * smoothstep01(0.0f, soft, 0.72f - sf);
        }
        sum += a1.y * g * bandColor(a1.w);
    }
    if (softbox) {
        const float lobe = std::exp((glm::dot(d, glm::vec3(lanes.soft)) - 1.0f) * lanes.soft2.x) * lanes.soft.w +
                           lanes.soft2.y * smoothstep01(-0.3f, 1.0f, d.y);
        sum += lobe * glm::vec3(0.92f, 0.95f, 1.0f);
    }
    return sum * lanes.info.z;
}

} // namespace avgen::scene
