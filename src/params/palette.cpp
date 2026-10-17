#include "params/palette.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace avgen::params {

namespace {

std::optional<glm::vec3> readColor(const nlohmann::json& a) {
    if (!a.is_array() || a.size() != 3 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number()) {
        return std::nullopt;
    }
    return glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
}

ParamDesc<float> desc(const char* path, float def, float lo, float hi, float softLo, float softHi, const char* label) {
    ParamDesc<float> d;
    d.path = path;
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = softLo;
    d.softMax = softHi;
    d.label = label;
    d.group = "palette";
    return d;
}

} // namespace

glm::vec3 Palette::toOklab(const glm::vec3& c) {
    const float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    const float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    const float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;
    const float l_ = std::cbrt(l);
    const float m_ = std::cbrt(m);
    const float s_ = std::cbrt(s);
    return {0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
            1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
            0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_};
}

glm::vec3 Palette::fromOklab(const glm::vec3& lab) {
    const float l_ = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z;
    const float m_ = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z;
    const float s_ = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z;
    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;
    return {4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
}

namespace {

// The state a role is read from: the state itself, or the nearest one that defines it.
template <typename Map>
const typename Map::mapped_type* nearest(const std::vector<PaletteState>& states, std::size_t i, const std::string& role,
                                         Map PaletteState::*member) {
    for (std::size_t d = 0; d < states.size(); ++d) {
        for (const std::ptrdiff_t sign : {-1, 1}) {
            const std::ptrdiff_t j = static_cast<std::ptrdiff_t>(i) + sign * static_cast<std::ptrdiff_t>(d);
            if (j < 0 || j >= static_cast<std::ptrdiff_t>(states.size())) {
                continue;
            }
            const Map& map = states[static_cast<std::size_t>(j)].*member;
            if (auto it = map.find(role); it != map.end()) {
                return &it->second;
            }
        }
    }
    return nullptr;
}

struct Pick {
    std::size_t a = 0;
    std::size_t b = 0;
    float f = 0.0f;
};

Pick pick(std::size_t count, float position) {
    Pick p;
    if (count == 0) {
        return p;
    }
    const float x = std::clamp(std::isfinite(position) ? position : 0.0f, 0.0f, static_cast<float>(count - 1));
    p.a = std::min(static_cast<std::size_t>(std::floor(x)), count - 1);
    p.b = std::min(p.a + 1, count - 1);
    p.f = x - static_cast<float>(p.a);
    return p;
}

} // namespace

glm::vec3 Palette::color(const std::string& role, float pos, float sat, float val) const {
    const Pick p = pick(states.size(), pos);
    if (states.empty()) {
        return glm::vec3(0.0f);
    }
    const glm::vec3* ca = nearest(states, p.a, role, &PaletteState::colors);
    const glm::vec3* cb = nearest(states, p.b, role, &PaletteState::colors);
    if (ca == nullptr || cb == nullptr) {
        return glm::vec3(0.0f);
    }
    glm::vec3 lab = glm::mix(toOklab(glm::max(*ca, glm::vec3(0.0f))), toOklab(glm::max(*cb, glm::vec3(0.0f))), p.f);
    lab.x *= std::max(val, 0.0f);
    lab.y *= std::max(sat, 0.0f);
    lab.z *= std::max(sat, 0.0f);
    return glm::max(fromOklab(lab), glm::vec3(0.0f));
}

float Palette::scalar(const std::string& role, float pos) const {
    const Pick p = pick(states.size(), pos);
    if (states.empty()) {
        return 0.0f;
    }
    const float* a = nearest(states, p.a, role, &PaletteState::scalars);
    const float* b = nearest(states, p.b, role, &PaletteState::scalars);
    if (a == nullptr || b == nullptr) {
        return 0.0f;
    }
    return *a + (*b - *a) * p.f;
}


// ---- ADR-1420: the selector -------------------------------------------------------------------

glm::vec3 Palette::stateLab(int state, const std::string& role) const {
    if (states.empty()) {
        return glm::vec3(0.0f);
    }
    const auto i = static_cast<std::size_t>(std::clamp(state, 0, static_cast<int>(states.size()) - 1));
    const glm::vec3* c = nearest(states, i, role, &PaletteState::colors);
    return c != nullptr ? toOklab(glm::max(*c, glm::vec3(0.0f))) : toOklab(glm::vec3(0.0f));
}

float Palette::stateScalar(int state, const std::string& role) const {
    if (states.empty()) {
        return 0.0f;
    }
    const auto i = static_cast<std::size_t>(std::clamp(state, 0, static_cast<int>(states.size()) - 1));
    const float* v = nearest(states, i, role, &PaletteState::scalars);
    return v != nullptr ? *v : 0.0f;
}

float Palette::selectorProgress(double time) const {
    const PaletteSelectorState& st = selectorState_;
    if (st.duration <= 0.0f) {
        return 1.0f;
    }
    const float x = std::clamp(static_cast<float>((time - st.start) / static_cast<double>(st.duration)), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x); // eased: no kink at either end
}

glm::vec3 Palette::selectorColor(const std::string& role, double time, float sat, float val) const {
    const glm::vec3 to = stateLab(selectorState_.target, role);
    const auto it = selectorState_.fromLab.find(role);
    glm::vec3 lab = it != selectorState_.fromLab.end() ? glm::mix(it->second, to, selectorProgress(time)) : to;
    lab.x *= std::max(val, 0.0f);
    lab.y *= std::max(sat, 0.0f);
    lab.z *= std::max(sat, 0.0f);
    return glm::max(fromOklab(lab), glm::vec3(0.0f));
}

float Palette::selectorScalar(const std::string& role, double time) const {
    const float to = stateScalar(selectorState_.target, role);
    const auto it = selectorState_.fromScalar.find(role);
    return it != selectorState_.fromScalar.end() ? it->second + (to - it->second) * selectorProgress(time) : to;
}

int Palette::selectValue(bool bases) const {
    const float v = selectParam == nullptr ? static_cast<float>(selector.select)
                                           : (bases ? selectParam->base() : selectParam->value());
    const int n = std::max(1, static_cast<int>(states.size()));
    return std::clamp(static_cast<int>(std::lround(std::isfinite(v) ? v : 0.0f)), 0, n - 1);
}

void Palette::retarget(int target, double time, float duration) {
    // The blend starts from wherever the palette stands now, so a retarget mid-blend never jumps.
    PaletteSelectorState& st = selectorState_;
    std::map<std::string, glm::vec3> lab;
    std::map<std::string, float> scalars;
    for (const PaletteState& s : states) {
        for (const auto& [role, c] : s.colors) {
            if (lab.count(role) == 0) {
                lab[role] = toOklab(selectorColor(role, time, 1.0f, 1.0f));
            }
        }
        for (const auto& [role, v] : s.scalars) {
            if (scalars.count(role) == 0) {
                scalars[role] = selectorScalar(role, time);
            }
        }
    }
    st.fromLab = std::move(lab);
    st.fromScalar = std::move(scalars);
    st.target = target;
    st.start = time;
    st.duration = std::max(duration, 0.0f);
    st.lastChange = time;
}

void Palette::resetSelector() {
    PaletteSelectorState st;
    st.target = selectValue(true);
    st.lastSelect = st.target;
    selectorState_ = std::move(st);
}

void Palette::step(double time, const signals::SignalBus& bus, bool bases) {
    applyTime_ = time;
    if (!selector.enabled || states.empty()) {
        return;
    }
    PaletteSelectorState& st = selectorState_;
    // A jump in time (a seek nothing replayed, a reload): restart at the selected state.
    if (st.lastTime < 0.0 || time < st.lastTime - 1e-6 || time - st.lastTime > 1.0) {
        resetSelector();
        selectorState_.lastChange = time; // the hold counts from the landing
        selectorState_.lastTime = time;
        // the advance signal's level at the landing is not an edge
        if (!selector.advanceSignal.empty()) {
            if (!advanceId_ || advanceBusSize_ != bus.size()) {
                advanceId_ = bus.find(selector.advanceSignal);
                advanceBusSize_ = bus.size();
            }
            if (advanceId_ && *advanceId_ < bus.size()) {
                selectorState_.lastSignal = bus.value(*advanceId_);
            }
        }
        return;
    }
    const auto param = [&](Parameter<float>* p, float def) { return p == nullptr ? def : (bases ? p->base() : p->value()); };
    const float transition = std::max(param(transitionParam, selector.transition), 0.0f);
    const bool locked = param(lockParam, selector.lock ? 1.0f : 0.0f) > 0.5f;
    const float hold = std::max(param(holdParam, selector.hold), 0.0f);
    // A manual selection wins at once (and starts the hold).
    const int sel = selectValue(bases);
    if (sel != st.lastSelect) {
        st.lastSelect = sel;
        if (sel != st.target) {
            retarget(sel, time, transition);
        }
    }
    // The automatic advance: the signal reaches its threshold (an event fires, or a level crosses it upward).
    if (!selector.advanceSignal.empty()) {
        if (!advanceId_ || advanceBusSize_ != bus.size()) {
            advanceId_ = bus.find(selector.advanceSignal);
            advanceBusSize_ = bus.size();
        }
        if (advanceId_ && *advanceId_ < bus.size()) {
            const float v = bus.value(*advanceId_);
            const bool isEvent = bus.info(*advanceId_).isEvent;
            const bool fired = isEvent ? (bus.event(*advanceId_) && v >= selector.threshold)
                                       : (st.lastSignal < selector.threshold && v >= selector.threshold);
            st.lastSignal = v;
            if (fired && !locked && time - st.lastChange >= static_cast<double>(hold)) {
                std::vector<int> order = selector.order;
                if (order.empty()) {
                    for (int i = 0; i < static_cast<int>(states.size()); ++i) {
                        order.push_back(i);
                    }
                }
                const auto at = std::find(order.begin(), order.end(), st.target);
                const std::size_t i = at == order.end() ? 0 : static_cast<std::size_t>(at - order.begin()) + 1;
                const int next = std::clamp(order[i % order.size()], 0, static_cast<int>(states.size()) - 1);
                if (next != st.target) {
                    retarget(next, time, transition);
                } else {
                    st.lastChange = time;
                }
            }
        }
    }
    st.lastTime = time;
}

void Palette::attach(ParameterSet& params) {
    positionParam = &params.add(desc("palette/position", position, -1.0f, 1000.0f, 0.0f,
                                     std::max(1.0f, static_cast<float>(states.size()) - 1.0f), "palette/position"));
    saturationParam = &params.add(desc("palette/saturation", saturation, 0.0f, 4.0f, 0.0f, 2.0f, "palette/saturation"));
    valueParam = &params.add(desc("palette/value", value, 0.0f, 4.0f, 0.0f, 2.0f, "palette/value"));
    if (selector.enabled) { // ADR-1420: only with a selector, so a palette without one is exactly ADR-1043's
        const float top = std::max(1.0f, static_cast<float>(states.size()) - 1.0f);
        selectParam = &params.add(desc("palette/select", static_cast<float>(selector.select), 0.0f, top, 0.0f, top,
                                       "palette/select"));
        transitionParam = &params.add(desc("palette/transition", selector.transition, 0.0f, 600.0f, 0.0f, 30.0f,
                                           "palette/transition"));
        lockParam = &params.add(desc("palette/lock", selector.lock ? 1.0f : 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, "palette/lock"));
        holdParam = &params.add(desc("palette/hold", selector.hold, 0.0f, 3600.0f, 0.0f, 120.0f, "palette/hold"));
    }
}

void Palette::detach(ParameterSet& params) {
    params.remove("palette/position");
    params.remove("palette/saturation");
    params.remove("palette/value");
    if (selector.enabled) {
        params.remove("palette/select");
        params.remove("palette/transition");
        params.remove("palette/lock");
        params.remove("palette/hold");
    }
    selectParam = nullptr;
    transitionParam = nullptr;
    lockParam = nullptr;
    holdParam = nullptr;
    positionParam = nullptr;
    saturationParam = nullptr;
    valueParam = nullptr;
}

void Palette::apply(ParameterSet& params) {
    if (states.empty()) {
        return;
    }
    const float pos = positionParam != nullptr ? positionParam->value() : position;
    const float sat = saturationParam != nullptr ? saturationParam->value() : saturation;
    const float val = valueParam != nullptr ? valueParam->value() : value;
    for (const PaletteBinding& b : bindings) {
        IParameter* target = params.find(b.target);
        if (target == nullptr) {
            if (std::find(unresolved_.begin(), unresolved_.end(), b.target) == unresolved_.end()) {
                unresolved_.push_back(b.target);
            }
            continue;
        }
        const bool isColor = std::any_of(states.begin(), states.end(),
                                         [&](const PaletteState& s) { return s.colors.count(b.role) != 0; });
        const std::size_t n = target->componentCount();
        if (isColor) {
            const glm::vec3 c =
                (selector.enabled ? selectorColor(b.role, applyTime_, sat, val) : color(b.role, pos, sat, val)) * b.gain;
            const auto write = [&](std::size_t i, float v) {
                target->setFinalComponent(i, b.mode == PaletteMode::Multiply ? target->finalComponent(i) * v : v);
            };
            if (n == 1) {
                // A scalar target reads one channel, or the colour's luminance when none is named.
                write(0, b.component >= 0 && b.component < 3 ? c[b.component]
                                                             : glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f)));
            } else {
                for (std::size_t i = 0; i < std::min<std::size_t>(n, 3); ++i) {
                    if (b.component < 0 || static_cast<std::size_t>(b.component) == i) {
                        write(i, c[static_cast<glm::length_t>(i)]);
                    }
                }
            }
        } else {
            const float v = (selector.enabled ? selectorScalar(b.role, applyTime_) : scalar(b.role, pos)) * b.gain;
            for (std::size_t i = 0; i < n; ++i) {
                if (b.component >= 0 && static_cast<std::size_t>(b.component) != i) {
                    continue;
                }
                target->setFinalComponent(i, b.mode == PaletteMode::Multiply ? target->finalComponent(i) * v : v);
            }
        }
    }
}

nlohmann::json Palette::toJson() const {
    nlohmann::json j;
    nlohmann::json st = nlohmann::json::array();
    for (const PaletteState& s : states) {
        nlohmann::json o;
        o["name"] = s.name;
        nlohmann::json colors = nlohmann::json::object();
        for (const auto& [role, c] : s.colors) {
            colors[role] = {c.r, c.g, c.b};
        }
        o["colors"] = std::move(colors);
        if (!s.scalars.empty()) {
            nlohmann::json scalars = nlohmann::json::object();
            for (const auto& [role, v] : s.scalars) {
                scalars[role] = v;
            }
            o["scalars"] = std::move(scalars);
        }
        st.push_back(std::move(o));
    }
    j["states"] = std::move(st);
    nlohmann::json bs = nlohmann::json::array();
    for (const PaletteBinding& b : bindings) {
        nlohmann::json o{{"role", b.role}, {"target", b.target}};
        if (b.component >= 0) {
            o["component"] = b.component;
        }
        if (b.gain != 1.0f) {
            o["gain"] = b.gain;
        }
        if (b.mode == PaletteMode::Multiply) {
            o["mode"] = "multiply";
        }
        bs.push_back(std::move(o));
    }
    j["bindings"] = std::move(bs);
    j["position"] = positionParam != nullptr ? positionParam->base() : position;
    j["saturation"] = saturationParam != nullptr ? saturationParam->base() : saturation;
    j["value"] = valueParam != nullptr ? valueParam->base() : value;
    if (selector.enabled) { // ADR-1420
        nlohmann::json sel;
        sel["select"] = selectParam != nullptr ? static_cast<int>(std::lround(selectParam->base())) : selector.select;
        sel["transition"] = transitionParam != nullptr ? transitionParam->base() : selector.transition;
        sel["lock"] = lockParam != nullptr ? lockParam->base() > 0.5f : selector.lock;
        sel["hold"] = holdParam != nullptr ? holdParam->base() : selector.hold;
        if (!selector.advanceSignal.empty()) {
            nlohmann::json adv{{"signal", selector.advanceSignal}, {"threshold", selector.threshold}};
            if (!selector.order.empty()) {
                adv["order"] = selector.order;
            }
            sel["advance"] = std::move(adv);
        }
        j["selector"] = std::move(sel);
    }
    return j;
}

Result<Palette> Palette::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("palette: must be an object");
    }
    Palette p;
    if (!j.contains("states") || !j["states"].is_array() || j["states"].empty()) {
        return fail("palette: 'states' must be a non-empty array");
    }
    for (const auto& sj : j["states"]) {
        if (!sj.is_object()) {
            return fail("palette: every state must be an object");
        }
        PaletteState s;
        s.name = sj.value("name", std::string{});
        if (sj.contains("colors")) {
            if (!sj["colors"].is_object()) {
                return fail("palette state '{}': 'colors' must be an object of role: [r, g, b]", s.name);
            }
            for (auto it = sj["colors"].begin(); it != sj["colors"].end(); ++it) {
                auto c = readColor(it.value());
                if (!c) {
                    return fail("palette state '{}': colour '{}' must be [r, g, b]", s.name, it.key());
                }
                s.colors[it.key()] = *c;
            }
        }
        if (sj.contains("scalars")) {
            if (!sj["scalars"].is_object()) {
                return fail("palette state '{}': 'scalars' must be an object of role: number", s.name);
            }
            for (auto it = sj["scalars"].begin(); it != sj["scalars"].end(); ++it) {
                if (!it.value().is_number()) {
                    return fail("palette state '{}': scalar '{}' must be a number", s.name, it.key());
                }
                s.scalars[it.key()] = it.value().get<float>();
            }
        }
        p.states.push_back(std::move(s));
    }
    if (j.contains("bindings")) {
        if (!j["bindings"].is_array()) {
            return fail("palette: 'bindings' must be an array");
        }
        for (const auto& bj : j["bindings"]) {
            if (!bj.is_object() || !bj.contains("role") || !bj["role"].is_string() || !bj.contains("target") ||
                !bj["target"].is_string()) {
                return fail("palette: every binding needs a 'role' and a 'target'");
            }
            PaletteBinding b;
            b.role = bj["role"].get<std::string>();
            b.target = bj["target"].get<std::string>();
            b.component = bj.value("component", -1);
            b.gain = bj.value("gain", 1.0f);
            const std::string mode = bj.value("mode", std::string("replace"));
            if (mode == "multiply") {
                b.mode = PaletteMode::Multiply;
            } else if (mode != "replace") {
                return fail("palette binding '{}': mode must be 'replace' or 'multiply'", b.target);
            }
            p.bindings.push_back(std::move(b));
        }
    }
    p.position = j.value("position", 0.0f);
    p.saturation = j.value("saturation", 1.0f);
    p.value = j.value("value", 1.0f);
    if (j.contains("selector")) { // ADR-1420
        const auto& sj = j["selector"];
        if (!sj.is_object()) {
            return fail("palette: 'selector' must be an object");
        }
        PaletteSelector& sel = p.selector;
        sel.enabled = true;
        sel.select = sj.value("select", 0);
        sel.transition = sj.value("transition", 4.0f);
        sel.lock = sj.value("lock", false);
        sel.hold = sj.value("hold", 16.0f);
        if (sel.select < 0 || sel.select >= static_cast<int>(p.states.size())) {
            return fail("palette selector: 'select' {} is not a state (0..{})", sel.select, p.states.size() - 1);
        }
        if (sj.contains("advance")) {
            const auto& aj = sj["advance"];
            if (!aj.is_object() || !aj.contains("signal") || !aj["signal"].is_string()) {
                return fail("palette selector: 'advance' needs a 'signal'");
            }
            sel.advanceSignal = aj["signal"].get<std::string>();
            sel.threshold = aj.value("threshold", 0.5f);
            if (aj.contains("hold")) {
                sel.hold = aj["hold"].get<float>();
            }
            if (aj.contains("order")) {
                if (!aj["order"].is_array() || aj["order"].empty()) {
                    return fail("palette selector: 'order' must be a non-empty array of states");
                }
                for (const auto& o : aj["order"]) {
                    if (!o.is_number_integer() || o.get<int>() < 0 || o.get<int>() >= static_cast<int>(p.states.size())) {
                        return fail("palette selector: 'order' names a state that does not exist");
                    }
                    sel.order.push_back(o.get<int>());
                }
            }
        }
        p.selectorState_.target = sel.select;
        p.selectorState_.lastSelect = sel.select;
    }
    return p;
}

} // namespace avgen::params
