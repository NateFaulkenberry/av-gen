#pragma once

// Live control (milestone 1.1, ADR-021): owns the OSC receiver and the MIDI input, applies the
// project's ControlMap every frame (bindings -> control signals / parameter bases, direct OSC
// commands -> engine actions), and keeps "learn" state for the UI. Runs entirely on the engine
// thread: the receivers only queue.

#include "control/control_map.hpp"
#include "control/midi.hpp"
#include "control/osc.hpp"
#include "core/error.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace avgen::app {

class Engine;

class ControlHub {
public:
    ControlHub();
    ~ControlHub();
    ControlHub(const ControlHub&) = delete;
    ControlHub& operator=(const ControlHub&) = delete;

    [[nodiscard]] control::ControlMap& map() { return map_; }
    [[nodiscard]] const control::ControlMap& map() const { return map_; }
    // Replaces the map and (re)opens OSC/MIDI to match it. Errors are reported, not fatal
    // (a port in use leaves OSC closed; see status()).
    void setMap(control::ControlMap map);
    // Opens/closes receivers to match the current map (called by setMap; call after editing the
    // map's port/filter fields from the UI).
    void applyIo();
    void closeAll();

    // Per-frame: drains both inboxes and applies them to the engine. Returns the number of
    // messages applied.
    std::size_t update(Engine& engine);

    // Inject paths for tests and other transports.
    void injectMidi(std::span<const std::uint8_t> bytes, const std::string& source = "inject");
    void injectOsc(control::OscMessage message);

    // Learn: the most recent messages seen (for the UI's "map last input to ...").
    [[nodiscard]] const std::optional<control::MidiMessage>& lastMidi() const { return lastMidi_; }
    [[nodiscard]] const std::optional<control::OscMessage>& lastOsc() const { return lastOsc_; }
    // Creates a binding from the last MIDI / OSC message to `signal` (control channel) or a
    // parameter path. Returns false when nothing has been received yet.
    bool bindLastMidi(const std::string& signal, const std::string& parameter = {}, bool asEvent = false);
    bool bindLastOsc(const std::string& signal, const std::string& parameter = {}, bool asEvent = false);

    struct Status {
        bool oscOpen = false;
        std::uint16_t oscPort = 0;
        std::string oscError;
        control::OscReceiver::Stats osc;
        bool midiOpen = false;
        std::string midiError;
        std::vector<std::string> midiSources;
        control::MidiInput::Stats midi;
        std::uint64_t applied = 0;   // messages applied since start
        std::uint64_t unmatched = 0; // messages no binding or direct command consumed
    };
    [[nodiscard]] Status status() const;

    // The engine-side sink for control signals and direct commands; separated so tests can drive
    // the hub without sockets.
    void applyMidi(Engine& engine, const control::MidiMessage& message);
    void applyOsc(Engine& engine, const control::OscMessage& message);

private:
    void applyTarget(Engine& engine, const control::BindingTarget& target, const control::Match& match);

    control::ControlMap map_;
    control::OscReceiver osc_;
    control::MidiInput midi_;
    std::string oscError_;
    std::string midiError_;
    std::vector<control::OscMessage> oscScratch_;
    std::vector<control::MidiMessage> midiScratch_;
    std::vector<control::OscMessage> injectedOsc_;
    std::optional<control::MidiMessage> lastMidi_;
    std::optional<control::OscMessage> lastOsc_;
    std::uint64_t applied_ = 0;
    std::uint64_t unmatched_ = 0;
    bool ioApplied_ = false;
};

} // namespace avgen::app
