#pragma once

// ADR-1700: MIDI Learn and the live monitor, as plain logic the panel draws and the tests drive.
//
// Learn. Armed, it takes the FIRST qualifying message that passes its device/channel filter as the
// source, and from then on only follows that source's value: a knob turned while learning sends
// a stream of CCs, and the stream describes one control, not one mapping per message. Never
// captured: clock and transport, note-offs, bank select (CC 0/32, which precede a program change),
// channel-mode messages (CC 120-127), and the CCs of an NRPN/RPN sequence (kMidiNrpnPart) -- the
// decoded NRPN that follows them is the source. Nothing is assigned until the person confirms.
//
// Monitor. A fixed ring of the most recent messages, written by the hub whatever the panel is
// doing (it costs a copy), so it never blocks or allocates. With coalescing on, a continuous
// message from the same control as one of the last eight rows (within a second) updates that row and moves it to the
// newest place, instead of adding one: a knob and a bend wheel moved together stay two rows.

#include "control/control_map.hpp"
#include "control/midi.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::control {

class MidiLearn {
public:
    enum class State : std::uint8_t { Idle, Listening, Captured };

    void arm();
    void cancel();
    // Restrict what may be captured: a device slot (-1 any) and a channel (-1 any).
    void setFilter(int deviceSlot, int channel);
    [[nodiscard]] int filterDevice() const { return filterDevice_; }
    [[nodiscard]] int filterChannel() const { return filterChannel_; }

    // Feeds one message (raw and decoded alike, in order). Returns true when this message was captured.
    bool feed(const MidiMessage& message);
    // Captures a message directly (the monitor's "learn from this row").
    void captureFrom(const MidiMessage& message);

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] const MidiMessage& captured() const { return captured_; }
    [[nodiscard]] float liveValue() const { return live_; } // the captured source's latest value, 0..1
    [[nodiscard]] std::uint64_t updates() const { return updates_; } // messages from it since the capture

    // Whether this message could be captured at all (the rules above, without the filter).
    [[nodiscard]] static bool learnable(const MidiMessage& message);

private:
    [[nodiscard]] bool sameSource(const MidiMessage& m) const;

    State state_ = State::Idle;
    MidiMessage captured_{};
    float live_ = 0.0f;
    std::uint64_t updates_ = 0;
    int filterDevice_ = -1;
    int filterChannel_ = -1;
};

// The source part of a mapping a captured message describes: kind, number, channel and the device it came from (its
// stable id and name; `anyDevice` / `anyChannel` widen it on purpose). A note becomes a gate (`note`), or a trigger
// (`noteEvent`) when `noteAsTrigger`.
[[nodiscard]] MidiBinding midiBindingFromMessage(const MidiMessage& message, const std::vector<MidiDeviceSlot>& devices,
                                                 bool anyDevice, bool anyChannel, bool noteAsTrigger);

// The mappings that apply in `sceneKey` whose source overlaps `candidate` (the collision Learn has to show).
[[nodiscard]] std::vector<const MidiBinding*> midiCollisions(const ControlMap& map, std::string_view sceneKey,
                                                             const MidiBinding& candidate);

// Commits a learned mapping (candidate with target, scope and scene set) and returns its id. Replace switches the
// colliding mappings off first -- a default by a tombstone, a user mapping by its enabled flag -- never deletes them.
enum class MidiLearnCommit : std::uint8_t { Add, Replace };
std::string commitMidiLearn(ControlMap& map, std::string_view sceneKey, MidiBinding candidate, MidiLearnCommit how);

// Edits an existing mapping without mutating a default: a user mapping is returned as is; a default gets a scene-scope
// copy that overrides it (reused if one already exists). Returns the mapping to edit, or null for an unknown id.
MidiBinding* editableMidiMapping(ControlMap& map, std::string_view sceneKey, std::string_view id);
// A copy of a mapping as a new user mapping of the same scope (a default duplicates into the scene), switched on.
std::string duplicateMidiMapping(ControlMap& map, std::string_view sceneKey, std::string_view id);
// Removes a user mapping; a default cannot be removed, only switched off (returns false).
bool removeMidiMapping(ControlMap& map, std::string_view id);
// Switches a mapping on or off: a default through the tombstone list, a user mapping through its flag.
void setMidiMappingEnabled(ControlMap& map, std::string_view id, bool enabled);
[[nodiscard]] bool midiMappingEnabled(const ControlMap& map, std::string_view id);
// Restores a default: removes its tombstone and every user mapping that overrides it.
void restoreMidiDefault(ControlMap& map, std::string_view id);
// Back to the shipped configuration for this scene: removes this scene's user mappings and every tombstone.
void resetSceneMidiMappings(ControlMap& map, std::string_view sceneKey);
// Moves a user mapping into the authored defaults (the scene author's "make this a default").
bool promoteMidiMappingToDefault(ControlMap& map, std::string_view id);

class MidiMonitor {
public:
    static constexpr std::size_t kCapacity = 512;
    struct Row {
        double seconds = 0.0;
        MidiMessage message{};
        std::uint32_t repeats = 1; // messages folded into this row by coalescing
        std::uint16_t matched = 0; // mappings it moved
        bool ignored = false;      // its device is switched off
    };
    void push(const MidiMessage& message, double seconds, std::uint16_t matched, bool ignored);
    void clear();
    void setCoalesce(bool on) { coalesce_ = on; }
    [[nodiscard]] bool coalesce() const { return coalesce_; }
    void setShowClock(bool on) { showClock_ = on; }
    [[nodiscard]] bool showClock() const { return showClock_; }
    [[nodiscard]] std::size_t size() const { return size_; }
    // i = 0 is the oldest kept row.
    [[nodiscard]] const Row& row(std::size_t i) const { return rows_[(head_ + i) % kCapacity]; }
    [[nodiscard]] std::uint64_t total() const { return total_; } // rows ever pushed (coalesced ones included)

private:
    std::array<Row, kCapacity> rows_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::uint64_t total_ = 0;
    bool coalesce_ = true;
    bool showClock_ = false;
};

// One monitor line: "CC 74  92/127", "Note On C4 vel 108", "Pitch bend centre +18%", "NRPN 1234 = 8192 (14-bit)".
[[nodiscard]] std::string midiMessageDescription(const MidiMessage& message);

} // namespace avgen::control
