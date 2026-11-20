#include "scene/clouds.hpp"

#include "params/parameter_set.hpp"

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
    if (lobes.size() + farLobes.size() > kMaxCloudLobes) return fail("clouds: too many lobes");
    // ADR-2100
    if (sea.enabled) {
        if (!(sea.top > sea.base)) return fail("clouds: sea.top must be above sea.base");
        if (!(sea.coverage >= 0.0f && sea.coverage <= 1.0f)) return fail("clouds: sea.coverage must be in [0, 1]");
        if (!(sea.coverageScale > 0.0f && sea.coverageScale <= 1.0f)) {
            return fail("clouds: sea.coverageScale must be in (0, 1] cycles per metre");
        }
        if (!(sea.density >= 0.0f && sea.density <= 4.0f)) return fail("clouds: sea.density must be in [0, 4]");
        if (!(sea.relief >= 0.0f && sea.relief <= 2000.0f)) return fail("clouds: sea.relief must be in [0, 2000] metres");
        if (!(sea.reliefScale >= 50.0f && sea.reliefScale <= 100000.0f)) {
            return fail("clouds: sea.reliefScale must be in [50, 100000] metres");
        }
        if (!(sea.billow >= 0.0f && sea.billow <= 500.0f)) return fail("clouds: sea.billow must be in [0, 500] metres");
        if (!(sea.farFade >= 0.0f && sea.farFade <= 1.0f)) return fail("clouds: sea.farFade must be in [0, 1]");
        if (sea.breaks.size() > 2) return fail("clouds: at most 2 sea breaks");
        for (const CloudSea::Break& b : sea.breaks) {
            if (!(b.radius > 0.0f) || !(b.elongation >= 0.1f)) {
                return fail("clouds: a sea break needs a positive radius and an elongation of at least 0.1");
            }
        }
    }
    if (glows.size() > kMaxCloudGlows) return fail("clouds: {} glows, more than the {} the renderer holds", glows.size(), kMaxCloudGlows);
    for (const CloudGlow& g : glows) {
        if (!(g.radius > 0.0f) || !(g.intensity >= 0.0f) || !std::isfinite(g.position.x + g.position.y + g.position.z)) {
            return fail("clouds: a glow needs a positive radius, a non-negative intensity and a finite position");
        }
    }
    if (!(glowGain >= 0.0f && glowGain <= 100.0f)) return fail("clouds: glowGain must be in [0, 100]");
    if (!(glowOcclusion >= 0.0f && glowOcclusion <= 10.0f)) return fail("clouds: glowOcclusion must be in [0, 10]");
    if (!(glowBreakup >= 0.0f && glowBreakup <= 1.0f)) return fail("clouds: glowBreakup must be in [0, 1]"); // ADR-2104
    if (!(glowShadow >= 0.0f && glowShadow <= 4.0f)) return fail("clouds: glowShadow must be in [0, 4]");
    if (!(glowBreakupScale > 0.0f && glowBreakupScale <= 1.0f)) return fail("clouds: glowBreakupScale must be in (0, 1]");
    if (!(glowShadowDistance > 0.0f && glowShadowDistance <= 500.0f)) {
        return fail("clouds: glowShadowDistance must be in (0, 500]");
    }
    if (!(density >= 0.0f && density <= 8.0f)) return fail("clouds: density must be in [0, 8]");
    if (!(swell >= 0.0f && swell <= 1.0f)) return fail("clouds: swell must be in [0, 1]");
    if (!(detailScale > 0.0f && detailScale <= 4.0f)) return fail("clouds: detailScale must be in (0, 4]");
    if (!(detailAmount >= 0.0f && detailAmount <= 1.0f)) return fail("clouds: detailAmount must be in [0, 1]");
    if (!(resolution >= 0.25f && resolution <= 1.0f)) return fail("clouds: resolution must be in [0.25, 1]");
    if (!(temporal >= 0.0f && temporal <= 0.97f)) return fail("clouds: temporal must be in [0, 0.97]");
    if (!(minStep > 0.0f && minStep <= 1000.0f)) return fail("clouds: minStep must be in (0, 1000] metres");
    if (!(stepGrowth >= 0.0f && stepGrowth <= 0.5f)) return fail("clouds: stepGrowth must be in [0, 0.5]");
    if (!(maxDistance > 0.0f && maxDistance <= 200000.0f)) return fail("clouds: maxDistance must be in (0, 200000]");
    if (!std::isfinite(drift.x + drift.y + drift.z)) return fail("clouds: drift must be finite");
    // ADR-2102
    std::size_t arms = 0;
    for (const CloudBeam& b : beams) {
        if (b.count < 1 || b.count > static_cast<int>(kMaxCloudBeamArms)) {
            return fail("clouds: a beam's count must be in [1, {}]", kMaxCloudBeamArms);
        }
        arms += static_cast<std::size_t>(b.count);
        if (!(b.length > 0.0f && b.length <= 100000.0f) || !(b.halfAngle > 0.0f && b.halfAngle < 45.0f) ||
            !(b.radius >= 0.0f) || !(b.intensity >= 0.0f) || !std::isfinite(b.origin.x + b.origin.y + b.origin.z) ||
            !std::isfinite(b.yaw + b.elevation + b.rpm) || b.elevation < -90.0f || b.elevation > 90.0f) {
            return fail("clouds: a beam needs a length in (0, 100000], a halfAngle in (0, 45) degrees, a non-negative "
                        "radius and intensity, an elevation in [-90, 90] and a finite origin, yaw and rpm");
        }
    }
    if (arms > kMaxCloudBeamArms) {
        return fail("clouds: {} beam arms, more than the {} the renderer holds", arms, kMaxCloudBeamArms);
    }
    if (!(beamAir >= 0.0f && beamAir <= 100.0f) || !(beamCloud >= 0.0f && beamCloud <= 100.0f)) {
        return fail("clouds: beamAir and beamCloud must be in [0, 100]");
    }
    if (halo.enabled && (glm::length(halo.direction) < 1e-6f || !(halo.radius > 0.0f && halo.radius <= 90.0f) ||
                         !(halo.intensity >= 0.0f))) {
        return fail("clouds: a halo needs a non-zero direction, a radius in (0, 90] degrees and a non-negative "
                    "intensity");
    }
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
                        std::pair{"powder", &c.powder}, std::pair{"glowGain", &c.glowGain},
                        std::pair{"glowOcclusion", &c.glowOcclusion}, std::pair{"density", &c.density},
                        std::pair{"glowBreakup", &c.glowBreakup}, std::pair{"glowShadow", &c.glowShadow},
                        std::pair{"glowBreakupScale", &c.glowBreakupScale},
                        std::pair{"glowShadowDistance", &c.glowShadowDistance},
                        std::pair{"swell", &c.swell}, std::pair{"detailScale", &c.detailScale},
                        std::pair{"detailAmount", &c.detailAmount}, std::pair{"resolution", &c.resolution},
                        std::pair{"temporal", &c.temporal}, std::pair{"minStep", &c.minStep},
                        std::pair{"stepGrowth", &c.stepGrowth}, std::pair{"maxDistance", &c.maxDistance}}) {
        if (auto r = num(k, *t); !r) return std::unexpected(r.error());
    }
    for (auto [k, t] : {std::pair{"lightDirection", &c.lightDirection}, std::pair{"lightColor", &c.lightColor},
                        std::pair{"ambientTop", &c.ambientTop}, std::pair{"ambientBottom", &c.ambientBottom},
                        std::pair{"warmColor", &c.warmColor}, std::pair{"warmDirection", &c.warmDirection},
                        std::pair{"hazeColor", &c.hazeColor}, std::pair{"drift", &c.drift}}) {
        if (auto r = vec(k, *t); !r) return std::unexpected(r.error());
    }
    if (j.contains("steps")) c.steps = j.at("steps").get<int>();
    if (j.contains("lightSteps")) c.lightSteps = j.at("lightSteps").get<int>();
    if (j.contains("seed")) c.seed = j.at("seed").get<std::uint32_t>();
    // ADR-2100: the sea and the glows
    if (j.contains("sea")) {
        const json& sj = j.at("sea");
        if (!sj.is_object()) return fail("clouds: 'sea' must be an object");
        c.sea.enabled = sj.value("enabled", true);
        for (auto [k, t] : {std::pair{"base", &c.sea.base}, std::pair{"top", &c.sea.top},
                            std::pair{"coverage", &c.sea.coverage}, std::pair{"coverageScale", &c.sea.coverageScale},
                            std::pair{"density", &c.sea.density}, std::pair{"relief", &c.sea.relief},
                            std::pair{"reliefScale", &c.sea.reliefScale}, std::pair{"billow", &c.sea.billow},
                            std::pair{"farFade", &c.sea.farFade}}) {
            if (!sj.contains(k)) continue;
            if (!sj.at(k).is_number()) return fail("clouds: 'sea.{}' must be a number", k);
            *t = sj.at(k).get<float>();
        }
        if (sj.contains("breaks")) { // ADR-2101
            const json& bj = sj.at("breaks");
            if (!bj.is_array()) return fail("clouds: 'sea.breaks' must be an array");
            for (const json& b : bj) {
                if (!b.is_object() || !b.contains("centre") || !b.at("centre").is_array() || b.at("centre").size() != 2) {
                    return fail("clouds: a sea break needs 'centre': [x, z]");
                }
                CloudSea::Break br;
                br.centre = glm::vec2(b.at("centre")[0].get<float>(), b.at("centre")[1].get<float>());
                br.radius = b.value("radius", br.radius);
                br.elongation = b.value("elongation", br.elongation);
                br.yaw = b.value("yaw", br.yaw);
                c.sea.breaks.push_back(br);
            }
        }
    }
    if (j.contains("glows")) {
        const json& gj = j.at("glows");
        if (!gj.is_array()) return fail("clouds: 'glows' must be an array");
        for (const json& g : gj) {
            if (!g.is_object() || !g.contains("position")) return fail("clouds: a glow needs a 'position'");
            CloudGlow glow;
            auto pos = vec3Of(g.at("position"), "position");
            if (!pos) return std::unexpected(pos.error());
            glow.position = *pos;
            if (g.contains("color")) {
                auto col = vec3Of(g.at("color"), "color");
                if (!col) return std::unexpected(col.error());
                glow.color = *col;
            }
            if (g.contains("radius")) glow.radius = g.at("radius").get<float>();
            if (g.contains("intensity")) glow.intensity = g.at("intensity").get<float>();
            if (g.contains("name")) glow.name = g.at("name").get<std::string>();
            c.glows.push_back(glow);
        }
    }
    if (j.contains("beams")) { // ADR-2102
        const json& bj = j.at("beams");
        if (!bj.is_array()) return fail("clouds: 'beams' must be an array");
        for (const json& b : bj) {
            if (!b.is_object() || !b.contains("origin")) return fail("clouds: a beam needs an 'origin'");
            CloudBeam beam;
            auto o = vec3Of(b.at("origin"), "origin");
            if (!o) return std::unexpected(o.error());
            beam.origin = *o;
            for (const auto& [key, value] : b.items()) {
                if (key == "origin") continue;
                if (key == "name") {
                    beam.name = value.get<std::string>();
                } else if (key == "color") {
                    auto col = vec3Of(value, "color");
                    if (!col) return std::unexpected(col.error());
                    beam.color = *col;
                } else if (key == "count") {
                    if (!value.is_number_integer()) return fail("clouds: a beam's 'count' must be an integer");
                    beam.count = value.get<int>();
                } else {
                    float* t = key == "yaw"         ? &beam.yaw
                               : key == "elevation" ? &beam.elevation
                               : key == "rpm"       ? &beam.rpm
                               : key == "length"    ? &beam.length
                               : key == "halfAngle" ? &beam.halfAngle
                               : key == "radius"    ? &beam.radius
                               : key == "intensity" ? &beam.intensity
                                                    : nullptr;
                    if (t == nullptr) {
                        return fail("clouds: a beam has an unknown key '{}' (origin, count, yaw, elevation, rpm, length, "
                                    "halfAngle, radius, color, intensity, name)",
                                    key);
                    }
                    if (!value.is_number()) return fail("clouds: a beam's '{}' must be a number", key);
                    *t = value.get<float>();
                }
            }
            c.beams.push_back(beam);
        }
    }
    if (auto r = num("beamAir", c.beamAir); !r) return std::unexpected(r.error());
    if (auto r = num("beamCloud", c.beamCloud); !r) return std::unexpected(r.error());
    if (j.contains("halo")) { // ADR-2102
        const json& hj = j.at("halo");
        if (!hj.is_object()) return fail("clouds: 'halo' must be an object");
        c.halo.enabled = hj.value("enabled", true);
        if (hj.contains("direction")) {
            auto d = vec3Of(hj.at("direction"), "halo.direction");
            if (!d) return std::unexpected(d.error());
            c.halo.direction = *d;
        }
        if (hj.contains("color")) {
            auto col = vec3Of(hj.at("color"), "halo.color");
            if (!col) return std::unexpected(col.error());
            c.halo.color = *col;
        }
        c.halo.radius = hj.value("radius", c.halo.radius);
        c.halo.intensity = hj.value("intensity", c.halo.intensity);
    }
    if (!j.contains("lobes") && !c.sea.enabled) {
        return fail("clouds: 'lobes' (a lobes file path, or an array) is required unless there is a 'sea'");
    }
    const json lj = j.contains("lobes") ? j.at("lobes") : json::array();
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
    if (j.contains("farLobes")) { // ADR-2104
        auto far = readCloudLobes(j.at("farLobes"), {});
        if (!far) return fail("clouds: farLobes: {}", far.error().message);
        c.farLobes = std::move(*far);
    }
    if (auto ok = c.validate(); !ok) return std::unexpected(ok.error());
    c.lightDirection = glm::normalize(c.lightDirection);
    c.warmDirection = glm::normalize(c.warmDirection);
    if (c.halo.enabled) c.halo.direction = glm::normalize(c.halo.direction);
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
              {"powder", powder}, {"seed", seed}, {"glowGain", glowGain}, {"glowOcclusion", glowOcclusion},
              {"drift", v3(drift)}, {"density", density}, {"swell", swell}, {"detailScale", detailScale},
              {"detailAmount", detailAmount}, {"resolution", resolution}, {"temporal", temporal},
              {"minStep", minStep}, {"stepGrowth", stepGrowth}, {"maxDistance", maxDistance}};
    if (glowBreakup > 0.0f || glowShadow > 0.0f) { // ADR-2104: written only when used
        j["glowBreakup"] = glowBreakup;
        j["glowShadow"] = glowShadow;
        j["glowBreakupScale"] = glowBreakupScale;
        j["glowShadowDistance"] = glowShadowDistance;
    }
    if (sea.enabled) {
        j["sea"] = {{"enabled", true}, {"base", sea.base}, {"top", sea.top}, {"coverage", sea.coverage},
                    {"coverageScale", sea.coverageScale}, {"density", sea.density}};
        if (sea.relief > 0.0f) {
            j["sea"]["relief"] = sea.relief;
            j["sea"]["reliefScale"] = sea.reliefScale;
            j["sea"]["billow"] = sea.billow;
        }
        if (sea.farFade > 0.0f) j["sea"]["farFade"] = sea.farFade;
        if (!sea.breaks.empty()) {
            json bs = json::array();
            for (const CloudSea::Break& b : sea.breaks) {
                bs.push_back({{"centre", json::array({b.centre.x, b.centre.y})}, {"radius", b.radius},
                              {"elongation", b.elongation}, {"yaw", b.yaw}});
            }
            j["sea"]["breaks"] = std::move(bs);
        }
    }
    if (!glows.empty()) {
        json gs = json::array();
        for (const CloudGlow& g : glows) {
            json gj = {{"position", v3(g.position)}, {"color", v3(g.color)}, {"radius", g.radius},
                       {"intensity", g.intensity}};
            if (!g.name.empty()) gj["name"] = g.name;
            gs.push_back(std::move(gj));
        }
        j["glows"] = std::move(gs);
    }
    if (!beams.empty()) { // ADR-2102
        json bs = json::array();
        for (const CloudBeam& b : beams) {
            json bj = {{"origin", v3(b.origin)}, {"count", b.count},         {"yaw", b.yaw},
                       {"elevation", b.elevation}, {"rpm", b.rpm},           {"length", b.length},
                       {"halfAngle", b.halfAngle}, {"radius", b.radius},     {"color", v3(b.color)},
                       {"intensity", b.intensity}};
            if (!b.name.empty()) bj["name"] = b.name;
            bs.push_back(std::move(bj));
        }
        j["beams"] = std::move(bs);
        j["beamAir"] = beamAir;
        j["beamCloud"] = beamCloud;
    }
    if (halo.enabled) {
        j["halo"] = {{"enabled", true}, {"direction", v3(halo.direction)}, {"color", v3(halo.color)},
                     {"radius", halo.radius}, {"intensity", halo.intensity}};
    }
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
    if (!farLobes.empty()) { // ADR-2104
        json arr = json::array();
        for (const CloudLobe& l : farLobes) {
            arr.push_back({{"c", v3(l.centre)}, {"r", l.radius}, {"scale", v3(l.scale)}, {"density", l.density},
                           {"soft", l.soft}});
        }
        j["farLobes"] = std::move(arr);
    }
    return j;
}

// ADR-2100: the live knobs.
namespace {

struct CloudFloat {
    const char* key;
    float Clouds::*field;
    float lo, hi, softLo, softHi;
};

constexpr CloudFloat kCloudFloats[] = {
    {"density", &Clouds::density, 0.0f, 8.0f, 0.0f, 3.0f},
    {"swell", &Clouds::swell, 0.0f, 1.0f, 0.0f, 1.0f},
    {"lightIntensity", &Clouds::lightIntensity, 0.0f, 100.0f, 0.0f, 4.0f},
    {"glowGain", &Clouds::glowGain, 0.0f, 100.0f, 0.0f, 4.0f},
    {"glowOcclusion", &Clouds::glowOcclusion, 0.0f, 10.0f, 0.0f, 3.0f},
    {"glowBreakup", &Clouds::glowBreakup, 0.0f, 1.0f, 0.0f, 1.0f},   // ADR-2104
    {"glowShadow", &Clouds::glowShadow, 0.0f, 4.0f, 0.0f, 2.0f},
    {"noiseAmount", &Clouds::noiseAmount, 0.0f, 1.0f, 0.0f, 1.0f},
    {"detailAmount", &Clouds::detailAmount, 0.0f, 1.0f, 0.0f, 1.0f},
    {"hazeDistance", &Clouds::hazeDistance, 1.0f, 200000.0f, 1000.0f, 40000.0f},
    {"powder", &Clouds::powder, 0.0f, 1.0f, 0.0f, 1.0f},
};
constexpr std::size_t kCloudFloatCount = sizeof(kCloudFloats) / sizeof(kCloudFloats[0]);

struct CloudVector {
    const char* key;
    glm::vec3 Clouds::*field;
    bool colour;
};

constexpr CloudVector kCloudVectors[] = {
    {"lightColor", &Clouds::lightColor, true},   {"ambientTop", &Clouds::ambientTop, true},
    {"ambientBottom", &Clouds::ambientBottom, true}, {"warmColor", &Clouds::warmColor, true},
    {"hazeColor", &Clouds::hazeColor, true},     {"drift", &Clouds::drift, false},
};
constexpr std::size_t kCloudVectorCount = sizeof(kCloudVectors) / sizeof(kCloudVectors[0]);

// the sea's two, after the vectors: coverage and its density
constexpr std::size_t kCloudSeaCount = 2;

} // namespace

CloudParameters registerCloudParameters(params::ParameterSet& params, const Clouds& rest, const std::string& prefix) {
    CloudParameters out;
    const std::string group = "Clouds";
    const auto addFloat = [&](const std::string& path, float value, float lo, float hi, float softLo, float softHi) {
        params::ParamDesc<float> d;
        d.path = path;
        d.group = group;
        d.defaultValue = value;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = softLo;
        d.softMax = softHi;
        out.all.push_back(&params.add(std::move(d)));
    };
    const auto addVec = [&](const std::string& path, glm::vec3 value, bool colour) {
        params::ParamDesc<glm::vec3> d;
        d.path = path;
        d.group = group;
        d.defaultValue = value;
        d.hardMin = colour ? glm::vec3(0.0f) : glm::vec3(-1000.0f);
        d.hardMax = colour ? glm::vec3(100.0f) : glm::vec3(1000.0f);
        d.softMin = colour ? glm::vec3(0.0f) : glm::vec3(-50.0f);
        d.softMax = colour ? glm::vec3(1.0f) : glm::vec3(50.0f);
        d.isColor = colour;
        out.all.push_back(&params.add(std::move(d)));
    };
    for (const CloudFloat& f : kCloudFloats) {
        addFloat(prefix + f.key, rest.*(f.field), f.lo, f.hi, f.softLo, f.softHi);
    }
    out.floats = kCloudFloatCount;
    for (const CloudVector& v : kCloudVectors) {
        addVec(prefix + v.key, rest.*(v.field), v.colour);
    }
    out.vectors = kCloudVectorCount;
    addFloat(prefix + "sea/coverage", rest.sea.coverage, 0.0f, 1.0f, 0.0f, 1.0f);
    addFloat(prefix + "sea/density", rest.sea.density, 0.0f, 4.0f, 0.0f, 2.0f);
    for (std::size_t i = 0; i < rest.glows.size(); ++i) {
        const std::string base = prefix + "glow/" + std::to_string(i) + "/";
        addFloat(base + "intensity", rest.glows[i].intensity, 0.0f, 1000.0f, 0.0f, 20.0f);
        addVec(base + "color", rest.glows[i].color, true);
    }
    // ADR-2102
    addFloat(prefix + "beamAir", rest.beamAir, 0.0f, 100.0f, 0.0f, 4.0f);
    addFloat(prefix + "beamCloud", rest.beamCloud, 0.0f, 100.0f, 0.0f, 4.0f);
    addFloat(prefix + "halo/intensity", rest.halo.intensity, 0.0f, 1000.0f, 0.0f, 10.0f);
    for (std::size_t i = 0; i < rest.beams.size(); ++i) {
        addFloat(prefix + "beam/" + std::to_string(i) + "/intensity", rest.beams[i].intensity, 0.0f, 1000.0f, 0.0f,
                 20.0f);
    }
    return out;
}

glm::vec3 cloudBeamDirection(const CloudBeam& beam, int arm, double seconds) {
    const double turns = static_cast<double>(beam.rpm) / 60.0 * seconds;
    const double spread = static_cast<double>(arm) / static_cast<double>(std::max(beam.count, 1));
    const double a = (static_cast<double>(beam.yaw) / 360.0 + std::fmod(turns, 1.0) + spread) * 6.283185307179586;
    const double e = static_cast<double>(beam.elevation) * 0.017453292519943295;
    return glm::vec3(static_cast<float>(std::cos(e) * std::cos(a)), static_cast<float>(std::sin(e)),
                     static_cast<float>(-std::cos(e) * std::sin(a)));
}

void applyCloudParameters(const CloudParameters& p, const Clouds& rest, Clouds& live) {
    live = rest;
    const auto f = [&](std::size_t i) -> const params::Parameter<float>* {
        return i < p.all.size() ? dynamic_cast<const params::Parameter<float>*>(p.all[i]) : nullptr;
    };
    const auto v = [&](std::size_t i) -> const params::Parameter<glm::vec3>* {
        return i < p.all.size() ? dynamic_cast<const params::Parameter<glm::vec3>*>(p.all[i]) : nullptr;
    };
    for (std::size_t k = 0; k < kCloudFloatCount; ++k) {
        if (const auto* q = f(k)) live.*(kCloudFloats[k].field) = q->value();
    }
    for (std::size_t k = 0; k < kCloudVectorCount; ++k) {
        if (const auto* q = v(kCloudFloatCount + k)) live.*(kCloudVectors[k].field) = q->value();
    }
    const std::size_t sea = kCloudFloatCount + kCloudVectorCount;
    if (const auto* q = f(sea)) live.sea.coverage = q->value();
    if (const auto* q = f(sea + 1)) live.sea.density = q->value();
    const std::size_t glow0 = sea + kCloudSeaCount;
    for (std::size_t i = 0; i < live.glows.size(); ++i) {
        if (const auto* q = f(glow0 + 2 * i)) live.glows[i].intensity = q->value();
        if (const auto* q = v(glow0 + 2 * i + 1)) live.glows[i].color = q->value();
    }
    const std::size_t beam0 = glow0 + 2 * rest.glows.size(); // ADR-2102
    if (const auto* q = f(beam0)) live.beamAir = q->value();
    if (const auto* q = f(beam0 + 1)) live.beamCloud = q->value();
    if (const auto* q = f(beam0 + 2)) live.halo.intensity = q->value();
    for (std::size_t i = 0; i < live.beams.size(); ++i) {
        if (const auto* q = f(beam0 + 3 + i)) live.beams[i].intensity = q->value();
    }
}

} // namespace avgen::scene
