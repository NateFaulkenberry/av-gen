#include "world/effect_params.hpp"

#include "params/parameter_set.hpp"

#include <algorithm>

namespace avgen::world {
namespace {

// Local desc builders, the same shape scene/post_settings.cpp uses. The soft range is what the UI
// offers; the hard range is what a modulation route is clamped to, and the two differ wherever an
// authored value may legitimately go past what a slider should reach for.
params::ParamDesc<float> f(std::string path, float def, float lo, float hi, float slo, float shi) {
    params::ParamDesc<float> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = slo;
    d.softMax = shi;
    return d;
}
params::ParamDesc<bool> b(std::string path, bool def) {
    params::ParamDesc<bool> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = false;
    d.hardMax = true;
    return d;
}
params::ParamDesc<glm::vec3> col(std::string path, glm::vec3 def) {
    params::ParamDesc<glm::vec3> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = glm::vec3(0.0f);
    d.hardMax = glm::vec3(8.0f);
    d.softMin = glm::vec3(0.0f);
    d.softMax = glm::vec3(1.0f);
    d.isColor = true;
    return d;
}

} // namespace

std::string worldEffectParameterPrefix(std::string_view effectName) {
    std::string prefix = "worldfx/";
    prefix.append(effectName);
    prefix.push_back('/');
    return prefix;
}

const WorldEffectParams* WorldEffectParameters::find(std::string_view name) const {
    for (const WorldEffectParams& p : effects) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

WorldEffectParameters registerWorldEffectParameters(params::ParameterSet& params,
                                                    std::span<const WorldEffect> effects) {
    WorldEffectParameters out;
    out.effects.reserve(effects.size());
    out.registered.reserve(effects.size() * 33);
    for (const WorldEffect& e : effects) {
        const std::string base = worldEffectParameterPrefix(e.name);
        WorldEffectParams p;
        p.name = e.name;
        const auto path = [&](const char* leaf) {
            std::string full = base + leaf;
            out.registered.push_back(full);
            return full;
        };
        p.enabled = &params.add(b(path("enabled"), e.enabled));

        p.color = &params.add(col(path("color"), e.appearance.color));
        p.intensity = &params.add(f(path("intensity"), e.appearance.intensity, 0.0f, 40.0f, 0.0f, 8.0f));
        p.edgeColor = &params.add(col(path("edgeColor"), e.appearance.edgeColor));
        p.edgeIntensity =
            &params.add(f(path("edgeIntensity"), e.appearance.edgeIntensity, 0.0f, 60.0f, 0.0f, 10.0f));
        p.width = &params.add(f(path("width"), e.appearance.width, 0.05f, 8.0f, 0.25f, 3.0f));

        p.rainbow = &params.add(b(path("rainbow"), e.appearance.rainbow));
        p.rainbowSpeed = &params.add(f(path("rainbowSpeed"), e.appearance.rainbowSpeed, -4.0f, 4.0f, -1.0f, 1.0f));
        // Cycles per metre. The soft range tops out where a 300 m beam holds about three full hue
        // sweeps, which is as busy as the eye reads as a rainbow rather than as a stripe pattern.
        p.rainbowScale = &params.add(f(path("rainbowScale"), e.appearance.rainbowScale, 0.0f, 0.5f, 0.0f, 0.05f));
        p.rainbowSaturation =
            &params.add(f(path("rainbowSaturation"), e.appearance.rainbowSaturation, 0.0f, 1.0f, 0.0f, 1.0f));
        p.rainbowBrightness =
            &params.add(f(path("rainbowBrightness"), e.appearance.rainbowBrightness, 0.0f, 4.0f, 0.0f, 2.0f));

        p.sparkle = &params.add(b(path("sparkle"), e.sparkle.enabled));
        p.sparkleDensity = &params.add(f(path("sparkleDensity"), e.sparkle.density, 0.0f, 6.0f, 0.05f, 2.0f));
        p.sparkleSize = &params.add(f(path("sparkleSize"), e.sparkle.size, 0.0f, 1.0f, 0.05f, 0.8f));
        p.sparkleIntensity =
            &params.add(f(path("sparkleIntensity"), e.sparkle.intensity, 0.0f, 30.0f, 0.0f, 6.0f));
        p.sparkleSpeed = &params.add(f(path("sparkleSpeed"), e.sparkle.speed, 0.0f, 8.0f, 0.0f, 3.0f));

        p.speed = &params.add(f(path("speed"), e.propagation.speed, 0.05f, 800.0f, 1.0f, 200.0f));
        p.range = &params.add(f(path("range"), e.propagation.range, 0.5f, 2000.0f, 5.0f, 400.0f));
        p.frontWidth = &params.add(f(path("frontWidth"), e.propagation.frontWidth, 0.05f, 200.0f, 0.5f, 40.0f));
        p.trailLength = &params.add(f(path("trailLength"), e.propagation.trailLength, 0.0f, 500.0f, 0.0f, 120.0f));
        p.falloff = &params.add(f(path("falloff"), e.propagation.falloff, 0.05f, 8.0f, 0.5f, 4.0f));
        p.startOffset = &params.add(f(path("startOffset"), e.propagation.startOffset, -200.0f, 400.0f, 0.0f, 60.0f));
        p.verticalExtent =
            &params.add(f(path("verticalExtent"), e.propagation.verticalExtent, 0.0f, 500.0f, 0.5f, 120.0f));
        p.ringCount = &params.add(f(path("ringCount"), e.propagation.ringCount, 0.0f, 16.0f, 0.0f, 6.0f));
        p.beamRadius = &params.add(f(path("beamRadius"), e.propagation.beamRadius, 0.0f, 1000.0f, 0.0f, 200.0f));

        p.responseGround = &params.add(f(path("response/ground"), e.response.ground, 0.0f, 6.0f, 0.0f, 2.0f));
        p.responseFoliage = &params.add(f(path("response/foliage"), e.response.foliage, 0.0f, 6.0f, 0.0f, 2.0f));
        p.responseSurface = &params.add(f(path("response/surface"), e.response.surface, 0.0f, 6.0f, 0.0f, 2.0f));
        p.responseEmissive =
            &params.add(f(path("response/emissive"), e.response.emissive, 0.0f, 6.0f, 0.0f, 2.0f));

        // Timing is float here and double on the effect: a parameter is float components by
        // definition (ADR-011), and seconds on a timeline do not need the mantissa.
        p.delay = &params.add(f(path("delay"), static_cast<float>(e.timing.delay), 0.0f, 120.0f, 0.0f, 8.0f));
        p.lifetime =
            &params.add(f(path("lifetime"), static_cast<float>(e.timing.lifetime), 0.0f, 600.0f, 0.0f, 30.0f));
        p.fadeIn = &params.add(f(path("fadeIn"), static_cast<float>(e.timing.fadeIn), 0.0f, 60.0f, 0.0f, 6.0f));
        p.fadeOut = &params.add(f(path("fadeOut"), static_cast<float>(e.timing.fadeOut), 0.0f, 60.0f, 0.0f, 6.0f));
        p.repeat =
            &params.add(f(path("repeat"), static_cast<float>(e.timing.repeatSeconds), 0.0f, 120.0f, 0.0f, 12.0f));

        out.effects.push_back(p);
    }
    return out;
}

void unregisterWorldEffectParameters(params::ParameterSet& params, WorldEffectParameters& registered) {
    for (const std::string& path : registered.registered) {
        params.remove(path);
    }
    registered.registered.clear();
    registered.effects.clear();
}

namespace {

// The whole copy-out, once, with a switch for which of a parameter's two values it reads.
//
// `final` is this frame's modulated value and is what the renderer wants; `base` is the authored
// value a slider wrote and is what a *save* wants. The same list of fields serves both, because two
// copies of that list is how the two drift.
void copyParameters(const WorldEffectParameters& registered, std::span<WorldEffect> live, bool fromBase) {
    if (registered.effects.empty()) {
        return;
    }
    const auto v1 = [fromBase](const params::IParameter* p) {
        return fromBase ? p->baseComponent(0) : p->finalComponent(0);
    };
    const auto v3 = [fromBase](const params::IParameter* p) {
        return fromBase ? glm::vec3(p->baseComponent(0), p->baseComponent(1), p->baseComponent(2))
                        : glm::vec3(p->finalComponent(0), p->finalComponent(1), p->finalComponent(2));
    };
    for (WorldEffect& e : live) {
        const WorldEffectParams* p = registered.find(e.name);
        if (p == nullptr || p->enabled == nullptr) {
            continue; // not registered: leave the authored values exactly as they are
        }
        e.enabled = v1(p->enabled) >= 0.5f;

        e.appearance.color = v3(p->color);
        e.appearance.intensity = v1(p->intensity);
        e.appearance.edgeColor = v3(p->edgeColor);
        e.appearance.edgeIntensity = v1(p->edgeIntensity);
        e.appearance.width = std::max(v1(p->width), 1e-3f);

        e.appearance.rainbow = v1(p->rainbow) >= 0.5f;
        e.appearance.rainbowSpeed = v1(p->rainbowSpeed);
        e.appearance.rainbowScale = v1(p->rainbowScale);
        e.appearance.rainbowSaturation = v1(p->rainbowSaturation);
        e.appearance.rainbowBrightness = v1(p->rainbowBrightness);

        e.sparkle.enabled = v1(p->sparkle) >= 0.5f;
        e.sparkle.density = v1(p->sparkleDensity);
        e.sparkle.size = v1(p->sparkleSize);
        e.sparkle.intensity = v1(p->sparkleIntensity);
        e.sparkle.speed = v1(p->sparkleSpeed);

        // Clamped where zero would be a division rather than an "off". The hard ranges keep a route
        // inside them; this is the belt for an authored value that predates a range.
        e.propagation.speed = std::max(v1(p->speed), 1e-3f);
        e.propagation.range = std::max(v1(p->range), 1e-2f);
        e.propagation.frontWidth = std::max(v1(p->frontWidth), 1e-3f);
        e.propagation.trailLength = std::max(v1(p->trailLength), 0.0f);
        e.propagation.falloff = std::max(v1(p->falloff), 1e-2f);
        e.propagation.startOffset = v1(p->startOffset);
        e.propagation.verticalExtent = std::max(v1(p->verticalExtent), 0.0f);
        e.propagation.ringCount = std::max(v1(p->ringCount), 0.0f);
        e.propagation.beamRadius = std::max(v1(p->beamRadius), 0.0f);

        e.response.ground = v1(p->responseGround);
        e.response.foliage = v1(p->responseFoliage);
        e.response.surface = v1(p->responseSurface);
        e.response.emissive = v1(p->responseEmissive);

        e.timing.delay = static_cast<double>(v1(p->delay));
        e.timing.lifetime = static_cast<double>(v1(p->lifetime));
        e.timing.fadeIn = static_cast<double>(v1(p->fadeIn));
        e.timing.fadeOut = static_cast<double>(v1(p->fadeOut));
        e.timing.repeatSeconds = static_cast<double>(v1(p->repeat));
    }
}

} // namespace

void applyWorldEffectParameters(const WorldEffectParameters& registered, std::span<WorldEffect> live) {
    copyParameters(registered, live, /*fromBase=*/false);
}

void captureWorldEffectParameters(const WorldEffectParameters& registered, std::span<WorldEffect> authored) {
    copyParameters(registered, authored, /*fromBase=*/true);
}

} // namespace avgen::world
