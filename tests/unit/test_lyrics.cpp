// Timed text import (ADR-089, spec 17/24/56): LRC, SRT and WebVTT into overlay cues.
//
// Lyrics are the one part of a music video nobody types twice, and the three formats they arrive in
// differ only in punctuation. The interesting cases are all about *ends*: LRC has none, SRT and
// WebVTT have them and sometimes lie, and a lyric with no end is a lyric that stays on screen for
// the rest of the song.

#include "seq/lyrics.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace avgen;
using Catch::Approx;

TEST_CASE("LRC lines become cues that end when the next one begins", "[lyrics]") {
    const std::string source =
        "[ti:Night Walk]\n"
        "[ar:Nobody]\n"
        "[00:12.40]I'M WALKING\n"
        "[00:15.80]THROUGH THE CITY\n"
        "[00:19.20]TONIGHT\n";
    const auto lines = seq::parseLrc(source);
    REQUIRE(lines.has_value());
    REQUIRE(lines->size() == 3);
    CHECK((*lines)[0].startSeconds == Approx(12.40));
    CHECK((*lines)[0].text == "I'M WALKING");
    CHECK((*lines)[2].startSeconds == Approx(19.20));

    seq::LyricImport options;
    options.gap = 0.1;
    options.defaultHold = 4.0;
    const auto cues = seq::lyricCues(*lines, options);
    REQUIRE(cues.size() == 3);
    CHECK(cues[0].startSeconds == Approx(12.40));
    CHECK(cues[0].endSeconds == Approx(15.70)); // up to the next line, less the gap
    CHECK(cues[1].endSeconds == Approx(19.10));
    CHECK(cues[2].endSeconds == Approx(23.20)); // the last one holds
    CHECK(cues[0].content == "I'M WALKING");
    CHECK(cues[0].kind == seq::OverlayKind::Text);
    // Ids are stable and ordered, so a re-import replaces rather than doubles.
    CHECK(cues[0].id == "lyric001");
    CHECK(cues[2].id == "lyric003");
    for (const seq::OverlayCue& cue : cues) {
        INFO(cue.id);
        CHECK(cue.validate().has_value());
    }
}

TEST_CASE("an LRC line with several timestamps is a repeated chorus", "[lyrics]") {
    const auto lines = seq::parseLrc("[00:20.00][01:05.50]SAY IT AGAIN\n");
    REQUIRE(lines.has_value());
    REQUIRE(lines->size() == 2);
    CHECK((*lines)[0].startSeconds == Approx(20.0));
    CHECK((*lines)[1].startSeconds == Approx(65.5));
    CHECK((*lines)[1].text == "SAY IT AGAIN");
}

TEST_CASE("SRT cues keep the end times they carry", "[lyrics]") {
    const std::string source =
        "1\n"
        "00:00:12,400 --> 00:00:15,800\n"
        "I'M WALKING\n"
        "\n"
        "2\n"
        "00:00:15,800 --> 00:00:19,200\n"
        "THROUGH THE CITY\n"
        "and it is quiet\n"
        "\n";
    const auto lines = seq::parseSrt(source);
    REQUIRE(lines.has_value());
    REQUIRE(lines->size() == 2);
    CHECK((*lines)[0].startSeconds == Approx(12.4));
    CHECK((*lines)[0].endSeconds == Approx(15.8));
    CHECK((*lines)[1].text == "THROUGH THE CITY\nand it is quiet");

    const auto cues = seq::lyricCues(*lines);
    CHECK(cues[0].endSeconds == Approx(15.8)); // not recomputed: the file said so
}

TEST_CASE("WebVTT skips its header and its notes", "[lyrics]") {
    const std::string source =
        "WEBVTT\n"
        "\n"
        "NOTE this is not a lyric\n"
        "and neither is this\n"
        "\n"
        "intro\n"
        "00:00:03.000 --> 00:00:06.500 line:90% align:center\n"
        "FIRST LIGHT\n"
        "\n"
        "00:01:02.250 --> 00:01:05.000\n"
        "AND THEN\n";
    const auto lines = seq::parseWebVtt(source);
    REQUIRE(lines.has_value());
    REQUIRE(lines->size() == 2);
    CHECK((*lines)[0].startSeconds == Approx(3.0));
    CHECK((*lines)[0].endSeconds == Approx(6.5));
    CHECK((*lines)[0].text == "FIRST LIGHT");
    CHECK((*lines)[1].startSeconds == Approx(62.25));
}

TEST_CASE("the format is decided by the content, not the file name", "[lyrics]") {
    CHECK(seq::sniffLyrics("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nhi\n").value() ==
          seq::LyricFormat::WebVtt);
    CHECK(seq::sniffLyrics("1\n00:00:01,000 --> 00:00:02,000\nhi\n").value() == seq::LyricFormat::Srt);
    CHECK(seq::sniffLyrics("[00:01.00]hi\n").value() == seq::LyricFormat::Lrc);
    // And a file that is none of them says so rather than producing nothing quietly.
    CHECK_FALSE(seq::sniffLyrics("just some words\nand more words\n").has_value());
    CHECK_FALSE(seq::parseLyrics("just some words\n").has_value());
}

TEST_CASE("a cue too short to read is given room", "[lyrics]") {
    const std::string source = "[00:10.00]ONE\n[00:10.05]TWO\n";
    const auto lines = seq::parseLrc(source);
    REQUIRE(lines.has_value());
    seq::LyricImport options;
    options.minimumSeconds = 0.5;
    const auto cues = seq::lyricCues(*lines, options);
    REQUIRE(cues.size() == 2);
    // The first would have lasted 50 ms, which is a flicker rather than a word.
    CHECK(cues[0].durationSeconds() >= 0.5);
    CHECK(cues[0].validate().has_value());
    // And the preset never takes longer than the cue it is easing.
    CHECK(cues[0].presetSeconds <= cues[0].durationSeconds() * 0.5);
}

TEST_CASE("an offset moves the whole import", "[lyrics]") {
    const auto lines = seq::parseLrc("[00:10.00]ONE\n[00:14.00]TWO\n");
    REQUIRE(lines.has_value());
    seq::LyricImport options;
    options.offsetSeconds = -1.25;
    const auto cues = seq::lyricCues(*lines, options);
    CHECK(cues[0].startSeconds == Approx(8.75));
    CHECK(cues[1].startSeconds == Approx(12.75));
}
