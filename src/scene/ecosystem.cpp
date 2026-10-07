#include "scene/ecosystem.hpp"

#include "params/parameter_set.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <string_view>

namespace avgen::scene {

using nlohmann::json;

namespace {

bool finite(float v) { return std::isfinite(v); }
bool finite3(const glm::vec3& v) { return finite(v.x) && finite(v.y) && finite(v.z); }

std::uint64_t hashPoints(const std::vector<EmitterPoint>& pts) {
    std::uint64_t h = 1469598103934665603ull;
    const auto* bytes = reinterpret_cast<const unsigned char*>(pts.data());
    for (std::size_t i = 0; i < pts.size() * sizeof(EmitterPoint); ++i) {
        h = (h ^ bytes[i]) * 1099511628211ull;
    }
    return h ^ pts.size();
}

constexpr std::string_view kLayerKeys[] = {
    "name",       "enabled",      "hosts",       "template",    "color",       "excitedColor",
    "intensity",  "excitedIntensity", "size",    "responseField", "responseGain", "responseThreshold",
    "lagField",   "travel",       "wakeField",   "wakeGain",    "breath",      "breathRate",  "flicker",     "flickerRate",
    "pulseRate",  "pulseDecay",   "sparsity",    "maxDistance", "nearFade"};
constexpr std::string_view kBlockKeys[] = {"enabled", "spriteRadius", "maxSprites", "layers"};

template <std::size_t N>
Result<void> knownKeys(const json& j, const std::string_view (&keys)[N], std::string_view what) {
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (std::find(std::begin(keys), std::end(keys), it.key()) == std::end(keys)) {
            return fail("{}: unknown key '{}'", what, it.key());
        }
    }
    return {};
}

Result<glm::vec3> readVec3(const json& j, std::string_view key) {
    const json& a = j.at(std::string(key));
    if (!a.is_array() || a.size() != 3 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number()) {
        return fail("'{}' must be [r, g, b]", key);
    }
    return glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
}

} // namespace

Result<std::vector<EmitterPoint>> emitterTemplateFromJson(const json& j) {
    if (!j.is_object() || !j.contains("points") || !j.at("points").is_array()) {
        return fail("an emitter template is {{\"points\": [[x, y, z, v, u, radius], ...]}}");
    }
    const json& arr = j.at("points");
    if (arr.size() > kMaxTemplatePoints) {
        return fail("an emitter template holds at most {} points (this one has {})", kMaxTemplatePoints, arr.size());
    }
    std::vector<EmitterPoint> out;
    out.reserve(arr.size());
    for (const json& e : arr) {
        if (!e.is_array() || e.size() != 6) {
            return fail("each emitter template point is [x, y, z, v, u, radius]");
        }
        for (const json& c : e) {
            if (!c.is_number()) {
                return fail("emitter template point components must be numbers");
            }
        }
        EmitterPoint p;
        p.position = glm::vec3(e[0].get<float>(), e[1].get<float>(), e[2].get<float>());
        p.v = e[3].get<float>();
        p.u = e[4].get<float>();
        p.radius = e[5].get<float>();
        if (!finite3(p.position) || !finite(p.v) || !finite(p.u) || !finite(p.radius) || p.radius < 0.0f) {
            return fail("emitter template point has a non-finite value or a negative radius");
        }
        out.push_back(p);
    }
    return out;
}

Result<std::vector<EmitterPoint>> loadEmitterTemplate(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) {
        return fail("cannot open emitter template '{}'", file.string());
    }
    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        return fail("emitter template '{}': {}", file.string(), e.what());
    }
    auto pts = emitterTemplateFromJson(j);
    if (!pts) {
        return fail("emitter template '{}': {}", file.string(), pts.error().message);
    }
    return pts;
}

Result<void> EmitterLayer::validate() const {
    if (name.empty() || name.find('/') != std::string::npos) {
        return fail("ecosystem layer: a name is required and may not contain '/'");
    }
    if (hosts.empty()) {
        return fail("ecosystem layer '{}': needs at least one host (a procedural node name)", name);
    }
    if (templatePath.empty()) {
        return fail("ecosystem layer '{}': needs a template", name);
    }
    const float scalars[] = {intensity, excitedIntensity, size, responseGain, responseThreshold, travel, wakeGain, breath,
                             breathRate, flicker, flickerRate, pulseRate, pulseDecay, sparsity, maxDistance, nearFade};
    for (float s : scalars) {
        if (!finite(s)) {
            return fail("ecosystem layer '{}': a value is not finite", name);
        }
    }
    if (!finite3(color) || !finite3(excitedColor)) {
        return fail("ecosystem layer '{}': colours must be finite", name);
    }
    if (intensity < 0.0f || excitedIntensity < 0.0f || size <= 0.0f || maxDistance <= 0.0f || nearFade < 0.0f ||
        breathRate < 0.0f ||
        flickerRate < 0.0f || pulseRate < 0.0f || pulseDecay <= 0.0f) {
        return fail("ecosystem layer '{}': intensities and rates must be >= 0; size, maxDistance and pulseDecay > 0",
                    name);
    }
    if (travel < 0.0f || travel > 1.0f || breath < 0.0f || breath > 1.0f || flicker < 0.0f || flicker > 1.0f ||
        sparsity < 0.0f || sparsity > 1.0f) {
        return fail("ecosystem layer '{}': travel, breath, flicker and sparsity are 0..1", name);
    }
    return {};
}

bool Ecosystem::active() const {
    if (!enabled) {
        return false;
    }
    return std::any_of(layers.begin(), layers.end(),
                       [](const EmitterLayer& l) { return l.enabled && l.points && !l.points->empty(); });
}

Result<void> Ecosystem::validate() const {
    if (layers.size() > kMaxEcosystemLayers) {
        return fail("ecosystem: at most {} layers", kMaxEcosystemLayers);
    }
    if (!finite(spriteRadius) || spriteRadius < 0.5f || spriteRadius > 64.0f) {
        return fail("ecosystem: spriteRadius must be 0.5..64 pixels");
    }
    if (maxSprites > (1u << 22)) {
        return fail("ecosystem: maxSprites must be at most {}", 1u << 22);
    }
    std::set<std::string> names;
    for (const EmitterLayer& l : layers) {
        if (auto v = l.validate(); !v) {
            return v;
        }
        if (!names.insert(l.name).second) {
            return fail("ecosystem: two layers are named '{}'", l.name);
        }
    }
    return {};
}

json Ecosystem::toJson() const {
    json j = json::object();
    j["enabled"] = enabled;
    j["spriteRadius"] = spriteRadius;
    j["maxSprites"] = maxSprites;
    json arr = json::array();
    for (const EmitterLayer& l : layers) {
        json e = json::object();
        e["name"] = l.name;
        e["enabled"] = l.enabled;
        e["hosts"] = l.hosts;
        e["template"] = l.templatePath;
        e["color"] = {l.color.x, l.color.y, l.color.z};
        e["excitedColor"] = {l.excitedColor.x, l.excitedColor.y, l.excitedColor.z};
        e["intensity"] = l.intensity;
        e["excitedIntensity"] = l.excitedIntensity;
        e["size"] = l.size;
        e["responseField"] = l.responseField;
        e["responseGain"] = l.responseGain;
        e["responseThreshold"] = l.responseThreshold;
        e["lagField"] = l.lagField;
        e["travel"] = l.travel;
        e["wakeField"] = l.wakeField;
        e["wakeGain"] = l.wakeGain;
        e["breath"] = l.breath;
        e["breathRate"] = l.breathRate;
        e["flicker"] = l.flicker;
        e["flickerRate"] = l.flickerRate;
        e["pulseRate"] = l.pulseRate;
        e["pulseDecay"] = l.pulseDecay;
        e["sparsity"] = l.sparsity;
        e["maxDistance"] = l.maxDistance;
        e["nearFade"] = l.nearFade;
        arr.push_back(std::move(e));
    }
    j["layers"] = std::move(arr);
    return j;
}

Result<Ecosystem> Ecosystem::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("'ecosystem' must be an object");
    }
    if (auto k = knownKeys(j, kBlockKeys, "ecosystem"); !k) {
        return std::unexpected(k.error());
    }
    Ecosystem eco;
    try {
        eco.enabled = j.value("enabled", eco.enabled);
        eco.spriteRadius = j.value("spriteRadius", eco.spriteRadius);
        eco.maxSprites = j.value("maxSprites", eco.maxSprites);
        if (j.contains("layers")) {
            const json& arr = j.at("layers");
            if (!arr.is_array()) {
                return fail("'ecosystem.layers' must be an array");
            }
            for (const json& e : arr) {
                if (!e.is_object()) {
                    return fail("an ecosystem layer must be an object");
                }
                if (auto k = knownKeys(e, kLayerKeys, "ecosystem layer"); !k) {
                    return std::unexpected(k.error());
                }
                EmitterLayer l;
                l.name = e.value("name", std::string());
                l.enabled = e.value("enabled", l.enabled);
                if (e.contains("hosts")) {
                    const json& h = e.at("hosts");
                    if (h.is_string()) {
                        l.hosts.push_back(h.get<std::string>());
                    } else if (h.is_array()) {
                        for (const json& s : h) {
                            if (!s.is_string()) {
                                return fail("ecosystem layer '{}': hosts are names", l.name);
                            }
                            l.hosts.push_back(s.get<std::string>());
                        }
                    } else {
                        return fail("ecosystem layer '{}': 'hosts' is a name or an array of names", l.name);
                    }
                }
                l.templatePath = e.value("template", std::string());
                if (e.contains("color")) {
                    auto c = readVec3(e, "color");
                    if (!c) return std::unexpected(c.error());
                    l.color = *c;
                    l.excitedColor = *c;
                }
                if (e.contains("excitedColor")) {
                    auto c = readVec3(e, "excitedColor");
                    if (!c) return std::unexpected(c.error());
                    l.excitedColor = *c;
                }
                l.intensity = e.value("intensity", l.intensity);
                l.excitedIntensity = e.value("excitedIntensity", l.excitedIntensity);
                l.size = e.value("size", l.size);
                l.responseField = e.value("responseField", l.responseField);
                l.responseGain = e.value("responseGain", l.responseGain);
                l.responseThreshold = e.value("responseThreshold", l.responseThreshold);
                l.lagField = e.value("lagField", l.lagField);
                l.travel = e.value("travel", l.travel);
                l.wakeField = e.value("wakeField", l.wakeField);
                l.wakeGain = e.value("wakeGain", l.wakeGain);
                l.breath = e.value("breath", l.breath);
                l.breathRate = e.value("breathRate", l.breathRate);
                l.flicker = e.value("flicker", l.flicker);
                l.flickerRate = e.value("flickerRate", l.flickerRate);
                l.pulseRate = e.value("pulseRate", l.pulseRate);
                l.pulseDecay = e.value("pulseDecay", l.pulseDecay);
                l.sparsity = e.value("sparsity", l.sparsity);
                l.maxDistance = e.value("maxDistance", l.maxDistance);
                l.nearFade = e.value("nearFade", l.nearFade);
                eco.layers.push_back(std::move(l));
            }
        }
    } catch (const json::exception& ex) {
        return fail("ecosystem: {}", ex.what());
    }
    if (auto v = eco.validate(); !v) {
        return std::unexpected(v.error());
    }
    return eco;
}

Result<void> Ecosystem::loadTemplates(const std::filesystem::path& base) {
    for (EmitterLayer& l : layers) {
        std::filesystem::path file = l.templatePath;
        if (file.is_relative()) {
            file = base / file;
        }
        auto pts = loadEmitterTemplate(file);
        if (!pts) {
            return fail("ecosystem layer '{}': {}", l.name, pts.error().message);
        }
        l.pointsHash = hashPoints(*pts);
        l.points = std::make_shared<const std::vector<EmitterPoint>>(std::move(*pts));
    }
    return {};
}

// ---- parameters ----------------------------------------------------------------------------------------------------

namespace {

struct Reg {
    params::ParameterSet& params;
    EcosystemParameters::Layer& out;
    std::string group;

    void f(const char* rel, const char* label, float def, float lo, float hi, float slo, float shi) {
        params::ParamDesc<float> d;
        d.path = out.prefix + rel;
        d.label = label;
        d.group = group;
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = slo;
        d.softMax = shi;
        out.all.push_back(&params.add(std::move(d)));
    }
    void c(const char* rel, const char* label, glm::vec3 def) {
        params::ParamDesc<glm::vec3> d;
        d.path = out.prefix + rel;
        d.label = label;
        d.group = group;
        d.defaultValue = def;
        d.hardMin = glm::vec3(0.0f);
        d.hardMax = glm::vec3(64.0f);
        d.softMin = glm::vec3(0.0f);
        d.softMax = glm::vec3(1.0f);
        d.isColor = true;
        out.all.push_back(&params.add(std::move(d)));
    }
    void b(const char* rel, const char* label, bool def) {
        params::ParamDesc<bool> d;
        d.path = out.prefix + rel;
        d.label = label;
        d.group = group;
        d.defaultValue = def;
        d.hardMin = false;
        d.hardMax = true;
        d.softMin = false;
        d.softMax = true;
        out.all.push_back(&params.add(std::move(d)));
    }
};

template <typename T>
void copy(const EcosystemParameters::Layer& p, std::string_view leaf, T& target) {
    const std::size_t n = p.prefix.size();
    for (params::IParameter* ip : p.all) {
        const std::string& path = ip->path();
        if (path.size() == n + leaf.size() && path.compare(n, leaf.size(), leaf) == 0) {
            if (auto* tp = dynamic_cast<params::Parameter<T>*>(ip)) {
                target = tp->value();
            }
            return;
        }
    }
}

} // namespace

EcosystemParameters registerEcosystemParameters(params::ParameterSet& params, const Ecosystem& rest,
                                                const std::string& prefix) {
    EcosystemParameters p;
    for (const EmitterLayer& l : rest.layers) {
        EcosystemParameters::Layer layer;
        layer.prefix = prefix + l.name + "/";
        std::string group = prefix + l.name;
        Reg r{params, layer, group};
        r.b("enabled", "on", l.enabled);
        r.c("color", "colour at rest", l.color);
        r.c("excitedColor", "colour when excited (fluorescence)", l.excitedColor);
        r.f("intensity", "rest glow (x scene white)", l.intensity, 0.0f, 1000.0f, 0.0f, 4.0f);
        r.f("excitedIntensity", "glow at full response", l.excitedIntensity, 0.0f, 10000.0f, 0.0f, 40.0f);
        r.f("size", "point size (x template radius)", l.size, 0.01f, 100.0f, 0.1f, 4.0f);
        r.f("responseGain", "response gain", l.responseGain, 0.0f, 1000.0f, 0.0f, 8.0f);
        r.f("responseThreshold", "species threshold", l.responseThreshold, -10.0f, 10.0f, 0.0f, 1.0f);
        r.f("travel", "light travels along the organism", l.travel, 0.0f, 1.0f, 0.0f, 1.0f);
        r.f("wakeGain", "an awakened region glows (x wake)", l.wakeGain, 0.0f, 1000.0f, 0.0f, 10.0f);
        r.f("breath", "autonomous breathing depth", l.breath, 0.0f, 1.0f, 0.0f, 1.0f);
        r.f("breathRate", "breathing rate (Hz)", l.breathRate, 0.0f, 20.0f, 0.0f, 1.0f);
        r.f("flicker", "twinkle depth", l.flicker, 0.0f, 1.0f, 0.0f, 1.0f);
        r.f("flickerRate", "twinkle rate (Hz)", l.flickerRate, 0.0f, 60.0f, 0.0f, 20.0f);
        r.f("pulseRate", "spontaneous flashes per point per minute", l.pulseRate, 0.0f, 600.0f, 0.0f, 10.0f);
        r.f("pulseDecay", "spontaneous flash fade (s)", l.pulseDecay, 0.01f, 60.0f, 0.05f, 5.0f);
        r.f("sparsity", "dark at rest", l.sparsity, 0.0f, 1.0f, 0.0f, 1.0f);
        r.f("maxDistance", "visible to (m)", l.maxDistance, 1.0f, 100000.0f, 10.0f, 2000.0f);
        r.f("nearFade", "fades out nearer than (m)", l.nearFade, 0.0f, 1000.0f, 0.0f, 20.0f);
        p.layers.push_back(std::move(layer));
    }
    return p;
}

void applyEcosystemParameters(const EcosystemParameters& p, const Ecosystem& rest, Ecosystem& live) {
    for (std::size_t i = 0; i < p.layers.size() && i < rest.layers.size() && i < live.layers.size(); ++i) {
        const auto& lp = p.layers[i];
        EmitterLayer& l = live.layers[i];
        copy(lp, "enabled", l.enabled);
        copy(lp, "color", l.color);
        copy(lp, "excitedColor", l.excitedColor);
        copy(lp, "intensity", l.intensity);
        copy(lp, "excitedIntensity", l.excitedIntensity);
        copy(lp, "size", l.size);
        copy(lp, "responseGain", l.responseGain);
        copy(lp, "responseThreshold", l.responseThreshold);
        copy(lp, "travel", l.travel);
        copy(lp, "wakeGain", l.wakeGain);
        copy(lp, "breath", l.breath);
        copy(lp, "breathRate", l.breathRate);
        copy(lp, "flicker", l.flicker);
        copy(lp, "flickerRate", l.flickerRate);
        copy(lp, "pulseRate", l.pulseRate);
        copy(lp, "pulseDecay", l.pulseDecay);
        copy(lp, "sparsity", l.sparsity);
        copy(lp, "maxDistance", l.maxDistance);
        copy(lp, "nearFade", l.nearFade);
    }
}

} // namespace avgen::scene
