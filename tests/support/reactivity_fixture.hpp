#pragma once

// The reactivity planner's fixture (ADR-924..927): a small glade with one target of every kind the
// reactive catalogue lists, a synthetic groove, and an authored section timeline. No licensed asset:
// the heroes are procedural boxes lit by material programs, the scatter layers use whatever glTF the
// caller hands in (the triangle GLB on the CPU; Quaternius's Mushroom_Common for pixels), and the
// character is the triangle GLB driven by an entity.
//
//   heroes      elder-cap (importance 0.6; a program-lit cap and gills, a plain emissive stem, spores
//               under the cap, a practical light beside it), lantern-cap, spire-cap, moss-cap; and the
//               hero point of the character "walker", whose body emits -- a character is no hero
//   scatter     fungi 0.3 m (glowmereTissue's kind of program, names the triggered field "ripple"),
//               shelf 0.55 m, lamps 1.5 m, grass 0.6 m glowing faintly (5% of the fungi), stones (dark)
//   particles   "spores" (free) and "elder-spores" (under the elder's cap)
//   world       fog, the ecology light, wind, a pond, an aurora, a light rig with a practical light
//   music       120 BPM, 32 bars: kick on every beat, claps on 2 and 4, off-beat hats, a bass that
//               changes note on each bar; bars 13-14 have no kick (the break)
//   sections    intro 0.30, groove 0.60, breakdown 0.20 (the kickless bars), build 0.75, drop 1.00,
//               outro 0.35

#include "app/engine.hpp"
#include "directing/time_ref.hpp"
#include "seq/sequence.hpp"
#include "song/section_cue.hpp"
#include "support/groove.hpp"
#include "support/temp_dir.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace avgen::testsupport {

struct GladeOptions {
    bool wave = true;          // the fungi name a wave field triggered every bar
    bool triggeredWave = true; // ...whose clock starts at the downbeat (false: the transport clock)
};

inline std::string gladeSceneJson(const std::filesystem::path& scatterAsset, const std::filesystem::path& characterAsset,
                                  const GladeOptions& options = {}) {
    const std::string asset = scatterAsset.generic_string();
    const std::string fungiWave = options.wave ? R"(, "emissiveField": "ripple", "emissiveFieldAmount": 6.0)" : "";
    const std::string trigger = options.triggeredWave ? R"(, "trigger": {"source": "beat", "everyN": 4, "offset": 0})" : "";
    const std::string field = options.wave ? R"(,
      { "name": "ripple", "kind": "field", "position": [0, 0, 0], "field": {
          "name": "ripple", "kind": "wave", "waveGeometry": "radial", "waveShape": "pulse",
          "wavelength": 10.0, "waveSpeed": 12.0, "waveWidth": 0.0)" + trigger + R"( } })"
                                           : "";
    const auto box = [](const char* name, float x, float y, float z, float s, const char* material,
                        const std::string& extra = {}) {
        return fmt::format(R"({{ "name": "{}", "kind": "procedural", "position": [{}, {}, {}], "procedural": {{
            "source": {{ "kind": "box", "size": [{}, {}, {}] }}, "distribution": {{ "kind": "single" }},
            "material": {}{} }} }})",
                           name, x, y, z, s, s * 0.6f, s, material, extra);
    };
    const char* cap = R"({ "program": "capglow", "baseColor": [0.02, 0.02, 0.05], "roughness": 0.5 })";
    const char* gills = R"({ "program": "tissue", "baseColor": [0.01, 0.02, 0.04], "roughness": 0.4 })";
    const char* stem = R"({ "baseColor": [0.3, 0.3, 0.32], "emissiveColor": [0.9, 0.8, 0.7], "emissiveIntensity": 0.3 })";
    std::string heroes;
    struct Hero {
        const char* cap;
        const char* gills;
        float x, z, size;
    };
    const Hero list[] = {{"elder-cap", "elder-gills", 0.0f, 0.0f, 4.0f},
                         {"lantern-cap", "lantern-gills", -14.0f, -6.0f, 2.0f},
                         {"spire-cap", "spire-gills", 14.0f, -6.0f, 1.6f},
                         {"moss-cap", "moss-gills", 22.0f, 8.0f, 1.2f}};
    for (const Hero& h : list) {
        heroes += ",\n      " + box(h.cap, h.x, h.size * 1.2f, h.z, h.size, cap);
        // The smallest hero's gills take the wave too: a node's light wave (procedural emissiveField).
        const bool wavy = options.wave && std::string_view(h.gills) == "moss-gills";
        heroes += ",\n      " + box(h.gills, h.x, h.size * 0.7f, h.z, h.size * 0.8f, gills,
                                    wavy ? R"(, "emissiveField": "ripple", "emissiveFieldAmount": 3.0)" : "");
    }
    heroes += ",\n      " + box("elder-stem", 0.0f, 1.0f, 0.0f, 1.0f, stem);
    return R"({
      "format": "avgen-scene", "version": 1, "name": "glade",
      "camera": { "mode": 1, "position": [0, 16, 46], "target": [0, 1.5, 0], "fov": 55.0 },
      "environment": { "background": [0.004, 0.006, 0.014], "intensity": 0.15, "stylized": true,
        "volumeDensity": 0.004, "volumeScattering": 0.5, "volumeSteps": 12, "volumeMaxDistance": 160.0,
        "fogColor": [0.10, 0.18, 0.32], "ecologyLight": 1.4, "ecologyLightRange": 200.0,
        "lightRig": { "format": "avgen-lightrig", "version": 1, "name": "Glade", "keyIntensity": 1.5,
          "ambientIntensity": 0.3, "ambientColor": [0.4, 0.5, 0.64],
          "lights": [
            { "name": "moon", "type": "directional", "role": "key", "azimuth": 64.0, "elevation": 30.0,
              "intensity": 1.0, "color": [0.5, 0.62, 1.0], "castsShadow": false, "followCamera": false },
            { "name": "elder-practical", "type": "point", "role": "practical", "azimuth": 0.0, "elevation": -90.0,
              "distance": 0.2, "intensity": 3.0, "color": [0.2, 0.8, 0.5], "followCamera": false } ] } },
      "wind": { "enabled": true, "direction": 0.6, "speed": 0.8, "gustAmount": 0.5, "turbulence": 0.25 },
      "materialPrograms": [
        { "name": "tissue", "ops": [ { "kind": "constant", "dst": 4, "constant": [0.08, 0.95, 0.53, 1.0] } ],
          "emission": 4, "emissionIntensity": 2.0 },
        { "name": "capglow", "ops": [ { "kind": "constant", "dst": 4, "constant": [1.0, 0.47, 0.15, 1.0] } ],
          "emission": 4, "emissionIntensity": 1.5 } ],
      "heroes": [
        { "name": "elder-cap", "position": [0, 4.8, 0], "radius": 3.0, "height": 6.0, "importance": 0.6 },
        { "name": "lantern-cap", "position": [-14, 2.4, -6], "radius": 1.5, "height": 3.0, "importance": 0.5 },
        { "name": "spire-cap", "position": [14, 1.9, -6], "radius": 1.2, "height": 3.0, "importance": 0.45 },
        { "name": "moss-cap", "position": [22, 1.4, 8], "radius": 1.0, "height": 2.0, "importance": 0.3 },
        { "name": "walker", "position": [6, 0, 12], "radius": 0.8, "height": 2.0, "importance": 0.55 } ],
      "effects": [
        { "id": "aurora", "type": "aurora", "name": "Aurora", "owner": { "kind": "world" }, "enabled": true,
          "activation": "always" } ],
      "nodes": [
        { "name": "meadow", "kind": "terrain",
          "world": { "name": "glade", "size": [120, 120], "features": [
            { "name": "pond", "kind": "river", "path": [[-30, 0.0, 16], [-16, 0.0, 22]], "width": 12.0,
              "amplitude": 2.0, "flatten": 1.0, "water": true, "waterDepth": 0.8 } ] },
          "terrain": { "chunkSize": 60.0, "resolution": 24, "lodLevels": 1, "viewDistance": 300.0,
                       "water": { "enabled": true, "glow": 0.4, "sparkle": 1.0, "swell": 0.02 } },
          "material": { "baseColor": [0.18, 0.2, 0.16], "roughness": 0.9 },
          "scatter": [
            { "name": "fungi", "asset": ")" + asset + R"(", "densities": { "meadow": 0.12, "forest": 0.12 },
              "height": 0.3, "emissiveIntensity": 6.0, "emissiveColor": [0.34, 0.08, 1.0],
              "materialProgram": "tissue", "castsShadow": false )" + fungiWave + R"( },
            { "name": "shelf", "asset": ")" + asset + R"(", "densities": { "meadow": 0.03, "forest": 0.03 },
              "height": 0.55, "emissiveIntensity": 4.0, "emissiveColor": [0.05, 1.0, 0.62],
              "materialProgram": "tissue", "castsShadow": false },
            { "name": "lamps", "asset": ")" + asset + R"(", "densities": { "meadow": 0.004, "forest": 0.004 },
              "height": 1.5, "emissiveIntensity": 6.0, "emissiveColor": [0.06, 0.82, 1.0],
              "materialProgram": "tissue", "castsShadow": false },
            { "name": "grass", "asset": ")" + asset + R"(", "densities": { "meadow": 0.2, "forest": 0.1 },
              "height": 0.6, "emissiveIntensity": 0.3, "emissiveColor": [0.05, 1.0, 0.55], "castsShadow": false },
            { "name": "stones", "asset": ")" + asset + R"(", "densities": { "meadow": 0.01, "forest": 0.01 },
              "height": 0.5, "castsShadow": false } ] },
        { "name": "spores", "kind": "particles", "position": [-6, 3, 24], "particles": { "position": [0, 0, 0], "shape": "disc", "seed": 17,
            "extent": [18, 2, 18], "capacity": 1024, "spawnRate": 60.0, "lifetimeMin": 6.0, "lifetimeMax": 9.0,
            "speedMin": 0.1, "speedMax": 0.3, "direction": [0, 1, 0], "spread": 0.4, "sizeStart": 0.18,
            "sizeEnd": 0.08, "blend": "additive", "colorStart": [0.3, 1.0, 0.7, 1.0], "colorEnd": [0.3, 1.0, 0.7, 0.0],
            "emissive": 3.0 } },
        { "name": "elder-spores", "kind": "particles", "parent": "elder-cap", "position": [0, 2, 0],
          "particles": { "position": [0, 0, 0], "shape": "disc", "seed": 23, "extent": [4, 0.2, 4], "capacity": 512,
            "spawnRate": 40.0, "lifetimeMin": 5.0, "lifetimeMax": 7.0, "speedMin": 0.1, "speedMax": 0.2,
            "direction": [0, -1, 0], "spread": 0.3, "sizeStart": 0.15, "sizeEnd": 0.06, "blend": "additive",
            "colorStart": [1.0, 0.47, 0.15, 1.0], "colorEnd": [1.0, 0.47, 0.15, 0.0], "emissive": 3.5 } },
        { "name": "walker", "kind": "gltf", "asset": ")" + characterAsset.generic_string() + R"(",
          "position": [6, 0, 12], "scale": [1.5, 1.5, 1.5] })" + heroes + field + R"(
      ],
      "entities": [ { "name": "walker", "node": "walker", "seed": 3 } ]
    })";
}

// 120 BPM, 32 bars, the break's two bars (13-14) kickless.
inline const Groove& gladeGroove() {
    static const Groove g = [] {
        GrooveSpec spec;
        spec.bpm = 120.0;
        spec.bars = 32;
        spec.kicklessBars = {12, 13};
        return makeGroove(spec);
    }();
    return g;
}

inline std::filesystem::path gladeWav() {
    static const std::filesystem::path path = [] {
        const auto p = processTempDir() / "reactivity_glade.wav";
        (void)gladeGroove().file.writeWav(p);
        return p;
    }();
    return path;
}

// The sections, on the groove's bar lines.
inline song::SectionTimeline gladeSections() {
    const auto& bars = gladeGroove().truth.downbeats;
    song::SectionTimeline t;
    t.durationSeconds = gladeGroove().truth.seconds;
    const auto section = [](const char* type, double start, double end, float energy) {
        song::Section s;
        s.type = type;
        s.startSeconds = start;
        s.endSeconds = end;
        s.energy = energy;
        s.density = energy * 0.5f;
        s.authored = true;
        return s;
    };
    t.sections.push_back(section("intro", 0.0, bars[4], 0.30f));
    t.sections.push_back(section("groove", bars[4], bars[12], 0.60f));
    t.sections.push_back(section("breakdown", bars[12], bars[14], 0.20f));
    t.sections.push_back(section("build", bars[14], bars[18], 0.75f));
    t.sections.push_back(section("drop", bars[18], bars[28], 1.00f));
    t.sections.push_back(section("outro", bars[28], t.durationSeconds, 0.35f));
    t.renumber();
    return t;
}

// The glade in an offline engine, with its audio and its sections. False when it will not load.
inline bool loadGlade(app::Engine& engine, const std::filesystem::path& scatterAsset,
                      const std::filesystem::path& characterAsset, const GladeOptions& options = {}) {
    if (!engine.setCompositionJson(nlohmann::json::parse(gladeSceneJson(scatterAsset, characterAsset, options)))) {
        return false;
    }
    if (!engine.loadAudio(gladeWav())) {
        return false;
    }
    seq::Sequence piece = engine.sequence();
    piece.sectionTimeline = gladeSections();
    return engine.setSequence(std::move(piece)).has_value();
}

} // namespace avgen::testsupport
