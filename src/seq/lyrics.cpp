#include "seq/lyrics.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>

namespace avgen::seq {
namespace {

std::string_view trim(std::string_view s) {
    const auto notSpace = [](char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; };
    while (!s.empty() && !notSpace(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && !notSpace(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

std::vector<std::string_view> splitLines(std::string_view source) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= source.size()) {
        const std::size_t nl = source.find('\n', start);
        if (nl == std::string_view::npos) {
            out.push_back(source.substr(start));
            break;
        }
        out.push_back(source.substr(start, nl - start));
        start = nl + 1;
    }
    // A trailing newline produces one empty tail; keep it, the SRT parser uses blank lines.
    return out;
}

[[nodiscard]] bool parseUInt(std::string_view s, unsigned& out) {
    if (s.empty()) {
        return false;
    }
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

// "mm:ss.xx", "m:ss", "hh:mm:ss,mmm", "hh:mm:ss.mmm" -- every timestamp any of the three formats
// uses, because they differ only in how many fields and which decimal separator.
[[nodiscard]] bool parseTimestamp(std::string_view s, double& out) {
    s = trim(s);
    if (s.empty()) {
        return false;
    }
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ':') {
            fields.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    if (fields.size() < 2 || fields.size() > 3) {
        return false;
    }
    double total = 0.0;
    for (std::size_t i = 0; i + 1 < fields.size(); ++i) {
        unsigned value = 0;
        if (!parseUInt(fields[i], value)) {
            return false;
        }
        total = total * 60.0 + static_cast<double>(value);
    }
    // The seconds field, with an optional fraction after '.' or ','.
    std::string_view seconds = fields.back();
    const std::size_t dot = seconds.find_first_of(".,");
    std::string_view whole = dot == std::string_view::npos ? seconds : seconds.substr(0, dot);
    unsigned wholeValue = 0;
    if (!parseUInt(whole, wholeValue)) {
        return false;
    }
    double fraction = 0.0;
    if (dot != std::string_view::npos) {
        std::string_view digits = seconds.substr(dot + 1);
        // Trim anything that is not a digit (WebVTT cue settings ride on the same line).
        std::size_t n = 0;
        while (n < digits.size() && digits[n] >= '0' && digits[n] <= '9') {
            ++n;
        }
        digits = digits.substr(0, n);
        if (!digits.empty()) {
            unsigned fracValue = 0;
            if (!parseUInt(digits, fracValue)) {
                return false;
            }
            fraction = static_cast<double>(fracValue) / std::pow(10.0, static_cast<double>(digits.size()));
        }
    }
    out = total * 60.0 + static_cast<double>(wholeValue) + fraction;
    return true;
}

// "00:00:12,400 --> 00:00:15,800" (SRT and WebVTT share this shape).
[[nodiscard]] bool parseRange(std::string_view line, double& from, double& to) {
    const std::size_t arrow = line.find("-->");
    if (arrow == std::string_view::npos) {
        return false;
    }
    if (!parseTimestamp(line.substr(0, arrow), from)) {
        return false;
    }
    std::string_view tail = trim(line.substr(arrow + 3));
    // WebVTT may append cue settings after the end time: "00:00:15.800 line:90% align:center".
    const std::size_t space = tail.find(' ');
    return parseTimestamp(space == std::string_view::npos ? tail : tail.substr(0, space), to);
}

// LRC lines carry only starts; SRT and WebVTT carry both. Closes the open ends the same way for
// everything, so a line whose end is missing or nonsensical behaves identically whatever it came
// from.
void closeEnds(std::vector<LyricLine>& lines, double defaultHold, double gap, double minimum) {
    std::stable_sort(lines.begin(), lines.end(),
                     [](const LyricLine& a, const LyricLine& b) { return a.startSeconds < b.startSeconds; });
    for (std::size_t i = 0; i < lines.size(); ++i) {
        LyricLine& line = lines[i];
        if (line.endSeconds > line.startSeconds + minimum) {
            continue;
        }
        // A line runs up to the next one, less the gap that keeps two lyrics off the same frame.
        // The last line has nothing to make room for, so it holds for exactly `defaultHold`:
        // subtracting a gap there would be making space beside a line that does not exist.
        const double end = i + 1 < lines.size() ? lines[i + 1].startSeconds - gap
                                                : line.startSeconds + defaultHold;
        line.endSeconds = std::max(end, line.startSeconds + minimum);
    }
}

} // namespace

const char* lyricFormatName(LyricFormat format) {
    switch (format) {
    case LyricFormat::Lrc:
        return "lrc";
    case LyricFormat::Srt:
        return "srt";
    case LyricFormat::WebVtt:
        return "vtt";
    }
    return "lrc";
}

Result<std::vector<LyricLine>> parseLrc(std::string_view source) {
    std::vector<LyricLine> out;
    int lineNumber = 0;
    for (std::string_view raw : splitLines(source)) {
        ++lineNumber;
        std::string_view line = trim(raw);
        if (line.empty()) {
            continue;
        }
        // Collect the leading timestamps; an LRC line may carry several for a repeated chorus.
        std::vector<double> starts;
        while (!line.empty() && line.front() == '[') {
            const std::size_t close = line.find(']');
            if (close == std::string_view::npos) {
                return fail("lyrics: line {}: '[' with no ']'", lineNumber);
            }
            const std::string_view inside = line.substr(1, close - 1);
            double t = 0.0;
            if (parseTimestamp(inside, t)) {
                starts.push_back(t);
            } else if (inside.find(':') == std::string_view::npos) {
                return fail("lyrics: line {}: '[{}]' is neither a timestamp nor a tag", lineNumber,
                            std::string(inside));
            }
            // Anything else in brackets is an LRC metadata tag ([ti:], [ar:], [offset:]) and is
            // deliberately ignored rather than rejected: they are common and they are not lyrics.
            line = trim(line.substr(close + 1));
        }
        if (starts.empty()) {
            continue; // a line with no timestamp is a comment or a header
        }
        const std::string text(line);
        if (text.empty()) {
            continue; // a timestamp with no words is a spacer, and drawing an empty layer is worse
        }
        for (const double start : starts) {
            out.push_back(LyricLine{.startSeconds = start, .endSeconds = 0.0, .text = text});
        }
    }
    if (out.empty()) {
        return fail("lyrics: no timed lines found (expected LRC '[mm:ss.xx] words')");
    }
    return out;
}

namespace {

// SRT and WebVTT differ in three things: a leading index, the decimal separator, and a header. The
// block structure is identical, so one reader serves both.
Result<std::vector<LyricLine>> parseCueBlocks(std::string_view source, bool skipHeader,
                                              const char* what) {
    std::vector<LyricLine> out;
    const std::vector<std::string_view> lines = splitLines(source);
    std::size_t i = 0;
    if (skipHeader) {
        while (i < lines.size() && trim(lines[i]).empty()) {
            ++i;
        }
        if (i < lines.size() && trim(lines[i]).starts_with("WEBVTT")) {
            ++i;
        }
    }
    while (i < lines.size()) {
        while (i < lines.size() && trim(lines[i]).empty()) {
            ++i;
        }
        if (i >= lines.size()) {
            break;
        }
        // NOTE and STYLE blocks in WebVTT: skip to the next blank line.
        const std::string_view first = trim(lines[i]);
        if (first.starts_with("NOTE") || first.starts_with("STYLE") || first.starts_with("REGION")) {
            while (i < lines.size() && !trim(lines[i]).empty()) {
                ++i;
            }
            continue;
        }
        double from = 0.0;
        double to = 0.0;
        if (!parseRange(first, from, to)) {
            // Then this line is an index (SRT) or a cue identifier (WebVTT), and the next is the
            // range. Anything else is a file that is not what it claims to be.
            ++i;
            if (i >= lines.size() || !parseRange(trim(lines[i]), from, to)) {
                return fail("{}: line {}: expected a '00:00:00,000 --> 00:00:00,000' range", what,
                            i + 1);
            }
        }
        ++i;
        std::string text;
        while (i < lines.size() && !trim(lines[i]).empty()) {
            if (!text.empty()) {
                text += '\n';
            }
            text += std::string(trim(lines[i]));
            ++i;
        }
        if (!text.empty()) {
            out.push_back(LyricLine{.startSeconds = from, .endSeconds = to, .text = std::move(text)});
        }
    }
    if (out.empty()) {
        return fail("{}: no cues found", what);
    }
    return out;
}

} // namespace

Result<std::vector<LyricLine>> parseSrt(std::string_view source) {
    return parseCueBlocks(source, /*skipHeader=*/false, "lyrics (srt)");
}

Result<std::vector<LyricLine>> parseWebVtt(std::string_view source) {
    return parseCueBlocks(source, /*skipHeader=*/true, "lyrics (vtt)");
}

Result<LyricFormat> sniffLyrics(std::string_view source) {
    const std::string_view head = source.substr(0, std::min<std::size_t>(source.size(), 4096));
    if (trim(head).starts_with("WEBVTT")) {
        return LyricFormat::WebVtt;
    }
    if (head.find("-->") != std::string_view::npos) {
        return LyricFormat::Srt;
    }
    // An LRC timestamp is a '[' followed by digits and a colon, which no subtitle format has.
    for (std::size_t i = 0; i + 3 < head.size(); ++i) {
        if (head[i] == '[' && std::isdigit(static_cast<unsigned char>(head[i + 1])) != 0) {
            return LyricFormat::Lrc;
        }
    }
    return fail("lyrics: this is not LRC, SRT or WebVTT (no '[mm:ss]' and no '-->')");
}

Result<std::vector<LyricLine>> parseLyrics(std::string_view source) {
    auto format = sniffLyrics(source);
    if (!format) {
        return std::unexpected(format.error());
    }
    switch (*format) {
    case LyricFormat::Lrc:
        return parseLrc(source);
    case LyricFormat::Srt:
        return parseSrt(source);
    case LyricFormat::WebVtt:
        return parseWebVtt(source);
    }
    return fail("lyrics: unreachable");
}

Result<std::vector<LyricLine>> loadLyrics(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail("cannot open '{}'", path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();
    auto lines = parseLyrics(source);
    if (!lines) {
        return fail("'{}': {}", path.filename().string(), lines.error().message);
    }
    return lines;
}

std::vector<OverlayCue> lyricCues(std::span<const LyricLine> lines, const LyricImport& options) {
    std::vector<LyricLine> closed(lines.begin(), lines.end());
    closeEnds(closed, options.defaultHold, options.gap, options.minimumSeconds);

    std::vector<OverlayCue> cues;
    cues.reserve(closed.size());
    for (std::size_t i = 0; i < closed.size(); ++i) {
        OverlayCue cue;
        cue.id = fmt::format("{}{:03}", options.idPrefix, i + 1);
        cue.kind = OverlayKind::Text;
        cue.content = closed[i].text;
        cue.style = options.style;
        cue.startSeconds = closed[i].startSeconds + options.offsetSeconds;
        cue.endSeconds = closed[i].endSeconds + options.offsetSeconds;
        cue.anchor = options.anchor;
        cue.color = options.color;
        cue.preset = options.preset;
        // A preset that takes longer than half the cue would never reach full opacity.
        cue.presetSeconds = std::min(options.presetSeconds, cue.durationSeconds() * 0.45);
        cue.order = options.order;
        cues.push_back(std::move(cue));
    }
    return cues;
}

} // namespace avgen::seq
