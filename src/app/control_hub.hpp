#pragma once

// Live control (milestone 1.1, ADR-021): owns the OSC receiver and the MIDI input, applies the
// project's ControlMap every frame (bindings -> control signals / parameter bases, direct OSC
// commands -> engine actions), and keeps "learn" state for the UI. Runs entirely on the engine
// thread: the receivers only queue. Follow-ups: MIDI clock (Clock/Start/Continue/Stop feed a
// MidiClockTracker the engine can use as its tempo source) and OSC query/feedback (replies to
// "<prefix>/query", pushed "<prefix>/param/<path>" messages when parameters change).

#include "control/control_map.hpp"
#include "control/midi.hpp"
#include "control/midi_clock.hpp"
#include "control/osc.hpp"
#include "core/error.hpp"
#include "core/time.hpp"
#include "params/parameter.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    // Opens/closes receivers (and the feedback sender) to match the current map (called by
    // setMap; call after editing the map's port/filter/feedback fields from the UI).
    void applyIo();
    void closeAll();
    // Whether this hub may open live sources at all: the OSC receiver, the feedback sender and MIDI.
    // False for an engine that only evaluates a project -- a render, a trace, the Director's scratch
    // copy -- which must neither take the editor's port nor be driven by what arrives on it. The
    // map is kept (it is saved with the project); only the sockets stay shut.
    void setLiveIo(bool enabled);
    [[nodiscard]] bool liveIo() const { return liveIo_; }

    // Per-frame: drains both inboxes and applies them to the engine, advances the MIDI clock to
    // the frame time and pushes parameter feedback. Returns the number of messages applied.
    std::size_t update(Engine& engine, const FrameTime& time);

    // Inject paths for tests and other transports.
    void injectMidi(std::span<const std::uint8_t> bytes, const std::string& source = "inject");
    void injectOsc(control::OscMessage message);

    // MIDI clock tracker (fed with Clock/Start/Continue/Stop; see Engine::setTempoSource).
    [[nodiscard]] control::MidiClockTracker& midiClock() { return clock_; }
    [[nodiscard]] const control::MidiClockTracker& midiClock() const { return clock_; }

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
        bool feedbackOpen = false;   // a feedback sender is open on map().feedbackHost
        std::string feedbackError;
        std::uint64_t feedbackSent = 0;  // parameter/preset messages sent (feedback + query replies)
        std::uint64_t clockMessages = 0; // MIDI real-time messages fed to the clock tracker
    };
    [[nodiscard]] Status status() const;

    // The engine-side sink for control signals and direct commands; separated so tests can drive
    // the hub without sockets.
    void applyMidi(Engine& engine, const control::MidiMessage& message);
    void applyOsc(Engine& engine, const control::OscMessage& message);

private:
    void applyTarget(Engine& engine, const control::BindingTarget& target, const control::Match& match);
    // Query replies and feedback.
    [[nodiscard]] control::OscSender* replySender(const control::OscMessage& request);
    [[nodiscard]] control::OscMessage parameterMessage(const params::IParameter& parameter) const;
    void sendMessages(control::OscSender& sender, std::vector<control::OscMessage>& messages);
    void replyParameter(Engine& engine, const control::OscMessage& request, const std::string& path);
    void replyAll(Engine& engine, const control::OscMessage& request);
    void replyPresets(Engine& engine, const control::OscMessage& request);
    void pushFeedback(Engine& engine);

    control::ControlMap map_;
    control::OscReceiver osc_;
    control::MidiInput midi_;
    control::MidiClockTracker clock_;
    control::OscSender feedback_;      // map().feedbackHost:feedbackPort
    std::string feedbackTarget_;       // "host:port" the feedback sender is open on
    std::string feedbackError_;
    control::OscSender reply_;         // per-sender replies (opened on demand)
    std::string replyTarget_;
    std::string oscError_;
    bool liveIo_ = true;
    std::string midiError_;
    std::vector<control::OscMessage> oscScratch_;
    std::vector<control::MidiMessage> midiScratch_;
    std::vector<control::OscMessage> injectedOsc_;
    std::optional<control::MidiMessage> lastMidi_;
    std::optional<control::OscMessage> lastOsc_;
    std::unordered_map<std::string, std::vector<float>> feedbackCache_; // path -> last seen base components
    std::unordered_set<std::string> oscWritten_; // paths written through OSC this frame (no echo)
    std::uint64_t applied_ = 0;
    std::uint64_t unmatched_ = 0;
    std::uint64_t feedbackSent_ = 0;
    std::uint64_t clockMessages_ = 0;
    std::uint64_t frameNs_ = 0;
    bool ioApplied_ = false;
};

} // namespace avgen::app
