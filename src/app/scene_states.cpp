#include "app/scene_states.hpp"

#include "core/log.hpp"
#include "params/serialization.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::app {

// ---- names --------------------------------------------------------------------------------------

const char* transitionEasingName(TransitionEasing e) {
    switch (e) {
    case TransitionEasing::Linear: return "linear";
    case TransitionEasing::Smooth: return "smooth";
    case TransitionEasing::EaseIn: return "easeIn";
    case TransitionEasing::EaseOut: return "easeOut";
    case TransitionEasing::EaseInOut: return "easeInOut";
    case TransitionEasing::Bezier: return "bezier";
    }
    return "linear";
}
std::optional<TransitionEasing> transitionEasingFromName(std::string_view n) {
    for (auto e : {TransitionEasing::Linear, TransitionEasing::Smooth, TransitionEasing::EaseIn, TransitionEasing::EaseOut,
                   TransitionEasing::EaseInOut, TransitionEasing::Bezier}) {
        if (n == transitionEasingName(e)) return e;
    }
    return std::nullopt;
}
const char* transitionQuantizeName(TransitionQuantize q) {
    switch (q) {
    case TransitionQuantize::None: return "none";
    case TransitionQuantize::Beat: return "beat";
    case TransitionQuantize::Bar: return "bar";
    }
    return "none";
}
std::optional<TransitionQuantize> transitionQuantizeFromName(std::string_view n) {
    for (auto q : {TransitionQuantize::None, TransitionQuantize::Beat, TransitionQuantize::Bar}) {
        if (n == transitionQuantizeName(q)) return q;
    }
    return std::nullopt;
}
const char* triggerKindName(TriggerKind k) {
    switch (k) {
    case TriggerKind::Manual: return "manual";
    case TriggerKind::Beat: return "beat";
    case TriggerKind::Bar: return "bar";
    case TriggerKind::Onset: return "onset";
    case TriggerKind::Signal: return "signal";
    case TriggerKind::Macro: return "macro";
    case TriggerKind::Cue: return "cue";
    }
    return "manual";
}
std::optional<TriggerKind> triggerKindFromName(std::string_view n) {
    for (auto k : {TriggerKind::Manual, TriggerKind::Beat, TriggerKind::Bar, TriggerKind::Onset, TriggerKind::Signal,
                   TriggerKind::Macro, TriggerKind::Cue}) {
        if (n == triggerKindName(k)) return k;
    }
    return std::nullopt;
}

float easeTransition(TransitionEasing easing, float t, float c0, float c1) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
    case TransitionEasing::Linear: return t;
    case TransitionEasing::Smooth: return t * t * (3.0f - 2.0f * t);
    case TransitionEasing::EaseIn: return t * t * t;
    case TransitionEasing::EaseOut: { const float u = 1.0f - t; return 1.0f - u * u * u; }
    case TransitionEasing::EaseInOut: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    case TransitionEasing::Bezier: {
        // cubic Bezier on y with control ys (0, c0, c1, 1), x treated as t (CSS-like approximation)
        const float u = 1.0f - t;
        return 3.0f * u * u * t * c0 + 3.0f * u * t * t * c1 + t * t * t;
    }
    }
    return t;
}

// ---- state machine --------------------------------------------------------------------------------

const SceneState* StateMachine::find(std::string_view name) const {
    for (const auto& s : states) {
        if (s.name == name) return &s;
    }
    return nullptr;
}
SceneState* StateMachine::find(std::string_view name) {
    for (auto& s : states) {
        if (s.name == name) return &s;
    }
    return nullptr;
}
int StateMachine::currentIndex() const {
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (states[i].name == current_) return static_cast<int>(i);
    }
    return -1;
}

void StateMachine::beginTransition(const SceneState& state, params::ParameterSet& params,
                                   const params::PresetBank& presets, double seconds, bool instant) {
    const params::Preset* preset = presets.find(state.preset);
    if (preset == nullptr) {
        log::warn("state '{}': preset '{}' not found", state.name, state.preset);
        return;
    }
    if (instant || state.transition.seconds <= 0.0) {
        params::applyPreset(params, *preset);
        current_ = state.name;
        pending_.clear();
        progress_ = 1.0f;
        waitUntil_ = -1.0;
        return;
    }
    from_ = params::capturePreset(params, "state-from");
    pending_ = state.name;
    progress_ = 0.0f;
    startSeconds_ = seconds;
    waitUntil_ = -1.0;
}

bool StateMachine::go(std::string_view name, params::ParameterSet& params, const params::PresetBank& presets,
                      bool instant) {
    const SceneState* state = find(name);
    if (state == nullptr || presets.find(state->preset) == nullptr) {
        return false;
    }
    if (!instant && state->transition.quantize != TransitionQuantize::None) {
        // Deferred: update() starts it on the next beat/bar.
        pendingQuantized_ = std::string(name);
        return true;
    }
    beginTransition(*state, params, presets, lastSeconds_, instant);
    return true;
}

void StateMachine::reset(params::ParameterSet& params, const params::PresetBank& presets) {
    current_.clear();
    pending_.clear();
    pendingQuantized_.clear();
    progress_ = 1.0f;
    beatCounter_ = 0;
    barCounter_ = 0;
    lastBarPhase_ = 0.0f;
    lastSignal_.assign(0, 0.0f);
    if (!initial.empty()) {
        go(initial, params, presets, true);
    }
}

void StateMachine::update(double seconds, double dt, const signals::SignalBus& bus, const BeatInfo& beat,
                          params::ParameterSet& params, const params::PresetBank& presets) {
    (void)dt;
    lastSeconds_ = seconds;
    // Beat / bar edges.
    if (beat.beatPulse) {
        ++beatCounter_;
    }
    const bool barEdge = beat.barPhase < lastBarPhase_ - 0.5f;
    lastBarPhase_ = beat.barPhase;
    if (barEdge) {
        ++barCounter_;
    }
    // Quantised start requested by go().
    if (!pendingQuantized_.empty()) {
        const SceneState* state = find(pendingQuantized_);
        if (state == nullptr) {
            pendingQuantized_.clear();
        } else {
            const bool start = state->transition.quantize == TransitionQuantize::Beat ? beat.beatPulse : barEdge;
            if (start) {
                const std::string name = pendingQuantized_;
                pendingQuantized_.clear();
                beginTransition(*state, params, presets, seconds, false);
            }
        }
    }
    // Triggers (evaluated in state order; the first that fires wins this frame).
    std::size_t slot = 0;
    std::string fire;
    for (const SceneState& s : states) {
        for (const StateTrigger& t : s.triggers) {
            const std::size_t mySlot = slot++;
            if (lastSignal_.size() <= mySlot) {
                lastSignal_.resize(mySlot + 1, 0.0f);
            }
            float value = 0.0f;
            bool fired = false;
            if (!t.enabled) {
                continue;
            }
            const std::string target = t.target.empty() ? s.name : t.target;
            const bool fromOk = t.fromState.empty() || t.fromState == current_;
            switch (t.kind) {
            case TriggerKind::Manual:
            case TriggerKind::Cue:
                break;
            case TriggerKind::Beat:
                fired = beat.beatPulse && t.every > 0 && (beatCounter_ % t.every) == 0;
                break;
            case TriggerKind::Bar:
                fired = barEdge && t.every > 0 && (barCounter_ % t.every) == 0;
                break;
            case TriggerKind::Onset:
                fired = beat.onset && beat.onsetStrength >= t.threshold;
                break;
            case TriggerKind::Signal:
            case TriggerKind::Macro: {
                const std::string name = t.kind == TriggerKind::Macro ? "macro." + t.signal : t.signal;
                if (auto id = bus.find(name)) {
                    value = bus.value(*id);
                    const float last = lastSignal_[mySlot];
                    fired = t.falling ? (last >= t.threshold && value < t.threshold)
                                      : (last < t.threshold && value >= t.threshold);
                }
                break;
            }
            }
            lastSignal_[mySlot] = value;
            if (fired && fromOk && fire.empty() && target != current_ && target != pending_) {
                fire = target;
            }
        }
    }
    if (!fire.empty()) {
        go(fire, params, presets, false);
    }
    // Advance the running transition.
    if (!pending_.empty()) {
        const SceneState* state = find(pending_);
        const params::Preset* preset = state != nullptr ? presets.find(state->preset) : nullptr;
        if (state == nullptr || preset == nullptr) {
            pending_.clear();
            progress_ = 1.0f;
            return;
        }
        const double elapsed = seconds - startSeconds_;
        const float t = state->transition.seconds > 0.0 ? static_cast<float>(elapsed / state->transition.seconds) : 1.0f;
        if (t >= 1.0f) {
            params::applyPreset(params, *preset);
            current_ = pending_;
            pending_.clear();
            progress_ = 1.0f;
        } else {
            progress_ = std::max(0.0f, t);
            const float eased = easeTransition(state->transition.easing, progress_, state->transition.bezierC0,
                                               state->transition.bezierC1);
            params::applyPresetBlend(params, from_, *preset, eased);
        }
    }
}

nlohmann::json StateMachine::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    j["initial"] = initial;
    nlohmann::json arr = nlohmann::json::array();
    for (const SceneState& s : states) {
        nlohmann::json sj;
        sj["name"] = s.name;
        sj["preset"] = s.preset;
        sj["transition"] = {{"seconds", s.transition.seconds},
                            {"easing", transitionEasingName(s.transition.easing)},
                            {"bezier", {s.transition.bezierC0, s.transition.bezierC1}},
                            {"quantize", transitionQuantizeName(s.transition.quantize)}};
        nlohmann::json triggers = nlohmann::json::array();
        for (const StateTrigger& t : s.triggers) {
            nlohmann::json tj;
            tj["kind"] = triggerKindName(t.kind);
            if (!t.signal.empty()) tj["signal"] = t.signal;
            tj["threshold"] = t.threshold;
            if (t.falling) tj["falling"] = true;
            tj["every"] = t.every;
            if (!t.fromState.empty()) tj["from"] = t.fromState;
            if (!t.target.empty()) tj["target"] = t.target;
            if (!t.enabled) tj["enabled"] = false;
            triggers.push_back(std::move(tj));
        }
        sj["triggers"] = std::move(triggers);
        arr.push_back(std::move(sj));
    }
    j["states"] = std::move(arr);
    return j;
}

Result<void> StateMachine::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'states' must be an object");
    }
    StateMachine out;
    if (j.contains("initial") && j["initial"].is_string()) {
        out.initial = j["initial"].get<std::string>();
    }
    if (j.contains("states")) {
        if (!j["states"].is_array()) {
            return fail("'states.states' must be an array");
        }
        for (const auto& sj : j["states"]) {
            if (!sj.is_object() || !sj.contains("name") || !sj["name"].is_string()) {
                return fail("state entries need a 'name'");
            }
            SceneState s;
            s.name = sj["name"].get<std::string>();
            if (sj.contains("preset") && sj["preset"].is_string()) {
                s.preset = sj["preset"].get<std::string>();
            }
            if (sj.contains("transition") && sj["transition"].is_object()) {
                const auto& tr = sj["transition"];
                if (tr.contains("seconds") && tr["seconds"].is_number()) s.transition.seconds = tr["seconds"].get<double>();
                if (tr.contains("easing") && tr["easing"].is_string()) {
                    auto e = transitionEasingFromName(tr["easing"].get<std::string>());
                    if (!e) return fail("state '{}': unknown easing '{}'", s.name, tr["easing"].get<std::string>());
                    s.transition.easing = *e;
                }
                if (tr.contains("bezier") && tr["bezier"].is_array() && tr["bezier"].size() == 2) {
                    s.transition.bezierC0 = tr["bezier"][0].get<float>();
                    s.transition.bezierC1 = tr["bezier"][1].get<float>();
                }
                if (tr.contains("quantize") && tr["quantize"].is_string()) {
                    auto q = transitionQuantizeFromName(tr["quantize"].get<std::string>());
                    if (!q) return fail("state '{}': unknown quantize '{}'", s.name, tr["quantize"].get<std::string>());
                    s.transition.quantize = *q;
                }
            }
            if (sj.contains("triggers")) {
                if (!sj["triggers"].is_array()) return fail("state '{}': 'triggers' must be an array", s.name);
                for (const auto& tj : sj["triggers"]) {
                    if (!tj.is_object()) return fail("state '{}': trigger entries must be objects", s.name);
                    StateTrigger t;
                    if (tj.contains("kind") && tj["kind"].is_string()) {
                        auto k = triggerKindFromName(tj["kind"].get<std::string>());
                        if (!k) return fail("state '{}': unknown trigger kind '{}'", s.name, tj["kind"].get<std::string>());
                        t.kind = *k;
                    }
                    if (tj.contains("signal") && tj["signal"].is_string()) t.signal = tj["signal"].get<std::string>();
                    if (tj.contains("threshold") && tj["threshold"].is_number()) t.threshold = tj["threshold"].get<float>();
                    if (tj.contains("falling") && tj["falling"].is_boolean()) t.falling = tj["falling"].get<bool>();
                    if (tj.contains("every") && tj["every"].is_number_integer()) t.every = std::max(1, tj["every"].get<int>());
                    if (tj.contains("from") && tj["from"].is_string()) t.fromState = tj["from"].get<std::string>();
                    if (tj.contains("target") && tj["target"].is_string()) t.target = tj["target"].get<std::string>();
                    if (tj.contains("enabled") && tj["enabled"].is_boolean()) t.enabled = tj["enabled"].get<bool>();
                    s.triggers.push_back(std::move(t));
                }
            }
            out.states.push_back(std::move(s));
        }
    }
    states = std::move(out.states);
    initial = std::move(out.initial);
    return {};
}

Result<void> StateMachine::validate(const params::PresetBank& presets) const {
    for (const SceneState& s : states) {
        if (s.name.empty()) return fail("a state has no name");
        if (!s.preset.empty() && presets.find(s.preset) == nullptr) {
            return fail("state '{}': preset '{}' not found", s.name, s.preset);
        }
        for (const StateTrigger& t : s.triggers) {
            if (!t.target.empty() && find(t.target) == nullptr) {
                return fail("state '{}': trigger target '{}' not found", s.name, t.target);
            }
        }
    }
    if (!initial.empty() && find(initial) == nullptr) {
        return fail("initial state '{}' not found", initial);
    }
    return {};
}

// ---- world macros ----------------------------------------------------------------------------------

nlohmann::json WorldMacro::toJson() const {
    nlohmann::json j;
    j["name"] = name;
    j["label"] = label;
    j["default"] = defaultValue;
    nlohmann::json targetsJson = nlohmann::json::array();
    for (const WorldMacroTarget& t : targets) {
        nlohmann::json tj;
        tj["path"] = t.path;
        if (t.component >= 0) tj["component"] = t.component;
        tj["min"] = t.min;
        tj["max"] = t.max;
        tj["curve"] = params::curveTypeName(t.curve);
        tj["curveAmount"] = t.curveAmount;
        tj["op"] = params::modOpName(t.op);
        targetsJson.push_back(std::move(tj));
    }
    j["targets"] = std::move(targetsJson);
    return j;
}

Result<WorldMacro> WorldMacro::fromJson(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("name") || !j["name"].is_string()) {
        return fail("world macro entries need a 'name'");
    }
    WorldMacro m;
    m.name = j["name"].get<std::string>();
    m.label = j.contains("label") && j["label"].is_string() ? j["label"].get<std::string>() : m.name;
    if (j.contains("default") && j["default"].is_number()) m.defaultValue = j["default"].get<float>();
    if (j.contains("targets")) {
        if (!j["targets"].is_array()) return fail("world macro '{}': 'targets' must be an array", m.name);
        for (const auto& tj : j["targets"]) {
            if (!tj.is_object() || !tj.contains("path") || !tj["path"].is_string()) {
                return fail("world macro '{}': targets need a 'path'", m.name);
            }
            WorldMacroTarget t;
            t.path = tj["path"].get<std::string>();
            if (tj.contains("component") && tj["component"].is_number_integer()) t.component = tj["component"].get<int>();
            if (tj.contains("min") && tj["min"].is_number()) t.min = tj["min"].get<float>();
            if (tj.contains("max") && tj["max"].is_number()) t.max = tj["max"].get<float>();
            if (tj.contains("curve") && tj["curve"].is_string()) {
                auto c = params::curveTypeFromName(tj["curve"].get<std::string>());
                if (!c) return fail("world macro '{}': unknown curve '{}'", m.name, tj["curve"].get<std::string>());
                t.curve = *c;
            }
            if (tj.contains("curveAmount") && tj["curveAmount"].is_number()) t.curveAmount = tj["curveAmount"].get<float>();
            if (tj.contains("op") && tj["op"].is_string()) {
                auto op = params::modOpFromName(tj["op"].get<std::string>());
                if (!op) return fail("world macro '{}': unknown op '{}'", m.name, tj["op"].get<std::string>());
                t.op = *op;
            }
            m.targets.push_back(std::move(t));
        }
    }
    return m;
}

std::vector<params::ModRoute> WorldMacro::routes() const {
    std::vector<params::ModRoute> out;
    for (const WorldMacroTarget& t : targets) {
        params::ModRoute r;
        r.source = "macro." + name;
        r.target = t.path;
        r.component = t.component;
        r.amount = 1.0f;
        r.op = t.op;
        r.polarity = params::Polarity::Unipolar;
        r.chain.curve = t.curve;
        r.chain.curveAmount = t.curveAmount;
        r.chain.remapEnabled = true;
        r.chain.remapInMin = 0.0f;
        r.chain.remapInMax = 1.0f;
        r.chain.remapOutMin = t.min;
        r.chain.remapOutMax = t.max;
        out.push_back(std::move(r));
    }
    return out;
}

bool WorldMacro::isGenerated(const params::ModRoute& route, std::string_view macroName) {
    return route.source == "macro." + std::string(macroName) && route.chain.remapEnabled;
}

void removeWorldMacroRoutes(std::string_view macroName, params::Modulator& modulator) {
    auto& routes = modulator.routes();
    routes.erase(std::remove_if(routes.begin(), routes.end(),
                                [&](const params::ModRoute& r) { return WorldMacro::isGenerated(r, macroName); }),
                 routes.end());
}

void applyWorldMacro(const WorldMacro& macro, params::Modulator& modulator) {
    removeWorldMacroRoutes(macro.name, modulator);
    for (auto& r : macro.routes()) {
        modulator.addRoute(std::move(r));
    }
}

} // namespace avgen::app
