#pragma once

// Timed text, imported (ADR-089, spec 17/24/56).
//
// Lyrics arrive in three formats and nobody types them twice. LRC is what lyric sites hand out,
// SRT is what every subtitle tool writes, WebVTT is what the web uses; all three are a time and a
// line, which is exactly what an `OverlayCue` is. So this file is a parser and a projection, and it
// owns no state, no fonts and no layers.
//
// The one judgement it makes is about *ends*. SRT and WebVTT carry an end time; LRC does not -- it
// has only a stream of starts -- so a line runs until the next one begins, less a small gap so two
// lyrics never share a frame, and the last line holds for `defaultHold`. That is stated here rather
// than hidden because it is the difference between a lyric that reads and one that flickers.

#include "core/error.hpp"
#include "seq/layers.hpp"

#include <glm/glm.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::seq {

enum class LyricFormat : std::uint8_t { Lrc, Srt, WebVtt };
[[nodiscard]] const char* lyricFormatName(LyricFormat format);

struct LyricLine {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    std::string text; // may contain '\n' for a multi-line cue
};

// Each parser is strict about its own format and says which line it gave up on.
[[nodiscard]] Result<std::vector<LyricLine>> parseLrc(std::string_view source);
[[nodiscard]] Result<std::vector<LyricLine>> parseSrt(std::string_view source);
[[nodiscard]] Result<std::vector<LyricLine>> parseWebVtt(std::string_view source);

// Which format `source` is, decided by what is in it rather than by a file extension: people rename
// these files constantly, and a `.txt` full of `[00:12.40]` is an LRC whatever it is called.
[[nodiscard]] Result<LyricFormat> sniffLyrics(std::string_view source);
[[nodiscard]] Result<std::vector<LyricLine>> parseLyrics(std::string_view source);
[[nodiscard]] Result<std::vector<LyricLine>> loadLyrics(const std::filesystem::path& path);

// How imported lines become cues. Everything here is editable afterwards -- an import is a starting
// point, not a format.
struct LyricImport {
    std::string idPrefix = "lyric";
    std::string style = "lyric";
    glm::vec2 anchor{0.5f, 0.16f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    OverlayPreset preset = OverlayPreset::FadeInOut;
    double presetSeconds = 0.35;
    int order = 10;
    // Only used where the source carries no end time (LRC): how long the last line holds, and how
    // much silence to leave between one line and the next.
    double defaultHold = 3.0;
    double gap = 0.08;
    // Lines shorter than this are given `defaultHold` instead: a lyric flashed for a tenth of a
    // second is a flicker, not a word.
    double minimumSeconds = 0.4;
    double offsetSeconds = 0.0; // nudge the whole import, for a file cut against a different master
};

[[nodiscard]] std::vector<OverlayCue> lyricCues(std::span<const LyricLine> lines,
                                                const LyricImport& options = {});

} // namespace avgen::seq
