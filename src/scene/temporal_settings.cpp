#include "scene/temporal_settings.hpp"

#include "params/parameter_set.hpp"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>

namespace avgen::scene {
namespace {

params::ParamDesc<float> f(const char* path, float def, float lo, float hi, float slo, float shi) {
    params::ParamDesc<float> d;
    d.path = path;
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = slo;
    d.softMax = shi;
    return d;
}

params::ParamDesc<bool> b(const char* path, bool def) {
    params::ParamDesc<bool> d;
    d.path = path;
    d.defaultValue = def;
    d.hardMin = false;
    d.hardMax = true;
    return d;
}

// The authoring ceiling (§8: 1-32). `scene/` must not include a WebGPU header, so this cannot
// simply BE `rendering::kMaxTemporalFrames` -- but two ceilings that silently disagree is how an
// authored 32 becomes a rendered 16. `temporal_effects.cpp` sees both headers and static_asserts
// them equal, so a change to either fails the build rather than the picture.
constexpr int kMaxFrames = kMaxTemporalFrames;

[[nodiscard]] int clampFrames(int frames) { return std::clamp(frames, 1, kMaxFrames); }

} // namespace

bool temporalEffectKindFromName(std::string_view name, TemporalEffectKind& out) {
    for (const auto kind : kTemporalEffectKinds) {
        if (name == temporalEffectKindName(kind)) {
            out = kind;
            return true;
        }
    }
    return false;
}

std::uint32_t temporalEffectHistoryFrames(TemporalEffectKind kind, const TemporalSettings& s) {
    switch (kind) {
    case TemporalEffectKind::FrameEcho:
        return s.echo.enabled ? static_cast<std::uint32_t>(clampFrames(s.echo.frames)) : 0u;
    case TemporalEffectKind::Count: break;
    }
    return 0u;
}

bool temporalEffectEnabled(TemporalEffectKind kind, const TemporalSettings& s) {
    switch (kind) {
    case TemporalEffectKind::FrameEcho: return s.echo.enabled;
    case TemporalEffectKind::Count: break;
    }
    return false;
}

std::uint32_t TemporalSettings::historyFrames() const {
    std::uint32_t needed = 0;
    for (const auto kind : kTemporalEffectKinds) {
        needed = std::max(needed, temporalEffectHistoryFrames(kind, *this));
    }
    return needed;
}

bool TemporalSettings::anyEnabled() const {
    return std::any_of(kTemporalEffectKinds.begin(), kTemporalEffectKinds.end(),
                       [this](TemporalEffectKind k) { return temporalEffectEnabled(k, *this); });
}

std::string temporalParameterPrefix(TemporalEffectKind kind) {
    return std::string("temporal/") + temporalEffectKindName(kind) + "/";
}

TemporalParameters registerTemporalParameters(params::ParameterSet& params, const TemporalSettings& defaults) {
    TemporalParameters p;
    const std::string echo = temporalParameterPrefix(TemporalEffectKind::FrameEcho);
    p.echoEnabled = &params.add(b((echo + "enabled").c_str(), defaults.echo.enabled));
    // A float carrying an integer count, because that is what the parameter system and every
    // modulation route speak. Rounded, never truncated, at the one place it is read back: a route
    // that lands on 5.999 must mean six frames, not five.
    p.echoFrames = &params.add(f((echo + "frames").c_str(), static_cast<float>(clampFrames(defaults.echo.frames)),
                                 1.0f, static_cast<float>(kMaxFrames), 1.0f, 16.0f));
    p.echoStrength = &params.add(f((echo + "strength").c_str(), defaults.echo.strength, 0.0f, 1.0f, 0.0f, 1.0f));
    p.echoDecay = &params.add(f((echo + "decay").c_str(), defaults.echo.decay, 0.0f, 0.99f, 0.0f, 0.95f));
    return p;
}

void applyTemporalParameters(const TemporalParameters& p, TemporalSettings& settings) {
    if (p.echoEnabled != nullptr) settings.echo.enabled = p.echoEnabled->value();
    if (p.echoFrames != nullptr) {
        settings.echo.frames = clampFrames(static_cast<int>(std::lround(p.echoFrames->value())));
    }
    if (p.echoStrength != nullptr) settings.echo.strength = p.echoStrength->value();
    if (p.echoDecay != nullptr) settings.echo.decay = p.echoDecay->value();
}

nlohmann::json temporalToJson(const TemporalSettings& s) {
    nlohmann::json j = nlohmann::json::object();
    // Written unconditionally rather than "only when enabled": a reader that can turn the effect
    // on must have a writer that can turn it off again, or disabling it does not survive a save.
    // That asymmetry is ADR-350's exact defect and it is invisible until someone reloads.
    nlohmann::json echo = nlohmann::json::object();
    echo["enabled"] = s.echo.enabled;
    echo["frames"] = clampFrames(s.echo.frames);
    echo["strength"] = s.echo.strength;
    echo["decay"] = s.echo.decay;
    j["echo"] = std::move(echo);
    return j;
}

Result<TemporalSettings> temporalFromJson(const nlohmann::json& j) {
    TemporalSettings s;
    if (!j.is_object()) {
        return fail("temporal: expected an object");
    }
    if (const auto it = j.find("echo"); it != j.end()) {
        if (!it->is_object()) {
            return fail("temporal.echo: expected an object");
        }
        const auto& e = *it;
        if (const auto v = e.find("enabled"); v != e.end() && v->is_boolean()) s.echo.enabled = v->get<bool>();
        if (const auto v = e.find("frames"); v != e.end() && v->is_number()) {
            s.echo.frames = clampFrames(v->get<int>());
        }
        if (const auto v = e.find("strength"); v != e.end() && v->is_number()) {
            s.echo.strength = std::clamp(v->get<float>(), 0.0f, 1.0f);
        }
        if (const auto v = e.find("decay"); v != e.end() && v->is_number()) {
            s.echo.decay = std::clamp(v->get<float>(), 0.0f, 0.99f);
        }
    }
    return s;
}

} // namespace avgen::scene
