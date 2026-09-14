#include "scene/tree_audio.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::scene {
namespace {

params::ParamDesc<float> f(std::string path, float def, float lo, float hi) {
    params::ParamDesc<float> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = lo;
    d.softMax = hi;
    return d;
}

params::ModRoute route(std::string source, std::string target, float amount, float attackMs, float decayMs,
                       params::ModOp op = params::ModOp::Add) {
    params::ModRoute r;
    r.source = std::move(source);
    r.target = std::move(target);
    r.amount = amount;
    r.op = op;
    r.chain.attackMs = attackMs;
    r.chain.decayMs = decayMs;
    return r;
}

} // namespace

TreeParameters registerTreeParameters(params::ParameterSet& params, const std::string& prefix,
                                      const TreeLook& look) {
    TreeParameters out;
    const auto add = [&params, &out](params::ParamDesc<float> d) {
        auto& p = params.add(std::move(d));
        out.all.push_back(&p);
        return &p;
    };
    // The rest values ARE the authored look. Every route below is `op: add`, so with no audio the
    // finals equal the bases and the scene renders exactly what was authored -- there is no "audio
    // off" branch anywhere, which is why silence cannot look different from a bug.
    out.windSpeed = add(f(prefix + "wind/speed", 0.55f, 0.0f, 3.0f));
    out.windGust = add(f(prefix + "wind/gust", 0.18f, 0.0f, 2.0f));
    out.windFlutter = add(f(prefix + "wind/flutter", 0.25f, 0.0f, 2.0f));
    out.windImpulse = add(f(prefix + "wind/impulse", 0.0f, 0.0f, 2.0f));
    out.foliageEmissive = add(f(prefix + "emissive/foliage", look.foliageEmissiveIntensity, 0.0f, 6.0f));
    out.veinEmissive = add(f(prefix + "emissive/veins", 1.0f, 0.0f, 6.0f));
    out.foliageHueDrift = add(f(prefix + "color/hueDrift", 0.0f, -1.0f, 1.0f));

    params::ParamDesc<glm::vec3> dir;
    dir.path = prefix + "wind/direction";
    dir.defaultValue = glm::vec3(1.0f, 0.0f, 0.25f);
    dir.hardMin = glm::vec3(-1.0f);
    dir.hardMax = glm::vec3(1.0f);
    dir.softMin = dir.hardMin;
    dir.softMax = dir.hardMax;
    out.windDirection = &params.add(std::move(dir));
    out.all.push_back(out.windDirection);
    return out;
}

std::vector<params::ModRoute> defaultTreeRoutes(const std::string& prefix) {
    std::vector<params::ModRoute> routes;

    // --- Continuous. Slow enough that nothing here can read as a beat. -------------------------
    // Bass to the wind's steady lean, with a three-second decay: this is the tree breathing, and at
    // this time constant a listener cannot associate it with any individual note.
    routes.push_back(route("audio.bass", prefix + "wind/speed", 0.10f, 900.0f, 3000.0f));
    // Overall energy to the canopy's emission. Colour, not position -- section 25 asks the tree to
    // breathe through colour, and a 2.4 s decay is what stops it flashing.
    routes.push_back(route("audio.rms", prefix + "emissive/foliage", 0.22f, 700.0f, 2400.0f));
    // Low-mid to the gust envelope: the intermediate scale, where secondary limbs live.
    routes.push_back(route("audio.lowMid", prefix + "wind/gust", 0.09f, 400.0f, 1600.0f));
    // Mid to hue drift. The palette moves, slowly, and never far.
    routes.push_back(route("audio.mid", prefix + "color/hueDrift", 0.14f, 1200.0f, 4000.0f));
    // High-mid and treble to flutter, which only reaches the outermost joints.
    routes.push_back(route("audio.highMid", prefix + "wind/flutter", 0.13f, 120.0f, 700.0f));
    routes.push_back(route("audio.treble", prefix + "wind/flutter", 0.07f, 60.0f, 400.0f));
    // Spectral flux is energy rather than pitch: it belongs on the same target as flutter.
    routes.push_back(route("audio.spectralFlux", prefix + "wind/flutter", 0.10f, 90.0f, 600.0f));

    // --- Punctuation. Peak-hold, so an onset is a shove and not a sustained lean. --------------
    params::ModRoute onset = route("audio.onset", prefix + "wind/impulse", 0.45f, 8.0f, 320.0f);
    onset.chain.envelope = params::EnvelopeMode::PeakHold;
    onset.chain.envelopeFallPerSecond = 5.0f;
    routes.push_back(onset);
    // The life pulse: a beat brightens the veins briefly. 12% of a rest value of 1.0, inside the
    // 4-to-24% band the house discipline uses for event routes.
    params::ModRoute beat = route("music.beat", prefix + "emissive/veins", 0.12f, 15.0f, 520.0f);
    beat.chain.envelope = params::EnvelopeMode::PeakHold;
    beat.chain.envelopeFallPerSecond = 4.0f;
    routes.push_back(beat);

    // --- Form. Seconds long, and the only thing here allowed to change the shot. ---------------
    routes.push_back(route("music.build", prefix + "wind/speed", 0.16f, 2500.0f, 4000.0f));
    routes.push_back(route("music.break", prefix + "wind/speed", -0.13f, 2000.0f, 3500.0f));
    routes.push_back(route("music.drop", prefix + "emissive/foliage", 0.30f, 200.0f, 2800.0f));
    return routes;
}

TreeMotionInputs readTreeMotion(const TreeParameters& parameters, double time) {
    TreeMotionInputs out;
    out.time = time;
    if (!parameters.bound()) {
        return out;
    }
    out.windSpeed = parameters.windSpeed->value();
    out.gust = parameters.windGust->value();
    out.flutter = parameters.windFlutter->value();
    out.impulse = parameters.windImpulse->value();
    const glm::vec3 dir = parameters.windDirection->value();
    out.windDirection = glm::length(dir) > 1e-5f ? dir : glm::vec3(1.0f, 0.0f, 0.25f);
    return out;
}

void applyTreeLook(const TreeParameters& parameters, const TreeLook& look, Scene& scene) {
    if (!parameters.bound()) {
        return;
    }
    const float foliage = parameters.foliageEmissive->value();
    const float veins = parameters.veinEmissive->value();
    // The vein gain goes to the PROGRAM's intensity, not to a material's. A program that asserts
    // emission owns the whole contract, so this is where the audio has to land for the branches --
    // writing it to `Material::emissiveIntensity` instead would be a route that resolves, applies,
    // and changes nothing, which is the hardest kind of dead wiring to notice.
    for (MaterialProgram& program : scene.materialPrograms) {
        if (program.name == "tree.veins") {
            program.emissionIntensity = look.veins.intensity * veins;
        }
    }
    const float drift = parameters.foliageHueDrift->value();
    for (Entity& entity : scene.entities) {
        if (entity.name.rfind("tree.foliage", 0) == 0) {
            const std::size_t tint = entity.name.size() > 12
                                         ? static_cast<std::size_t>(entity.name.back() - '0')
                                         : 0;
            const std::size_t t = std::min(tint, static_cast<std::size_t>(kFoliageTints - 1));
            entity.material.emissiveIntensity = foliage * look.foliageEmissiveScale[t];
            // Hue drift as a channel rotation rather than a real hue rotate: the palette is
            // deliberately narrow -- emerald through turquoise -- so sliding energy between the
            // green and blue channels moves along exactly the axis the palette occupies, and cannot
            // wander off it into a colour the art direction never chose.
            glm::vec3 c = look.foliageEmissiveTint[t];
            const float shift = drift * 0.30f;
            entity.material.emissiveColor = glm::vec3(c.r, c.g * (1.0f - shift), c.b * (1.0f + shift));
        } else if (entity.name.rfind("tree.", 0) == 0 && entity.name != "tree.ground" &&
                   entity.material.program.empty()) {
            // Only reached when the veins are switched off. With a program present the branch's
            // emission is the program's to assert, and this must not write a value the shader will
            // silently drop.
            const float base = entity.name == "tree.trunk"       ? look.trunkEmissive
                               : entity.name == "tree.primary"   ? look.primaryEmissive
                               : entity.name == "tree.secondary" ? look.secondaryEmissive
                               : entity.name == "tree.roots"     ? look.tertiaryEmissive * 0.5f
                                                                 : look.tertiaryEmissive;
            entity.material.emissiveIntensity = base * veins;
        }
    }
}

} // namespace avgen::scene
