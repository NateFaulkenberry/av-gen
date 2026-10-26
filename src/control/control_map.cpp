#include "control/control_map.hpp"

#include <nlohmann/json.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <set>

namespace avgen::control {

namespace {

bool containsNoCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || needle == "*") {
        return true;
    }
    auto lower = [](std::string_view s) {
        std::string out(s);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    };
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

nlohmann::json targetToJson(const BindingTarget& t) {
    nlohmann::json j = nlohmann::json::object();
    if (t.action != MidiAction::None) { // ADR-1705
        j["action"] = midiActionName(t.action);
    }
    if (!t.signal.empty()) {
        j["signal"] = t.signal;
    }
    if (!t.parameter.empty()) {
        j["parameter"] = t.parameter;
        j["component"] = t.component;
        j["min"] = t.min;
        j["max"] = t.max;
    } else if (t.min != 0.0f || t.max != 1.0f) {
        // ADR-1700: a channel's own output range (it always existed in the struct, and was dropped on save).
        j["min"] = t.min;
        j["max"] = t.max;
    }
    return j;
}

Result<BindingTarget> targetFromJson(const nlohmann::json& j) {
    BindingTarget t;
    if (!j.is_object()) {
        return fail("binding must be an object");
    }
    if (j.contains("signal")) {
        if (!j["signal"].is_string()) {
            return fail("binding 'signal' must be a string");
        }
        t.signal = j["signal"].get<std::string>();
    }
    if (j.contains("parameter")) {
        if (!j["parameter"].is_string()) {
            return fail("binding 'parameter' must be a string");
        }
        t.parameter = j["parameter"].get<std::string>();
    }
    if (j.contains("component")) {
        if (!j["component"].is_number_integer()) {
            return fail("binding 'component' must be an integer");
        }
        t.component = j["component"].get<int>();
    }
    if (j.contains("min")) {
        if (!j["min"].is_number()) {
            return fail("binding 'min' must be a number");
        }
        t.min = j["min"].get<float>();
    }
    if (j.contains("max")) {
        if (!j["max"].is_number()) {
            return fail("binding 'max' must be a number");
        }
        t.max = j["max"].get<float>();
    }
    if (j.contains("action")) { // ADR-1705
        const auto action = j["action"].is_string() ? midiActionFromName(j["action"].get<std::string>()) : std::nullopt;
        if (!action) {
            return fail("binding 'action' must be one of transport.play, transport.return-to-start, live.next-scene, "
                        "live.previous-scene");
        }
        if (!t.signal.empty() || !t.parameter.empty()) {
            return fail("binding 'action' cannot be combined with a 'signal' or a 'parameter'");
        }
        t.action = *action;
        return t;
    }
    if (t.signal.empty() && t.parameter.empty()) {
        return fail("binding needs a 'signal' or a 'parameter'");
    }
    return t;
}

} // namespace

// ---- ADR-1700: a MIDI binding in JSON --------------------------------------------------------------------------------

nlohmann::json midiBindingToJson(const MidiBinding& b) {
    if (b.invalid()) {
        // Written back exactly as it was read: an unreadable record is the person's, not this build's to drop.
        auto raw = nlohmann::json::parse(b.invalidRaw, nullptr, false);
        return raw.is_discarded() ? nlohmann::json(b.invalidRaw) : raw;
    }
    nlohmann::json j = targetToJson(b.target);
    if (b.scope != MidiScope::Default) {
        j["id"] = b.id;
        j["scope"] = midiScopeName(b.scope); // ADR-1705: "rig" only ever in the rig file
        if (b.scope == MidiScope::Scene) {
            j["scene"] = b.scene;
        }
        if (!b.overrides.empty()) {
            j["overrides"] = b.overrides;
        }
    } else if (b.idAuthored) {
        j["id"] = b.id;
    }
    j["source"] = b.source;
    j["channel"] = b.channel;
    j["kind"] = midiBindKindName(b.kind);
    j["number"] = b.number;
    // The rest only when it differs from the default, so an authored binding saves as it was written.
    if (!b.device.empty()) j["device"] = b.device;
    if (!b.deviceName.empty()) j["deviceName"] = b.deviceName;
    if (b.numberHigh >= 0) j["numberHigh"] = b.numberHigh;
    if (!b.enabled) j["enabled"] = false;
    if (b.toggle) j["toggle"] = true;
    if (!b.velocity) j["velocity"] = false;
    if (b.pickup) j["pickup"] = true; // ADR-1606
    if (b.invert) j["invert"] = true;
    if (b.curve != MidiCurve::Linear) j["curve"] = midiCurveName(b.curve);
    if (b.scale != 1.0f) j["scale"] = b.scale;
    if (b.offset != 0.0f) j["offset"] = b.offset;
    if (b.smoothMs > 0.0f) j["smoothMs"] = b.smoothMs;
    if (b.encoder != MidiEncoder::Absolute) j["encoder"] = midiEncoderName(b.encoder);
    return j;
}

// `warnings` non-null: a user record (unknown keys are reported). Null: an authored default (unknown keys are ignored,
// as they always were).
Result<MidiBinding> midiBindingFromJson(const nlohmann::json& e, MidiScope defaultScope, std::vector<std::string>* warnings) {
    if (!e.is_object()) {
        return fail("a binding must be an object");
    }
    auto target = targetFromJson(e);
    if (!target) {
        return std::unexpected(target.error());
    }
    MidiBinding b;
    b.target = *target;
    b.scope = defaultScope;
    const auto str = [&](const char* key, std::string& out) -> Result<void> {
        if (const auto it = e.find(key); it != e.end()) {
            if (!it->is_string()) {
                return fail("'{}' must be a string", key);
            }
            out = it->get<std::string>();
        }
        return {};
    };
    const auto integer = [&](const char* key, int& out, int lo, int hi) -> Result<void> {
        if (const auto it = e.find(key); it != e.end()) {
            if (!it->is_number_integer()) {
                return fail("'{}' must be an integer", key);
            }
            const int v = it->get<int>();
            if (v < lo || v > hi) {
                return fail("'{}' {} is outside {}..{}", key, v, lo, hi);
            }
            out = v;
        }
        return {};
    };
    const auto boolean = [&](const char* key, bool& out) -> Result<void> {
        if (const auto it = e.find(key); it != e.end()) {
            if (!it->is_boolean()) {
                return fail("'{}' must be true or false", key);
            }
            out = it->get<bool>();
        }
        return {};
    };
    const auto number = [&](const char* key, float& out, float lo, float hi) -> Result<void> {
        if (const auto it = e.find(key); it != e.end()) {
            if (!it->is_number()) {
                return fail("'{}' must be a number", key);
            }
            const float v = it->get<float>();
            if (!(v >= lo && v <= hi)) {
                return fail("'{}' {} is outside {}..{}", key, v, lo, hi);
            }
            out = v;
        }
        return {};
    };
    std::string kindName = "cc";
    std::string scopeName;
    std::string curveName;
    std::string encoderName;
    for (const Result<void>& r : {
             str("id", b.id), str("scene", b.scene), str("overrides", b.overrides), str("source", b.source),
             str("device", b.device), str("deviceName", b.deviceName), str("kind", kindName), str("scope", scopeName),
             str("curve", curveName), str("encoder", encoderName), integer("channel", b.channel, -1, 15),
             integer("number", b.number, -1, 16383), integer("numberHigh", b.numberHigh, -1, 127),
             boolean("enabled", b.enabled), boolean("toggle", b.toggle), boolean("velocity", b.velocity),
             boolean("pickup", b.pickup), boolean("invert", b.invert), number("scale", b.scale, -16.0f, 16.0f),
             number("offset", b.offset, -1.0f, 1.0f), number("smoothMs", b.smoothMs, 0.0f, 10000.0f)}) {
        if (!r) {
            return std::unexpected(r.error());
        }
    }
    b.idAuthored = !b.id.empty();
    const auto kind = midiBindKindFromName(kindName);
    if (!kind) {
        return fail("midi binding kind '{}' unknown", kindName);
    }
    b.kind = *kind;
    if (b.kind != MidiBindKind::Nrpn && b.kind != MidiBindKind::Rpn && b.number > 127) {
        return fail("number {} is outside 0..127 for a {} binding", b.number, kindName);
    }
    if (b.numberHigh >= 0 && b.number >= 0 && b.numberHigh < b.number) {
        return fail("numberHigh {} is below number {}", b.numberHigh, b.number);
    }
    if (!curveName.empty()) {
        const auto c = midiCurveFromName(curveName);
        if (!c) {
            return fail("curve '{}' unknown (linear, exponential, logarithmic, s-curve)", curveName);
        }
        b.curve = *c;
    }
    if (!encoderName.empty()) {
        const auto enc = midiEncoderFromName(encoderName);
        if (!enc) {
            return fail("encoder '{}' unknown (absolute, relative-2c, relative-offset, relative-sign)", encoderName);
        }
        b.encoder = *enc;
    }
    if (warnings != nullptr && defaultScope == MidiScope::Rig) {
        // ADR-1705: the rig file holds rig mappings only; a scene or a project scope there is not something it can mean.
        if (!scopeName.empty() && scopeName != "rig") {
            return fail("scope '{}' in the rig: the rig holds rig mappings only", scopeName);
        }
        b.scope = MidiScope::Rig;
        b.scene.clear();
        if (!b.overrides.empty()) {
            return fail("a rig mapping cannot override another mapping");
        }
    } else if (warnings != nullptr) {
        if (scopeName == "scene") {
            b.scope = MidiScope::Scene;
        } else if (scopeName == "project" || scopeName.empty()) {
            b.scope = MidiScope::Project;
        } else if (scopeName == "rig") {
            return fail("a rig mapping belongs in the app's rig file, not in a project");
        } else {
            return fail("scope '{}' is not project or scene", scopeName);
        }
        if (b.scope == MidiScope::Scene && b.scene.empty()) {
            return fail("a scene mapping needs the 'scene' it belongs to");
        }
        static const std::set<std::string> known = {
            "id", "scope", "scene", "overrides", "source", "device", "deviceName", "channel", "kind", "number",
            "numberHigh", "enabled", "toggle", "velocity", "pickup", "invert", "curve", "scale", "offset", "smoothMs",
            "encoder", "signal", "parameter", "component", "min", "max", "action"};
        for (const auto& [key, value] : e.items()) {
            if (!known.contains(key)) {
                warnings->push_back(fmt::format("{} '{}': unknown key '{}' ignored",
                                                defaultScope == MidiScope::Rig ? "MIDI rig" : "control.midi.mappings",
                                                b.id, key));
            }
        }
    }
    return b;
}

namespace {

std::uint64_t fnv1a(std::string_view text) {
    std::uint64_t h = 1469598103934665603ull;
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h;
}

// ADR-1700: every binding gets an id. An authored default without one is named by what it is -- its source and its
// target -- so the id is the same on every load and a tombstone or an override keeps finding it; a duplicate gets
// "-2", "-3". A hand-written user mapping without one is named the same way ("u-").
void assignMidiIds(ControlMap& m) {
    std::set<std::string> taken;
    for (const auto& b : m.midi) {
        if (!b.id.empty()) taken.insert(b.id);
    }
    for (const auto& b : m.midiUser) {
        if (!b.id.empty()) taken.insert(b.id);
    }
    for (const auto& b : m.midiRig) { // ADR-1705: a rig id is not reused by a default named after its content
        if (!b.id.empty()) taken.insert(b.id);
    }
    const auto name = [&](MidiBinding& b, const char* prefix) {
        if (!b.id.empty()) {
            return;
        }
        std::string canonical =
            fmt::format("{}|{}|{}|{}|{}|{}|{}|{}|{}", midiBindKindName(b.kind), b.number, b.numberHigh, b.channel,
                        b.source, b.device, b.target.signal, b.target.parameter, b.target.component);
        if (b.target.action != MidiAction::None) { // ADR-1705; appended, so every existing default keeps its id
            canonical += fmt::format("|{}", midiActionName(b.target.action));
        }
        const std::string base = fmt::format("{}{:08x}", prefix, fnv1a(canonical) & 0xFFFFFFFFull);
        std::string id = base;
        for (int n = 2; taken.contains(id); ++n) {
            id = base + "-" + std::to_string(n);
        }
        taken.insert(id);
        b.id = std::move(id);
    };
    for (auto& b : m.midi) {
        name(b, "d-");
    }
    for (auto& b : m.midiUser) {
        name(b, "u-");
    }
}

} // namespace

const char* midiBindKindName(MidiBindKind kind) {
    switch (kind) {
    case MidiBindKind::ControlChange: return "cc";
    case MidiBindKind::Note: return "note";
    case MidiBindKind::NoteEvent: return "noteEvent";
    case MidiBindKind::PitchBend: return "pitchBend";
    case MidiBindKind::ChannelPressure: return "pressure";
    case MidiBindKind::Program: return "program";
    case MidiBindKind::PolyPressure: return "polyPressure";
    case MidiBindKind::Nrpn: return "nrpn";
    case MidiBindKind::Rpn: return "rpn";
    }
    return "cc";
}

std::optional<MidiBindKind> midiBindKindFromName(std::string_view name) {
    if (name == "cc") return MidiBindKind::ControlChange;
    if (name == "note") return MidiBindKind::Note;
    if (name == "noteEvent") return MidiBindKind::NoteEvent;
    if (name == "pitchBend") return MidiBindKind::PitchBend;
    if (name == "pressure") return MidiBindKind::ChannelPressure;
    if (name == "program") return MidiBindKind::Program;
    if (name == "polyPressure") return MidiBindKind::PolyPressure;
    if (name == "nrpn") return MidiBindKind::Nrpn;
    if (name == "rpn") return MidiBindKind::Rpn;
    return std::nullopt;
}

bool midiBindKindIsNote(MidiBindKind kind) {
    return kind == MidiBindKind::Note || kind == MidiBindKind::NoteEvent;
}

bool midiBindKindHasNumber(MidiBindKind kind) {
    return kind != MidiBindKind::PitchBend && kind != MidiBindKind::ChannelPressure;
}

bool midiBindKindIsContinuous(MidiBindKind kind) {
    switch (kind) {
    case MidiBindKind::ControlChange:
    case MidiBindKind::PitchBend:
    case MidiBindKind::ChannelPressure:
    case MidiBindKind::PolyPressure:
    case MidiBindKind::Nrpn:
    case MidiBindKind::Rpn:
        return true;
    default:
        return false;
    }
}

const char* midiCurveName(MidiCurve curve) {
    switch (curve) {
    case MidiCurve::Linear: return "linear";
    case MidiCurve::Exponential: return "exponential";
    case MidiCurve::Logarithmic: return "logarithmic";
    case MidiCurve::SCurve: return "s-curve";
    }
    return "linear";
}

std::optional<MidiCurve> midiCurveFromName(std::string_view name) {
    if (name == "linear") return MidiCurve::Linear;
    if (name == "exponential") return MidiCurve::Exponential;
    if (name == "logarithmic") return MidiCurve::Logarithmic;
    if (name == "s-curve") return MidiCurve::SCurve;
    return std::nullopt;
}

float applyMidiCurve(MidiCurve curve, float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    switch (curve) {
    case MidiCurve::Linear: return x;
    case MidiCurve::Exponential: return x * x;                         // slow start: fine control at the bottom
    case MidiCurve::Logarithmic: return 1.0f - (1.0f - x) * (1.0f - x); // fast start: fine control at the top
    case MidiCurve::SCurve: return x * x * (3.0f - 2.0f * x);          // smoothstep: fine at both ends
    }
    return x;
}

const char* midiEncoderName(MidiEncoder encoder) {
    switch (encoder) {
    case MidiEncoder::Absolute: return "absolute";
    case MidiEncoder::TwosComplement: return "relative-2c";
    case MidiEncoder::BinaryOffset: return "relative-offset";
    case MidiEncoder::SignMagnitude: return "relative-sign";
    }
    return "absolute";
}

std::optional<MidiEncoder> midiEncoderFromName(std::string_view name) {
    if (name == "absolute") return MidiEncoder::Absolute;
    if (name == "relative-2c") return MidiEncoder::TwosComplement;
    if (name == "relative-offset") return MidiEncoder::BinaryOffset;
    if (name == "relative-sign") return MidiEncoder::SignMagnitude;
    return std::nullopt;
}

int relativeEncoderDelta(MidiEncoder encoder, std::uint8_t value) {
    const int v = value & 0x7F;
    switch (encoder) {
    case MidiEncoder::Absolute: return 0;
    case MidiEncoder::TwosComplement: return v < 64 ? v : v - 128;
    case MidiEncoder::BinaryOffset: return v - 64;
    case MidiEncoder::SignMagnitude: return (v & 0x40) != 0 ? -(v & 0x3F) : (v & 0x3F);
    }
    return 0;
}

const char* midiScopeName(MidiScope scope) {
    switch (scope) {
    case MidiScope::Default: return "default";
    case MidiScope::Rig: return "rig";
    case MidiScope::Project: return "project";
    case MidiScope::Scene: return "scene";
    }
    return "default";
}

const char* midiCombineName(MidiCombine combine) {
    switch (combine) {
    case MidiCombine::Replace: return "replace";
    case MidiCombine::Max: return "max";
    case MidiCombine::Add: return "add";
    }
    return "replace";
}

std::optional<MidiCombine> midiCombineFromName(std::string_view name) {
    if (name == "replace") return MidiCombine::Replace;
    if (name == "max") return MidiCombine::Max;
    if (name == "add") return MidiCombine::Add;
    return std::nullopt;
}

const char* midiMappingStateName(MidiMappingState state) {
    switch (state) {
    case MidiMappingState::Active: return "active";
    case MidiMappingState::Disabled: return "disabled";
    case MidiMappingState::DefaultOff: return "default off";
    case MidiMappingState::Overridden: return "overridden";
    case MidiMappingState::Shadowed: return "shadowed";
    case MidiMappingState::OtherScene: return "other scene";
    case MidiMappingState::Invalid: return "invalid";
    }
    return "active";
}

const char* midiActionName(MidiAction action) {
    switch (action) {
    case MidiAction::None: return "";
    case MidiAction::TransportPlay: return "transport.play";
    case MidiAction::TransportReturnToStart: return "transport.return-to-start";
    case MidiAction::LiveNextScene: return "live.next-scene";
    case MidiAction::LivePreviousScene: return "live.previous-scene";
    }
    return "";
}

const char* midiActionLabel(MidiAction action) {
    switch (action) {
    case MidiAction::None: return "None";
    case MidiAction::TransportPlay: return "Play / pause";
    case MidiAction::TransportReturnToStart: return "Return to start";
    case MidiAction::LiveNextScene: return "Next live scene";
    case MidiAction::LivePreviousScene: return "Previous live scene";
    }
    return "None";
}

std::optional<MidiAction> midiActionFromName(std::string_view name) {
    for (const MidiAction a : kMidiActions) {
        if (name == midiActionName(a)) {
            return a;
        }
    }
    return std::nullopt;
}

std::vector<std::string> midiDestinationKeys(const BindingTarget& target) {
    std::vector<std::string> keys;
    if (target.action != MidiAction::None) {
        keys.push_back(std::string("action.") + midiActionName(target.action));
    }
    if (!target.signal.empty()) {
        keys.push_back("control." + target.signal);
    }
    if (!target.parameter.empty()) {
        keys.push_back(target.component < 0 ? target.parameter
                                            : target.parameter + "[" + std::to_string(target.component) + "]");
    }
    return keys;
}

namespace {

bool anyDevice(const MidiBinding& b) {
    return b.device.empty() && (b.source.empty() || b.source == "*");
}

int rangeTop(const MidiBinding& b) {
    return b.numberHigh >= b.number ? b.numberHigh : b.number;
}

bool sameFamily(MidiBindKind a, MidiBindKind b) {
    return a == b || (midiBindKindIsNote(a) && midiBindKindIsNote(b));
}

} // namespace

bool midiSourcesOverlap(const MidiBinding& a, const MidiBinding& b) {
    if (!sameFamily(a.kind, b.kind)) {
        return false;
    }
    if (a.channel >= 0 && b.channel >= 0 && a.channel != b.channel) {
        return false;
    }
    if (midiBindKindHasNumber(a.kind) && a.number >= 0 && b.number >= 0) {
        if (rangeTop(a) < b.number || rangeTop(b) < a.number) {
            return false;
        }
    }
    if (anyDevice(a) || anyDevice(b)) {
        return true;
    }
    if (!a.device.empty() && !b.device.empty()) {
        return a.device == b.device;
    }
    // One names a device by id (with its name), the other by a substring: compare the name.
    const MidiBinding& byId = a.device.empty() ? b : a;
    const MidiBinding& bySubstring = a.device.empty() ? a : b;
    return containsNoCase(byId.deviceName, bySubstring.source);
}

bool midiDeviceMatches(const MidiBinding& binding, std::string_view deviceId, std::string_view deviceName) {
    if (!binding.device.empty()) {
        return deviceId == binding.device;
    }
    return containsNoCase(deviceName, binding.source);
}

std::string midiNoteName(int note) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (note < 0 || note > 127) {
        return "?";
    }
    return std::string(names[note % 12]) + std::to_string(note / 12 - 1);
}

std::string midiSourceLabel(const MidiBinding& b) {
    const bool range = b.numberHigh > b.number && b.number >= 0;
    switch (b.kind) {
    case MidiBindKind::ControlChange:
        return b.number < 0 ? "Any CC" : "CC " + std::to_string(b.number);
    case MidiBindKind::Note:
    case MidiBindKind::NoteEvent:
        if (b.number < 0) return "Any note";
        if (range) return "Notes " + midiNoteName(b.number) + "-" + midiNoteName(b.numberHigh);
        return "Note " + midiNoteName(b.number);
    case MidiBindKind::PitchBend: return "Pitch bend";
    case MidiBindKind::ChannelPressure: return "Channel pressure";
    case MidiBindKind::PolyPressure:
        if (b.number < 0) return "Poly pressure";
        if (range) return "Poly pressure " + midiNoteName(b.number) + "-" + midiNoteName(b.numberHigh);
        return "Poly pressure " + midiNoteName(b.number);
    case MidiBindKind::Program: return b.number < 0 ? "Any program" : "Program " + std::to_string(b.number + 1);
    case MidiBindKind::Nrpn: return b.number < 0 ? "Any NRPN" : "NRPN " + std::to_string(b.number);
    case MidiBindKind::Rpn: return b.number < 0 ? "Any RPN" : "RPN " + std::to_string(b.number);
    }
    return "?";
}

std::string newMidiMappingId() {
    static std::mt19937_64 rng{std::random_device{}()};
    return fmt::format("m-{:012x}", rng() & 0xFFFFFFFFFFFFull);
}

bool pickupAccepts(PickupState& state, float incoming, float current, float tolerance) {
    // Something else moved the target since this binding last set it: let go until the knob comes back to it.
    if (state.latched && state.lastSent >= 0.0f && std::abs(current - state.lastSent) > 1e-3f) {
        state.latched = false;
    }
    if (!state.latched) {
        const bool near = std::abs(incoming - current) <= tolerance;
        // A fast knob can step over the value between two messages: passing through it picks it up too.
        const bool crossed = state.lastIncoming >= 0.0f && (state.lastIncoming - current) * (incoming - current) <= 0.0f;
        state.latched = near || crossed;
    }
    state.lastIncoming = incoming;
    if (state.latched) {
        state.lastSent = incoming;
    }
    return state.latched;
}

nlohmann::json ControlMap::toJson() const {
    nlohmann::json midiJson = nlohmann::json::array();
    for (const auto& b : midi) {
        midiJson.push_back(midiBindingToJson(b));
    }
    nlohmann::json oscJson = nlohmann::json::array();
    for (const auto& b : osc) {
        nlohmann::json j = targetToJson(b.target);
        j["address"] = b.address;
        j["argIndex"] = b.argIndex;
        if (b.event) {
            j["event"] = true;
        }
        j["inMin"] = b.inMin;
        j["inMax"] = b.inMax;
        oscJson.push_back(std::move(j));
    }
    nlohmann::json out{{"osc", {{"enabled", oscEnabled}, {"port", oscPort}, {"bind", oscBind}, {"prefix", oscPrefix}, {"direct", directOsc},
                                {"feedbackHost", feedbackHost}, {"feedbackPort", feedbackPort}, {"feedback", feedbackEnabled},
                                {"bindings", std::move(oscJson)}}},
                       {"midi", {{"enabled", midiEnabled}, {"filter", midiFilter}, {"bindings", std::move(midiJson)}}}};
    if (midiPickup) {
        out["midi"]["pickup"] = true; // ADR-1606: written only when on, so every existing project saves as it was
    }
    // ADR-1700: the user layer, each key written only when it holds something, so a project without one saves as it was.
    if (!midiUser.empty()) {
        nlohmann::json mappings = nlohmann::json::array();
        for (const auto& b : midiUser) {
            mappings.push_back(midiBindingToJson(b));
        }
        out["midi"]["mappings"] = std::move(mappings);
    }
    if (!disabledDefaults.empty()) {
        out["midi"]["disabledDefaults"] = disabledDefaults;
    }
    if (!combine.empty()) {
        nlohmann::json policies = nlohmann::json::object();
        for (const auto& [key, policy] : combine) {
            policies[key] = midiCombineName(policy);
        }
        out["midi"]["combine"] = std::move(policies);
    }
    if (!channelRests.empty()) { // ADR-1612: likewise
        nlohmann::json channels = nlohmann::json::array();
        for (const auto& c : channelRests) {
            nlohmann::json e{{"name", c.name}, {"rest", c.rest}};
            if (c.pickup) {
                e["pickup"] = true;
            }
            channels.push_back(std::move(e));
        }
        out["channels"] = std::move(channels);
    }
    return out;
}

Result<ControlMap> ControlMap::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'control' must be an object");
    }
    ControlMap m;
    if (const auto oscIt = j.find("osc"); oscIt != j.end()) {
        const auto& o = *oscIt;
        if (!o.is_object()) {
            return fail("control.osc must be an object");
        }
        m.oscEnabled = o.value("enabled", true);
        const int port = o.value("port", 9000);
        if (port < 0 || port > 65535) {
            return fail("control.osc.port {} is out of range", port);
        }
        m.oscPort = static_cast<std::uint16_t>(port);
        m.oscBind = o.value("bind", std::string("0.0.0.0"));
        m.oscPrefix = o.value("prefix", std::string("/avgen"));
        if (!m.oscPrefix.empty() && !isValidAddress(m.oscPrefix)) {
            return fail("control.osc.prefix '{}' is not a valid OSC address", m.oscPrefix);
        }
        m.directOsc = o.value("direct", true);
        m.feedbackHost = o.value("feedbackHost", std::string());
        const int feedbackPort = o.value("feedbackPort", 9001);
        if (feedbackPort < 0 || feedbackPort > 65535) {
            return fail("control.osc.feedbackPort {} is out of range", feedbackPort);
        }
        m.feedbackPort = static_cast<std::uint16_t>(feedbackPort);
        m.feedbackEnabled = o.value("feedback", false);
        if (o.contains("bindings")) {
            if (!o["bindings"].is_array()) {
                return fail("control.osc.bindings must be an array");
            }
            for (const auto& e : o["bindings"]) {
                auto target = targetFromJson(e);
                if (!target) {
                    return fail("osc binding: {}", target.error().message);
                }
                if (target->action != MidiAction::None) {
                    return fail("osc binding: 'action' is a MIDI destination; OSC uses the direct scheme's transport");
                }
                OscBinding b;
                b.target = *target;
                if (!e.contains("address") || !e["address"].is_string()) {
                    return fail("osc binding needs an 'address'");
                }
                b.address = e["address"].get<std::string>();
                b.argIndex = e.value("argIndex", 0);
                b.event = e.value("event", false);
                b.inMin = e.value("inMin", 0.0f);
                b.inMax = e.value("inMax", 1.0f);
                m.osc.push_back(std::move(b));
            }
        }
    }
    if (const auto midiIt = j.find("midi"); midiIt != j.end()) {
        const auto& mj = *midiIt;
        if (!mj.is_object()) {
            return fail("control.midi must be an object");
        }
        m.midiEnabled = mj.value("enabled", true);
        m.midiFilter = mj.value("filter", std::string("*"));
        m.midiPickup = mj.value("pickup", false); // ADR-1606
        if (mj.contains("bindings")) {
            if (!mj["bindings"].is_array()) {
                return fail("control.midi.bindings must be an array");
            }
            for (const auto& e : mj["bindings"]) {
                // An authored default that cannot be read fails the load, as it always did: it is the author's error.
                auto b = midiBindingFromJson(e, MidiScope::Default, nullptr);
                if (!b) {
                    return fail("midi binding: {}", b.error().message);
                }
                m.midi.push_back(std::move(*b));
            }
        }
        if (const auto it = mj.find("mappings"); it != mj.end()) { // ADR-1700
            if (!it->is_array()) {
                return fail("control.midi.mappings must be an array");
            }
            for (const auto& e : *it) {
                // A user record that cannot be read is kept verbatim, reported and never applied -- not deleted.
                auto b = midiBindingFromJson(e, MidiScope::Project, &m.warnings);
                if (!b) {
                    MidiBinding kept;
                    kept.scope = MidiScope::Project;
                    kept.enabled = false;
                    kept.invalidRaw = e.dump();
                    kept.invalidReason = b.error().message;
                    if (e.is_object() && e.contains("id") && e["id"].is_string()) {
                        kept.id = e["id"].get<std::string>();
                    }
                    m.warnings.push_back(fmt::format("control.midi.mappings: a mapping was set aside: {}", kept.invalidReason));
                    m.midiUser.push_back(std::move(kept));
                    continue;
                }
                m.midiUser.push_back(std::move(*b));
            }
        }
        if (const auto it = mj.find("disabledDefaults"); it != mj.end()) { // ADR-1700
            if (!it->is_array()) {
                return fail("control.midi.disabledDefaults must be an array of mapping ids");
            }
            for (const auto& e : *it) {
                if (e.is_string()) {
                    m.disabledDefaults.push_back(e.get<std::string>());
                } else {
                    m.warnings.push_back("control.midi.disabledDefaults: an entry that is not a string was ignored");
                }
            }
        }
        if (const auto it = mj.find("combine"); it != mj.end()) { // ADR-1700
            if (!it->is_object()) {
                return fail("control.midi.combine must be an object of destination -> replace|max|add");
            }
            for (const auto& [key, value] : it->items()) {
                const auto policy = value.is_string() ? midiCombineFromName(value.get<std::string>()) : std::nullopt;
                if (!policy) {
                    m.warnings.push_back(fmt::format("control.midi.combine '{}': not replace, max or add; ignored", key));
                    continue;
                }
                m.setCombine(key, *policy);
            }
        }
        assignMidiIds(m);
    }
    if (const auto it = j.find("channels"); it != j.end()) { // ADR-1612
        if (!it->is_array()) {
            return fail("control.channels must be an array of {name, rest[, pickup]}");
        }
        for (const auto& e : *it) {
            if (!e.is_object() || !e.contains("name") || !e["name"].is_string() || e["name"].get<std::string>().empty()) {
                return fail("control.channels: every entry needs a 'name'");
            }
            ChannelRest c;
            c.name = e["name"].get<std::string>();
            if (c.name.starts_with("control.")) {
                return fail("control.channels: '{}' is named without the 'control.' prefix", c.name);
            }
            if (!e.contains("rest") || !e["rest"].is_number()) {
                return fail("control.channels '{}' needs a numeric 'rest' (0..1)", c.name);
            }
            c.rest = e["rest"].get<float>();
            if (!(c.rest >= 0.0f && c.rest <= 1.0f)) {
                return fail("control.channels '{}': rest {} is outside 0..1", c.name, c.rest);
            }
            c.pickup = e.value("pickup", false);
            for (const auto& other : m.channelRests) {
                if (other.name == c.name) {
                    return fail("control.channels: '{}' is declared twice", c.name);
                }
            }
            m.channelRests.push_back(std::move(c));
        }
    }
    return m;
}

MidiCombine ControlMap::combineFor(std::string_view destinationKey) const {
    for (const auto& [key, policy] : combine) {
        if (key == destinationKey) {
            return policy;
        }
    }
    return MidiCombine::Replace;
}

void ControlMap::setCombine(const std::string& destinationKey, MidiCombine policy) {
    std::erase_if(combine, [&](const auto& entry) { return entry.first == destinationKey; });
    if (policy != MidiCombine::Replace) {
        combine.emplace_back(destinationKey, policy);
        std::sort(combine.begin(), combine.end()); // one order on disk whatever order they were set in
    }
}

MidiBinding* ControlMap::findMidi(std::string_view id) {
    for (auto* list : {&midi, &midiUser, &midiRig}) {
        for (auto& b : *list) {
            if (b.id == id) {
                return &b;
            }
        }
    }
    return nullptr;
}

const MidiBinding* ControlMap::findMidi(std::string_view id) const {
    return const_cast<ControlMap*>(this)->findMidi(id);
}

std::vector<ResolvedMapping> resolveMidiMappings(const ControlMap& map, std::string_view sceneKey) {
    std::vector<ResolvedMapping> out;
    out.reserve(map.midi.size() + map.midiRig.size() + map.midiUser.size());
    for (const auto& b : map.midi) {
        out.push_back({&b, MidiMappingState::Active, {}});
    }
    for (const auto& b : map.midiRig) { // ADR-1705
        out.push_back({&b, MidiMappingState::Active, {}});
    }
    for (const auto& b : map.midiUser) {
        out.push_back({&b, MidiMappingState::Active, {}});
    }
    const auto rank = [](const MidiBinding& b) { return static_cast<int>(b.scope); };
    // 1. Each mapping's own state.
    for (auto& r : out) {
        const MidiBinding& b = *r.binding;
        if (b.invalid()) {
            r.state = MidiMappingState::Invalid;
        } else if (b.scope == MidiScope::Scene && b.scene != sceneKey) {
            r.state = MidiMappingState::OtherScene;
        } else if (b.scope == MidiScope::Default &&
                   std::find(map.disabledDefaults.begin(), map.disabledDefaults.end(), b.id) != map.disabledDefaults.end()) {
            r.state = MidiMappingState::DefaultOff;
        } else if (!b.enabled) {
            r.state = MidiMappingState::Disabled;
        }
    }
    // 2. Explicit overrides: a live user mapping (in this scene) replaces the one it names.
    for (const auto& r : out) {
        const MidiBinding& b = *r.binding;
        if (b.overrides.empty() || r.state == MidiMappingState::OtherScene || r.state == MidiMappingState::Invalid) {
            continue;
        }
        for (auto& target : out) {
            if (target.binding->id == b.overrides && rank(*target.binding) < rank(b) &&
                target.state == MidiMappingState::Active) {
                target.state = MidiMappingState::Overridden;
                target.by = b.id;
            }
        }
    }
    // 3. Shadowing: a lower scope's mapping on an overlapping source that drives a destination a higher scope's active
    // mapping also drives does not apply. One physical control never drives one destination twice by accident.
    for (auto& low : out) {
        if (low.state != MidiMappingState::Active) {
            continue;
        }
        const auto lowKeys = midiDestinationKeys(low.binding->target);
        for (const auto& high : out) {
            if (high.state != MidiMappingState::Active || rank(*high.binding) <= rank(*low.binding)) {
                continue;
            }
            if (!midiSourcesOverlap(*low.binding, *high.binding)) {
                continue;
            }
            const auto highKeys = midiDestinationKeys(high.binding->target);
            const bool shared = std::any_of(lowKeys.begin(), lowKeys.end(), [&](const std::string& k) {
                return std::find(highKeys.begin(), highKeys.end(), k) != highKeys.end();
            });
            if (shared) {
                low.state = MidiMappingState::Shadowed;
                low.by = high.binding->id;
                break;
            }
        }
    }
    return out;
}

const ChannelRest* ControlMap::restFor(std::string_view channel) const {
    for (const auto& c : channelRests) {
        if (c.name == channel) {
            return &c;
        }
    }
    return nullptr;
}

std::vector<ControlMap::Channel> ControlMap::channels() const {
    std::vector<Channel> out;
    auto add = [&](const std::string& name, bool event) {
        if (name.empty()) {
            return;
        }
        for (const auto& c : out) {
            if (c.name == name) {
                return;
            }
        }
        const ChannelRest* rest = event ? nullptr : restFor(name);
        out.push_back({name, event, rest != nullptr ? rest->rest : 0.0f});
    };
    // ADR-1612: a declared channel exists (at its rest) even before a binding or OSC names it.
    for (const auto& c : channelRests) {
        add(c.name, false);
    }
    for (const auto& b : midi) {
        add(b.target.signal, b.kind == MidiBindKind::NoteEvent);
    }
    for (const auto& b : midiUser) { // ADR-1700
        add(b.target.signal, b.kind == MidiBindKind::NoteEvent);
    }
    for (const auto& b : midiRig) { // ADR-1705: a rig channel exists in every scene
        add(b.target.signal, b.kind == MidiBindKind::NoteEvent);
    }
    for (const auto& b : osc) {
        add(b.target.signal, b.event);
    }
    return out;
}

std::optional<float> midiSourceValue(const MidiBinding& binding, const MidiMessage& message) {
    if (binding.channel >= 0 && message.channel != static_cast<std::uint8_t>(binding.channel)) {
        return std::nullopt;
    }
    const auto numberMatches = [&](int number) {
        if (binding.number < 0) {
            return true;
        }
        return number >= binding.number && number <= rangeTop(binding);
    };
    switch (binding.kind) {
    case MidiBindKind::ControlChange:
        if (message.kind != MidiKind::ControlChange || (binding.number >= 0 && message.data1 != binding.number)) {
            return std::nullopt;
        }
        return message.normalized();
    case MidiBindKind::Note:
        if ((message.kind != MidiKind::NoteOn && message.kind != MidiKind::NoteOff) || !numberMatches(message.data1)) {
            return std::nullopt;
        }
        return message.kind == MidiKind::NoteOn ? message.normalized() : 0.0f;
    case MidiBindKind::NoteEvent:
        if (message.kind != MidiKind::NoteOn || !numberMatches(message.data1)) {
            return std::nullopt;
        }
        return message.normalized();
    case MidiBindKind::PitchBend:
        if (message.kind != MidiKind::PitchBend) {
            return std::nullopt;
        }
        return message.normalized();
    case MidiBindKind::ChannelPressure:
        if (message.kind != MidiKind::ChannelPressure) {
            return std::nullopt;
        }
        // ADR-1703: the pressure is data2 (the parser's field for it). This read data1, which a channel pressure
        // message never sets, so every `pressure` binding produced 0 whatever the player did.
        return message.normalized();
    case MidiBindKind::Program:
        if (message.kind != MidiKind::ProgramChange || (binding.number >= 0 && message.data1 != binding.number)) {
            return std::nullopt;
        }
        return binding.number >= 0 ? 1.0f : static_cast<float>(message.data1) / 127.0f;
    case MidiBindKind::PolyPressure:
        if (message.kind != MidiKind::PolyPressure || !numberMatches(message.data1)) {
            return std::nullopt;
        }
        return message.normalized();
    case MidiBindKind::Nrpn:
    case MidiBindKind::Rpn: {
        const MidiKind want = binding.kind == MidiBindKind::Nrpn ? MidiKind::Nrpn : MidiKind::Rpn;
        if (message.kind != want || (binding.number >= 0 && message.param != binding.number)) {
            return std::nullopt;
        }
        return message.normalized();
    }
    }
    return std::nullopt;
}

std::optional<Match> matchOsc(const OscBinding& binding, const OscMessage& message) {
    if (!matchAddress(binding.address, message.address) && message.address != binding.address) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(std::max(0, binding.argIndex));
    float value = binding.event ? 1.0f : 0.0f;
    if (message.hasNumber(index)) {
        value = message.number(index);
        const float span = binding.inMax - binding.inMin;
        value = std::abs(span) > 1e-9f ? (value - binding.inMin) / span : value;
    } else if (!binding.event) {
        return std::nullopt;
    }
    return Match{std::clamp(value, 0.0f, 1.0f), binding.event};
}

std::optional<DirectCommand> parseDirectOsc(const OscMessage& message, std::string_view prefix) {
    std::string_view address = message.address;
    if (!prefix.empty()) {
        if (address.size() <= prefix.size() || address.substr(0, prefix.size()) != prefix || address[prefix.size()] != '/') {
            return std::nullopt;
        }
        address.remove_prefix(prefix.size());
    }
    // address is now "/<verb>/<rest>"
    const auto second = address.find('/', 1);
    const std::string_view verb = address.substr(1, second == std::string_view::npos ? std::string_view::npos : second - 1);
    const std::string_view rest = second == std::string_view::npos ? std::string_view() : address.substr(second + 1);
    DirectCommand cmd;
    for (std::size_t i = 0; i < message.args.size(); ++i) {
        if (message.hasNumber(i)) {
            cmd.values.push_back(message.number(i));
        }
    }
    using K = DirectCommand::Kind;
    if (verb == "param") {
        if (rest.empty() || cmd.values.empty()) {
            return std::nullopt;
        }
        cmd.kind = K::SetParameter;
        cmd.path = std::string(rest);
        return cmd;
    }
    if (verb == "signal") {
        if (rest.empty() || cmd.values.empty()) {
            return std::nullopt;
        }
        cmd.kind = K::SetSignal;
        cmd.path = std::string(rest);
        return cmd;
    }
    if (verb == "pulse") {
        if (rest.empty()) {
            return std::nullopt;
        }
        cmd.kind = K::Pulse;
        cmd.path = std::string(rest);
        if (cmd.values.empty()) {
            cmd.values.push_back(1.0f);
        }
        return cmd;
    }
    if (verb == "control" && rest == "reset") { // ADR-1612: every control channel back to its rest
        cmd.kind = K::ControlReset;
        return cmd;
    }
    if (verb == "state") {
        // "/avgen/state/go <name> [instant]" or "/avgen/state/<name>"
        std::string_view name = rest == "go" ? message.text(0) : rest;
        if (name.empty()) {
            return std::nullopt;
        }
        cmd.kind = K::StateGo;
        cmd.path = std::string(name);
        if (rest == "go" && message.hasNumber(1)) {
            cmd.values.push_back(message.number(1));
        }
        return cmd;
    }
    if (verb == "preset") {
        if (rest == "recall") {
            const auto name = message.text(0);
            if (name.empty()) {
                return std::nullopt;
            }
            cmd.kind = K::PresetRecall;
            cmd.path = std::string(name);
            return cmd;
        }
        if (rest == "morph") {
            const auto a = message.text(0);
            const auto b = message.text(1);
            if (a.empty() || b.empty() || !message.hasNumber(2)) {
                return std::nullopt;
            }
            cmd.kind = K::PresetMorph;
            cmd.path = std::string(a);
            cmd.second = std::string(b);
            cmd.values = {message.number(2)};
            return cmd;
        }
        return std::nullopt;
    }
    if (verb == "query") {
        if (rest == "all") {
            cmd.kind = K::QueryAll;
            return cmd;
        }
        if (rest == "presets") {
            cmd.kind = K::QueryPresets;
            return cmd;
        }
        const auto path = message.text(0);
        if (!path.empty()) {
            cmd.path = std::string(path);
        } else if (!rest.empty()) {
            cmd.path = std::string(rest);
        } else {
            return std::nullopt;
        }
        cmd.kind = K::Query;
        return cmd;
    }
    if (verb == "transport") {
        if (rest == "play") cmd.kind = K::Play;
        else if (rest == "pause") cmd.kind = K::Pause;
        else if (rest == "stop") cmd.kind = K::Stop;
        else if (rest == "toggle") cmd.kind = K::Toggle;
        else if (rest == "seek") {
            if (cmd.values.empty()) {
                return std::nullopt;
            }
            cmd.kind = K::Seek;
        } else {
            return std::nullopt;
        }
        return cmd;
    }
    return std::nullopt;
}

std::string parameterAddress(std::string_view prefix, std::string_view path) {
    std::string out(prefix);
    out += "/param/";
    out += path;
    return out;
}

bool splitHostPort(std::string_view text, std::string& host, std::uint16_t& port) {
    const auto colon = text.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= text.size()) {
        return false;
    }
    int value = 0;
    for (const char c : text.substr(colon + 1)) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
        if (value > 65535) {
            return false;
        }
    }
    host = std::string(text.substr(0, colon));
    port = static_cast<std::uint16_t>(value);
    return true;
}

} // namespace avgen::control
