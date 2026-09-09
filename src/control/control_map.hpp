#pragma once

// Control map (milestone 1.1, ADR-021): how MIDI and OSC reach the engine. Two paths:
//   1. Bindings turn messages into *control signals* on the bus ("control.<channel>", 0..1, or
//      event pulses) that ordinary modulation routes consume, and/or set a parameter's base
//      value directly (with a min..max range).
//   2. The direct OSC scheme needs no binding: "<prefix>/param/<path> f..." sets a parameter,
//      "<prefix>/signal/<channel> f" sets a control signal, "<prefix>/pulse/<channel> [f]" fires
//      one, "<prefix>/preset/recall s", "<prefix>/preset/morph s s f", "<prefix>/transport/play|
//      pause|stop|toggle", "<prefix>/transport/seek f". Prefix defaults to "/avgen".
//   3. Query and feedback (follow-up): "<prefix>/query s<path>" (or "<prefix>/query/<path>")
//      answers "<prefix>/param/<path> f..." to the sender (or the feedback host);
//      "<prefix>/query/all" answers every serialised parameter in bundles; "<prefix>/query/
//      presets" answers "<prefix>/presets s...". With feedback enabled and a host set, parameter
//      base values that change (other than through OSC) are pushed as "<prefix>/param/<path>".
// Saved in the project under "control". Pure functions here; sockets and devices live in the
// osc/midi modules and the app's ControlHub.

#include "control/midi.hpp"
#include "control/osc.hpp"
#include "core/error.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::control {

enum class MidiBindKind : std::uint8_t { ControlChange, Note, NoteEvent, PitchBend, ChannelPressure, Program };
[[nodiscard]] const char* midiBindKindName(MidiBindKind kind);
[[nodiscard]] std::optional<MidiBindKind> midiBindKindFromName(std::string_view name);

// Where a matched value goes: a control signal (channel name without "control."), a parameter
// base value (path + component, mapped to min..max), or both.
struct BindingTarget {
    std::string signal;          // "" = none
    std::string parameter;       // "" = none
    int component = -1;          // -1 = every component
    float min = 0.0f;            // parameter range for value 0..1
    float max = 1.0f;
};

struct MidiBinding {
    std::string source = "*";    // substring of the source name ("*" = any)
    int channel = -1;            // 0..15, -1 = any
    MidiBindKind kind = MidiBindKind::ControlChange;
    int number = -1;             // controller / note / program number (-1 = any for Note/NoteEvent)
    bool toggle = false;         // Note: note-on flips 0/1 instead of following velocity
    BindingTarget target;
    // Runtime (not serialised): toggle state.
    bool toggleState = false;
};

struct OscBinding {
    std::string address;         // exact address or OSC pattern ("/fader/*")
    int argIndex = 0;            // which argument carries the value
    bool event = false;          // fire an event channel instead of setting a value
    float inMin = 0.0f;          // incoming range mapped to 0..1 (for controllers sending 0..127 etc.)
    float inMax = 1.0f;
    BindingTarget target;
};

struct ControlMap {
    bool oscEnabled = true;
    std::uint16_t oscPort = 9000;
    std::string oscBind = "0.0.0.0";
    std::string oscPrefix = "/avgen";
    bool directOsc = true;       // accept the direct scheme
    std::string feedbackHost;    // "" = replies go to the sender only, no pushed feedback
    std::uint16_t feedbackPort = 9001;
    bool feedbackEnabled = false; // push parameter changes to feedbackHost:feedbackPort
    bool midiEnabled = true;
    std::string midiFilter = "*";
    std::vector<MidiBinding> midi;
    std::vector<OscBinding> osc;

    [[nodiscard]] nlohmann::json toJson() const;
    static Result<ControlMap> fromJson(const nlohmann::json& j);
    [[nodiscard]] bool empty() const { return midi.empty() && osc.empty(); }
    // Every signal channel named by a binding (deduplicated) with its event flag, so the control
    // source can declare them before messages arrive.
    struct Channel {
        std::string name;
        bool event = false;
    };
    [[nodiscard]] std::vector<Channel> channels() const;
};

// A matched binding: the normalised value (0..1) and whether it is an event pulse.
struct Match {
    float value = 0.0f;
    bool event = false;
};
// Returns the value when `message` matches `binding` (toggle state is updated in place).
[[nodiscard]] std::optional<Match> matchMidi(MidiBinding& binding, const MidiMessage& message);
[[nodiscard]] std::optional<Match> matchOsc(const OscBinding& binding, const OscMessage& message);

// Direct OSC commands.
struct DirectCommand {
    enum class Kind : std::uint8_t {
        SetParameter, SetSignal, Pulse, PresetRecall, PresetMorph, Play, Pause, Stop, Toggle, Seek,
        Query, QueryAll, QueryPresets
    } kind = Kind::SetParameter;
    std::string path;            // parameter path / channel / preset name (or preset A) / queried path
    std::string second;          // preset B for morph
    std::vector<float> values;   // numeric arguments (components, morph t, seek seconds, pulse strength)
};
// Parses "<prefix>/..." addresses; std::nullopt when the address is outside the scheme or the
// arguments do not fit (e.g. recall without a name).
[[nodiscard]] std::optional<DirectCommand> parseDirectOsc(const OscMessage& message, std::string_view prefix);
// The OSC address for a parameter path under the prefix (for the UI / feedback): "/avgen/param/orb/scale".
[[nodiscard]] std::string parameterAddress(std::string_view prefix, std::string_view path);
// Splits "ip:port" (as OscMessage::sender / OscReceiver::Stats::lastSender) into its parts.
[[nodiscard]] bool splitHostPort(std::string_view text, std::string& host, std::uint16_t& port);

} // namespace avgen::control
