#include "audio/tempo_metadata.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <ranges>

namespace avgen::audio {
namespace {

// ---- little byte helpers ---------------------------------------------------------------------

using Bytes = std::span<const std::uint8_t>;

[[nodiscard]] bool fourcc(Bytes b, std::size_t at, const char (&tag)[5]) {
    return at + 4 <= b.size() && std::memcmp(b.data() + at, tag, 4) == 0;
}

[[nodiscard]] std::uint32_t beU32(Bytes b, std::size_t at) {
    return (static_cast<std::uint32_t>(b[at]) << 24) | (static_cast<std::uint32_t>(b[at + 1]) << 16) |
           (static_cast<std::uint32_t>(b[at + 2]) << 8) | static_cast<std::uint32_t>(b[at + 3]);
}

[[nodiscard]] std::uint32_t leU32(Bytes b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

[[nodiscard]] std::uint32_t beU24(Bytes b, std::size_t at) {
    return (static_cast<std::uint32_t>(b[at]) << 16) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           static_cast<std::uint32_t>(b[at + 2]);
}

// ID3 "synchsafe" integer: 4 bytes, 7 bits each, high bit always clear. Used for the tag size in
// every ID3v2 version, and additionally for *frame* sizes in v2.4 only -- the single most common
// way a hand-written ID3 reader silently reads the wrong frame.
[[nodiscard]] std::uint32_t syncsafeU32(Bytes b, std::size_t at) {
    return (static_cast<std::uint32_t>(b[at] & 0x7Fu) << 21) |
           (static_cast<std::uint32_t>(b[at + 1] & 0x7Fu) << 14) |
           (static_cast<std::uint32_t>(b[at + 2] & 0x7Fu) << 7) | static_cast<std::uint32_t>(b[at + 3] & 0x7Fu);
}

[[nodiscard]] bool isSyncsafe(Bytes b, std::size_t at) {
    return ((b[at] | b[at + 1] | b[at + 2] | b[at + 3]) & 0x80u) == 0;
}

[[nodiscard]] std::string trimmed(std::string_view s) {
    const auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    std::size_t begin = 0;
    while (begin < s.size() && isSpace(s[begin])) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin && isSpace(s[end - 1])) {
        --end;
    }
    return std::string(s.substr(begin, end - begin));
}

[[nodiscard]] bool iequalsAscii(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// ---- ID3v2 -----------------------------------------------------------------------------------

struct Id3Hit {
    std::string value;
    std::string key;    // "TBPM" / "TBP"
    std::string format; // "ID3v2.3"
};

// Undoes ID3 unsynchronisation: every 0xFF 0x00 pair becomes a single 0xFF. A tag flagged
// unsynchronised whose payload is read raw yields frame sizes that are wrong by the number of
// inserted zeroes, which walks the frame cursor into the middle of a frame.
[[nodiscard]] std::vector<std::uint8_t> deUnsynchronise(Bytes in) {
    std::vector<std::uint8_t> out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        out.push_back(in[i]);
        if (in[i] == 0xFFu && i + 1 < in.size() && in[i + 1] == 0x00u) {
            ++i; // drop the inserted zero
        }
    }
    return out;
}

// Decodes an ID3 text frame body to a narrow string. Byte 0 is the encoding.
// 0 = ISO-8859-1, 1 = UTF-16 with BOM, 2 = UTF-16BE (v2.4), 3 = UTF-8 (v2.4).
// A BPM is ASCII digits in every real file, so the UTF-16 cases just drop the high byte of each
// unit rather than pulling in a converter -- correct for the digits-and-dot alphabet, and any
// non-ASCII content fails validation anyway, which is the right answer for a BPM field.
[[nodiscard]] std::string decodeTextFrame(Bytes body) {
    if (body.empty()) {
        return {};
    }
    const std::uint8_t encoding = body[0];
    Bytes text = body.subspan(1);
    std::string out;
    if (encoding == 1 || encoding == 2) {
        std::size_t at = 0;
        bool littleEndian = (encoding == 2) ? false : true;
        if (encoding == 1 && text.size() >= 2) {
            if (text[0] == 0xFFu && text[1] == 0xFEu) {
                littleEndian = true;
                at = 2;
            } else if (text[0] == 0xFEu && text[1] == 0xFFu) {
                littleEndian = false;
                at = 2;
            }
        }
        for (; at + 1 < text.size(); at += 2) {
            const std::uint8_t lo = littleEndian ? text[at] : text[at + 1];
            const std::uint8_t hi = littleEndian ? text[at + 1] : text[at];
            if (lo == 0 && hi == 0) {
                break;
            }
            out.push_back(hi == 0 ? static_cast<char>(lo) : '?'); // '?' will fail validation
        }
    } else {
        for (const std::uint8_t c : text) {
            if (c == 0) {
                break;
            }
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

// Parses an ID3v2 tag starting at `bytes[0]` ("ID3" ...). Returns the BPM frame if present.
[[nodiscard]] std::optional<Id3Hit> findId3Bpm(Bytes bytes) {
    constexpr std::size_t kHeader = 10;
    if (bytes.size() < kHeader || !(bytes[0] == 'I' && bytes[1] == 'D' && bytes[2] == '3')) {
        return std::nullopt;
    }
    const std::uint8_t major = bytes[3];
    if (major < 2 || major > 4) {
        return std::nullopt; // ID3v2.5+ does not exist; anything else is not a tag we know
    }
    const std::uint8_t flags = bytes[5];
    if (!isSyncsafe(bytes, 6)) {
        return std::nullopt; // a non-synchsafe size is a malformed header, not a tag
    }
    const std::uint32_t declared = syncsafeU32(bytes, 6);
    const std::size_t available = std::min<std::size_t>(declared, bytes.size() - kHeader);
    if (available == 0) {
        return std::nullopt;
    }

    const bool unsynchronised = (flags & 0x80u) != 0;
    std::vector<std::uint8_t> owned;
    Bytes body = bytes.subspan(kHeader, available);
    if (unsynchronised) {
        owned = deUnsynchronise(body);
        body = owned;
    }

    const std::string format = fmt::format("ID3v2.{}", major);
    // v2.2 uses 3-byte frame IDs and 3-byte sizes; v2.3/v2.4 use 4 and 4.
    const std::size_t idLen = (major == 2) ? 3 : 4;
    const std::size_t frameHeader = (major == 2) ? 6 : 10;
    const std::string_view wanted = (major == 2) ? "TBP" : "TBPM";

    std::size_t at = 0;
    if (major >= 3 && (flags & 0x40u) != 0) { // extended header
        if (at + 4 > body.size()) {
            return std::nullopt;
        }
        // v2.3 stores the extended header size exclusive of its own 4 size bytes; v2.4 stores it
        // synchsafe and inclusive.
        const std::uint32_t extSize = (major == 4) ? syncsafeU32(body, 0) : beU32(body, 0) + 4;
        if (extSize > body.size()) {
            return std::nullopt;
        }
        at = extSize;
    }

    while (at + frameHeader <= body.size()) {
        if (body[at] == 0) {
            break; // padding
        }
        const std::string id(reinterpret_cast<const char*>(body.data() + at), idLen);
        std::uint32_t size = 0;
        if (major == 2) {
            size = beU24(body, at + 3);
        } else if (major == 4) {
            // v2.4 frame sizes are synchsafe -- but taggers exist (and old libraries) that write
            // a plain big-endian size into a v2.4 tag. Prefer the synchsafe reading; fall back to
            // the plain one when the synchsafe size does not land on a plausible next frame.
            const std::uint32_t syncsafe = syncsafeU32(body, at + 4);
            const std::uint32_t plain = beU32(body, at + 4);
            size = syncsafe;
            if (syncsafe != plain && !isSyncsafe(body, at + 4)) {
                size = plain;
            }
        } else {
            size = beU32(body, at + 4);
        }
        const std::size_t bodyAt = at + frameHeader;
        if (size == 0 || bodyAt + size > body.size()) {
            break;
        }
        if (id == wanted) {
            Bytes frame = body.subspan(bodyAt, size);
            // v2.4 per-frame unsynchronisation / data-length-indicator.
            std::vector<std::uint8_t> frameOwned;
            if (major == 4) {
                const std::uint8_t frameFlags = body[at + 9];
                if ((frameFlags & 0x02u) != 0) { // frame-level unsynchronisation
                    frameOwned = deUnsynchronise(frame);
                    frame = frameOwned;
                }
                if ((frameFlags & 0x01u) != 0 && frame.size() >= 4) { // data length indicator
                    frame = frame.subspan(4);
                }
            }
            return Id3Hit{decodeTextFrame(frame), std::string(wanted), format};
        }
        at = bodyAt + size;
    }
    return std::nullopt;
}

// ---- Vorbis comments (FLAC) -------------------------------------------------------------------

struct VorbisHit {
    std::string value;
    std::string key; // the key exactly as written in the file, e.g. "BPM"
};

// A Vorbis comment block: LE u32 vendor length, vendor, LE u32 count, then count entries each
// being LE u32 length + "KEY=value". Keys are case-insensitive by the Vorbis spec.
[[nodiscard]] std::optional<VorbisHit> findVorbisBpm(Bytes block) {
    if (block.size() < 8) {
        return std::nullopt;
    }
    const std::uint32_t vendorLen = leU32(block, 0);
    std::size_t at = 4;
    if (vendorLen > block.size() - at) {
        return std::nullopt;
    }
    at += vendorLen;
    if (at + 4 > block.size()) {
        return std::nullopt;
    }
    const std::uint32_t count = leU32(block, at);
    at += 4;
    // Cap the iteration by what the block can physically hold (4 bytes minimum per entry) so a
    // corrupt count cannot spin.
    const std::uint32_t maxEntries = static_cast<std::uint32_t>((block.size() - at) / 4 + 1);
    for (std::uint32_t i = 0; i < std::min(count, maxEntries); ++i) {
        if (at + 4 > block.size()) {
            break;
        }
        const std::uint32_t len = leU32(block, at);
        at += 4;
        if (len > block.size() - at) {
            break;
        }
        const std::string_view entry(reinterpret_cast<const char*>(block.data() + at), len);
        at += len;
        const auto eq = entry.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = entry.substr(0, eq);
        // BPM and TEMPO are both in use; accept either. BPM wins if a file carries both, because
        // it is by far the more common and TEMPO is also used for prose ("allegro").
        if (iequalsAscii(key, "BPM") || iequalsAscii(key, "TEMPO")) {
            VorbisHit hit{std::string(entry.substr(eq + 1)), std::string(key)};
            if (iequalsAscii(key, "BPM")) {
                return hit;
            }
            // TEMPO: keep looking for a BPM, but remember this one.
            for (std::uint32_t j = i + 1; j < std::min(count, maxEntries) && at + 4 <= block.size(); ++j) {
                const std::uint32_t l2 = leU32(block, at);
                at += 4;
                if (l2 > block.size() - at) {
                    break;
                }
                const std::string_view e2(reinterpret_cast<const char*>(block.data() + at), l2);
                at += l2;
                const auto eq2 = e2.find('=');
                if (eq2 != std::string_view::npos && iequalsAscii(e2.substr(0, eq2), "BPM")) {
                    return VorbisHit{std::string(e2.substr(eq2 + 1)), std::string(e2.substr(0, eq2))};
                }
            }
            return hit;
        }
    }
    return std::nullopt;
}

// ---- container walkers -------------------------------------------------------------------------

// FLAC: "fLaC" then metadata blocks, each a 4-byte header (1 bit last, 7 bits type, 24-bit BE
// length). Type 4 is VORBIS_COMMENT.
[[nodiscard]] std::optional<VorbisHit> flacVorbisBpm(Bytes bytes) {
    if (!fourcc(bytes, 0, "fLaC")) {
        return std::nullopt;
    }
    std::size_t at = 4;
    while (at + 4 <= bytes.size()) {
        const std::uint8_t header = bytes[at];
        const bool last = (header & 0x80u) != 0;
        const std::uint8_t type = header & 0x7Fu;
        const std::uint32_t len = beU24(bytes, at + 1);
        at += 4;
        if (len > bytes.size() - at) {
            break;
        }
        if (type == 4) {
            return findVorbisBpm(bytes.subspan(at, len));
        }
        at += len;
        if (last) {
            break;
        }
    }
    return std::nullopt;
}

// RIFF/WAVE: "RIFF" size "WAVE" then chunks of (fourcc, LE u32 size, payload, pad to even).
// An ID3v2 tag rides in an 'id3 ' chunk (the casing varies; both are seen in the wild).
// Nothing here invents a convention: the chunk is the documented way to put ID3 in RIFF, and it
// is what Serato, Traktor and Mixed In Key write.
[[nodiscard]] std::optional<Id3Hit> riffId3Bpm(Bytes bytes) {
    if (!fourcc(bytes, 0, "RIFF") || !fourcc(bytes, 8, "WAVE")) {
        return std::nullopt;
    }
    std::size_t at = 12;
    while (at + 8 <= bytes.size()) {
        const std::uint32_t size = leU32(bytes, at + 4);
        const std::size_t payload = at + 8;
        if (size > bytes.size() - payload) {
            break;
        }
        if (fourcc(bytes, at, "id3 ") || fourcc(bytes, at, "ID3 ")) {
            if (auto hit = findId3Bpm(bytes.subspan(payload, size))) {
                return hit;
            }
        }
        at = payload + size + (size & 1u); // chunks are word-aligned
    }
    return std::nullopt;
}

// AIFF/AIFC: "FORM" size "AIFF"/"AIFC" then chunks of (fourcc, BE u32 size, payload, pad to even).
// The ID3v2 spec names the 'ID3 ' chunk as the AIFF carrier.
[[nodiscard]] std::optional<Id3Hit> aiffId3Bpm(Bytes bytes) {
    if (!fourcc(bytes, 0, "FORM") || !(fourcc(bytes, 8, "AIFF") || fourcc(bytes, 8, "AIFC"))) {
        return std::nullopt;
    }
    std::size_t at = 12;
    while (at + 8 <= bytes.size()) {
        const std::uint32_t size = beU32(bytes, at + 4);
        const std::size_t payload = at + 8;
        if (size > bytes.size() - payload) {
            break;
        }
        if (fourcc(bytes, at, "ID3 ") || fourcc(bytes, at, "id3 ")) {
            if (auto hit = findId3Bpm(bytes.subspan(payload, size))) {
                return hit;
            }
        }
        at = payload + size + (size & 1u);
    }
    return std::nullopt;
}

} // namespace

// ---- public ------------------------------------------------------------------------------------

std::string_view tempoProvenanceName(TempoProvenance source) {
    switch (source) {
    case TempoProvenance::None:
        return "None";
    case TempoProvenance::EmbeddedMetadata:
        return "Embedded Metadata";
    case TempoProvenance::Detected:
        return "Detected";
    case TempoProvenance::ExternalClock:
        return "MIDI Clock";
    case TempoProvenance::UserOverride:
        return "User Override";
    }
    return "None";
}

std::string_view tempoProvenanceToken(TempoProvenance source) {
    switch (source) {
    case TempoProvenance::None:
        return "none";
    case TempoProvenance::EmbeddedMetadata:
        return "embedded";
    case TempoProvenance::Detected:
        return "detected";
    case TempoProvenance::ExternalClock:
        return "midi";
    case TempoProvenance::UserOverride:
        return "user";
    }
    return "none";
}

TempoProvenance tempoProvenanceFromToken(std::string_view token) {
    if (token == "embedded") {
        return TempoProvenance::EmbeddedMetadata;
    }
    if (token == "detected") {
        return TempoProvenance::Detected;
    }
    if (token == "midi") {
        return TempoProvenance::ExternalClock;
    }
    if (token == "user") {
        return TempoProvenance::UserOverride;
    }
    return TempoProvenance::None;
}

double TempoMap::bpmAt(double seconds) const {
    if (events.empty()) {
        return 0.0;
    }
    double bpm = events.front().bpm;
    for (const TempoEvent& e : events) {
        if (e.timeSeconds > seconds) {
            break;
        }
        bpm = e.bpm;
    }
    return bpm;
}

double TempoMap::secondsPerBeatAt(double seconds) const {
    const double bpm = bpmAt(seconds);
    return bpm > 0.0 ? 60.0 / bpm : 0.0;
}

TempoMap TempoMap::constant(double bpm) {
    TempoMap map;
    if (bpm > 0.0) {
        map.events.push_back(TempoEvent{.timeSeconds = 0.0, .bpm = bpm});
    }
    return map;
}

TempoMap TempoMap::fromAudioTempo(const AudioTempo& tempo) {
    return tempo.available ? constant(tempo.bpm) : TempoMap{};
}

std::optional<double> parseBpmString(std::string_view text) {
    const std::string s = trimmed(text);
    if (s.empty()) {
        return std::nullopt;
    }
    // from_chars is locale-independent and, unlike strtod, refuses "nan"/"inf" by default for the
    // general form only after we have already rejected a leading letter -- so check the alphabet
    // first. A BPM is digits, at most one '.', and an optional leading '+'.
    std::size_t at = (s[0] == '+') ? 1 : 0;
    if (at >= s.size()) {
        return std::nullopt;
    }
    bool sawDigit = false;
    bool sawDot = false;
    for (std::size_t i = at; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= '0' && c <= '9') {
            sawDigit = true;
        } else if (c == '.' && !sawDot) {
            sawDot = true;
        } else {
            return std::nullopt; // "banana", "128bpm", "1,5", "-128", "NaN", "inf"
        }
    }
    if (!sawDigit) {
        return std::nullopt;
    }
    double value = 0.0;
    const char* begin = s.data() + at;
    const char* end = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    if (!std::isfinite(value) || value < kMinPlausibleBpm || value > kMaxPlausibleBpm) {
        return std::nullopt; // zero, and anything absurd
    }
    return value;
}

AudioTempo readEmbeddedTempo(Bytes bytes, std::string_view name) {
    const auto accept = [&](const std::string& raw, std::string key, std::string format,
                            bool integerSemantics) -> AudioTempo {
        if (const auto bpm = parseBpmString(raw)) {
            log::info("embedded tempo: file='{}' format={} field={} bpm={}", name, format, key, *bpm);
            return AudioTempo{.available = true,
                              .bpm = *bpm,
                              .source = TempoProvenance::EmbeddedMetadata,
                              .metadataKey = std::move(key),
                              .metadataFormat = std::move(format),
                              .confidence = 1.0,
                              .integerSemantics = integerSemantics};
        }
        // Not an error. The file made a claim we cannot use; say so once and carry on.
        log::warn("embedded tempo ignored (invalid value): file='{}' format={} field={} value='{}'", name,
                  format, key, raw);
        return {};
    };

    if (auto hit = findId3Bpm(bytes)) { // bare ID3v2 at the head of an MP3
        // ID3 TBPM is specified as "an integer, represented as a numerical string". Real taggers
        // write fractions into it anyway, which we keep -- the flag records the spec, not the value.
        return accept(hit->value, std::move(hit->key), std::move(hit->format), true);
    }
    if (auto hit = riffId3Bpm(bytes)) {
        return accept(hit->value, std::move(hit->key), hit->format + " (RIFF 'id3 ' chunk)", true);
    }
    if (auto hit = aiffId3Bpm(bytes)) {
        return accept(hit->value, std::move(hit->key), hit->format + " (AIFF 'ID3 ' chunk)", true);
    }
    if (auto hit = flacVorbisBpm(bytes)) {
        // Vorbis comments are free-form text with no declared numeric type, so a fraction here is
        // as native as an integer.
        return accept(hit->value, std::move(hit->key), "Vorbis comment (FLAC)", false);
    }
    log::debug("embedded tempo: none found in '{}'", name);
    return {};
}

Result<AudioTempo> readEmbeddedTempo(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return fail("audio file not found: {}", path.string());
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail("cannot open '{}' for metadata", path.string());
    }
    const auto size = std::filesystem::file_size(path, ec);
    const std::size_t want =
        ec ? kTempoMetadataProbeBytes : std::min<std::size_t>(kTempoMetadataProbeBytes, size);
    std::vector<std::uint8_t> head(want);
    in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(want));
    head.resize(static_cast<std::size_t>(in.gcount()));
    if (head.empty()) {
        return fail("'{}' is empty", path.string());
    }
    return readEmbeddedTempo(head, path.filename().string());
}

} // namespace avgen::audio
