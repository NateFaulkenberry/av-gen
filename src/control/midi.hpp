#pragma once

// MIDI input (milestone 1.1, ADR-021; ADR-1701): device enumeration, an input that delivers parsed
// messages from every (or one) source through a bounded, allocation-free inbox, and a byte-level
// parser usable without any device (tests, other transports). macOS backend: CoreMIDI (system
// framework, no third-party dependency); other platforms: a stub that reports no devices.
//
// ADR-1701: a message is plain data. The device it came from is a 16-bit slot into the input's
// device table (stable id + name), not a string, so a message never allocates and two devices
// with the same name stay two devices.

#include "core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace avgen::control {

enum class MidiKind : std::uint8_t {
    NoteOff,
    NoteOn,          // velocity 0 is delivered as NoteOff
    PolyPressure,    // data1 = note, data2 = pressure
    ControlChange,
    ProgramChange,
    ChannelPressure, // data2 = pressure (there is no number)
    PitchBend,       // value14 = 14-bit 0..16383 (8192 = centre)
    Clock,           // system real-time: 24 per quarter note
    Start,
    Continue,
    Stop,
    Nrpn,            // ADR-1702: decoded from CC 99/98 + 6/38/96/97 (NrpnDecoder), never by the parser
    Rpn,             // ADR-1702: decoded from CC 101/100 + 6/38/96/97
    DeviceRemoved,   // ADR-1701: a source went away (device = its slot, or kAllMidiDevices); queued in order
    Other            // anything else (sysex is dropped)
};
[[nodiscard]] const char* midiKindName(MidiKind kind);

inline constexpr std::uint16_t kNoMidiDevice = 0xFFFF;   // a message built by hand (tests)
inline constexpr std::uint16_t kAllMidiDevices = 0xFFFE; // DeviceRemoved: every device (notes were dropped)

// MidiMessage::flags
inline constexpr std::uint8_t kMidiHiRes = 1u << 0;    // Nrpn/Rpn: value14 carries 14 bits (an LSB arrived)
inline constexpr std::uint8_t kMidiNrpnPart = 1u << 1; // a CC that belongs to an NRPN/RPN sequence (ADR-1702)

struct MidiMessage {
    MidiKind kind = MidiKind::Other;
    std::uint8_t channel = 0;    // 0..15 (channel voice messages)
    std::uint8_t data1 = 0;      // note / controller number / program
    std::uint8_t data2 = 0;      // velocity / value / pressure
    std::uint16_t value14 = 0;   // pitch bend; NRPN/RPN value
    std::uint16_t param = 0;     // NRPN/RPN parameter number (0..16383)
    std::uint16_t device = kNoMidiDevice; // slot in the input's device table
    std::uint8_t flags = 0;
    std::uint64_t timestampNs = 0; // host time when received (0 when unknown)

    // 0..1 at the message's own resolution: data2 / 127 (CC, notes, pressure); pitch bend with its centre at exactly
    // 0.5 (8192); NRPN/RPN value14 / 16383 when it carries 14 bits, else its 7 bits / 127 (ADR-1702).
    [[nodiscard]] float normalized() const;
    [[nodiscard]] bool continuous() const; // CC, pitch bend, channel/poly pressure, NRPN/RPN
};

// Parses raw MIDI bytes (running status supported, real-time bytes may interleave). Partial
// trailing messages are kept in `state` for the next call. Appends to `out` (which keeps its
// capacity, so a reused vector does not allocate).
struct MidiParserState {
    std::uint8_t runningStatus = 0;
    std::uint8_t pending[2] = {0, 0};
    int pendingCount = 0;
    bool inSysex = false;
};
std::size_t parseMidiBytes(std::span<const std::uint8_t> bytes, MidiParserState& state,
                           std::vector<MidiMessage>& out, std::uint64_t timestampNs = 0,
                           std::uint16_t device = kNoMidiDevice);

struct MidiDeviceInfo {
    std::string name;            // display name (manufacturer + model / endpoint name)
    std::string id;              // stable identifier when the backend has one, else the name
    bool virtualSource = false;  // created by software (e.g. our own test source)
};
[[nodiscard]] std::vector<MidiDeviceInfo> listMidiInputs();

// ADR-1701: one row of an input's device table. A slot is never reused: the same id reconnecting
// gets its old slot back, so whatever refers to the device (a mapping, the activity lights) finds
// it again. Two sources with the same id connected at once (a backend without unique ids and two
// identical devices) get "id#2", "id#3".
struct MidiDeviceSlot {
    std::string id;
    std::string name;
    bool connected = false;
    bool virtualSource = false;
};

class MidiInput {
public:
    MidiInput();
    ~MidiInput();
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;

    // Opens every current input source ("" or "*"), or the sources whose name contains `filter`
    // (case-insensitive). Sources that appear later are connected automatically when `filter`
    // is "*" or "". Errors: backend unavailable, no source matched a non-empty filter.
    [[nodiscard]] Result<void> open(const std::string& filter = "*");
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] std::vector<std::string> connectedSources() const;

    // Moves queued messages into `out` (arrival order, then the coalesced overflow; see ADR-1701).
    // Returns the count. `out` keeps its capacity: a caller that reuses it does not allocate.
    std::size_t drain(std::vector<MidiMessage>& out);
    struct Stats {
        std::uint64_t messages = 0;  // received
        std::uint64_t coalesced = 0; // continuous messages folded into the overflow table (queue 3/4 full)
        std::uint64_t dropped = 0;   // lost: queue full (notes, transport) or overflow table full
        std::uint64_t droppedNotes = 0; // of which notes: every gate is released when this moves
    };
    [[nodiscard]] Stats stats() const;
    void setQueueLimit(std::size_t limit); // default 4096; allocates (call from setup, not per event)

    // The device table (a copy) and a counter that moves whenever it changes.
    [[nodiscard]] std::vector<MidiDeviceSlot> devices() const;
    [[nodiscard]] std::uint64_t deviceGeneration() const;

    // Injects bytes as if received from a device called `source` (tests, other transports).
    // Thread-safe. The device's id is "inject:<source>".
    void inject(std::span<const std::uint8_t> bytes, const std::string& source = "inject");
    // Marks an injected device gone, as unplugging a real one does (queues DeviceRemoved).
    void injectRemoval(const std::string& source);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A software MIDI source other applications (and our own MidiInput) can receive from. macOS:
// CoreMIDI virtual source; elsewhere: unavailable (open() errors). Used by the tests to send
// real messages through the OS.
class MidiVirtualSource {
public:
    MidiVirtualSource();
    ~MidiVirtualSource();
    MidiVirtualSource(const MidiVirtualSource&) = delete;
    MidiVirtualSource& operator=(const MidiVirtualSource&) = delete;
    [[nodiscard]] Result<void> open(const std::string& name);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] Result<void> send(std::span<const std::uint8_t> bytes);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool hasMidiBackend(); // false on the stub

} // namespace avgen::control
