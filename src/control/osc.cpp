#include "control/osc.hpp"

#include "core/log.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace avgen::control {

namespace {

// The largest UDP payload we accept in one datagram (the theoretical IPv4 maximum is 65507).
constexpr std::size_t kMaxPacketSize = 65536;
// The receiver thread wakes at least this often to notice close().
constexpr int kPollTimeoutMs = 100;
// Nesting limit for bundles inside bundles (protects the stack from hostile packets).
constexpr int kMaxBundleDepth = 32;
// Work budget for '{a,b}' alternatives in address patterns: every alternative tried costs one
// unit. Bounds the stack and the exponential worst case of a hostile pattern such as "{a,a}"
// repeated hundreds of times (the match then reports false).
constexpr int kMaxAlternativeSteps = 4096;
constexpr std::uint64_t kNtpUnixOffset = 2208988800ULL; // seconds from 1900-01-01 to 1970-01-01
constexpr double kNtpFractionScale = 4294967296.0;      // 2^32
constexpr std::size_t kDefaultQueueLimit = 4096;
constexpr std::string_view kBundleTag{"#bundle\0", 8};

// ---- byte helpers (OSC is big-endian, everything is 4-byte aligned) ----

constexpr std::size_t padded4(std::size_t n) {
    return (n + 3) & ~std::size_t{3};
}

void putU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void putU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    putU32(out, static_cast<std::uint32_t>(value >> 32));
    putU32(out, static_cast<std::uint32_t>(value));
}

void putPadding(std::vector<std::uint8_t>& out, std::size_t unpaddedLength) {
    out.resize(out.size() + (padded4(unpaddedLength) - unpaddedLength), 0);
}

void putString(std::vector<std::uint8_t>& out, std::string_view text) {
    out.insert(out.end(), text.begin(), text.end());
    out.push_back(0);
    putPadding(out, text.size() + 1);
}

void putBlob(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> bytes) {
    putU32(out, static_cast<std::uint32_t>(bytes.size()));
    out.insert(out.end(), bytes.begin(), bytes.end());
    putPadding(out, bytes.size());
}

void putArg(std::vector<std::uint8_t>& out, const OscArg& arg) {
    std::visit(
        [&out](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::int32_t>) {
                putU32(out, static_cast<std::uint32_t>(value));
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                putU64(out, static_cast<std::uint64_t>(value));
            } else if constexpr (std::is_same_v<T, float>) {
                putU32(out, std::bit_cast<std::uint32_t>(value));
            } else if constexpr (std::is_same_v<T, double>) {
                putU64(out, std::bit_cast<std::uint64_t>(value));
            } else if constexpr (std::is_same_v<T, std::string>) {
                putString(out, value);
            } else if constexpr (std::is_same_v<T, OscBlob>) {
                putBlob(out, value.bytes);
            }
            // std::monostate (N/I) and bool (T/F) carry no payload.
        },
        arg);
}

// The canonical type tag for an argument value.
char defaultTag(const OscArg& arg) {
    return std::visit(
        [](const auto& value) -> char {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>) {
                return value ? 'T' : 'F';
            } else if constexpr (std::is_same_v<T, std::int32_t>) {
                return 'i';
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return 'h';
            } else if constexpr (std::is_same_v<T, float>) {
                return 'f';
            } else if constexpr (std::is_same_v<T, double>) {
                return 'd';
            } else if constexpr (std::is_same_v<T, std::string>) {
                return 's';
            } else if constexpr (std::is_same_v<T, OscBlob>) {
                return 'b';
            } else {
                return 'N';
            }
        },
        arg);
}

// True when a received/explicit tag can describe the argument (so re-encoding a decoded message
// keeps 'I' vs 'N', 't' vs 'h', 'c'/'r'/'m' vs 'i', 'S' vs 's').
bool tagFits(char tag, const OscArg& arg) {
    return std::visit(
        [tag](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>) {
                return value ? tag == 'T' : tag == 'F';
            } else if constexpr (std::is_same_v<T, std::int32_t>) {
                return tag == 'i' || tag == 'c' || tag == 'r' || tag == 'm';
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return tag == 'h' || tag == 't';
            } else if constexpr (std::is_same_v<T, float>) {
                return tag == 'f';
            } else if constexpr (std::is_same_v<T, double>) {
                return tag == 'd';
            } else if constexpr (std::is_same_v<T, std::string>) {
                return tag == 's' || tag == 'S';
            } else if constexpr (std::is_same_v<T, OscBlob>) {
                return tag == 'b';
            } else {
                return tag == 'N' || tag == 'I';
            }
        },
        arg);
}

// Uses the message's own type tags when they describe its arguments one-to-one, otherwise derives
// them from the argument values.
std::string typeTagsFor(const OscMessage& message) {
    if (message.typeTags.size() == message.args.size()) {
        bool fits = true;
        for (std::size_t i = 0; i < message.args.size() && fits; ++i) {
            fits = tagFits(message.typeTags[i], message.args[i]);
        }
        if (fits) {
            return message.typeTags;
        }
    }
    std::string tags;
    tags.reserve(message.args.size());
    for (const OscArg& arg : message.args) {
        tags.push_back(defaultTag(arg));
    }
    return tags;
}

// Bounds-checked reader over one packet. Every read fails cleanly instead of overrunning.
struct Cursor {
    std::span<const std::uint8_t> data;
    std::size_t pos = 0;

    [[nodiscard]] std::size_t remaining() const { return data.size() - pos; }

    bool readU32(std::uint32_t& value) {
        if (remaining() < 4) {
            return false;
        }
        value = (std::uint32_t{data[pos]} << 24) | (std::uint32_t{data[pos + 1]} << 16) |
                (std::uint32_t{data[pos + 2]} << 8) | std::uint32_t{data[pos + 3]};
        pos += 4;
        return true;
    }

    bool readU64(std::uint64_t& value) {
        std::uint32_t hi = 0;
        std::uint32_t lo = 0;
        if (remaining() < 8 || !readU32(hi) || !readU32(lo)) {
            return false;
        }
        value = (std::uint64_t{hi} << 32) | std::uint64_t{lo};
        return true;
    }

    // A NUL-terminated string whose terminator and padding (to a multiple of 4) lie inside the
    // packet. Padding bytes are not required to be zero.
    bool readString(std::string_view& text) {
        const auto rest = data.subspan(pos);
        const auto terminator = std::ranges::find(rest, std::uint8_t{0});
        if (terminator == rest.end()) {
            return false;
        }
        const auto length = static_cast<std::size_t>(terminator - rest.begin());
        const std::size_t total = padded4(length + 1);
        if (total > rest.size()) {
            return false;
        }
        text = std::string_view(reinterpret_cast<const char*>(rest.data()), length);
        pos += total;
        return true;
    }

    bool readBlob(std::span<const std::uint8_t>& bytes) {
        std::uint32_t size = 0;
        if (!readU32(size) || size > remaining()) {
            return false;
        }
        const std::size_t total = padded4(size);
        if (total > remaining()) {
            return false;
        }
        bytes = data.subspan(pos, size);
        pos += total;
        return true;
    }
};

bool isBundle(std::span<const std::uint8_t> element) {
    return element.size() >= kBundleTag.size() &&
           std::equal(kBundleTag.begin(), kBundleTag.end(), element.begin(),
                      [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; });
}

Result<OscMessage> decodeMessage(std::span<const std::uint8_t> element, std::uint64_t timeTag) {
    Cursor cursor{element};
    std::string_view address;
    if (!cursor.readString(address)) {
        return fail("OSC message: missing or unterminated address");
    }
    if (address.empty() || address.front() != '/') {
        return fail("OSC message: address '{}' does not start with '/'", address);
    }
    std::string_view tags;
    if (!cursor.readString(tags)) {
        return fail("OSC message '{}': missing or unterminated type tag string", address);
    }
    if (tags.empty() || tags.front() != ',') {
        return fail("OSC message '{}': type tag string must start with ','", address);
    }
    OscMessage message;
    message.address = std::string(address);
    message.timeTag = timeTag;
    message.typeTags = std::string(tags.substr(1));
    message.args.reserve(message.typeTags.size());
    const auto truncated = [&address](char tag) {
        return fail("OSC message '{}': truncated argument for type tag '{}'", address, tag);
    };
    for (const char tag : message.typeTags) {
        switch (tag) {
        case 'i':
        case 'c':
        case 'r':
        case 'm': {
            std::uint32_t raw = 0;
            if (!cursor.readU32(raw)) {
                return truncated(tag);
            }
            message.args.emplace_back(static_cast<std::int32_t>(raw));
            break;
        }
        case 'h':
        case 't': {
            std::uint64_t raw = 0;
            if (!cursor.readU64(raw)) {
                return truncated(tag);
            }
            message.args.emplace_back(static_cast<std::int64_t>(raw));
            break;
        }
        case 'f': {
            std::uint32_t raw = 0;
            if (!cursor.readU32(raw)) {
                return truncated(tag);
            }
            message.args.emplace_back(std::bit_cast<float>(raw));
            break;
        }
        case 'd': {
            std::uint64_t raw = 0;
            if (!cursor.readU64(raw)) {
                return truncated(tag);
            }
            message.args.emplace_back(std::bit_cast<double>(raw));
            break;
        }
        case 's':
        case 'S': {
            std::string_view text;
            if (!cursor.readString(text)) {
                return truncated(tag);
            }
            message.args.emplace_back(std::string(text));
            break;
        }
        case 'b': {
            std::span<const std::uint8_t> bytes;
            if (!cursor.readBlob(bytes)) {
                return truncated(tag);
            }
            message.args.emplace_back(OscBlob{std::vector<std::uint8_t>(bytes.begin(), bytes.end())});
            break;
        }
        case 'T':
            message.args.emplace_back(true);
            break;
        case 'F':
            message.args.emplace_back(false);
            break;
        case 'N':
        case 'I':
            message.args.emplace_back(std::monostate{});
            break;
        case '[':
        case ']':
            // Arrays are flattened: their elements become ordinary arguments.
            break;
        default:
            return fail("OSC message '{}': unknown type tag '{}'", address, tag);
        }
    }
    return message;
}

Result<void> decodeBundle(std::span<const std::uint8_t> element, std::vector<OscMessage>& out, int depth) {
    if (depth > kMaxBundleDepth) {
        return fail("OSC bundle: nesting deeper than {} levels", kMaxBundleDepth);
    }
    Cursor cursor{element, kBundleTag.size()};
    std::uint64_t timeTag = 0;
    if (!cursor.readU64(timeTag)) {
        return fail("OSC bundle: truncated time tag");
    }
    while (cursor.remaining() > 0) {
        std::uint32_t size = 0;
        if (!cursor.readU32(size)) {
            return fail("OSC bundle: truncated element size");
        }
        if (size > cursor.remaining()) {
            return fail("OSC bundle: element size {} exceeds the {} bytes left in the packet", size,
                        cursor.remaining());
        }
        const auto inner = element.subspan(cursor.pos, size);
        cursor.pos += size;
        if (isBundle(inner)) {
            if (auto nested = decodeBundle(inner, out, depth + 1); !nested) {
                return nested;
            }
        } else {
            auto message = decodeMessage(inner, timeTag);
            if (!message) {
                return std::unexpected(std::move(message.error()));
            }
            out.push_back(std::move(*message));
        }
    }
    return {};
}

// ---- address patterns ----

// `body` is the text between '[' and ']'. Supports ranges ("a-z"), negation (leading '!') and a
// literal '-' at the end. An empty class matches nothing; "[!]" matches any single character.
bool matchClass(std::string_view body, char c) {
    bool negate = false;
    std::size_t i = 0;
    if (!body.empty() && body.front() == '!') {
        negate = true;
        i = 1;
    }
    bool hit = false;
    for (; i < body.size(); ++i) {
        const char lo = body[i];
        if (i + 2 < body.size() && body[i + 1] == '-') {
            const char hi = body[i + 2];
            if (std::min(lo, hi) <= c && c <= std::max(lo, hi)) {
                hit = true;
            }
            i += 2;
        } else if (lo == c) {
            hit = true;
        }
    }
    return hit != negate;
}

// Glob-style matching with a single backtrack point for '*' (linear in pattern x segment length,
// no exponential blow-up on "*a*a*a..."). '*' and '?' never consume '/'. Alternatives "{a,b}" are
// literal strings; the remainder of the pattern after a brace is matched recursively, with a
// work budget so a hostile pattern with hundreds of braces cannot explode.
bool matchPattern(std::string_view pattern, std::string_view address, int& budget) {
    std::size_t pi = 0;
    std::size_t ai = 0;
    std::size_t starPattern = std::string_view::npos; // pattern index just after the last '*'
    std::size_t starAddress = 0;                      // address index that '*' would consume next
    while (pi < pattern.size() || ai < address.size()) {
        bool matched = false;
        if (pi < pattern.size()) {
            const char pc = pattern[pi];
            if (pc == '*') {
                while (pi < pattern.size() && pattern[pi] == '*') {
                    ++pi; // "**" is "*"
                }
                starPattern = pi;
                starAddress = ai;
                continue;
            }
            if (pc == '?') {
                matched = ai < address.size() && address[ai] != '/';
                if (matched) {
                    ++pi;
                    ++ai;
                }
            } else if (const auto close = pc == '[' ? pattern.find(']', pi + 1) : std::string_view::npos;
                       pc == '[' && close != std::string_view::npos) {
                matched = ai < address.size() && address[ai] != '/' &&
                          matchClass(pattern.substr(pi + 1, close - pi - 1), address[ai]);
                if (matched) {
                    pi = close + 1;
                    ++ai;
                }
            } else if (const auto brace = pc == '{' ? pattern.find('}', pi + 1) : std::string_view::npos;
                       pc == '{' && brace != std::string_view::npos) {
                const std::string_view list = pattern.substr(pi + 1, brace - pi - 1);
                const std::string_view rest = pattern.substr(brace + 1);
                const std::string_view tail = address.substr(ai);
                std::size_t start = 0;
                while (true) {
                    const auto comma = list.find(',', start);
                    const std::string_view alternative = list.substr(
                        start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
                    if (--budget < 0) {
                        return false;
                    }
                    if (tail.starts_with(alternative) &&
                        matchPattern(rest, tail.substr(alternative.size()), budget)) {
                        return true;
                    }
                    if (comma == std::string_view::npos) {
                        break;
                    }
                    start = comma + 1;
                }
                // No alternative completes the match from here: fall through to '*' backtracking.
            } else {
                // Literal (including '/', and '[' or '{' without a closing bracket).
                matched = ai < address.size() && address[ai] == pc;
                if (matched) {
                    ++pi;
                    ++ai;
                }
            }
        }
        if (matched) {
            continue;
        }
        // Mismatch: let the most recent '*' consume one more character of its segment.
        if (starPattern != std::string_view::npos && starAddress < address.size() &&
            address[starAddress] != '/') {
            ++starAddress;
            ai = starAddress;
            pi = starPattern;
            continue;
        }
        return false;
    }
    return true;
}

// ---- sockets ----

std::string errnoText() {
    return std::strerror(errno);
}

std::string describeSender(const sockaddr_storage& from, socklen_t length) {
    if (from.ss_family != AF_INET || length < sizeof(sockaddr_in)) {
        return "?";
    }
    sockaddr_in addr{};
    std::memcpy(&addr, &from, sizeof(addr));
    char host[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &addr.sin_addr, host, sizeof(host)) == nullptr) {
        return "?";
    }
    return fmt::format("{}:{}", host, ntohs(addr.sin_port));
}

} // namespace

// ---- OscMessage ----

float OscMessage::number(std::size_t index, float fallback) const {
    if (index >= args.size()) {
        return fallback;
    }
    return std::visit(
        [fallback](const auto& value) -> float {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>) {
                return value ? 1.0f : 0.0f;
            } else if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> ||
                                 std::is_same_v<T, double>) {
                return static_cast<float>(value);
            } else if constexpr (std::is_same_v<T, float>) {
                return value;
            } else {
                return fallback;
            }
        },
        args[index]);
}

bool OscMessage::hasNumber(std::size_t index) const {
    if (index >= args.size()) {
        return false;
    }
    const OscArg& arg = args[index];
    return std::holds_alternative<bool>(arg) || std::holds_alternative<std::int32_t>(arg) ||
           std::holds_alternative<std::int64_t>(arg) || std::holds_alternative<float>(arg) ||
           std::holds_alternative<double>(arg);
}

std::string_view OscMessage::text(std::size_t index, std::string_view fallback) const {
    if (index >= args.size()) {
        return fallback;
    }
    if (const auto* value = std::get_if<std::string>(&args[index])) {
        return *value;
    }
    return fallback;
}

std::size_t OscMessage::numberCount() const {
    std::size_t count = 0;
    while (hasNumber(count)) {
        ++count;
    }
    return count;
}

// ---- encoding / decoding ----

std::vector<std::uint8_t> encodeMessage(const OscMessage& message) {
    std::vector<std::uint8_t> out;
    out.reserve(padded4(message.address.size() + 1) + padded4(message.args.size() + 2) +
                message.args.size() * 4);
    putString(out, message.address);
    putString(out, "," + typeTagsFor(message));
    for (const OscArg& arg : message.args) {
        putArg(out, arg);
    }
    return out;
}

std::vector<std::uint8_t> encodeBundle(std::span<const OscMessage> messages, std::uint64_t timeTag) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), kBundleTag.begin(), kBundleTag.end());
    putU64(out, timeTag);
    for (const OscMessage& message : messages) {
        const std::vector<std::uint8_t> element = encodeMessage(message);
        putU32(out, static_cast<std::uint32_t>(element.size()));
        out.insert(out.end(), element.begin(), element.end());
    }
    return out;
}

Result<std::vector<OscMessage>> decodePacket(std::span<const std::uint8_t> packet) {
    if (packet.empty()) {
        return fail("OSC: empty packet");
    }
    std::vector<OscMessage> messages;
    if (isBundle(packet)) {
        if (auto result = decodeBundle(packet, messages, 0); !result) {
            return std::unexpected(std::move(result.error()));
        }
        return messages;
    }
    auto message = decodeMessage(packet, 1);
    if (!message) {
        return std::unexpected(std::move(message.error()));
    }
    messages.push_back(std::move(*message));
    return messages;
}

// ---- address patterns ----

bool matchAddress(std::string_view pattern, std::string_view address) {
    int budget = kMaxAlternativeSteps;
    return matchPattern(pattern, address, budget);
}

bool isValidAddress(std::string_view address) {
    if (address.empty() || address.front() != '/') {
        return false;
    }
    for (const char c : address) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= 0x20 || byte == 0x7F) {
            return false;
        }
    }
    if (address.size() == 1) {
        return true;
    }
    return address.back() != '/' && address.find("//") == std::string_view::npos;
}

// ---- OscReceiver ----

struct OscReceiver::Impl {
    int fd = -1;
    std::uint16_t boundPort = 0;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<std::size_t> queueLimit{kDefaultQueueLimit};

    mutable std::mutex mutex; // guards inbox, stats, droppedTotal
    std::deque<OscMessage> inbox;
    Stats stats;
    std::uint64_t droppedTotal = 0;

    void run() {
        std::vector<std::uint8_t> buffer(kMaxPacketSize);
        while (running.load(std::memory_order_acquire)) {
            pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;
            const int ready = ::poll(&pfd, 1, kPollTimeoutMs);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                recordError(fmt::format("poll(): {}", errnoText()));
                break;
            }
            if (ready == 0) {
                continue;
            }
            sockaddr_storage from{};
            socklen_t fromLength = sizeof(from);
            const ssize_t received = ::recvfrom(fd, buffer.data(), buffer.size(), 0,
                                                reinterpret_cast<sockaddr*>(&from), &fromLength);
            if (received < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                }
                recordError(fmt::format("recvfrom(): {}", errnoText()));
                continue;
            }
            const auto packet =
                std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(received));
            auto decoded = decodePacket(packet);
            std::string sender = describeSender(from, fromLength);

            std::lock_guard lock(mutex);
            ++stats.packets;
            stats.lastSender = std::move(sender);
            if (!decoded) {
                ++stats.errors;
                stats.lastError = std::move(decoded.error().message);
                continue;
            }
            stats.messages += decoded->size();
            for (OscMessage& message : *decoded) {
                message.sender = stats.lastSender;
                inbox.push_back(std::move(message));
            }
            const std::size_t limit = queueLimit.load(std::memory_order_relaxed);
            if (inbox.size() > limit) {
                const std::size_t drop = inbox.size() - limit;
                inbox.erase(inbox.begin(), inbox.begin() + static_cast<std::ptrdiff_t>(drop));
                droppedTotal += drop;
                stats.errors += drop;
                stats.lastError =
                    fmt::format("inbox full ({} messages): dropped {} oldest ({} dropped in total)", limit,
                                drop, droppedTotal);
            }
        }
    }

    void recordError(std::string text) {
        std::lock_guard lock(mutex);
        ++stats.errors;
        stats.lastError = std::move(text);
    }
};

OscReceiver::OscReceiver()
    : impl_(std::make_unique<Impl>()) {}

OscReceiver::~OscReceiver() {
    close();
}

Result<void> OscReceiver::open(std::uint16_t port, const std::string& bindAddress) {
    close();
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1) {
        return fail("OSC receiver: '{}' is not an IPv4 address", bindAddress);
    }
    const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        return fail("OSC receiver: socket(): {}", errnoText());
    }
    const int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    const int receiveBuffer = 1 << 20; // best effort: absorb bursts of controller traffic
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receiveBuffer, sizeof(receiveBuffer));
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        const std::string reason = errnoText();
        ::close(fd);
        return fail("OSC receiver: cannot bind {}:{}: {}", bindAddress, port, reason);
    }
    sockaddr_in bound{};
    socklen_t boundLength = sizeof(bound);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &boundLength) != 0) {
        const std::string reason = errnoText();
        ::close(fd);
        return fail("OSC receiver: getsockname(): {}", reason);
    }
    impl_->fd = fd;
    impl_->boundPort = ntohs(bound.sin_port);
    {
        std::lock_guard lock(impl_->mutex);
        impl_->inbox.clear();
        impl_->stats = Stats{};
        impl_->droppedTotal = 0;
    }
    impl_->running.store(true, std::memory_order_release);
    impl_->thread = std::thread([impl = impl_.get()] { impl->run(); });
    log::info("OSC receiver listening on {}:{}", bindAddress, impl_->boundPort);
    return {};
}

void OscReceiver::close() {
    if (impl_->fd < 0 && !impl_->thread.joinable()) {
        return;
    }
    impl_->running.store(false, std::memory_order_release);
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
    log::debug("OSC receiver on port {} closed", impl_->boundPort);
    impl_->boundPort = 0;
}

bool OscReceiver::isOpen() const {
    return impl_->fd >= 0;
}

std::uint16_t OscReceiver::port() const {
    return impl_->boundPort;
}

std::size_t OscReceiver::drain(std::vector<OscMessage>& out) {
    std::lock_guard lock(impl_->mutex);
    const std::size_t count = impl_->inbox.size();
    out.reserve(out.size() + count);
    for (OscMessage& message : impl_->inbox) {
        out.push_back(std::move(message));
    }
    impl_->inbox.clear();
    return count;
}

OscReceiver::Stats OscReceiver::stats() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->stats;
}

void OscReceiver::setQueueLimit(std::size_t limit) {
    const std::size_t clamped = std::max<std::size_t>(limit, 1);
    impl_->queueLimit.store(clamped, std::memory_order_relaxed);
    std::lock_guard lock(impl_->mutex);
    if (impl_->inbox.size() > clamped) {
        impl_->inbox.erase(impl_->inbox.begin(),
                           impl_->inbox.begin() + static_cast<std::ptrdiff_t>(impl_->inbox.size() - clamped));
    }
}

// ---- OscSender ----

struct OscSender::Impl {
    int fd = -1;
    sockaddr_in destination{};
    std::string target; // "host:port" for diagnostics
};

OscSender::OscSender()
    : impl_(std::make_unique<Impl>()) {}

OscSender::~OscSender() {
    close();
}

Result<void> OscSender::open(const std::string& host, std::uint16_t port) {
    close();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* results = nullptr;
    if (const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &results); rc != 0) {
        return fail("OSC sender: cannot resolve '{}': {}", host, ::gai_strerror(rc));
    }
    sockaddr_in destination{};
    bool found = false;
    for (const addrinfo* entry = results; entry != nullptr; entry = entry->ai_next) {
        if (entry->ai_family == AF_INET && entry->ai_addr != nullptr &&
            entry->ai_addrlen >= sizeof(sockaddr_in)) {
            std::memcpy(&destination, entry->ai_addr, sizeof(destination));
            found = true;
            break;
        }
    }
    ::freeaddrinfo(results);
    if (!found) {
        return fail("OSC sender: '{}' has no IPv4 address", host);
    }
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        return fail("OSC sender: socket(): {}", errnoText());
    }
    impl_->fd = fd;
    impl_->destination = destination;
    impl_->target = fmt::format("{}:{}", host, port);
    return {};
}

void OscSender::close() {
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
}

bool OscSender::isOpen() const {
    return impl_->fd >= 0;
}

Result<void> OscSender::send(const OscMessage& message) {
    return sendRaw(encodeMessage(message));
}

Result<void> OscSender::sendBundle(std::span<const OscMessage> messages, std::uint64_t timeTag) {
    return sendRaw(encodeBundle(messages, timeTag));
}

Result<void> OscSender::sendRaw(std::span<const std::uint8_t> packet) {
    if (impl_->fd < 0) {
        return fail("OSC sender is not open");
    }
    if (packet.empty()) {
        return fail("OSC sender: refusing to send an empty packet");
    }
    const ssize_t sent =
        ::sendto(impl_->fd, packet.data(), packet.size(), 0,
                 reinterpret_cast<const sockaddr*>(&impl_->destination), sizeof(impl_->destination));
    if (sent < 0) {
        return fail("OSC sender: sendto {} ({} bytes): {}", impl_->target, packet.size(), errnoText());
    }
    if (static_cast<std::size_t>(sent) != packet.size()) {
        return fail("OSC sender: sent {} of {} bytes to {}", sent, packet.size(), impl_->target);
    }
    return {};
}

// ---- NTP time tags ----

std::uint64_t ntpNow() {
    const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(sinceEpoch);
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(sinceEpoch - seconds).count();
    const std::uint64_t whole = static_cast<std::uint64_t>(seconds.count()) + kNtpUnixOffset;
    const std::uint64_t fraction = (static_cast<std::uint64_t>(nanos) << 32) / 1000000000ULL;
    return (whole << 32) | (fraction & 0xFFFFFFFFULL);
}

double ntpToSeconds(std::uint64_t ntp) {
    return static_cast<double>(ntp >> 32) + static_cast<double>(ntp & 0xFFFFFFFFULL) / kNtpFractionScale;
}

std::uint64_t ntpFromSeconds(double seconds) {
    if (!(seconds > 0.0)) { // also NaN
        return 0;
    }
    const double whole = std::floor(seconds);
    if (whole >= kNtpFractionScale) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const double fraction = (seconds - whole) * kNtpFractionScale;
    const std::uint64_t fractionBits =
        std::min<std::uint64_t>(static_cast<std::uint64_t>(fraction), 0xFFFFFFFFULL);
    return (static_cast<std::uint64_t>(whole) << 32) | fractionBits;
}

} // namespace avgen::control
