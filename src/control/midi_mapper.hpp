#pragma once

// ADR-1700: the MIDI mapping engine. Takes the active mappings `resolveMidiMappings` picks for the
// scene, indexes them by message kind and number, and turns each message into writes on the
// existing destinations: a control channel (`control.<name>`, which routes then consume -- the
// modulation system stays the authority) or a parameter's base value, exactly the two things a
// binding has always written.
//
// What it adds to a binding's plain "value -> destination":
//   * the response: invert, curve, scale/offset, the min..max range, smoothing, relative encoders;
//   * note behaviour: a gate held while ANY note of its range is held (a fixed set of held notes,
//     released when their device goes away, the mapping leaves, or the scene changes), a toggle,
//     a trigger with or without velocity;
//   * ADR-1606 pickup, as before, for the continuous kinds;
//   * several mappings on one destination combined by the destination's policy (replace, max, add).
//
// Threading: everything here runs on the engine thread. `rebuild` allocates (an edit, a scene
// change, a project load); `apply` and `advance` do not.

#include "control/control_map.hpp"
#include "control/midi.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace avgen::control {

// The destinations, implemented by the engine side (ControlHub) and by tests.
class MidiTargets {
public:
    virtual ~MidiTargets() = default;
    // A parameter's base component (component -1: the first), or nothing when the path is not in this scene.
    [[nodiscard]] virtual std::optional<float> parameter(const std::string& path, int component) = 0;
    virtual void setParameter(const std::string& path, int component, float value) = 0; // -1: every component
    [[nodiscard]] virtual float signal(const std::string& name) = 0;
    // ADR-1612: whether the channel has a declared rest, i.e. a value a pickup knob takes over from.
    [[nodiscard]] virtual bool signalHasRest(const std::string& name) = 0;
    virtual void setSignal(const std::string& name, float value) = 0;
    virtual void pulseSignal(const std::string& name, float strength) = 0;
    // ADR-1705: an app action fired (a rising edge). The app performs it after the frame's messages; tests count it.
    virtual void action(MidiAction /*action*/) {}
};

enum class MidiDeviceState : std::uint8_t {
    Any,       // the mapping takes every device (or a name substring that some connected device matches)
    Connected, // its device is connected
    ByName,    // its device id is not here, a connected device has its name: that one is used
    Absent     // its device is not connected (the mapping is kept, and matches when it returns)
};
[[nodiscard]] const char* midiDeviceStateName(MidiDeviceState state);

// What the panel shows for each mapping, by id. Kept for every mapping of the map, active or not.
struct MidiMappingStatus {
    MidiMappingState state = MidiMappingState::Active;
    std::string by;
    MidiDeviceState device = MidiDeviceState::Any;
    bool destinationMissing = false; // no such parameter in this scene
    // ADR-1705: a rig mapping whose destination this scene lacks: kept, listed, not applied here. Not a warning.
    bool inactiveForScene = false;
    float lastInput = -1.0f;         // the last raw value (0..1) that matched, -1 none yet
    float lastOutput = -1.0f;        // the last value written, in the destination's units
    double lastSeconds = -1.0;       // when (the hub's clock)
    int held = 0;                    // notes holding a gate
    bool waitingForPickup = false;   // ADR-1606: the knob has not reached the value yet
};

class MidiMapper {
public:
    // Rebuilds from `map` for `sceneKey`. With `keepState` (an edit), a mapping that stays keeps its runtime state (a held
    // gate, a pickup latch, a relative position) and a gate whose mapping leaves is released first. Without it (a new
    // project, another scene), every gate is released and every mapping starts fresh.
    void rebuild(const ControlMap& map, std::string_view sceneKey, MidiTargets& targets, bool keepState = true);
    // The input's device table changed: device filters are resolved again (slot -> matches).
    void setDevices(const std::vector<MidiDeviceSlot>& devices);
    // One message (raw or decoded NRPN/RPN). DeviceRemoved releases that device's held notes. Returns how many mappings
    // it moved.
    int apply(const MidiMessage& message, MidiTargets& targets, double nowSeconds);
    // Once a frame, after apply: smoothing.
    void advance(double dtSeconds, MidiTargets& targets);
    // Every gate low (a scene change, MIDI turned off).
    void releaseAll(MidiTargets& targets);
    // A knob's pickup latches let go (something else moved the targets: a project load, a preset).
    void resetPickup();

    [[nodiscard]] const MidiMappingStatus* status(std::string_view id) const;
    [[nodiscard]] std::size_t activeCount() const { return entries_.size(); }
    [[nodiscard]] std::uint64_t pickupHeld() const { return pickupHeld_; }

private:
    struct Held {
        std::uint16_t device = 0;
        std::uint8_t channel = 0;
        std::uint8_t note = 0;
    };
    static constexpr std::size_t kMaxHeld = 16; // beyond it a gate stays high until the set empties
    struct Runtime {
        PickupState pickup;
        bool toggle = false;
        std::array<Held, kMaxHeld> held{};
        std::uint8_t heldCount = 0;
        bool heldOverflow = false;
        float relative = -1.0f; // relative encoder position 0..1, -1 not started
        bool smoothPrimed = false;
        float smoothValue = 0.0f;
        float smoothTarget = 0.0f;
        bool smoothing = false;
        bool high = false;        // ADR-1705: an action's source is above half-way (its rising edge fires)
        bool contributed = false; // has a value for max/add combination
        float contribution = 0.0f; // its current output, 0..1 on its own scale
    };
    struct Entry {
        MidiBinding binding;
        bool effectivePickup = false;
        MidiCombine combine = MidiCombine::Replace;
        std::vector<std::uint16_t> combineGroup; // the entries sharing this destination (max/add), itself included
        std::vector<bool> deviceMatches;          // by device slot
        Runtime rt;
        std::size_t status = 0;                   // index into statuses_
    };

    void indexEntry(std::uint16_t index);
    void resolveDevices(Entry& e);
    [[nodiscard]] bool deviceOk(const Entry& e, std::uint16_t device) const;
    [[nodiscard]] float shape(const Entry& e, float x) const;
    [[nodiscard]] float current01(const Entry& e, MidiTargets& targets) const; // -1 unknown
    void write(Entry& e, float y, MidiTargets& targets, double nowSeconds);
    void pulse(Entry& e, float y, MidiTargets& targets, double nowSeconds);
    void fireAction(Entry& e, bool high, MidiTargets& targets, double nowSeconds);
    void releaseGate(Entry& e, MidiTargets& targets);
    void collect(const MidiMessage& m);

    std::vector<Entry> entries_;
    std::vector<MidiMappingStatus> statuses_;
    std::unordered_map<std::string, std::size_t> statusIndex_;
    std::vector<MidiDeviceSlot> devices_;

    // The index: entries by (kind, number), and by kind for "any number" and the kinds without one.
    std::array<std::vector<std::uint16_t>, 128> byCc_, byNote_, byPoly_, byProgram_;
    std::vector<std::uint16_t> anyCc_, anyNote_, anyPoly_, anyProgram_, pitchBend_, pressure_, anyNrpn_, anyRpn_;
    std::unordered_map<std::uint16_t, std::vector<std::uint16_t>> byNrpn_, byRpn_;
    std::vector<std::uint16_t> scratch_; // candidates of one message (capacity kept; merged in order, never sorted)
    std::uint64_t pickupHeld_ = 0;
};

} // namespace avgen::control
