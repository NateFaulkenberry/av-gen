#pragma once

// Internal seam between the platform-independent MIDI code (parser, inbox, public classes in
// midi_common.cpp) and the per-platform backends (midi_coremidi.cpp, midi_stub.cpp). Not part
// of the public interface; include only from src/control/midi_*.cpp.

#include "control/midi.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace avgen::control::detail {

// Thread-safe queue of parsed messages shared by every transport (device backend, inject).
// Producers may run on any thread (CoreMIDI's receive thread, tests); the consumer drains from
// the UI/engine thread. Beyond the limit the oldest message is evicted and counted as dropped.
class MidiInbox {
public:
    static constexpr std::size_t kDefaultQueueLimit = 4096;

    // Parses `bytes` with the caller-owned per-source parser state and queues the messages.
    void receive(std::span<const std::uint8_t> bytes, MidiParserState& state,
                 std::uint64_t timestampNs, const std::string& source);
    // Queues already parsed messages (moved from `messages`).
    void push(std::vector<MidiMessage>& messages);

    std::size_t drain(std::vector<MidiMessage>& out);
    [[nodiscard]] MidiInput::Stats stats() const;
    void setQueueLimit(std::size_t limit);
    void clear();

private:
    mutable std::mutex mutex_;
    std::deque<MidiMessage> queue_;
    std::size_t limit_ = kDefaultQueueLimit;
    MidiInput::Stats stats_;
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
