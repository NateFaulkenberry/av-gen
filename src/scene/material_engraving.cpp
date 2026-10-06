#include "scene/material_engraving.hpp"

#include "params/parameter_set.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

namespace {

using json = nlohmann::json;

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

Result<void> readNumber(const json& j, const std::string& where, const char* key, float& out) {
    if (!j.contains(key)) {
        return {};
    }
    const json& v = j.at(key);
    if (!v.is_number() || !std::isfinite(v.get<double>())) {
        return fail("{} '{}' must be a finite number", where, key);
    }
    out = v.get<float>();
    return {};
}

Result<void> readVec3(const json& j, const std::string& where, const char* key, glm::vec3& out) {
    if (!j.contains(key)) {
        return {};
    }
    const json& a = j.at(key);
    if (!a.is_array() || a.size() != 3 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number()) {
        return fail("{} '{}' must be [x, y, z]", where, key);
    }
    out = glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
    if (!std::isfinite(out.x) || !std::isfinite(out.y) || !std::isfinite(out.z)) {
        return fail("{} '{}' must be finite", where, key);
    }
    return {};
}

float smoothstep01(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float safeAtan2(float y, float x) { return std::abs(x) + std::abs(y) < 1e-12f ? 0.0f : std::atan2(y, x); }

} // namespace

const char* engravingFamilyName(EngravingFamily f) {
    switch (f) {
    case EngravingFamily::Rosette: return "rosette";
    case EngravingFamily::Contour: return "contour";
    case EngravingFamily::Engine: return "engine";
    }
    return "rosette";
}

#define AVGEN_TRY(EXPR)                                                                                         \
    if (auto r_ = (EXPR); !r_) {                                                                                \
        return std::unexpected(r_.error());                                                                     \
    }

Result<Engraving> readEngraving(const json& j) {
    Engraving e;
    const std::string where = "engraving";
    AVGEN_TRY(checkKeys(j, where, {"depth", "crawl", "grating", "spacing", "panels", "layers"}));
    AVGEN_TRY(readNumber(j, where, "depth", e.depth));
    AVGEN_TRY(readNumber(j, where, "crawl", e.crawl));
    AVGEN_TRY(readNumber(j, where, "grating", e.grating));
    AVGEN_TRY(readNumber(j, where, "spacing", e.spacing));
    AVGEN_TRY(readNumber(j, where, "panels", e.panels));
    if (e.depth < 0.0f || e.grating < 0.0f || e.panels < 0.0f || !(e.spacing > 0.0f)) {
        return fail("{}: depth, grating and panels must be >= 0 and spacing > 0", where);
    }
    if (!j.contains("layers")) {
        return fail("{} needs 'layers' (at least one rosette, contour or engine layer)", where);
    }
    const json& arr = j.at("layers");
    if (!arr.is_array() || arr.empty()) {
        return fail("{} 'layers' must be a non-empty array", where);
    }
    if (arr.size() > static_cast<std::size_t>(kMaxEngravingLayers)) {
        return fail("{} has {} layers; at most {} are cut", where, arr.size(), kMaxEngravingLayers);
    }
    for (std::size_t k = 0; k < arr.size(); ++k) {
        const json& l = arr[k];
        const std::string lw = where + ".layers[" + std::to_string(k) + "]";
        AVGEN_TRY(checkKeys(l, lw,
                            {"family", "center", "axis", "petals", "frequency", "weight", "depth", "inner", "outer"}));
        EngravingLayer layer;
        if (!l.contains("family") || !l.at("family").is_string()) {
            return fail("{} needs 'family' (rosette, contour or engine)", lw);
        }
        const std::string fam = l.at("family").get<std::string>();
        if (fam == "rosette") {
            layer.family = EngravingFamily::Rosette;
        } else if (fam == "contour") {
            layer.family = EngravingFamily::Contour;
            layer.petals = 7.0f;
        } else if (fam == "engine") {
            layer.family = EngravingFamily::Engine;
            layer.petals = 1.0f;
        } else {
            return fail("{}: unknown family '{}' (expected rosette, contour or engine)", lw, fam);
        }
        AVGEN_TRY(readVec3(l, lw, "center", layer.center));
        AVGEN_TRY(readVec3(l, lw, "axis", layer.axis));
        AVGEN_TRY(readNumber(l, lw, "petals", layer.petals));
        AVGEN_TRY(readNumber(l, lw, "frequency", layer.frequency));
        AVGEN_TRY(readNumber(l, lw, "weight", layer.weight));
        AVGEN_TRY(readNumber(l, lw, "depth", layer.depth));
        AVGEN_TRY(readNumber(l, lw, "inner", layer.inner));
        AVGEN_TRY(readNumber(l, lw, "outer", layer.outer));
        if (glm::length(layer.axis) < 1e-6f) {
            return fail("{} 'axis' must be a non-zero direction", lw);
        }
        if (!(layer.frequency > 0.0f) || layer.weight < 0.0f || layer.weight > 1.0f || layer.petals < 0.0f) {
            return fail("{}: frequency must be > 0, weight in 0..1 and petals >= 0", lw);
        }
        if (layer.inner < 0.0f || !(layer.outer > layer.inner)) {
            return fail("{}: inner must be >= 0 and outer > inner", lw);
        }
        e.layers.push_back(layer);
    }
    return e;
}

#undef AVGEN_TRY

json engravingToJson(const Engraving& e) {
    json j = json::object();
    j["depth"] = e.depth;
    j["crawl"] = e.crawl;
    j["grating"] = e.grating;
    j["spacing"] = e.spacing;
    j["panels"] = e.panels;
    json layers = json::array();
    for (const EngravingLayer& l : e.layers) {
        layers.push_back(json{{"family", engravingFamilyName(l.family)},
                              {"center", {l.center.x, l.center.y, l.center.z}},
                              {"axis", {l.axis.x, l.axis.y, l.axis.z}},
                              {"petals", l.petals},
                              {"frequency", l.frequency},
                              {"weight", l.weight},
                              {"depth", l.depth},
                              {"inner", l.inner},
                              {"outer", l.outer}});
    }
    j["layers"] = std::move(layers);
    return j;
}

EngravingParameters registerEngravingParameters(params::ParameterSet& params, const std::string& prefix,
                                                const std::string& group, const Engraving& rest,
                                                std::vector<params::IParameter*>* all) {
    EngravingParameters out;
    if (!rest.enabled()) {
        return out;
    }
    const auto add = [&](const std::string& key, float value, float lo, float hi, float softLo, float softHi) {
        params::ParamDesc<float> d;
        d.path = prefix + "material/engraving/" + key;
        d.label = "material/engraving/" + key;
        d.group = group;
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
    out.depth = add("depth", rest.depth, 0.0f, 4.0f, 0.0f, 1.0f);
    out.crawl = add("crawl", rest.crawl, -100.0f, 100.0f, -0.5f, 0.5f);
    out.grating = add("grating", rest.grating, 0.0f, 20.0f, 0.0f, 3.0f);
    out.spacing = add("spacing", rest.spacing, 100.0f, 20000.0f, 600.0f, 4000.0f);
    out.panels = add("panels", rest.panels, 0.0f, 100.0f, 0.0f, 4.0f);
    for (std::size_t k = 0; k < rest.layers.size(); ++k) {
        const std::string base = std::to_string(k) + "/";
        EngravingParameters::Layer p;
        p.frequency = add(base + "frequency", rest.layers[k].frequency, 1e-3f, 1.0e4f, 0.5f, 60.0f);
        p.weight = add(base + "weight", rest.layers[k].weight, 0.0f, 1.0f, 0.0f, 1.0f);
        out.layers.push_back(p);
    }
    return out;
}

void applyEngravingParameters(const EngravingParameters& p, Engraving& live) {
    const auto copy = [](const params::Parameter<float>* param, float& target) {
        if (param != nullptr) {
            target = param->value();
        }
    };
    copy(p.depth, live.depth);
    copy(p.crawl, live.crawl);
    copy(p.grating, live.grating);
    copy(p.spacing, live.spacing);
    copy(p.panels, live.panels);
    for (std::size_t k = 0; k < p.layers.size() && k < live.layers.size(); ++k) {
        copy(p.layers[k].frequency, live.layers[k].frequency);
        copy(p.layers[k].weight, live.layers[k].weight);
    }
}

EngravingUv engravingUv(const EngravingLayer& layer, glm::vec3 q, float crawlPhase) {
    EngravingUv out;
    const float f = std::max(layer.frequency, 1e-3f);
    switch (layer.family) {
    case EngravingFamily::Rosette: {
        const glm::vec3 e = q - layer.center;
        const float r = std::sqrt(e.x * e.x + e.y * e.y);
        out.u = safeAtan2(e.y, e.x) + crawlPhase;
        out.v = r;
        out.petals = layer.petals;
        out.angular = true;
        out.amp = smoothstep01(layer.inner, layer.inner + 6.0f / f, r);
        break;
    }
    case EngravingFamily::Contour: {
        const glm::vec3 d = q - layer.center;
        const float rxy = std::sqrt(d.x * d.x + d.y * d.y);
        out.u = safeAtan2(d.y, d.x) - crawlPhase * 0.3f;
        out.v = glm::length(d) - crawlPhase * 0.3f;
        out.petals = layer.petals;
        out.angular = true;
        out.amp = smoothstep01(2.0f / f, 8.0f / f, rxy);
        break;
    }
    case EngravingFamily::Engine: {
        const glm::vec3 a = glm::normalize(layer.axis);
        const glm::vec3 up = std::abs(a.y) > 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 b = glm::normalize(up - a * glm::dot(up, a));
        out.u = glm::dot(q, b) * 3.0f + crawlPhase * 2.0f;
        out.v = glm::dot(q, a);
        out.petals = 1.0f;
        out.angular = false;
        out.amp = 1.0f;
        break;
    }
    }
    return out;
}

// ---- ADR-1149: per-region temper and polish --------------------------------------------------------------

#define AVGEN_TRY(EXPR)                                                                                         \
    if (auto r_ = (EXPR); !r_) {                                                                                \
        return std::unexpected(r_.error());                                                                     \
    }

Result<SurfaceRegions> readSurfaceRegions(const json& j) {
    SurfaceRegions r;
    const std::string where = "regions";
    AVGEN_TRY(checkKeys(j, where, {"film", "filmNoise", "noiseScale", "polish", "points"}));
    AVGEN_TRY(readNumber(j, where, "film", r.film));
    AVGEN_TRY(readNumber(j, where, "filmNoise", r.filmNoise));
    AVGEN_TRY(readNumber(j, where, "noiseScale", r.noiseScale));
    AVGEN_TRY(readNumber(j, where, "polish", r.polish));
    if (r.film < 0.0f || r.filmNoise < 0.0f || !(r.noiseScale > 0.0f) || r.polish < 0.0f || r.polish > 1.0f) {
        return fail("{}: film and filmNoise must be >= 0, noiseScale > 0 and polish in 0..1", where);
    }
    if (j.contains("points")) {
        const json& arr = j.at("points");
        if (!arr.is_array()) {
            return fail("{} 'points' must be an array", where);
        }
        if (arr.size() > static_cast<std::size_t>(kMaxSurfaceRegions)) {
            return fail("{} has {} points; at most {} are read", where, arr.size(), kMaxSurfaceRegions);
        }
        for (std::size_t k = 0; k < arr.size(); ++k) {
            const json& p = arr[k];
            const std::string pw = where + ".points[" + std::to_string(k) + "]";
            AVGEN_TRY(checkKeys(p, pw, {"center", "scale", "sharpness", "weight"}));
            SurfaceRegion region;
            AVGEN_TRY(readVec3(p, pw, "center", region.center));
            AVGEN_TRY(readVec3(p, pw, "scale", region.scale));
            AVGEN_TRY(readNumber(p, pw, "sharpness", region.sharpness));
            AVGEN_TRY(readNumber(p, pw, "weight", region.weight));
            if (!(region.sharpness > 0.0f) || region.weight < 0.0f || region.weight > 1.0f) {
                return fail("{}: sharpness must be > 0 and weight in 0..1", pw);
            }
            r.points.push_back(region);
        }
    }
    if (!r.enabled()) {
        return fail("{} needs 'points' (or a filmNoise > 0): an empty block changes nothing", where);
    }
    return r;
}

#undef AVGEN_TRY

json surfaceRegionsToJson(const SurfaceRegions& r) {
    json j = json::object();
    j["film"] = r.film;
    j["filmNoise"] = r.filmNoise;
    j["noiseScale"] = r.noiseScale;
    j["polish"] = r.polish;
    json points = json::array();
    for (const SurfaceRegion& p : r.points) {
        points.push_back(json{{"center", {p.center.x, p.center.y, p.center.z}},
                              {"scale", {p.scale.x, p.scale.y, p.scale.z}},
                              {"sharpness", p.sharpness},
                              {"weight", p.weight}});
    }
    j["points"] = std::move(points);
    return j;
}

float surfaceRegionWeight(const SurfaceRegions& r, glm::vec3 q) {
    float fw = 0.0f;
    for (const SurfaceRegion& p : r.points) {
        const glm::vec3 d = (q - p.center) * p.scale;
        fw = std::max(fw, p.weight * std::exp(-p.sharpness * glm::dot(d, d)));
    }
    return fw;
}

SurfaceRegionParameters registerSurfaceRegionParameters(params::ParameterSet& params, const std::string& prefix,
                                                        const std::string& group, const SurfaceRegions& rest,
                                                        std::vector<params::IParameter*>* all) {
    SurfaceRegionParameters out;
    if (!rest.enabled()) {
        return out;
    }
    const auto add = [&](const std::string& key, float value, float lo, float hi, float softLo, float softHi) {
        params::ParamDesc<float> d;
        d.path = prefix + "material/regions/" + key;
        d.label = "material/regions/" + key;
        d.group = group;
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
    out.film = add("film", rest.film, 0.0f, 2000.0f, 0.0f, 400.0f);
    out.filmNoise = add("filmNoise", rest.filmNoise, 0.0f, 2000.0f, 0.0f, 200.0f);
    out.polish = add("polish", rest.polish, 0.0f, 1.0f, 0.0f, 1.0f);
    return out;
}

void applySurfaceRegionParameters(const SurfaceRegionParameters& p, SurfaceRegions& live) {
    if (p.film != nullptr) {
        live.film = p.film->value();
    }
    if (p.filmNoise != nullptr) {
        live.filmNoise = p.filmNoise->value();
    }
    if (p.polish != nullptr) {
        live.polish = p.polish->value();
    }
}

} // namespace avgen::scene
