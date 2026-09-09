#include "control/control_map.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

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
    if (!t.signal.empty()) {
        j["signal"] = t.signal;
    }
    if (!t.parameter.empty()) {
        j["parameter"] = t.parameter;
        j["component"] = t.component;
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
    if (t.signal.empty() && t.parameter.empty()) {
        return fail("binding needs a 'signal' or a 'parameter'");
    }
    return t;
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
    return std::nullopt;
}

nlohmann::json ControlMap::toJson() const {
    nlohmann::json midiJson = nlohmann::json::array();
    for (const auto& b : midi) {
        nlohmann::json j = targetToJson(b.target);
        j["source"] = b.source;
        j["channel"] = b.channel;
        j["kind"] = midiBindKindName(b.kind);
        j["number"] = b.number;
        if (b.toggle) {
            j["toggle"] = true;
        }
        midiJson.push_back(std::move(j));
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
    return nlohmann::json{{"osc", {{"enabled", oscEnabled}, {"port", oscPort}, {"bind", oscBind}, {"prefix", oscPrefix}, {"direct", directOsc},
                                   {"feedbackHost", feedbackHost}, {"feedbackPort", feedbackPort}, {"feedback", feedbackEnabled},
                                   {"bindings", std::move(oscJson)}}},
                          {"midi", {{"enabled", midiEnabled}, {"filter", midiFilter}, {"bindings", std::move(midiJson)}}}};
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
        if (mj.contains("bindings")) {
            if (!mj["bindings"].is_array()) {
                return fail("control.midi.bindings must be an array");
            }
            for (const auto& e : mj["bindings"]) {
                auto target = targetFromJson(e);
                if (!target) {
                    return fail("midi binding: {}", target.error().message);
                }
                MidiBinding b;
                b.target = *target;
                b.source = e.value("source", std::string("*"));
                b.channel = e.value("channel", -1);
                const std::string kind = e.value("kind", std::string("cc"));
                auto k = midiBindKindFromName(kind);
                if (!k) {
                    return fail("midi binding kind '{}' unknown", kind);
                }
                b.kind = *k;
                b.number = e.value("number", -1);
                b.toggle = e.value("toggle", false);
                m.midi.push_back(std::move(b));
            }
        }
    }
    return m;
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
        out.push_back({name, event});
    };
    for (const auto& b : midi) {
        add(b.target.signal, b.kind == MidiBindKind::NoteEvent);
    }
    for (const auto& b : osc) {
        add(b.target.signal, b.event);
    }
    return out;
}

std::optional<Match> matchMidi(MidiBinding& binding, const MidiMessage& message) {
    if (!containsNoCase(message.source, binding.source)) {
        return std::nullopt;
    }
    if (binding.channel >= 0 && message.channel != static_cast<std::uint8_t>(binding.channel)) {
        return std::nullopt;
    }
    switch (binding.kind) {
    case MidiBindKind::ControlChange:
        if (message.kind != MidiKind::ControlChange || (binding.number >= 0 && message.data1 != binding.number)) {
            return std::nullopt;
        }
        return Match{message.normalized(), false};
    case MidiBindKind::Note:
        if ((message.kind != MidiKind::NoteOn && message.kind != MidiKind::NoteOff) ||
            (binding.number >= 0 && message.data1 != binding.number)) {
            return std::nullopt;
        }
        if (binding.toggle) {
            if (message.kind == MidiKind::NoteOn) {
                binding.toggleState = !binding.toggleState;
                return Match{binding.toggleState ? 1.0f : 0.0f, false};
            }
            return std::nullopt;
        }
        return Match{message.kind == MidiKind::NoteOn ? message.normalized() : 0.0f, false};
    case MidiBindKind::NoteEvent:
        if (message.kind != MidiKind::NoteOn || (binding.number >= 0 && message.data1 != binding.number)) {
            return std::nullopt;
        }
        return Match{message.normalized(), true};
    case MidiBindKind::PitchBend:
        if (message.kind != MidiKind::PitchBend) {
            return std::nullopt;
        }
        return Match{message.normalized(), false};
    case MidiBindKind::ChannelPressure:
        if (message.kind != MidiKind::ChannelPressure) {
            return std::nullopt;
        }
        return Match{static_cast<float>(message.data1) / 127.0f, false};
    case MidiBindKind::Program:
        if (message.kind != MidiKind::ProgramChange || (binding.number >= 0 && message.data1 != binding.number)) {
            return std::nullopt;
        }
        return Match{binding.number >= 0 ? 1.0f : static_cast<float>(message.data1) / 127.0f, binding.number >= 0};
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
