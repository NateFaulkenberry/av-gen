#pragma once

// Open Sound Control 1.0 (milestone 1.1, ADR-021): messages, bundles, address patterns, a UDP
// receiver thread with a lock-free-ish inbox, and a sender. No third-party dependency (POSIX
// sockets). Research: docs/research/audiovisual-systems.md §18.
//
// Wire format: OSC 1.0 (4-byte aligned strings/blobs, big-endian numbers, type tags ",ifsb" plus
// "h t d c r m T F N I" and arrays "[ ]" are tolerated: unknown tags fail the message, not the
// packet). Bundles ("#bundle" + 64-bit NTP time tag) are flattened: every contained message
// carries the bundle's time tag; nested bundles are allowed.

#include "core/error.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace avgen::control {

struct OscBlob {
    std::vector<std::uint8_t> bytes;
};
// Argument order matches the type tag string. True/False/Nil/Impulse become bool / std::monostate.
using OscArg = std::variant<std::monostate, bool, std::int32_t, std::int64_t, float, double, std::string, OscBlob>;

struct OscMessage {
    std::string address;            // "/scene/orb/scale"
    std::vector<OscArg> args;
    std::uint64_t timeTag = 1;      // NTP 64-bit; 1 = immediately (also for bare messages)
    std::string typeTags;           // as received, without the leading ','
    std::string sender;             // "ip:port" of the packet's source (filled by the receiver; "" otherwise)

    // Conveniences: numeric arguments read as float (bool -> 0/1, int -> float); missing -> fallback.
    [[nodiscard]] float number(std::size_t index, float fallback = 0.0f) const;
    [[nodiscard]] bool hasNumber(std::size_t index) const;
    [[nodiscard]] std::string_view text(std::size_t index, std::string_view fallback = "") const;
    [[nodiscard]] std::size_t numberCount() const; // count of leading numeric args
};

// ---- encoding / decoding ----
// Encodes one message (no bundle). Strings/blobs are padded to 4 bytes.
[[nodiscard]] std::vector<std::uint8_t> encodeMessage(const OscMessage& message);
// Encodes a bundle of messages with the given time tag.
[[nodiscard]] std::vector<std::uint8_t> encodeBundle(std::span<const OscMessage> messages, std::uint64_t timeTag = 1);
// Decodes a packet (message or bundle, nested bundles flattened). Malformed data is an error;
// a bundle with one malformed element fails as a whole.
[[nodiscard]] Result<std::vector<OscMessage>> decodePacket(std::span<const std::uint8_t> packet);

// ---- address patterns (OSC 1.0: ? * [abc] [a-z] [!abc] {foo,bar}) ----
// Returns true when `pattern` (from a message) matches the concrete `address` (a method in our
// address space). Matching is per path segment ('/' is never matched by wildcards).
[[nodiscard]] bool matchAddress(std::string_view pattern, std::string_view address);
// A syntactically valid address: starts with '/', no spaces or control characters, no empty
// segments except the root.
[[nodiscard]] bool isValidAddress(std::string_view address);

// ---- UDP receiver ----
// Owns a socket and a thread; decoded messages are queued and drained on the caller's thread.
class OscReceiver {
public:
    OscReceiver();
    ~OscReceiver();
    OscReceiver(const OscReceiver&) = delete;
    OscReceiver& operator=(const OscReceiver&) = delete;

    // Binds `bindAddress:port` (bindAddress "0.0.0.0" = every interface, "127.0.0.1" = local only;
    // port 0 = any free port, see port()). Starts the thread. Errors: bind/listen failures.
    [[nodiscard]] Result<void> open(std::uint16_t port, const std::string& bindAddress = "0.0.0.0");
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] std::uint16_t port() const;

    // Moves every queued message into `out` (in arrival order). Returns the count.
    std::size_t drain(std::vector<OscMessage>& out);
    // Diagnostics for the UI: packets and messages received, decode errors, last sender.
    struct Stats {
        std::uint64_t packets = 0;
        std::uint64_t messages = 0;
        std::uint64_t errors = 0;
        std::string lastSender; // "ip:port"
        std::string lastError;
    };
    [[nodiscard]] Stats stats() const;
    // Bound on the inbox (oldest messages are dropped beyond it). Default 4096.
    void setQueueLimit(std::size_t limit);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---- UDP sender (feedback to controllers, tests) ----
class OscSender {
public:
    OscSender();
    ~OscSender();
    OscSender(const OscSender&) = delete;
    OscSender& operator=(const OscSender&) = delete;
    [[nodiscard]] Result<void> open(const std::string& host, std::uint16_t port); // resolves IPv4 host/name
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] Result<void> send(const OscMessage& message);
    [[nodiscard]] Result<void> sendBundle(std::span<const OscMessage> messages, std::uint64_t timeTag = 1);
    [[nodiscard]] Result<void> sendRaw(std::span<const std::uint8_t> packet);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// NTP time tag helpers (seconds since 1900 in the upper 32 bits, fraction below).
[[nodiscard]] std::uint64_t ntpNow();
[[nodiscard]] double ntpToSeconds(std::uint64_t ntp);      // seconds since 1900
[[nodiscard]] std::uint64_t ntpFromSeconds(double seconds);

} // namespace avgen::control
