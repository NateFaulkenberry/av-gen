#pragma once

// The rows Ground Pulse and Travel Beam share (ADR-702). Internal to the two kind files.
//
// Both types are ADR-207's surface wave with different defaults, presets, targets and a different
// question ("what is spreading" against "what is travelling"), so they share one payload
// (`WaveEffect`) and therefore one table of rows. The leaves are exactly the `worldfx/<name>/...`
// leaves ADR-207's hand-written registrar used, so a converted route or timeline track keeps its
// leaf and changes only its prefix; the JSON paths are ADR-207's file layout, now inside the
// instance's `parameters` block.
//
// What is NOT a row, and why: the endpoints (a kind and a name, not a number), the explicit
// direction and the blend weights (a vector and three ratios nobody automates), vertical growth,
// and the sparkle's fade distance and seed. They were not parameters before ADR-702 either; they are
// written by `writeWaveExtra` beside the rows.

#include "world/effects/effect_registry.hpp"

#include <nlohmann/json.hpp>

// An unnamed namespace inside a header on purpose: two translation units include this, the rows are
// constexpr data with lambda accessors, and a per-TU copy is simpler to reason about than whether two
// definitions of one inline variable holding closures are the same entity.
namespace avgen::world::wave_rows {
namespace {

using E = EffectInstance;

#define AVGEN_WAVE_GET(expr) +[](const E& e) { return (expr); }
#define AVGEN_WAVE_SETF(lhs) +[](E& e, float v) { (lhs) = v; }
#define AVGEN_WAVE_SETC(lhs) +[](E& e, glm::vec3 v) { (lhs) = v; }
#define AVGEN_WAVE_SETB(lhs) +[](E& e, bool v) { (lhs) = v; }

inline constexpr const char* kPropagationNames[] = {"directional", "radial", "path"};
inline constexpr const char* kDirectionNames[] = {"explicit",       "sourceForward",  "cameraForward",
                                                  "cameraVelocity", "sourceToTarget", "cameraToTarget",
                                                  "blended"};

// ADR-1662: the path rows are written only by a path wave, so no other wave's file changes.
inline bool isPathWave(const E& e) { return e.wave.propagation.kind == PropagationKind::PathWave; }

inline constexpr EffectField kFields[] = {
    colorField("color", "Colour", AVGEN_WAVE_GET(e.wave.appearance.color),
               AVGEN_WAVE_SETC(e.wave.appearance.color)).json("appearance/color").main(),
    floatField("intensity", "Brightness", 0.0f, 40.0f, 0.0f, 8.0f, AVGEN_WAVE_GET(e.wave.appearance.intensity),
               AVGEN_WAVE_SETF(e.wave.appearance.intensity)).json("appearance/intensity").main(),
    colorField("edgeColor", "Edge colour", AVGEN_WAVE_GET(e.wave.appearance.edgeColor),
               AVGEN_WAVE_SETC(e.wave.appearance.edgeColor)).json("appearance/edgeColor").main(),
    floatField("edgeIntensity", "Edge brightness", 0.0f, 60.0f, 0.0f, 10.0f,
               AVGEN_WAVE_GET(e.wave.appearance.edgeIntensity), AVGEN_WAVE_SETF(e.wave.appearance.edgeIntensity))
        .json("appearance/edgeIntensity").main(),
    floatField("width", "Width", 0.05f, 8.0f, 0.25f, 3.0f, AVGEN_WAVE_GET(e.wave.appearance.width),
               AVGEN_WAVE_SETF(e.wave.appearance.width)).json("appearance/width").main().floorAt(1e-3f),
    boolField("rainbow", "Rainbow", AVGEN_WAVE_GET(e.wave.appearance.rainbow),
              AVGEN_WAVE_SETB(e.wave.appearance.rainbow)).json("appearance/rainbow").main(),
    // Beside the Rainbow toggle, BEFORE the Rainbow section opens: a Main-page row after that
    // section's first row belongs to it, and the Main page never draws its header.
    boolField("sparkle", "Sparkle", AVGEN_WAVE_GET(e.wave.sparkle.enabled),
              AVGEN_WAVE_SETB(e.wave.sparkle.enabled)).json("sparkle/enabled").main(),
    floatField("rainbowSpeed", "Rainbow speed", -4.0f, 4.0f, -1.0f, 1.0f,
               AVGEN_WAVE_GET(e.wave.appearance.rainbowSpeed), AVGEN_WAVE_SETF(e.wave.appearance.rainbowSpeed))
        .json("appearance/rainbowSpeed").sec("Rainbow"),
    floatField("rainbowScale", "Rainbow scale", 0.0f, 0.5f, 0.0f, 0.05f,
               AVGEN_WAVE_GET(e.wave.appearance.rainbowScale), AVGEN_WAVE_SETF(e.wave.appearance.rainbowScale))
        .json("appearance/rainbowScale").fmt("%.3f"),
    floatField("rainbowSaturation", "Rainbow saturation", 0.0f, 1.0f, 0.0f, 1.0f,
               AVGEN_WAVE_GET(e.wave.appearance.rainbowSaturation),
               AVGEN_WAVE_SETF(e.wave.appearance.rainbowSaturation)).json("appearance/rainbowSaturation"),
    floatField("rainbowBrightness", "Rainbow brightness", 0.0f, 4.0f, 0.0f, 2.0f,
               AVGEN_WAVE_GET(e.wave.appearance.rainbowBrightness),
               AVGEN_WAVE_SETF(e.wave.appearance.rainbowBrightness)).json("appearance/rainbowBrightness"),
    floatField("sparkleDensity", "Sparkle density", 0.0f, 6.0f, 0.05f, 2.0f,
               AVGEN_WAVE_GET(e.wave.sparkle.density), AVGEN_WAVE_SETF(e.wave.sparkle.density))
        .json("sparkle/density").sec("Sparkle"),
    floatField("sparkleSize", "Sparkle size", 0.0f, 1.0f, 0.05f, 0.8f, AVGEN_WAVE_GET(e.wave.sparkle.size),
               AVGEN_WAVE_SETF(e.wave.sparkle.size)).json("sparkle/size"),
    floatField("sparkleIntensity", "Sparkle brightness", 0.0f, 30.0f, 0.0f, 6.0f,
               AVGEN_WAVE_GET(e.wave.sparkle.intensity), AVGEN_WAVE_SETF(e.wave.sparkle.intensity))
        .json("sparkle/intensity"),
    floatField("sparkleSpeed", "Twinkle speed", 0.0f, 8.0f, 0.0f, 3.0f, AVGEN_WAVE_GET(e.wave.sparkle.speed),
               AVGEN_WAVE_SETF(e.wave.sparkle.speed)).json("sparkle/speed"),
    choiceField("propagation", "Shape", kPropagationNames,
                AVGEN_WAVE_GET(static_cast<float>(e.wave.propagation.kind)),
                +[](E& e, float v) {
                    e.wave.propagation.kind = static_cast<PropagationKind>(
                        std::clamp(static_cast<int>(v + 0.5f), 0, 2));
                })
        .json("propagation/kind").sec("Propagation")
        .tooltip("Directional: a front sweeping along an axis. Radial: a ring spreading from the source.\n"
                 "Path: a front travelling along a terrain feature (a river), away from the camera (ADR-1662)."),
    choiceField("direction", "Direction", kDirectionNames,
                AVGEN_WAVE_GET(static_cast<float>(e.wave.propagation.direction)),
                +[](E& e, float v) {
                    e.wave.propagation.direction = static_cast<DirectionMode>(
                        std::clamp(static_cast<int>(v + 0.5f), 0, 6));
                })
        .json("propagation/direction")
        .tooltip("Which way a directional front travels. A radial ring ignores it."),
    floatField("speed", "Speed", 0.05f, 800.0f, 1.0f, 200.0f, AVGEN_WAVE_GET(e.wave.propagation.speed),
               AVGEN_WAVE_SETF(e.wave.propagation.speed)).json("propagation/speed").fmt("%.1f m/s")
        .floorAt(1e-3f),
    floatField("range", "Range", 0.5f, 2000.0f, 5.0f, 400.0f, AVGEN_WAVE_GET(e.wave.propagation.range),
               AVGEN_WAVE_SETF(e.wave.propagation.range)).json("propagation/range").fmt("%.0f m").floorAt(1e-3f),
    floatField("frontWidth", "Front width", 0.05f, 200.0f, 0.5f, 40.0f,
               AVGEN_WAVE_GET(e.wave.propagation.frontWidth), AVGEN_WAVE_SETF(e.wave.propagation.frontWidth))
        .json("propagation/frontWidth").fmt("%.1f m").floorAt(1e-3f),
    floatField("trailLength", "Trail", 0.0f, 500.0f, 0.0f, 120.0f, AVGEN_WAVE_GET(e.wave.propagation.trailLength),
               AVGEN_WAVE_SETF(e.wave.propagation.trailLength)).json("propagation/trailLength").fmt("%.1f m")
        .floorAt(0.0f),
    floatField("falloff", "Falloff", 0.05f, 8.0f, 0.5f, 4.0f, AVGEN_WAVE_GET(e.wave.propagation.falloff),
               AVGEN_WAVE_SETF(e.wave.propagation.falloff)).json("propagation/falloff").floorAt(1e-3f),
    floatField("startOffset", "Start offset", -200.0f, 400.0f, 0.0f, 60.0f,
               AVGEN_WAVE_GET(e.wave.propagation.startOffset), AVGEN_WAVE_SETF(e.wave.propagation.startOffset))
        .json("propagation/startOffset").fmt("%.1f m"),
    floatField("verticalExtent", "Height", 0.0f, 500.0f, 0.5f, 120.0f,
               AVGEN_WAVE_GET(e.wave.propagation.verticalExtent),
               AVGEN_WAVE_SETF(e.wave.propagation.verticalExtent)).json("propagation/verticalExtent")
        .fmt("%.1f m").floorAt(0.0f),
    floatField("ringCount", "Rings", 0.0f, 16.0f, 0.0f, 6.0f, AVGEN_WAVE_GET(e.wave.propagation.ringCount),
               AVGEN_WAVE_SETF(e.wave.propagation.ringCount)).json("propagation/ringCount").floorAt(0.0f),
    floatField("beamRadius", "Beam radius", 0.0f, 1000.0f, 0.0f, 200.0f,
               AVGEN_WAVE_GET(e.wave.propagation.beamRadius), AVGEN_WAVE_SETF(e.wave.propagation.beamRadius))
        .json("propagation/beamRadius").fmt("%.0f m").floorAt(0.0f),
    // ADR-1662: a path wave's shape (Propagation > Path). Ignored by the other kinds.
    floatField("path/ease", "Path ease", 0.0f, 1.0f, 0.0f, 1.0f, AVGEN_WAVE_GET(e.wave.propagation.path.ease),
               AVGEN_WAVE_SETF(e.wave.propagation.path.ease)).json("propagation/path/ease").sec("Path")
        .tooltip("0: the front keeps its speed. 1: it starts at twice the mean speed and slows to a stop.")
        .writtenWhen(isPathWave),
    floatField("path/wakeGrowth", "Wake growth", 0.0f, 2.0f, 0.0f, 1.0f,
               AVGEN_WAVE_GET(e.wave.propagation.path.wakeGrowth),
               AVGEN_WAVE_SETF(e.wave.propagation.path.wakeGrowth)).json("propagation/path/wakeGrowth")
        .tooltip("The wake lengthens by this many metres per metre travelled, so it keeps a length on screen.")
        .floorAt(0.0f).writtenWhen(isPathWave),
    floatField("path/coreWidth", "Core width", 0.1f, 100.0f, 0.5f, 30.0f,
               AVGEN_WAVE_GET(e.wave.propagation.path.coreWidth),
               AVGEN_WAVE_SETF(e.wave.propagation.path.coreWidth)).json("propagation/path/coreWidth")
        .fmt("%.1f m").floorAt(0.1f).writtenWhen(isPathWave),
    floatField("path/coreOffset", "Twin offset", 0.0f, 100.0f, 0.0f, 40.0f,
               AVGEN_WAVE_GET(e.wave.propagation.path.coreOffset),
               AVGEN_WAVE_SETF(e.wave.propagation.path.coreOffset)).json("propagation/path/coreOffset")
        .fmt("%.1f m").floorAt(0.0f).tooltip("0: one core on the path. Above 0: two filaments this far either side.")
        .writtenWhen(isPathWave),
    floatField("path/haloWidth", "Halo width", 0.0f, 400.0f, 0.0f, 120.0f,
               AVGEN_WAVE_GET(e.wave.propagation.path.haloWidth),
               AVGEN_WAVE_SETF(e.wave.propagation.path.haloWidth)).json("propagation/path/haloWidth")
        .fmt("%.0f m").floorAt(0.0f).writtenWhen(isPathWave),
    floatField("path/reach", "Land reach", 0.0f, 2000.0f, 0.0f, 600.0f,
               AVGEN_WAVE_GET(e.wave.propagation.path.reach),
               AVGEN_WAVE_SETF(e.wave.propagation.path.reach)).json("propagation/path/reach").fmt("%.0f m")
        .floorAt(0.0f).tooltip("How far across the path the land is lit, facet by facet. 0: the land is not lit.")
        .writtenWhen(isPathWave),
    floatField("path/reachHeight", "Land height", 0.0f, 1000.0f, 0.0f, 300.0f,
               AVGEN_WAVE_GET(e.wave.propagation.path.reachHeight),
               AVGEN_WAVE_SETF(e.wave.propagation.path.reachHeight)).json("propagation/path/reachHeight")
        .fmt("%.0f m").floorAt(0.0f).writtenWhen(isPathWave),
    floatField("path/land", "Land light", 0.0f, 4.0f, 0.0f, 1.0f, AVGEN_WAVE_GET(e.wave.propagation.path.land),
               AVGEN_WAVE_SETF(e.wave.propagation.path.land)).json("propagation/path/land").floorAt(0.0f)
        .writtenWhen(isPathWave),
    floatField("path/bed", "Bed light", 0.0f, 8.0f, 0.0f, 3.0f, AVGEN_WAVE_GET(e.wave.propagation.path.bed),
               AVGEN_WAVE_SETF(e.wave.propagation.path.bed)).json("propagation/path/bed").floorAt(0.0f)
        .tooltip("Below the path's level (a riverbed seen through the water): the light, cast from the head.")
        .writtenWhen(isPathWave),
    floatField("path/embed", "Under the surface", 0.0f, 1.0f, 0.0f, 1.0f,
               AVGEN_WAVE_GET(e.wave.propagation.path.embed),
               AVGEN_WAVE_SETF(e.wave.propagation.path.embed)).json("propagation/path/embed")
        .tooltip("On water: the share of the light drawn under the reflection; the rest lies on top.")
        .writtenWhen(isPathWave),
    floatField("path/disturb", "Ripples", 0.0f, 1.0f, 0.0f, 0.3f, AVGEN_WAVE_GET(e.wave.propagation.path.disturb),
               AVGEN_WAVE_SETF(e.wave.propagation.path.disturb)).json("propagation/path/disturb").floorAt(0.0f)
        .tooltip("On water: the wake's ripples behind the front, which break the reflection and heal.")
        .writtenWhen(isPathWave),
    floatField("path/nearFade", "Near fade", 0.0f, 2000.0f, 0.0f, 800.0f,
               AVGEN_WAVE_GET(e.wave.propagation.path.nearFade),
               AVGEN_WAVE_SETF(e.wave.propagation.path.nearFade)).json("propagation/path/nearFade")
        .fmt("%.0f m").floorAt(0.0f).tooltip("The light fades in over this distance from the camera (0: off).")
        .writtenWhen(isPathWave),
    floatField("response/ground", "Ground", 0.0f, 6.0f, 0.0f, 2.0f, AVGEN_WAVE_GET(e.wave.response.ground),
               AVGEN_WAVE_SETF(e.wave.response.ground)).json("response/ground").sec("Surface response")
        .floorAt(0.0f),
    floatField("response/foliage", "Foliage", 0.0f, 6.0f, 0.0f, 2.0f, AVGEN_WAVE_GET(e.wave.response.foliage),
               AVGEN_WAVE_SETF(e.wave.response.foliage)).json("response/foliage").floorAt(0.0f),
    floatField("response/surface", "Other surfaces", 0.0f, 6.0f, 0.0f, 2.0f,
               AVGEN_WAVE_GET(e.wave.response.surface), AVGEN_WAVE_SETF(e.wave.response.surface))
        .json("response/surface").floorAt(0.0f),
    floatField("response/emissive", "Emissive boost", 0.0f, 6.0f, 0.0f, 2.0f,
               AVGEN_WAVE_GET(e.wave.response.emissive), AVGEN_WAVE_SETF(e.wave.response.emissive))
        .json("response/emissive").floorAt(0.0f),
};

inline EffectEndpoint getSource(const E& e) { return e.wave.source; }
inline void setSource(E& e, const EffectEndpoint& p) { e.wave.source = p; }
inline bool hasTarget(const E& e) { return e.wave.hasTarget; }
inline EffectEndpoint getTarget(const E& e) { return e.wave.target; }
inline void setTarget(E& e, bool has, const EffectEndpoint& p) {
    e.wave.hasTarget = has;
    e.wave.target = p;
}

inline nlohmann::json vec3(const glm::vec3& v) { return nlohmann::json::array({v.x, v.y, v.z}); }

inline void writeExtra(const E& e, nlohmann::json& block) {
    const WaveEffect& w = e.wave;
    block["source"] = waveEndpointToJson(w.source);
    if (w.hasTarget) {
        block["target"] = waveEndpointToJson(w.target);
    }
    nlohmann::json& p = block["propagation"];
    p["explicitDirection"] = vec3(w.propagation.explicitDirection);
    p["forwardWeight"] = w.propagation.forwardWeight;
    p["velocityWeight"] = w.propagation.velocityWeight;
    p["targetWeight"] = w.propagation.targetWeight;
    p["verticalGrowth"] = w.propagation.verticalGrowth;
    if (!w.propagation.path.node.empty() || !w.propagation.path.feature.empty()) { // ADR-1662
        p["path"]["node"] = w.propagation.path.node;
        p["path"]["feature"] = w.propagation.path.feature;
    }
    nlohmann::json& s = block["sparkle"];
    s["fadeDistance"] = w.sparkle.fadeDistance;
    s["seed"] = w.sparkle.seed;
}

inline Result<void> readExtra(E& e, const nlohmann::json& block) {
    WaveEffect& w = e.wave;
    if (block.contains("source")) {
        auto src = waveEndpointFromJson(block.at("source"));
        if (!src) {
            return fail("source: {}", src.error().message);
        }
        w.source = *src;
    }
    w.hasTarget = false;
    if (block.contains("target")) {
        auto dst = waveEndpointFromJson(block.at("target"));
        if (!dst) {
            return fail("target: {}", dst.error().message);
        }
        w.target = *dst;
        w.hasTarget = true;
    }
    const auto num = [](const nlohmann::json& j, const char* k, float fallback) {
        return j.contains(k) && j.at(k).is_number() ? j.at(k).get<float>() : fallback;
    };
    if (block.contains("propagation") && block.at("propagation").is_object()) {
        const nlohmann::json& p = block.at("propagation");
        if (p.contains("explicitDirection") && p.at("explicitDirection").is_array() &&
            p.at("explicitDirection").size() == 3) {
            const nlohmann::json& d = p.at("explicitDirection");
            w.propagation.explicitDirection = {d[0].get<float>(), d[1].get<float>(), d[2].get<float>()};
        }
        w.propagation.forwardWeight = num(p, "forwardWeight", w.propagation.forwardWeight);
        w.propagation.velocityWeight = num(p, "velocityWeight", w.propagation.velocityWeight);
        w.propagation.targetWeight = num(p, "targetWeight", w.propagation.targetWeight);
        w.propagation.verticalGrowth = num(p, "verticalGrowth", w.propagation.verticalGrowth);
        if (p.contains("path") && p.at("path").is_object()) { // ADR-1662
            const nlohmann::json& q = p.at("path");
            if (q.contains("node") && q.at("node").is_string()) {
                w.propagation.path.node = q.at("node").get<std::string>();
            }
            if (q.contains("feature") && q.at("feature").is_string()) {
                w.propagation.path.feature = q.at("feature").get<std::string>();
            }
        }
    }
    if (block.contains("sparkle") && block.at("sparkle").is_object()) {
        const nlohmann::json& s = block.at("sparkle");
        w.sparkle.fadeDistance = num(s, "fadeDistance", w.sparkle.fadeDistance);
        if (s.contains("seed") && s.at("seed").is_number_unsigned()) {
            w.sparkle.seed = s.at("seed").get<std::uint32_t>();
        }
    }
    return {};
}

inline Result<void> validate(const E& e) { return e.wave.validate(); }

// The wave never asks a field bus anything, so its registry `fill` is never called (the Surface
// bucket resolves through `resolveWave`); declared so the schema is complete.
inline bool fill(const E&, std::size_t, const EffectContext&, const ResolvedAtmospheric&, ResolvedAtmospheric&) {
    return false;
}

// The parts of a schema both wave types have. The type file sets identity, targets, defaults,
// styles and routes on top.
inline EffectSchema baseSchema() {
    EffectSchema s;
    s.fields = kFields;
    s.stage = RenderStage::Material;
    s.category = EffectCategory::Lighting;
    s.getSource = getSource;
    s.setSource = setSource;
    s.hasTarget = hasTarget;
    s.getTarget = getTarget;
    s.setTarget = setTarget;
    s.resolve.bucket = EffectBucket::Surface;
    s.writeExtra = writeExtra;
    s.readExtra = readExtra;
    s.validate = validate;
    s.beatLeaf = "intensity";
    return s;
}

#undef AVGEN_WAVE_GET
#undef AVGEN_WAVE_SETF
#undef AVGEN_WAVE_SETC
#undef AVGEN_WAVE_SETB

} // namespace
} // namespace avgen::world::wave_rows
