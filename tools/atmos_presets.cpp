// Dumps every atmospheric preset (ADR-230, §10) as JSON, so a scene emitter can use the real preset
// code rather than a second copy of its numbers.
//
// A build target rather than a table in the Python emitter, for the usual reason: a script that held its own copy of every tuned radiance would drift from
// `applyCometStyle` / `applyAuroraStyle` silently, and the first symptom would be a shipped example
// that no longer matches the preset the UI offers under the same name.
//
//   avgen_atmos_presets                 -- every preset, as {"comet": {...}, "aurora": {...}}
//   avgen_atmos_presets "Rainbow Cosmic" [name]   -- one preset, as a scene-ready effect
//
// The one-preset form is what the emitter calls: it names the style and the effect, and gets back
// exactly what `EffectInstance::toJson` would write, defaults and all.

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <string_view>

using namespace avgen;

int main(int argc, char** argv) {
    using json = nlohmann::json;

    if (argc >= 2) {
        const std::string style = argv[1];
        const std::string name = argc >= 3 ? argv[2] : style;
        world::EffectInstance effect;
        effect.name = name;
        effect.kind = world::EffectKind::Comet;
        if (world::applyCometStyle(effect, style)) {
            // The shipped comet lifecycle: an event with a window a sequencer can move, not scenery.
            effect = world::bioluminescentComet(name);
            world::applyCometStyle(effect, style);
        } else if (world::applyAuroraStyle(effect, style)) {
            effect = world::glowmereAurora(name);
            world::applyAuroraStyle(effect, style);
        } else {
            std::fprintf(stderr, "unknown preset '%s'\n", style.c_str());
            return 2;
        }
        // ADR-702: an instance needs an id; a preset dumped for a scene is World-owned.
        effect.id = "preset";
        if (auto ok = effect.validate(); !ok) {
            std::fprintf(stderr, "preset '%s' does not validate: %s\n", style.c_str(),
                         ok.error().message.c_str());
            return 3;
        }
        std::printf("%s\n", effect.toJson().dump(2).c_str());
        return 0;
    }

    json out;
    json comets = json::object();
    for (const std::string_view style : world::cometStyleNames()) {
        world::EffectInstance e = world::bioluminescentComet(std::string(style));
        if (!world::applyCometStyle(e, style)) {
            std::fprintf(stderr, "comet style '%.*s' did not apply\n", static_cast<int>(style.size()),
                         style.data());
            return 3;
        }
        e.id = "preset";
        comets[std::string(style)] = e.toJson();
    }
    json auroras = json::object();
    for (const std::string_view style : world::auroraStyleNames()) {
        world::EffectInstance e = world::glowmereAurora(std::string(style));
        if (!world::applyAuroraStyle(e, style)) {
            std::fprintf(stderr, "aurora style '%.*s' did not apply\n", static_cast<int>(style.size()),
                         style.data());
            return 3;
        }
        e.id = "preset";
        auroras[std::string(style)] = e.toJson();
    }
    out["comet"] = std::move(comets);
    out["aurora"] = std::move(auroras);
    std::printf("%s\n", out.dump(2).c_str());
    return 0;
}
