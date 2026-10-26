#pragma once

// Internal seam between the platform-independent MIDI code (parser, inbox, public classes in
// midi_common.cpp) and the per-platform backends (midi_coremidi.cpp, midi_stub.cpp). Not part
// of the public interface; include only from src/control/midi_*.cpp.

#include "control/midi.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace avgen::control::detail {

// ADR-1701: the bounded queue every transport (device backend, inject) feeds, and the device table.
// Producers may run on any thread (CoreMIDI's receive thread, tests); the consumer drains from the
// engine thread. The ring is allocated once (setQueueLimit) and a push never allocates: a message is
// plain data and the critical section is a copy.
//
// Full policy. Continuous messages (CC, pitch bend, pressure) stop entering the ring at 3/4 of its
// capacity and go to a small table keyed by device, channel, kind and number instead, where the
// latest value replaces the previous one: a stalled consumer then still receives every control's
// final position, not a backlog of obsolete ones. Everything else (notes, program, transport, device
// removals) may use the whole ring. A note that finds the ring full is dropped and counted, and the
// next drain ends with a DeviceRemoved for kAllMidiDevices so the consumer releases every gate rather
// than leave one held by a lost note-off.
class MidiInbox {
public:
    static constexpr std::size_t kDefaultQueueLimit = 4096;
    static constexpr std::size_t kOverflowSlots = 64;

    MidiInbox();
    void push(std::span<const MidiMessage> messages);
    std::size_t drain(std::vector<MidiMessage>& out);
    [[nodiscard]] MidiInput::Stats stats() const;
    void setQueueLimit(std::size_t limit);
    void clear();

    // The device table. `connectDevice` returns the slot for `id` (an earlier slot of a device that
    // reconnects, else a new one) and marks it connected; `disconnectDevice` marks it gone and queues
    // a DeviceRemoved for it, behind its last messages.
    std::uint16_t connectDevice(const std::string& id, const std::string& name, bool virtualSource);
    void disconnectDevice(std::uint16_t slot);
    [[nodiscard]] std::vector<MidiDeviceSlot> devices() const;
    [[nodiscard]] std::uint64_t deviceGeneration() const;

private:
    void pushLocked(const MidiMessage& m);
    struct Overflow {
        bool used = false;
        MidiMessage message;
    };

    mutable std::mutex mutex_;
    std::vector<MidiMessage> ring_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::array<Overflow, kOverflowSlots> overflow_{};
    std::size_t overflowUsed_ = 0;
    bool notesLost_ = false;
    MidiInput::Stats stats_;
    mutable std::mutex devicesMutex_;
    std::vector<MidiDeviceSlot> devices_;
    std::uint64_t deviceGeneration_ = 0;
};

// Device side of MidiInput. Implementations deliver bytes to the inbox they were created with.
class MidiInputBackend {
public:
    virtual ~MidiInputBackend() = default;
    [[nodiscard]] virtual Result<void> open(const std::string& filter) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    [[nodiscard]] virtual std::vector<std::string> connectedSources() const = 0;
};

class MidiVirtualSourceBackend {
public:
    virtual ~MidiVirtualSourceBackend() = default;
    [[nodiscard]] virtual Result<void> open(const std::string& name) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    [[nodiscard]] virtual Result<void> send(std::span<const std::uint8_t> bytes) = 0;
};

// Implemented by exactly one backend translation unit per platform.
std::unique_ptr<MidiInputBackend> makeMidiInputBackend(MidiInbox& inbox);
std::unique_ptr<MidiVirtualSourceBackend> makeMidiVirtualSourceBackend();

// Case-insensitive "name contains filter"; "" and "*" match everything.
[[nodiscard]] bool midiFilterMatches(const std::string& filter, const std::string& name);
[[nodiscard]] bool midiFilterIsWildcard(const std::string& filter);

} // namespace avgen::control::detail
