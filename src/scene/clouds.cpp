#include "scene/clouds.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace avgen::scene {

using json = nlohmann::json;

namespace {

Result<glm::vec3> vec3Of(const json& j, const char* key) {
    if (!j.is_array() || j.size() != 3 || !j[0].is_number() || !j[1].is_number() || !j[2].is_number()) {
        return fail("clouds: '{}' must be an array of three numbers", key);
    }
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

Result<CloudLobe> lobeOf(const json& l) {
    if (!l.is_object() || !l.contains("c") || !l.contains("r")) {
        return fail("clouds: a lobe needs 'c' (centre) and 'r' (radius)");
    }
    CloudLobe lobe;
    auto c = vec3Of(l.at("c"), "c");
    if (!c) return std::unexpected(c.error());
    lobe.centre = *c;
    if (!l.at("r").is_number()) return fail("clouds: a lobe's 'r' must be a number");
    lobe.radius = l.at("r").get<float>();
    if (l.contains("scale")) {
        auto s = vec3Of(l.at("scale"), "scale");
        if (!s) return std::unexpected(s.error());
        lobe.scale = *s;
    }
    if (l.contains("density")) lobe.density = l.at("density").get<float>();
    if (l.contains("soft")) lobe.soft = l.at("soft").get<float>();
    if (!(lobe.radius > 0.0f) || !(lobe.scale.x > 0.0f && lobe.scale.y > 0.0f && lobe.scale.z > 0.0f) ||
        !(lobe.density >= 0.0f && lobe.density <= 1.0f) || !(lobe.soft > 0.0f && lobe.soft <= 1.0f) ||
        !std::isfinite(lobe.centre.x + lobe.centre.y + lobe.centre.z)) {
        return fail("clouds: a lobe has a radius or scale that is not positive, a density outside [0, 1] or a softness "
                    "outside (0, 1]");
    }
    return lobe;
}

} // namespace

Result<std::vector<CloudLobe>> readCloudLobes(const json& j, const std::vector<std::string>& masses) {
    std::vector<CloudLobe> out;
    auto addAll = [&](const json& arr) -> Result<void> {
        if (!arr.is_array()) return fail("clouds: a mass must be an array of lobes");
        for (const json& l : arr) {
            auto lobe = lobeOf(l);
            if (!lobe) return std::unexpected(lobe.error());
            out.push_back(*lobe);
        }
        return {};
    };
    if (j.is_array()) {
        if (auto r = addAll(j); !r) return std::unexpected(r.error());
    } else if (j.is_object() && j.contains("masses") && j.at("masses").is_object()) {
        const json& ms = j.at("masses");
        for (const std::string& want : masses) {
            if (!ms.contains(want)) return fail("clouds: the lobes file has no mass '{}'", want);
        }
        for (auto it = ms.begin(); it != ms.end(); ++it) {
            if (!masses.empty() && std::find(masses.begin(), masses.end(), it.key()) == masses.end()) continue;
            if (auto r = addAll(it.value()); !r) return std::unexpected(r.error());
        }
    } else {
        return fail("clouds: lobes must be an array, or an object with 'masses'");
    }
    if (out.size() > kMaxCloudLobes) {
        return fail("clouds: {} lobes, more than the {} the renderer holds", out.size(), kMaxCloudLobes);
    }
    return out;
}

Result<void> Clouds::validate() const {
    if (!(extinction > 0.0f && extinction <= 1.0f)) return fail("clouds: extinction must be in (0, 1] per metre");
    if (steps < 8 || steps > 512) return fail("clouds: steps must be in [8, 512]");
    if (lightSteps < 0 || lightSteps > 32) return fail("clouds: lightSteps must be in [0, 32]");
    if (!(lightDistance > 0.0f && lightDistance <= 20000.0f)) return fail("clouds: lightDistance must be in (0, 20000]");
    if (!(lightIntensity >= 0.0f && lightIntensity <= 100.0f)) return fail("clouds: lightIntensity must be in [0, 100]");
    if (!(anisotropy >= 0.0f && anisotropy < 0.95f)) return fail("clouds: anisotropy must be in [0, 0.95)");
    if (!(warmHeight > 0.0f)) return fail("clouds: warmHeight must be positive");
    if (!(noiseScale > 0.0f && noiseScale <= 1.0f)) return fail("clouds: noiseScale must be in (0, 1]");
    if (!(noiseAmount >= 0.0f && noiseAmount <= 1.0f)) return fail("clouds: noiseAmount must be in [0, 1]");
    if (!(hazeDistance > 0.0f)) return fail("clouds: hazeDistance must be positive");
    if (!(powder >= 0.0f && powder <= 1.0f)) return fail("clouds: powder must be in [0, 1]");
    if (glm::length(lightDirection) < 1e-6f || glm::length(warmDirection) < 1e-6f) {
        return fail("clouds: lightDirection and warmDirection must not be zero");
    }
    if (lobes.size() > kMaxCloudLobes) return fail("clouds: too many lobes");
    return {};
}

Result<Clouds> Clouds::fromJson(const json& j, const std::filesystem::path& base) {
    if (!j.is_object()) return fail("'clouds' must be an object");
    Clouds c;
    const auto num = [&](const char* key, float& target) -> Result<void> {
        if (!j.contains(key)) return {};
        if (!j.at(key).is_number()) return fail("clouds: '{}' must be a number", key);
        target = j.at(key).get<float>();
        return {};
    };
    const auto vec = [&](const char* key, glm::vec3& target) -> Result<void> {
        if (!j.contains(key)) return {};
        auto v = vec3Of(j.at(key), key);
        if (!v) return std::unexpected(v.error());
        target = *v;
        return {};
    };
    if (j.contains("enabled")) c.enabled = j.at("enabled").get<bool>();
    if (j.contains("masses")) c.masses = j.at("masses").get<std::vector<std::string>>();
    for (auto [k, t] : {std::pair{"extinction", &c.extinction}, std::pair{"lightDistance", &c.lightDistance},
                        std::pair{"lightIntensity", &c.lightIntensity}, std::pair{"anisotropy", &c.anisotropy},
                        std::pair{"warmHeight", &c.warmHeight}, std::pair{"noiseScale", &c.noiseScale},
                        std::pair{"noiseAmount", &c.noiseAmount}, std::pair{"hazeDistance", &c.hazeDistance},
                        std::pair{"powder", &c.powder}}) {
        if (auto r = num(k, *t); !r) return std::unexpected(r.error());
    }
    for (auto [k, t] : {std::pair{"lightDirection", &c.lightDirection}, std::pair{"lightColor", &c.lightColor},
                        std::pair{"ambientTop", &c.ambientTop}, std::pair{"ambientBottom", &c.ambientBottom},
                        std::pair{"warmColor", &c.warmColor}, std::pair{"warmDirection", &c.warmDirection},
                        std::pair{"hazeColor", &c.hazeColor}}) {
        if (auto r = vec(k, *t); !r) return std::unexpected(r.error());
    }
    if (j.contains("steps")) c.steps = j.at("steps").get<int>();
    if (j.contains("lightSteps")) c.lightSteps = j.at("lightSteps").get<int>();
    if (j.contains("seed")) c.seed = j.at("seed").get<std::uint32_t>();
    if (!j.contains("lobes")) return fail("clouds: 'lobes' (a lobes file path, or an array) is required");
    const json& lj = j.at("lobes");
    if (lj.is_string()) {
        c.source = lj.get<std::string>();
        const std::filesystem::path path = base / c.source;
        std::ifstream in(path);
        if (!in) return fail("clouds: cannot open lobes file '{}'", path.string());
        json file;
        try {
            in >> file;
        } catch (const std::exception& e) {
            return fail("clouds: lobes file '{}': {}", path.string(), e.what());
        }
        auto lobes = readCloudLobes(file, c.masses);
        if (!lobes) return fail("{} (file '{}')", lobes.error().message, path.string());
        c.lobes = std::move(*lobes);
    } else {
        auto lobes = readCloudLobes(lj, c.masses);
        if (!lobes) return std::unexpected(lobes.error());
        c.lobes = std::move(*lobes);
    }
    if (auto ok = c.validate(); !ok) return std::unexpected(ok.error());
    c.lightDirection = glm::normalize(c.lightDirection);
    c.warmDirection = glm::normalize(c.warmDirection);
    return c;
}

json Clouds::toJson() const {
    const auto v3 = [](const glm::vec3& v) { return json::array({v.x, v.y, v.z}); };
    json j = {{"enabled", enabled}, {"extinction", extinction}, {"steps", steps}, {"lightSteps", lightSteps},
              {"lightDistance", lightDistance}, {"lightDirection", v3(lightDirection)},
              {"lightColor", v3(lightColor)}, {"lightIntensity", lightIntensity}, {"anisotropy", anisotropy},
              {"ambientTop", v3(ambientTop)}, {"ambientBottom", v3(ambientBottom)}, {"warmColor", v3(warmColor)},
              {"warmDirection", v3(warmDirection)}, {"warmHeight", warmHeight}, {"noiseScale", noiseScale},
              {"noiseAmount", noiseAmount}, {"hazeColor", v3(hazeColor)}, {"hazeDistance", hazeDistance},
              {"powder", powder}, {"seed", seed}};
    if (!masses.empty()) j["masses"] = masses;
    if (!source.empty()) {
        j["lobes"] = source; // by reference, as authored
    } else {
        json arr = json::array();
        for (const CloudLobe& l : lobes) {
            arr.push_back({{"c", v3(l.centre)}, {"r", l.radius}, {"scale", v3(l.scale)}, {"density", l.density},
                           {"soft", l.soft}});
        }
        j["lobes"] = std::move(arr);
    }
    return j;
}

} // namespace avgen::scene
