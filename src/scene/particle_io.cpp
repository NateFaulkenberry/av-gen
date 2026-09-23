#include "scene/particle_io.hpp"

#include "spatial/detail.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace avgen::scene {

using json = nlohmann::json;

namespace {

// The JSON primitives every reader in this repository shares. `composition.cpp` carried its own
// byte-identical copies of these while this code lived there; the extraction uses the ones
// `composition_data.cpp` and `graph/detail.hpp` already share instead of moving the copy too.
using spatial::detail::readBool;
using spatial::detail::readFloat;
using spatial::detail::readString;
using spatial::detail::vecToJson;

// `spatial::detail` spells the vector reader `readVecN`; this block was written against `readVec`,
// and renaming eleven call sites would have made a pure move look like an edit.
template <glm::length_t N>
Result<glm::vec<N, float>> readVec(const json& j, const char* key, const glm::vec<N, float>& def) {
    return spatial::detail::readVecN<N>(j, key, def);
}

const char* shapeName(EmitterShape shape) {
    switch (shape) {
    case EmitterShape::Point:
        return "point";
    case EmitterShape::Sphere:
        return "sphere";
    case EmitterShape::Disc:
        return "disc";
    case EmitterShape::Box:
        return "box";
    case EmitterShape::Spline:
        return "spline";
    }
    return "sphere";
}

Result<EmitterShape> shapeFromName(const std::string& name) {
    if (name == "point") {
        return EmitterShape::Point;
    }
    if (name == "sphere") {
        return EmitterShape::Sphere;
    }
    if (name == "disc") {
        return EmitterShape::Disc;
    }
    if (name == "box") {
        return EmitterShape::Box;
    }
    if (name == "spline") {
        return EmitterShape::Spline;
    }
    return fail("unknown emitter shape '{}'", name);
}

// ADR-370: the leaf card's silhouette. Written and read only when it is not Round, so a scene that
// never asks for a leaf is byte-identical on a re-save.
const char* particleShape2dName(ParticleShape shape) {
    return shape == ParticleShape::Leaf ? "leaf" : "round";
}

Result<ParticleShape> particleShape2dFromName(const std::string& name) {
    if (name == "round") {
        return ParticleShape::Round;
    }
    if (name == "leaf") {
        return ParticleShape::Leaf;
    }
    return fail("unknown particle shape '{}' (expected round or leaf)", name);
}

const char* blendName(ParticleBlend blend) {
    return blend == ParticleBlend::Alpha ? "alpha" : "additive";
}

Result<ParticleBlend> blendFromName(const std::string& name) {
    if (name == "additive") {
        return ParticleBlend::Additive;
    }
    if (name == "alpha") {
        return ParticleBlend::Alpha;
    }
    return fail("unknown particle blend '{}'", name);
}

} // namespace

json particlesToJson(const ParticleSystem& s) {
    json j;
    j["enabled"] = s.enabled;
    j["capacity"] = s.capacity;
    j["seed"] = s.seed;
    j["shape"] = shapeName(s.shape);
    if (!s.spline.empty()) {
        j["spline"] = s.spline;
    }
    j["position"] = vecToJson(s.position);
    j["extent"] = vecToJson(s.extent);
    j["spawnRate"] = s.spawnRate;
    j["burst"] = s.burst;
    j["lifetimeMin"] = s.lifetimeMin;
    j["lifetimeMax"] = s.lifetimeMax;
    j["direction"] = vecToJson(s.direction);
    j["spread"] = s.spread;
    j["speedMin"] = s.speedMin;
    j["speedMax"] = s.speedMax;
    j["gravity"] = vecToJson(s.gravity);
    j["drag"] = s.drag;
    j["turbulence"] = s.turbulence;
    j["turbulenceScale"] = s.turbulenceScale;
    j["turbulenceSpeed"] = s.turbulenceSpeed;
    j["attractorPosition"] = vecToJson(s.attractorPosition);
    j["attractorStrength"] = s.attractorStrength;
    j["attractorRadius"] = s.attractorRadius;
    j["orbit"] = s.orbit;
    j["sizeStart"] = s.sizeStart;
    j["sizeEnd"] = s.sizeEnd;
    j["colorStart"] = vecToJson(s.colorStart);
    j["colorEnd"] = vecToJson(s.colorEnd);
    j["emissive"] = s.emissive;
    j["blend"] = blendName(s.blend);
    // ADR-367: written only when it does something. It used to be written unconditionally, which is
    // how a default nobody chose ended up baked into every scene file the editor ever saved.
    if (s.softness != 0.0f) {
        j["softness"] = s.softness;
    }
    if (s.windInfluence != 0.0f) {
        j["windInfluence"] = s.windInfluence;
    }
    // ADR-370.
    if (s.shape2d != ParticleShape::Round) {
        j["shape2d"] = particleShape2dName(s.shape2d);
        j["tumbleRate"] = s.tumbleRate;
        j["leafAspect"] = s.leafAspect;
        j["twoSided"] = s.twoSided;
    }
    // ADR-040. Only written when they differ from the defaults so existing files stay short and
    // round-tripping a pre-ADR-040 scene produces the same JSON it started with.
    if (s.velocityStretch != 0.0f) {
        j["velocityStretch"] = s.velocityStretch;
        j["stretchMax"] = s.stretchMax;
        j["stretchMin"] = s.stretchMin;
    }
    if (s.trailEnabled) {
        j["trailEnabled"] = true;
        j["trailLength"] = s.trailLength;
        j["trailStride"] = s.trailStride;
        j["trailWidth"] = s.trailWidth;
        j["trailTaper"] = s.trailTaper;
        j["trailFade"] = s.trailFade;
        j["trailTint"] = vecToJson(s.trailTint);
    }
    // ---- ADR-520 ----
    // Written only when they do something, for the reason ADR-367 gives above: a default nobody
    // chose, written unconditionally, becomes a value baked into every file the editor ever saves.
    if (s.volumeFollow != glm::vec3(0.0f)) {
        j["volumeFollow"] = vecToJson(s.volumeFollow);
    }
    if (s.volumeWrap) {
        j["volumeWrap"] = true;
    }
    if (s.collision != CollisionResponse::None) {
        j["collision"] = collisionResponseName(s.collision);
        j["collisionHeight"] = s.collisionHeight;
        j["collisionRestitution"] = s.collisionRestitution;
        if (s.collision == CollisionResponse::Splash) {
            j["splashLifetime"] = s.splashLifetime;
            j["splashSize"] = s.splashSize;
            j["ringThickness"] = s.ringThickness;
        }
    }
    if (s.sizeVariance != 0.3f) {
        j["sizeVariance"] = s.sizeVariance;
    }
    if (s.sizeSkew != 1.0f) {
        j["sizeSkew"] = s.sizeSkew;
    }
    if (s.dragSizeBias != 0.0f) {
        j["dragSizeBias"] = s.dragSizeBias;
    }
    if (s.pulseRate != 0.0f) {
        j["pulseRate"] = s.pulseRate;
        j["pulseDepth"] = s.pulseDepth;
        j["pulseSync"] = s.pulseSync;
        j["pulseSharpness"] = s.pulseSharpness;
    }
    if (s.clusterCount != 0u) {
        j["clusterCount"] = s.clusterCount;
        j["clusterRadius"] = s.clusterRadius;
    }
    if (s.scatterAnchor.active()) {
        json a = json::object();
        a["terrain"] = s.scatterAnchor.terrain;
        a["layers"] = s.scatterAnchor.layers;
        a["randomBelow"] = s.scatterAnchor.randomBelow;
        a["litOnly"] = s.scatterAnchor.litOnly;
        a["viewDistance"] = s.scatterAnchor.viewDistance;
        j["scatterAnchor"] = std::move(a);
    }
    if (s.pauseRate != 0.0f) {
        j["pauseRate"] = s.pauseRate;
        j["pauseFraction"] = s.pauseFraction;
    }
    if (s.scatterStrength != 0.0f) {
        j["scatterStrength"] = s.scatterStrength;
        j["scatterAnisotropy"] = s.scatterAnisotropy;
    }
    if (!s.emitMaskField.empty()) {
        j["emitMaskField"] = s.emitMaskField;
    }
    if (s.fogCoupling != 1.0f) {
        j["fogCoupling"] = s.fogCoupling;
    }
    if (s.volumeGlow != 0.0f) {
        j["volumeGlow"] = s.volumeGlow;
    }
    auto scalarCurve = [](const ParticleCurve& c) {
        json keys = json::array();
        for (const CurveKey& k : c.keys) {
            keys.push_back(json{{"t", k.t}, {"value", k.value}});
        }
        return keys;
    };
    if (!s.sizeCurve.keys.empty()) {
        j["sizeCurve"] = scalarCurve(s.sizeCurve);
    }
    if (!s.opacityCurve.keys.empty()) {
        j["opacityCurve"] = scalarCurve(s.opacityCurve);
    }
    if (!s.colorCurve.keys.empty()) {
        json keys = json::array();
        for (const ColorKey& k : s.colorCurve.keys) {
            keys.push_back(json{{"t", k.t}, {"color", vecToJson(k.color)}});
        }
        j["colorCurve"] = std::move(keys);
    }
    if (!s.fieldForces.empty()) {
        json forces = json::array();
        for (const FieldForce& f : s.fieldForces) {
            json fj;
            fj["field"] = f.field;
            fj["mode"] = fieldForceModeName(f.mode);
            fj["enabled"] = f.enabled;
            fj["strength"] = f.strength;
            fj["mix"] = f.mix;
            fj["axis"] = vecToJson(f.axis);
            forces.push_back(std::move(fj));
        }
        j["fieldForces"] = std::move(forces);
    }
    return j;
}

// Missing fields keep the ParticleSystem defaults so hand-written files can stay short.
Result<ParticleSystem> particlesFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("'particles' must be an object");
    }
    ParticleSystem s;
#define AVGEN_READ(field, reader)                                                                            \
    do {                                                                                                     \
        auto r = reader(j, #field, s.field);                                                                 \
        if (!r) {                                                                                            \
            return std::unexpected(r.error());                                                               \
        }                                                                                                    \
        s.field = *r;                                                                                        \
    } while (false)
    AVGEN_READ(enabled, readBool);
    if (j.contains("capacity")) {
        if (!j.at("capacity").is_number_unsigned()) {
            return fail("'capacity' must be a positive integer");
        }
        s.capacity = j.at("capacity").get<std::uint32_t>();
    }
    if (j.contains("seed")) {
        if (!j.at("seed").is_number_unsigned()) {
            return fail("'seed' must be a positive integer");
        }
        s.seed = j.at("seed").get<std::uint32_t>();
    }
    if (j.contains("shape")) {
        auto name = readString(j, "shape", "");
        if (!name) {
            return std::unexpected(name.error());
        }
        auto shape = shapeFromName(*name);
        if (!shape) {
            return std::unexpected(shape.error());
        }
        s.shape = *shape;
    }
    if (j.contains("spline")) {
        auto splineName = readString(j, "spline", "");
        if (!splineName) {
            return std::unexpected(splineName.error());
        }
        s.spline = *splineName;
    }
    AVGEN_READ(position, readVec<3>);
    AVGEN_READ(extent, readVec<3>);
    AVGEN_READ(spawnRate, readFloat);
    AVGEN_READ(burst, readFloat);
    AVGEN_READ(lifetimeMin, readFloat);
    AVGEN_READ(lifetimeMax, readFloat);
    AVGEN_READ(direction, readVec<3>);
    AVGEN_READ(spread, readFloat);
    AVGEN_READ(speedMin, readFloat);
    AVGEN_READ(speedMax, readFloat);
    AVGEN_READ(gravity, readVec<3>);
    AVGEN_READ(drag, readFloat);
    AVGEN_READ(turbulence, readFloat);
    AVGEN_READ(turbulenceScale, readFloat);
    AVGEN_READ(turbulenceSpeed, readFloat);
    AVGEN_READ(attractorPosition, readVec<3>);
    AVGEN_READ(attractorStrength, readFloat);
    AVGEN_READ(attractorRadius, readFloat);
    AVGEN_READ(orbit, readFloat);
    AVGEN_READ(sizeStart, readFloat);
    AVGEN_READ(sizeEnd, readFloat);
    AVGEN_READ(colorStart, readVec<4>);
    AVGEN_READ(colorEnd, readVec<4>);
    AVGEN_READ(emissive, readFloat);
    if (j.contains("blend")) {
        auto name = readString(j, "blend", "");
        if (!name) {
            return std::unexpected(name.error());
        }
        auto blend = blendFromName(*name);
        if (!blend) {
            return std::unexpected(blend.error());
        }
        s.blend = *blend;
    }
    AVGEN_READ(softness, readFloat);
    // ADR-370: the leaf card. Absent is Round, which is the ordinary state.
    if (j.contains("shape2d") && j.at("shape2d").is_string()) {
        auto shape = particleShape2dFromName(j.at("shape2d").get<std::string>());
        if (!shape) {
            return std::unexpected(shape.error());
        }
        s.shape2d = *shape;
    }
    AVGEN_READ(windInfluence, readFloat);
    AVGEN_READ(tumbleRate, readFloat);
    AVGEN_READ(leafAspect, readFloat);
    AVGEN_READ(twoSided, readFloat);
    // ---- ADR-040: stretching, trails, atmosphere coupling and lifetime curves ----
    AVGEN_READ(velocityStretch, readFloat);
    AVGEN_READ(stretchMax, readFloat);
    AVGEN_READ(stretchMin, readFloat);
    AVGEN_READ(trailEnabled, readBool);
    AVGEN_READ(trailWidth, readFloat);
    AVGEN_READ(trailTaper, readFloat);
    AVGEN_READ(trailFade, readFloat);
    AVGEN_READ(trailTint, readVec<3>);
    AVGEN_READ(fogCoupling, readFloat);
    AVGEN_READ(volumeGlow, readFloat);
    // ---- ADR-520 ----
    AVGEN_READ(volumeFollow, readVec<3>);
    AVGEN_READ(volumeWrap, readBool);
    AVGEN_READ(collisionHeight, readFloat);
    AVGEN_READ(collisionRestitution, readFloat);
    AVGEN_READ(splashLifetime, readFloat);
    AVGEN_READ(splashSize, readFloat);
    AVGEN_READ(ringThickness, readFloat);
    AVGEN_READ(sizeVariance, readFloat);
    AVGEN_READ(sizeSkew, readFloat);
    AVGEN_READ(dragSizeBias, readFloat);
    AVGEN_READ(pulseRate, readFloat);
    AVGEN_READ(pulseDepth, readFloat);
    AVGEN_READ(pulseSync, readFloat);
    AVGEN_READ(pulseSharpness, readFloat);
    AVGEN_READ(clusterRadius, readFloat);
    AVGEN_READ(pauseRate, readFloat);
    AVGEN_READ(pauseFraction, readFloat);
    AVGEN_READ(scatterStrength, readFloat);
    AVGEN_READ(scatterAnisotropy, readFloat);
    AVGEN_READ(emitMaskField, readString);
#undef AVGEN_READ
    if (j.contains("collision")) {
        auto name = readString(j, "collision", "");
        if (!name) {
            return std::unexpected(name.error());
        }
        auto mode = collisionResponseFromName(*name);
        if (!mode) {
            return fail("unknown collision response '{}' (expected none, kill, bounce or splash)", *name);
        }
        s.collision = *mode;
    }
    if (j.contains("clusterCount")) {
        if (!j.at("clusterCount").is_number_unsigned()) {
            return fail("'clusterCount' must be a positive integer");
        }
        s.clusterCount = j.at("clusterCount").get<std::uint32_t>();
    }
    if (j.contains("scatterAnchor")) {
        const json& a = j.at("scatterAnchor");
        if (!a.is_object()) {
            return fail("'scatterAnchor' must be an object");
        }
        auto terrain = readString(a, "terrain", "");
        auto randomBelow = readFloat(a, "randomBelow", s.scatterAnchor.randomBelow);
        auto litOnly = readBool(a, "litOnly", s.scatterAnchor.litOnly);
        auto viewDistance = readFloat(a, "viewDistance", s.scatterAnchor.viewDistance);
        if (!terrain || !randomBelow || !litOnly || !viewDistance) {
            return fail("'scatterAnchor' needs a string 'terrain', numeric 'randomBelow' and 'viewDistance' and a "
                        "boolean 'litOnly'");
        }
        if (!a.contains("layers") || !a.at("layers").is_array() || a.at("layers").empty()) {
            return fail("'scatterAnchor' needs a non-empty 'layers' array of scatter layer names");
        }
        for (const json& layer : a.at("layers")) {
            if (!layer.is_string()) {
                return fail("'scatterAnchor.layers' entries must be strings");
            }
            s.scatterAnchor.layers.push_back(layer.get<std::string>());
        }
        if (terrain->empty()) {
            return fail("'scatterAnchor' needs the name of the terrain node whose layers it reads");
        }
        s.scatterAnchor.terrain = *terrain;
        s.scatterAnchor.randomBelow = *randomBelow;
        s.scatterAnchor.litOnly = *litOnly;
        s.scatterAnchor.viewDistance = *viewDistance;
    }
    for (const auto& [key, target] : {std::pair<const char*, std::uint32_t*>{"trailLength", &s.trailLength},
                                      std::pair<const char*, std::uint32_t*>{"trailStride", &s.trailStride}}) {
        if (j.contains(key)) {
            if (!j.at(key).is_number_unsigned()) {
                return fail("'{}' must be a positive integer", key);
            }
            *target = j.at(key).get<std::uint32_t>();
        }
    }
    {
        auto readScalarCurve = [&](const char* key, ParticleCurve& curve) -> Result<void> {
            if (!j.contains(key)) {
                return Result<void>{};
            }
            const json& keys = j.at(key);
            if (!keys.is_array()) {
                return fail("'{}' must be an array of keys", key);
            }
            for (const json& kj : keys) {
                if (!kj.is_object()) {
                    return fail("'{}' entries must be objects", key);
                }
                auto t = readFloat(kj, "t", 0.0f);
                auto v = readFloat(kj, "value", 0.0f);
                if (!t || !v) {
                    return fail("'{}': keys need numeric 't' and 'value'", key);
                }
                curve.keys.push_back(CurveKey{*t, *v});
            }
            return Result<void>{};
        };
        if (auto r = readScalarCurve("sizeCurve", s.sizeCurve); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = readScalarCurve("opacityCurve", s.opacityCurve); !r) {
            return std::unexpected(r.error());
        }
        if (j.contains("colorCurve")) {
            const json& keys = j.at("colorCurve");
            if (!keys.is_array()) {
                return fail("'colorCurve' must be an array of keys");
            }
            for (const json& kj : keys) {
                if (!kj.is_object()) {
                    return fail("'colorCurve' entries must be objects");
                }
                auto t = readFloat(kj, "t", 0.0f);
                auto c = readVec<3>(kj, "color", glm::vec3(1.0f));
                if (!t || !c) {
                    return fail("'colorCurve': keys need a numeric 't' and a 'color'");
                }
                s.colorCurve.keys.push_back(ColorKey{*t, *c});
            }
        }
    }
    if (auto r = validateParticleSystem(s); !r) {
        return std::unexpected(r.error());
    }
    if (j.contains("fieldForces")) {
        const json& forces = j.at("fieldForces");
        if (!forces.is_array()) {
            return fail("'fieldForces' must be an array");
        }
        if (forces.size() > static_cast<std::size_t>(kMaxFieldForces)) {
            return fail("at most {} field forces", kMaxFieldForces);
        }
        for (const json& fj : forces) {
            if (!fj.is_object()) {
                return fail("'fieldForces' entries must be objects");
            }
            FieldForce f;
            auto field = readString(fj, "field", "");
            if (!field) {
                return std::unexpected(field.error());
            }
            f.field = *field;
            if (f.field.empty()) {
                return fail("field force needs a 'field' name");
            }
            if (fj.contains("mode")) {
                auto mode = readString(fj, "mode", "force");
                if (!mode) {
                    return std::unexpected(mode.error());
                }
                auto m = fieldForceModeFromName(*mode);
                if (!m) {
                    return fail("unknown field force mode '{}'", *mode);
                }
                f.mode = *m;
            }
            auto enabled = readBool(fj, "enabled", true);
            auto strength = readFloat(fj, "strength", 1.0f);
            auto mix = readFloat(fj, "mix", 1.0f);
            auto axis = readVec<3>(fj, "axis", f.axis);
            if (!enabled || !strength || !mix || !axis) {
                return fail("field force '{}': invalid fields", f.field);
            }
            f.enabled = *enabled;
            f.strength = *strength;
            f.mix = *mix;
            f.axis = *axis;
            s.fieldForces.push_back(std::move(f));
        }
    }
    return s;
}

} // namespace avgen::scene
