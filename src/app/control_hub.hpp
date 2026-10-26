#pragma once

// Live control (milestone 1.1, ADR-021): owns the OSC receiver and the MIDI input, applies the
// project's ControlMap every frame (bindings -> control signals / parameter bases, direct OSC
// commands -> engine actions), and keeps "learn" state for the UI. Runs entirely on the engine
// thread: the receivers only queue. Follow-ups: MIDI clock (Clock/Start/Continue/Stop feed a
// MidiClockTracker the engine can use as its tempo source) and OSC query/feedback (replies to
// "<prefix>/query", pushed "<prefix>/param/<path>" messages when parameters change).
//
// ADR-1700: MIDI performance mapping. The MIDI half of a frame is: drain the bounded inbox (ADR-1701),
// then per message, in arrival order: transport to the clock; device removals release held notes;
// NRPN/RPN decoding (ADR-1702); activity, the monitor and Learn; live Sonic input; then the mapper,
// which writes control channels and parameter bases. All of it before the sources publish
// `control.*` and before the routes run, so a message drained this frame is in this frame's
// parameters. The UI edits `map()` on the same thread and calls `mappingsChanged()`.

#include "control/control_map.hpp"
#include "control/midi.hpp"
#include "control/midi_clock.hpp"
#include "control/midi_learn.hpp"
#include "control/midi_mapper.hpp"
#include "control/midi_nrpn.hpp"
#include "control/osc.hpp"
#include "core/error.hpp"
#include "core/time.hpp"
#include "params/parameter.hpp"

#include <array>
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
    // (a port in use leaves OSC closed; see status()). ADR-1705: the hub's rig is kept -- whatever `map.midiRig`
    // holds is replaced by it -- so a project load, a live scene switch or a new document never drops the rig.
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
    // ADR-1063: the newest program change received while live Sonic input runs (the scene switcher's), taken once.
    [[nodiscard]] std::optional<int> takeProgramChange() {
        auto p = programChange_;
        programChange_.reset();
        return p;
    }
    [[nodiscard]] const std::optional<control::OscMessage>& lastOsc() const { return lastOsc_; }
    // Creates a binding from the last OSC message to `signal` (control channel) or a parameter path. Returns false when
    // nothing has been received yet. (MIDI is learned through learn(), ADR-1700.)
    bool bindLastOsc(const std::string& signal, const std::string& parameter = {}, bool asEvent = false);

    // ---- ADR-1700: MIDI performance mapping ----
    // The scene the scene-scope mappings are for (Engine::midiSceneKey). A change releases every gate and rebuilds.
    void setSceneKey(const std::string& key);
    [[nodiscard]] const std::string& sceneKey() const { return sceneKey_; }
    // The UI changed map()'s MIDI mappings: rebuilt before the next message, keeping held gates and pickup latches.
    void mappingsChanged() { rebuildPending_ = true; }

    // ---- ADR-1705: the rig ----
    // Replaces the rig (the app's file, at start-up): applied on the next update, fresh (gates released).
    void setRig(std::vector<control::MidiBinding> rig);
    [[nodiscard]] const std::vector<control::MidiBinding>& rig() const { return map_.midiRig; }
    // Bumped whenever the rig's content changes (setRig, or an edit seen on rebuild): the app saves the rig file when it
    // differs from the revision it last saved. Never bumped by a project load.
    [[nodiscard]] std::uint64_t rigRevision() const { return rigRevision_; }
    // ADR-1705: the live scene steps mapped actions asked for since the last call (+1 next, -1 previous; summed), taken
    // once. The app performs them (Application::serviceLiveScenes), as it does a program change.
    [[nodiscard]] int takeLiveSceneSteps() {
        const int steps = liveSceneSteps_;
        liveSceneSteps_ = 0;
        return steps;
    }
    // Actions fired through mappings since construction (tests and the panel).
    [[nodiscard]] std::uint64_t actionsFired() const { return actionsFired_; }
    [[nodiscard]] control::MidiMapper& mapper() { return mapper_; }
    [[nodiscard]] const control::MidiMapper& mapper() const { return mapper_; }
    [[nodiscard]] control::MidiLearn& learn() { return learn_; }
    [[nodiscard]] control::MidiMonitor& monitor() { return monitor_; }
    [[nodiscard]] const control::MidiMonitor& monitor() const { return monitor_; }
    // The input's device table as of this frame (index = device slot), and what each has sent.
    [[nodiscard]] const std::vector<control::MidiDeviceSlot>& midiDevices() const { return devices_; }
    struct DeviceActivity {
        double lastSeconds = -1.0;   // the hub clock when it last sent a channel or system message
        std::uint64_t messages = 0;
        std::uint64_t clock = 0;     // clock and transport messages (not shown in the monitor by default)
        std::array<double, 16> channelSeconds{};
        DeviceActivity() { channelSeconds.fill(-1.0); }
    };
    [[nodiscard]] const std::vector<DeviceActivity>& deviceActivity() const { return activity_; }
    // Seconds on the hub's own clock (steady, from construction): what the activity times are measured on.
    [[nodiscard]] double clockSeconds() const;
    // Devices switched off on this machine (by stable id): their messages are shown, counted and otherwise ignored.
    void setDisabledDevices(std::vector<std::string> ids);
    [[nodiscard]] const std::vector<std::string>& disabledDevices() const { return disabledDevices_; }
    [[nodiscard]] bool deviceEnabled(std::uint16_t slot) const;
    // A test seam: the input's device removal for an injected source (as unplugging it).
    void injectMidiRemoval(const std::string& source);

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
        std::uint64_t pickupHeld = 0; // ADR-1606: knob moves held back by soft takeover
        std::uint64_t unmatched = 0; // messages no binding or direct command consumed
        std::uint64_t ignored = 0;   // ADR-1700: messages from a device switched off
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
    void applyMidiWith(Engine& engine, const control::MidiMessage& message, control::MidiTargets& targets, double now);
    void syncDevices();
    void rebuildMappings(control::MidiTargets& targets);
    void noteRigChange();
    friend class HubTargets;
    void queueAction(control::MidiAction action);
    void performActions(Engine& engine);
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
    // ADR-1700
    control::MidiMapper mapper_;
    control::NrpnDecoder nrpn_;
    control::MidiLearn learn_;
    control::MidiMonitor monitor_;
    std::vector<control::MidiDeviceSlot> devices_;
    std::uint64_t deviceGeneration_ = ~0ull;
    std::vector<DeviceActivity> activity_;
    std::vector<std::string> disabledDevices_;
    std::vector<bool> deviceDisabled_; // by slot
    std::string sceneKey_;
    bool rebuildPending_ = true;
    bool freshRebuild_ = true;  // a new map or scene: gates released, runtime state forgotten
    std::uint64_t ignored_ = 0;
    std::uint64_t startNs_ = 0;
    std::optional<int> programChange_;
    // ADR-1705
    std::string rigFingerprint_;
    std::uint64_t rigRevision_ = 0;
    int liveSceneSteps_ = 0;
    int pendingPlayToggles_ = 0;
    bool pendingReturnToStart_ = false;
    std::uint64_t actionsFired_ = 0;
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
