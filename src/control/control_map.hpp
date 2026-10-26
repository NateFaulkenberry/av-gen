#pragma once

// Control map (milestone 1.1, ADR-021): how MIDI and OSC reach the engine. Two paths:
//   1. Bindings turn messages into *control signals* on the bus ("control.<channel>", 0..1, or
//      event pulses) that ordinary modulation routes consume, and/or set a parameter's base
//      value directly (with a min..max range).
//   2. The direct OSC scheme needs no binding: "<prefix>/param/<path> f..." sets a parameter,
//      "<prefix>/signal/<channel> f" sets a control signal, "<prefix>/pulse/<channel> [f]" fires
//      one, "<prefix>/preset/recall s", "<prefix>/preset/morph s s f", "<prefix>/transport/play|
//      pause|stop|toggle", "<prefix>/transport/seek f", "<prefix>/control/reset" (ADR-1612). Prefix defaults
//      to "/avgen".
//   3. Query and feedback (follow-up): "<prefix>/query s<path>" (or "<prefix>/query/<path>")
//      answers "<prefix>/param/<path> f..." to the sender (or the feedback host);
//      "<prefix>/query/all" answers every serialised parameter in bundles; "<prefix>/query/
//      presets" answers "<prefix>/presets s...". With feedback enabled and a host set, parameter
//      base values that change (other than through OSC) are pushed as "<prefix>/param/<path>".
// Saved in the project under "control". Pure functions here; sockets and devices live in the
// osc/midi modules and the app's ControlHub.
//
// ADR-1700: MIDI performance mapping. `control.midi.bindings` are the scene's authored defaults and
// are written back exactly as they were read; a performer's own mappings are `control.midi.mappings`
// (project or scene scope), defaults they switched off are `disabledDefaults`, and a destination's
// combination policy is `combine`. Which of them apply, and in what order, is `resolveMidiMappings`.

#include "control/midi.hpp"
#include "control/osc.hpp"
#include "core/error.hpp"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::control {

// `note` is a gate (level while held; with `toggle`, flips on each note-on), `noteEvent` a trigger (a one-frame pulse).
// ADR-1700 adds polyPressure (note identity kept), nrpn and rpn (ADR-1702).
enum class MidiBindKind : std::uint8_t {
    ControlChange, Note, NoteEvent, PitchBend, ChannelPressure, Program, PolyPressure, Nrpn, Rpn
};
[[nodiscard]] const char* midiBindKindName(MidiBindKind kind);
[[nodiscard]] std::optional<MidiBindKind> midiBindKindFromName(std::string_view name);
[[nodiscard]] bool midiBindKindIsNote(MidiBindKind kind);       // note, noteEvent
[[nodiscard]] bool midiBindKindHasNumber(MidiBindKind kind);    // cc, note kinds, program, polyPressure, nrpn, rpn
[[nodiscard]] bool midiBindKindIsContinuous(MidiBindKind kind); // cc, pitchBend, pressure, polyPressure, nrpn, rpn

// ADR-1700: the response of a continuous mapping, after invert and before scale/offset.
enum class MidiCurve : std::uint8_t { Linear, Exponential, Logarithmic, SCurve };
[[nodiscard]] const char* midiCurveName(MidiCurve curve);
[[nodiscard]] std::optional<MidiCurve> midiCurveFromName(std::string_view name);
[[nodiscard]] float applyMidiCurve(MidiCurve curve, float x); // 0..1 -> 0..1, monotonic, fixed ends

// ADR-1700: how a CC's value is read. Relative encoders are chosen by the person, never inferred: the same bytes mean
// different things on different devices.
enum class MidiEncoder : std::uint8_t { Absolute, TwosComplement, BinaryOffset, SignMagnitude };
[[nodiscard]] const char* midiEncoderName(MidiEncoder encoder);
[[nodiscard]] std::optional<MidiEncoder> midiEncoderFromName(std::string_view name);
// The step a relative encoder's value means (0 for Absolute): two's complement 1..63 up, 127.. down; binary offset
// 64 = none, 65 = +1, 63 = -1; sign-magnitude bit 6 = down, low six bits = size.
[[nodiscard]] int relativeEncoderDelta(MidiEncoder encoder, std::uint8_t value);

// ADR-1700: where a mapping comes from, lowest precedence first. ADR-1705: Rig is the performer's controller setup,
// kept in the app's per-user files (control/midi_rig.hpp), never in a project, and resolved above the scene's
// defaults and below the project's and the scene's own mappings.
enum class MidiScope : std::uint8_t { Default, Rig, Project, Scene };
[[nodiscard]] const char* midiScopeName(MidiScope scope);

// ADR-1700: several mappings driving one destination. Replace: the latest message wins (and what a binding always
// did). Max / Add: the largest / the sum of every contributing mapping's current output, clamped to the destination.
enum class MidiCombine : std::uint8_t { Replace, Max, Add };
[[nodiscard]] const char* midiCombineName(MidiCombine combine);
[[nodiscard]] std::optional<MidiCombine> midiCombineFromName(std::string_view name);

// ADR-1705: the app actions a mapping can fire, named by the keyboard command ids they share (docs/help/features.json).
// Fired on a rising edge: a note-on, a program change, a continuous control crossing half-way upwards.
enum class MidiAction : std::uint8_t {
    None,
    TransportPlay,         // "transport.play": play / pause, as Space
    TransportReturnToStart, // "transport.return-to-start", as Return
    LiveNextScene,         // "live.next-scene", as Page Down
    LivePreviousScene      // "live.previous-scene", as Page Up
};
[[nodiscard]] const char* midiActionName(MidiAction action);  // the command id ("" for None)
[[nodiscard]] const char* midiActionLabel(MidiAction action); // "Next live scene"
[[nodiscard]] std::optional<MidiAction> midiActionFromName(std::string_view name);
inline constexpr std::array<MidiAction, 4> kMidiActions = {MidiAction::TransportPlay, MidiAction::TransportReturnToStart,
                                                           MidiAction::LiveNextScene, MidiAction::LivePreviousScene};

// Where a matched value goes: a control signal (channel name without "control."), a parameter
// base value (path + component, mapped to min..max), or both. ADR-1705: or an app action, alone.
struct BindingTarget {
    std::string signal;          // "" = none
    std::string parameter;       // "" = none
    MidiAction action = MidiAction::None; // ADR-1705: exclusive of the two above (MIDI only)
    int component = -1;          // -1 = every component
    float min = 0.0f;            // parameter range for value 0..1
    float max = 1.0f;
};

// ADR-1606: soft takeover ("pickup"). After something other than the knob moves its target -- a preset recall, a state,
// a project load, the UI -- the knob does not jump the value to wherever it physically sits: it is ignored until it
// reaches the target's current value (within `tolerance`) or passes through it, and follows from then on. The state is
// the binding's, at runtime; `pickupAccepts` is the whole rule, pure for the tests.
struct PickupState {
    bool latched = false;      // following the knob
    float lastSent = -1.0f;    // the value (0..1) this binding last applied, -1 none
    float lastIncoming = -1.0f; // the knob's last position (0..1), -1 none
};
// Whether a knob at `incoming` (0..1) may set a target that now reads `current` (0..1). Updates `state`.
[[nodiscard]] bool pickupAccepts(PickupState& state, float incoming, float current, float tolerance = 0.02f);

struct MidiBinding {
    // ADR-1700: identity and layer. A user mapping's id is minted when it is made; an authored default without one gets
    // "d-<hash of its source and target>" at load (idAuthored false, and it is then not written back).
    std::string id;
    bool idAuthored = false;
    MidiScope scope = MidiScope::Default;
    std::string scene;           // Scene scope: the scene key (Engine::midiSceneKey) it belongs to
    std::string overrides;       // a user mapping that replaces the mapping with this id while it exists
    bool enabled = true;

    // The source.
    std::string source = "*";    // substring of the device name ("*" = any); used when `device` is empty
    std::string device;          // ADR-1700: the stable device id (CoreMIDI unique id) it was learned from
    std::string deviceName;      // that device's name when learned: what is shown, and the fallback match elsewhere
    int channel = -1;            // 0..15, -1 = any
    MidiBindKind kind = MidiBindKind::ControlChange;
    int number = -1;             // controller / note / program / NRPN-RPN number (-1 = any)
    int numberHigh = -1;         // note kinds, polyPressure: the top of a note range number..numberHigh (-1 = one note)

    // Behaviour.
    bool toggle = false;         // Note: note-on flips between min and max instead of gating
    bool velocity = true;        // Note / NoteEvent: the level follows velocity (false: full level)
    bool pickup = false;         // ADR-1606: soft takeover for this binding (continuous kinds)
    bool invert = false;
    MidiCurve curve = MidiCurve::Linear;
    float scale = 1.0f;          // sensitivity: y = clamp(curve(x) * scale + offset, 0, 1); relative: the step size
    float offset = 0.0f;
    float smoothMs = 0.0f;       // one-pole smoothing time constant (0 = none)
    MidiEncoder encoder = MidiEncoder::Absolute; // ControlChange only
    BindingTarget target;

    // A user record that could not be read: kept verbatim (and written back verbatim) and never applied.
    std::string invalidRaw;
    std::string invalidReason;
    [[nodiscard]] bool invalid() const { return !invalidReason.empty(); }
};

// ADR-1700: the destinations a binding writes, as stable keys: "control.<signal>" and "<path>" or "<path>[<c>]".
// ADR-1705: an action is "action.<command id>".
[[nodiscard]] std::vector<std::string> midiDestinationKeys(const BindingTarget& target);
// Whether two bindings can be moved by the same message: same kind, overlapping numbers, channels and devices ("any"
// overlaps everything).
[[nodiscard]] bool midiSourcesOverlap(const MidiBinding& a, const MidiBinding& b);
// Whether a source (device slot named `deviceName` with id `deviceId`, or none) passes the binding's device filter.
[[nodiscard]] bool midiDeviceMatches(const MidiBinding& binding, std::string_view deviceId, std::string_view deviceName);
// A human description of the source: "CC 74", "Note C3", "Notes C1-B1", "Pitch bend", "NRPN 1234".
[[nodiscard]] std::string midiSourceLabel(const MidiBinding& binding);
// Middle C is note 60 = "C4" (the Scientific Pitch convention, as Apple's and Ableton's monitors label it; some
// devices and DAWs call 60 "C3").
[[nodiscard]] std::string midiNoteName(int note);
// A fresh user mapping id ("m-" + 12 hex digits).
[[nodiscard]] std::string newMidiMappingId();

struct OscBinding {
    std::string address;         // exact address or OSC pattern ("/fader/*")
    int argIndex = 0;            // which argument carries the value
    bool event = false;          // fire an event channel instead of setting a value
    float inMin = 0.0f;          // incoming range mapped to 0..1 (for controllers sending 0..127 etc.)
    float inMax = 1.0f;
    BindingTarget target;
};

// ADR-1612: a control channel's rest -- the value `control.<name>` holds until its knob first moves, and returns to on a
// reset (a project load, `<prefix>/control/reset`). A knob that should cut as well as boost rests in its middle (0.5,
// read through a bipolar route). With `pickup` (or a pickup binding, or `control.midi.pickup`) the knob is held until it
// reaches the channel's value, as ADR-1606 holds a parameter's: the rest is something to take over from.
struct ChannelRest {
    std::string name;            // without "control."
    float rest = 0.0f;           // 0..1
    bool pickup = false;
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
    bool midiPickup = false;     // ADR-1606: soft takeover for every continuous binding (CC, pitch bend, pressure)
    std::vector<MidiBinding> midi;      // the scene's authored defaults ("bindings")
    std::vector<MidiBinding> midiUser;  // ADR-1700: the performer's mappings, project and scene scope ("mappings")
    // ADR-1705: the rig, the performer's mappings for every scene. Carried by the map so the resolver, the mapper and the
    // editing helpers see one list, but owned by the app's per-user files: toJson never writes it, fromJson never reads
    // it, and ControlHub::setMap keeps it across a project load.
    std::vector<MidiBinding> midiRig;
    std::vector<std::string> disabledDefaults; // ADR-1700: tombstones: ids of defaults switched off
    std::vector<std::pair<std::string, MidiCombine>> combine; // ADR-1700: destination key -> policy (not Replace)
    std::vector<std::string> warnings; // ADR-1700: what fromJson set aside (unreadable user records, unknown keys)
    std::vector<OscBinding> osc;
    std::vector<ChannelRest> channelRests; // ADR-1612: "channels" in the project's control block; written when set

    [[nodiscard]] nlohmann::json toJson() const;
    // ADR-1612: the declared rest of a channel, or null.
    [[nodiscard]] const ChannelRest* restFor(std::string_view channel) const;
    static Result<ControlMap> fromJson(const nlohmann::json& j);
    [[nodiscard]] bool empty() const { return midi.empty() && midiUser.empty() && midiRig.empty() && osc.empty(); }
    // ADR-1700
    [[nodiscard]] MidiCombine combineFor(std::string_view destinationKey) const;
    void setCombine(const std::string& destinationKey, MidiCombine policy);
    [[nodiscard]] MidiBinding* findMidi(std::string_view id); // a default, a user mapping or a rig mapping
    [[nodiscard]] const MidiBinding* findMidi(std::string_view id) const;
    // Every signal channel named by a binding (deduplicated) with its event flag, so the control
    // source can declare them before messages arrive.
    struct Channel {
        std::string name;
        bool event = false;
        float rest = 0.0f; // ADR-1612
    };
    [[nodiscard]] std::vector<Channel> channels() const;
};

// ADR-1700: one MIDI mapping as JSON. `defaultScope` Default reads an authored binding (no warnings); Project reads a
// project's user record (project or scene scope); ADR-1705: Rig reads a record of the rig file (rig scope only). A
// project record that says "rig" is refused: the rig is never written into a project.
[[nodiscard]] nlohmann::json midiBindingToJson(const MidiBinding& binding);
[[nodiscard]] Result<MidiBinding> midiBindingFromJson(const nlohmann::json& json, MidiScope defaultScope,
                                                      std::vector<std::string>* warnings);

// ADR-1700: the state of every mapping in a map for one scene, and the active ones in the order they apply.
enum class MidiMappingState : std::uint8_t {
    Active,
    Disabled,       // its own enabled flag is off
    DefaultOff,     // a default the performer switched off (a tombstone)
    Overridden,     // a user mapping replaces it (`by`)
    Shadowed,       // a higher scope maps an overlapping source to the same destination (`by`)
    OtherScene,     // a scene mapping for another scene
    Invalid         // an unreadable user record
};
[[nodiscard]] const char* midiMappingStateName(MidiMappingState state);
struct ResolvedMapping {
    const MidiBinding* binding = nullptr;
    MidiMappingState state = MidiMappingState::Active;
    std::string by; // the id that overrides or shadows it
};
// Every binding of `map` (defaults, then rig mappings, then user mappings, each in their order) with its state for
// `sceneKey`. The precedence is ADR-1700's, with ADR-1705's rig: tombstones, then explicit overrides, then
// scene > project > rig > default shadowing on an overlapping source that drives the same destination.
[[nodiscard]] std::vector<ResolvedMapping> resolveMidiMappings(const ControlMap& map, std::string_view sceneKey);

// A matched binding: the normalised value (0..1) and whether it is an event pulse.
struct Match {
    float value = 0.0f;
    bool event = false;
};
// ADR-1700: whether `message` is one the binding's source describes -- kind, channel, number or note range -- and its
// raw value (0..1 at the message's resolution). The device filter is not checked here (it needs the device table:
// midiDeviceMatches), nor behaviour (gates, toggles, encoders: the mapper's state).
[[nodiscard]] std::optional<float> midiSourceValue(const MidiBinding& binding, const MidiMessage& message);
[[nodiscard]] std::optional<Match> matchOsc(const OscBinding& binding, const OscMessage& message);

// Direct OSC commands.
struct DirectCommand {
    enum class Kind : std::uint8_t {
        SetParameter, SetSignal, Pulse, PresetRecall, PresetMorph, Play, Pause, Stop, Toggle, Seek,
        Query, QueryAll, QueryPresets, StateGo, ControlReset
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
