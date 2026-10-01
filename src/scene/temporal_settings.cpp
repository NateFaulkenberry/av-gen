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
    case TemporalEffectKind::Mosh:
        return s.mosh.enabled ? static_cast<std::uint32_t>(clampFrames(s.mosh.frames)) : 0u;
    case TemporalEffectKind::Count: break;
    }
    return 0u;
}

bool temporalEffectEnabled(TemporalEffectKind kind, const TemporalSettings& s) {
    switch (kind) {
    case TemporalEffectKind::FrameEcho: return s.echo.enabled;
    case TemporalEffectKind::Mosh: return s.mosh.enabled;
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
    // ADR-1049
    const std::string mosh = temporalParameterPrefix(TemporalEffectKind::Mosh);
    const MoshSettings& m = defaults.mosh;
    p.moshEnabled = &params.add(b((mosh + "enabled").c_str(), m.enabled));
    p.moshFrames = &params.add(f((mosh + "frames").c_str(), static_cast<float>(clampFrames(m.frames)), 1.0f,
                                 static_cast<float>(kMaxFrames), 1.0f, 16.0f));
    p.moshAmount = &params.add(f((mosh + "amount").c_str(), m.amount, 0.0f, 1.0f, 0.0f, 1.0f));
    p.moshBlock = &params.add(f((mosh + "block").c_str(), m.block, 2.0f, 512.0f, 8.0f, 128.0f));
    p.moshSmear = &params.add(f((mosh + "smear").c_str(), m.smear, 0.0f, 1000.0f, 0.0f, 200.0f));
    p.moshShift = &params.add(f((mosh + "shift").c_str(), m.shift, 0.0f, 200.0f, 0.0f, 40.0f));
    p.moshRate = &params.add(f((mosh + "rate").c_str(), m.rate, 0.0f, 120.0f, 0.0f, 30.0f));
    p.moshSeed = &params.add(f((mosh + "seed").c_str(), m.seed, -1.0e6f, 1.0e6f, 0.0f, 100.0f));
    return p;
}

void applyTemporalParameters(const TemporalParameters& p, TemporalSettings& settings) {
    if (p.echoEnabled != nullptr) settings.echo.enabled = p.echoEnabled->value();
    if (p.echoFrames != nullptr) {
        settings.echo.frames = clampFrames(static_cast<int>(std::lround(p.echoFrames->value())));
    }
    if (p.echoStrength != nullptr) settings.echo.strength = p.echoStrength->value();
    if (p.echoDecay != nullptr) settings.echo.decay = p.echoDecay->value();
    MoshSettings& m = settings.mosh;
    if (p.moshEnabled != nullptr) m.enabled = p.moshEnabled->value();
    if (p.moshFrames != nullptr) m.frames = clampFrames(static_cast<int>(std::lround(p.moshFrames->value())));
    if (p.moshAmount != nullptr) m.amount = p.moshAmount->value();
    if (p.moshBlock != nullptr) m.block = p.moshBlock->value();
    if (p.moshSmear != nullptr) m.smear = p.moshSmear->value();
    if (p.moshShift != nullptr) m.shift = p.moshShift->value();
    if (p.moshRate != nullptr) m.rate = p.moshRate->value();
    if (p.moshSeed != nullptr) m.seed = p.moshSeed->value();
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
    const MoshSettings& m = s.mosh;
    j["mosh"] = nlohmann::json{{"enabled", m.enabled}, {"frames", clampFrames(m.frames)}, {"amount", m.amount},
                               {"block", m.block},     {"smear", m.smear},                {"shift", m.shift},
                               {"rate", m.rate},       {"seed", m.seed}};
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
    if (const auto it = j.find("mosh"); it != j.end()) { // ADR-1049
        if (!it->is_object()) {
            return fail("temporal.mosh: expected an object");
        }
        const auto& e = *it;
        MoshSettings& m = s.mosh;
        if (const auto v = e.find("enabled"); v != e.end() && v->is_boolean()) m.enabled = v->get<bool>();
        if (const auto v = e.find("frames"); v != e.end() && v->is_number()) m.frames = clampFrames(v->get<int>());
        const auto num = [&](const char* key, float& out, float lo, float hi) {
            if (const auto v = e.find(key); v != e.end() && v->is_number()) {
                out = std::clamp(v->get<float>(), lo, hi);
            }
        };
        num("amount", m.amount, 0.0f, 1.0f);
        num("block", m.block, 2.0f, 512.0f);
        num("smear", m.smear, 0.0f, 1000.0f);
        num("shift", m.shift, 0.0f, 200.0f);
        num("rate", m.rate, 0.0f, 120.0f);
        num("seed", m.seed, -1.0e6f, 1.0e6f);
    }
    return s;
}

} // namespace avgen::scene
