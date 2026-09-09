#pragma once

// MIDI input (milestone 1.1, ADR-021): device enumeration, an input that delivers parsed
// messages from every (or one) source through a thread-safe inbox, and a byte-level parser
// usable without any device (tests, other transports). macOS backend: CoreMIDI (system
// framework, no third-party dependency); other platforms: a stub that reports no devices.

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
    PolyPressure,
    ControlChange,
    ProgramChange,
    ChannelPressure,
    PitchBend,       // value = 14-bit 0..16383 (8192 = centre)
    Clock,           // system real-time: 24 per quarter note
    Start,
    Continue,
    Stop,
    Other            // anything else (sysex is dropped)
};
[[nodiscard]] const char* midiKindName(MidiKind kind);

struct MidiMessage {
    MidiKind kind = MidiKind::Other;
    std::uint8_t channel = 0;    // 0..15 (channel voice messages)
    std::uint8_t data1 = 0;      // note / controller number / program
    std::uint8_t data2 = 0;      // velocity / value / pressure
    std::uint16_t value14 = 0;   // pitch bend
    std::uint64_t timestampNs = 0; // host time when received (0 when unknown)
    std::string source;          // device/source name it arrived from
    [[nodiscard]] float normalized() const; // data2 / 127 (pitch bend: value14 / 16383)
};

// Parses raw MIDI bytes (running status supported, real-time bytes may interleave). Partial
// trailing messages are kept in `state` for the next call.
struct MidiParserState {
    std::uint8_t runningStatus = 0;
    std::uint8_t pending[2] = {0, 0};
    int pendingCount = 0;
    bool inSysex = false;
};
std::size_t parseMidiBytes(std::span<const std::uint8_t> bytes, MidiParserState& state,
                           std::vector<MidiMessage>& out, std::uint64_t timestampNs = 0,
                           const std::string& source = {});

struct MidiDeviceInfo {
    std::string name;            // display name (manufacturer + model / endpoint name)
    std::string id;              // stable identifier when the backend has one, else the name
    bool virtualSource = false;  // created by software (e.g. our own test source)
};
[[nodiscard]] std::vector<MidiDeviceInfo> listMidiInputs();

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

    // Moves queued messages into `out` (arrival order). Returns the count.
    std::size_t drain(std::vector<MidiMessage>& out);
    struct Stats {
        std::uint64_t messages = 0;
        std::uint64_t dropped = 0;   // beyond the queue limit
        std::string lastSource;
    };
    [[nodiscard]] Stats stats() const;
    void setQueueLimit(std::size_t limit); // default 4096

    // Injects bytes as if received from `source` (tests, other transports). Thread-safe.
    void inject(std::span<const std::uint8_t> bytes, const std::string& source = "inject");

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
