// Embedded Tempo extraction (ADR-394).
//
// Every fixture here is a container synthesised byte by byte in the test. That is deliberate:
// committing binaries would give a handful of files nobody can vary, and the malformed cases
// ("banana", 0, -128, 10000, NaN) cannot be produced by any real tagger at all.
//
// ADR-182: every "no tempo found" arm has a sibling that differs only by the presence of the tag
// and DOES find one, through the same call. A probe that only ever returns nothing proves nothing.

#include "audio/audio_file.hpp"
#include "audio/tempo_metadata.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using avgen::audio::AudioTempo;
using avgen::audio::TempoMap;
using avgen::audio::TempoProvenance;
using Catch::Approx;

namespace {

using Bytes = std::vector<std::uint8_t>;

void appendAscii(Bytes& b, std::string_view s) {
    b.insert(b.end(), s.begin(), s.end());
}
void appendBeU32(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 24));
    b.push_back(static_cast<std::uint8_t>(v >> 16));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}
void appendLeU32(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v >> 16));
    b.push_back(static_cast<std::uint8_t>(v >> 24));
}
void appendBeU24(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 16));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}
void appendSyncsafe(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>((v >> 21) & 0x7Fu));
    b.push_back(static_cast<std::uint8_t>((v >> 14) & 0x7Fu));
    b.push_back(static_cast<std::uint8_t>((v >> 7) & 0x7Fu));
    b.push_back(static_cast<std::uint8_t>(v & 0x7Fu));
}

// One ID3v2 text frame. `payload` is the frame body *after* the encoding byte.
Bytes textFrame(int major, std::string_view id, Bytes payload, std::uint8_t encoding = 0x03) {
    Bytes body;
    body.push_back(encoding);
    body.insert(body.end(), payload.begin(), payload.end());
    Bytes out;
    appendAscii(out, id);
    if (major == 2) {
        appendBeU24(out, static_cast<std::uint32_t>(body.size()));
    } else if (major == 4) {
        appendSyncsafe(out, static_cast<std::uint32_t>(body.size()));
        out.push_back(0);
        out.push_back(0);
    } else {
        appendBeU32(out, static_cast<std::uint32_t>(body.size())); // v2.3: plain big-endian
        out.push_back(0);
        out.push_back(0);
    }
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

Bytes textFrame(int major, std::string_view id, std::string_view value, std::uint8_t encoding = 0x03) {
    Bytes payload(value.begin(), value.end());
    payload.push_back(0);
    return textFrame(major, id, payload, encoding);
}

// A complete ID3v2 tag wrapping already-built frames.
Bytes id3Tag(int major, Bytes frames, std::uint8_t flags = 0) {
    Bytes out;
    appendAscii(out, "ID3");
    out.push_back(static_cast<std::uint8_t>(major));
    out.push_back(0); // revision
    out.push_back(flags);
    appendSyncsafe(out, static_cast<std::uint32_t>(frames.size()));
    out.insert(out.end(), frames.begin(), frames.end());
    return out;
}

// An MP3-shaped file: an ID3v2 tag then a plausible MPEG frame header. The extractor never
// decodes, but a fixture that looks like the real thing is worth more than a bare tag.
Bytes mp3WithTbpm(std::string_view value, int major = 4) {
    const std::string_view id = (major == 2) ? "TBP" : "TBPM";
    Bytes tag = id3Tag(major, textFrame(major, id, value));
    tag.insert(tag.end(), {0xFFu, 0xFBu, 0x90u, 0xC0u});
    tag.resize(tag.size() + 64, 0);
    return tag;
}

Bytes mp3WithoutTbpm(int major = 4) {
    // Identical shape, a different frame. The ONLY difference from mp3WithTbpm is which frame.
    Bytes tag = id3Tag(major, textFrame(major, (major == 2) ? "TT2" : "TIT2", "a title"));
    tag.insert(tag.end(), {0xFFu, 0xFBu, 0x90u, 0xC0u});
    tag.resize(tag.size() + 64, 0);
    return tag;
}

// A FLAC file head: "fLaC", a STREAMINFO block, then a VORBIS_COMMENT block with the given
// entries. Byte layout checked against a real ffmpeg-produced FLAC.
Bytes flacWithComments(const std::vector<std::string>& entries) {
    Bytes out;
    appendAscii(out, "fLaC");
    out.push_back(0x00); // STREAMINFO, not last
    appendBeU24(out, 34);
    out.resize(out.size() + 34, 0);

    Bytes block;
    const std::string vendor = "avgen-test";
    appendLeU32(block, static_cast<std::uint32_t>(vendor.size()));
    appendAscii(block, vendor);
    appendLeU32(block, static_cast<std::uint32_t>(entries.size()));
    for (const std::string& e : entries) {
        appendLeU32(block, static_cast<std::uint32_t>(e.size()));
        appendAscii(block, e);
    }
    out.push_back(0x84); // last block, type 4 = VORBIS_COMMENT
    appendBeU24(out, static_cast<std::uint32_t>(block.size()));
    out.insert(out.end(), block.begin(), block.end());
    return out;
}

// A RIFF/WAVE head with fmt + data, optionally carrying an 'id3 ' chunk. `tag` empty means no
// chunk -- which is exactly the shape of the two WAVs actually shipped in assets/audio.
Bytes wav(Bytes tag, const char* chunkId = "id3 ") {
    Bytes chunks;
    appendAscii(chunks, "fmt ");
    appendLeU32(chunks, 16);
    Bytes fmt{0x01, 0x00, 0x02, 0x00, 0x44, 0xAC, 0x00, 0x00,
              0x10, 0xB1, 0x02, 0x00, 0x04, 0x00, 0x10, 0x00};
    chunks.insert(chunks.end(), fmt.begin(), fmt.end());
    if (!tag.empty()) {
        appendAscii(chunks, chunkId);
        appendLeU32(chunks, static_cast<std::uint32_t>(tag.size()));
        chunks.insert(chunks.end(), tag.begin(), tag.end());
        if (tag.size() % 2 != 0) {
            chunks.push_back(0); // word alignment
        }
    }
    appendAscii(chunks, "data");
    appendLeU32(chunks, 8);
    chunks.resize(chunks.size() + 8, 0);

    Bytes out;
    appendAscii(out, "RIFF");
    appendLeU32(out, static_cast<std::uint32_t>(chunks.size() + 4));
    appendAscii(out, "WAVE");
    out.insert(out.end(), chunks.begin(), chunks.end());
    return out;
}

Bytes aiff(Bytes tag) {
    Bytes chunks;
    appendAscii(chunks, "COMM");
    appendBeU32(chunks, 18);
    chunks.resize(chunks.size() + 18, 0);
    if (!tag.empty()) {
        appendAscii(chunks, "ID3 ");
        appendBeU32(chunks, static_cast<std::uint32_t>(tag.size()));
        chunks.insert(chunks.end(), tag.begin(), tag.end());
        if (tag.size() % 2 != 0) {
            chunks.push_back(0);
        }
    }
    appendAscii(chunks, "SSND");
    appendBeU32(chunks, 8);
    chunks.resize(chunks.size() + 8, 0);

    Bytes out;
    appendAscii(out, "FORM");
    appendBeU32(out, static_cast<std::uint32_t>(chunks.size() + 4));
    appendAscii(out, "AIFF");
    out.insert(out.end(), chunks.begin(), chunks.end());
    return out;
}

AudioTempo readBytes(const Bytes& b, std::string_view name = "fixture") {
    return avgen::audio::readEmbeddedTempo(b, name);
}

} // namespace

// ---- validation ---------------------------------------------------------------------------------

TEST_CASE("parseBpmString accepts the values a tagger really writes", "[tempo][metadata]") {
    using avgen::audio::parseBpmString;
    REQUIRE(parseBpmString("128").value() == Approx(128.0));
    REQUIRE(parseBpmString("90").value() == Approx(90.0));
    // Fractional BPM survives: not rounded, not truncated.
    REQUIRE(parseBpmString("127.5").value() == Approx(127.5));
    REQUIRE(parseBpmString("128.25").value() == Approx(128.25));
    REQUIRE(parseBpmString("174.333").value() == Approx(174.333));
    // Padding is what a tagger leaves behind, not a defect in the value.
    REQUIRE(parseBpmString("  128  ").value() == Approx(128.0));
    REQUIRE(parseBpmString("+128").value() == Approx(128.0));
    REQUIRE(parseBpmString("128.0").value() == Approx(128.0));
}

TEST_CASE("parseBpmString rejects every unusable value", "[tempo][metadata]") {
    using avgen::audio::parseBpmString;
    // Each of these must be rejected, and the case above proves the parser can still say yes --
    // otherwise "rejects everything" would pass this test.
    const char* bad[] = {
        "",        " ",     "\t\n",  "banana", "128bpm", "bpm128", "one hundred",
        "nan",     "NaN",   "inf",   "-inf",   "Infinity",
        "0",       "0.0",   "-1",    "-128",   "-127.5",
        "10000",   "1e9",   "99999", "1000",   // absurd: above kMaxPlausibleBpm
        "0.5",                                  // below kMinPlausibleBpm
        "1,5",     "1.2.3", "128 130", "1 28", "0x80", "++128", ".",
    };
    for (const char* s : bad) {
        INFO("value: '" << s << "'");
        REQUIRE_FALSE(parseBpmString(s).has_value());
    }
}

// ---- ID3v2 in MP3 --------------------------------------------------------------------------------

TEST_CASE("an MP3 with TBPM yields an embedded tempo and one without yields none", "[tempo][metadata]") {
    // The ADR-182 pair: the two fixtures differ only by which frame the tag carries.
    const AudioTempo with = readBytes(mp3WithTbpm("128"), "with.mp3");
    const AudioTempo without = readBytes(mp3WithoutTbpm(), "without.mp3");

    REQUIRE(with.available);
    REQUIRE(with.bpm == Approx(128.0));
    REQUIRE(with.source == TempoProvenance::EmbeddedMetadata);
    REQUIRE(with.metadataKey == "TBPM");
    REQUIRE(with.metadataFormat == "ID3v2.4");
    REQUIRE(with.confidence == Approx(1.0));
    REQUIRE(with.isEmbedded());

    REQUIRE_FALSE(without.available);
    REQUIRE(without.source == TempoProvenance::None);
    REQUIRE(without.metadataKey.empty());
    REQUIRE(without.metadataFormat.empty());
}

TEST_CASE("ID3v2.3 and v2.2 tags are read, not just v2.4", "[tempo][metadata]") {
    // v2.3 frame sizes are plain big-endian and v2.4's are synchsafe; a reader that assumes one
    // walks into the middle of a frame on the other. v2.2 uses 3-character frame IDs entirely.
    const AudioTempo v23 = readBytes(mp3WithTbpm("140", 3), "v23.mp3");
    REQUIRE(v23.available);
    REQUIRE(v23.bpm == Approx(140.0));
    REQUIRE(v23.metadataFormat == "ID3v2.3");
    REQUIRE(v23.metadataKey == "TBPM");

    const AudioTempo v22 = readBytes(mp3WithTbpm("85", 2), "v22.mp3");
    REQUIRE(v22.available);
    REQUIRE(v22.bpm == Approx(85.0));
    REQUIRE(v22.metadataFormat == "ID3v2.2");
    REQUIRE(v22.metadataKey == "TBP");

    REQUIRE_FALSE(readBytes(mp3WithoutTbpm(3), "v23-none.mp3").available);
    REQUIRE_FALSE(readBytes(mp3WithoutTbpm(2), "v22-none.mp3").available);
}

TEST_CASE("TBPM is found behind an earlier frame and behind unsynchronisation", "[tempo][metadata]") {
    // TBPM second: proves the frame walk actually walks rather than reading frame 0.
    Bytes frames = textFrame(4, "TIT2", "a title");
    Bytes tbpm = textFrame(4, "TBPM", "150");
    frames.insert(frames.end(), tbpm.begin(), tbpm.end());
    const AudioTempo second = readBytes(id3Tag(4, frames), "second.mp3");
    REQUIRE(second.available);
    REQUIRE(second.bpm == Approx(150.0));

    // Unsynchronised: a preceding frame whose body contains 0xFF 0x00 0x00. With the tag flagged
    // unsynchronised those bytes are stored as 0xFF 0x00 0x00 0x00, so a reader that skips the
    // de-unsynchronisation step computes every later offset one byte short and misses TBPM.
    Bytes payload{0xFFu, 0x00u, 0x00u, 0x41u, 0x00u};
    Bytes unsyncFrames = textFrame(4, "TIT2", payload);
    // Re-encode the frame body the way an unsynchronised tag stores it.
    Bytes stored;
    for (std::size_t i = 0; i < unsyncFrames.size(); ++i) {
        stored.push_back(unsyncFrames[i]);
        if (unsyncFrames[i] == 0xFFu) {
            stored.push_back(0x00u); // the inserted zero
        }
    }
    Bytes tbpm2 = textFrame(4, "TBPM", "96");
    stored.insert(stored.end(), tbpm2.begin(), tbpm2.end());
    const AudioTempo unsynced = readBytes(id3Tag(4, stored, 0x80u), "unsync.mp3");
    REQUIRE(unsynced.available);
    REQUIRE(unsynced.bpm == Approx(96.0));
}

TEST_CASE("a fractional TBPM keeps its fraction and still reports integer semantics",
          "[tempo][metadata]") {
    // ID3 specifies TBPM as an integer numerical string, but Mixed In Key and Traktor write
    // fractions into it. The value is kept exactly; the flag records what the spec promises, so a
    // round-trip and the UI can tell 128 from 127.5 without the parser having rounded either.
    const AudioTempo frac = readBytes(mp3WithTbpm("127.5"), "frac.mp3");
    REQUIRE(frac.available);
    REQUIRE(frac.bpm == Approx(127.5));
    REQUIRE(frac.integerSemantics);

    const AudioTempo whole = readBytes(mp3WithTbpm("128"), "whole.mp3");
    REQUIRE(whole.bpm == Approx(128.0));
    REQUIRE(whole.integerSemantics);
}

TEST_CASE("an invalid TBPM is ignored rather than failing the import", "[tempo][metadata]") {
    // The sibling above shows a valid TBPM in the same container yields a tempo, so "none" here
    // is the validation rejecting the value -- not the reader failing to find the frame.
    for (const char* bad : {"banana", "0", "-128", "10000", "", "NaN", "128bpm"}) {
        INFO("TBPM: '" << bad << "'");
        const AudioTempo t = readBytes(mp3WithTbpm(bad), "bad.mp3");
        REQUIRE_FALSE(t.available);
        REQUIRE(t.source == TempoProvenance::None);
        REQUIRE(t.bpm == Approx(0.0));
    }
    REQUIRE(readBytes(mp3WithTbpm("128"), "good.mp3").available);
}

// ---- FLAC ----------------------------------------------------------------------------------------

TEST_CASE("a FLAC Vorbis comment supplies BPM or TEMPO, and neither supplies none",
          "[tempo][metadata]") {
    const AudioTempo bpm = readBytes(flacWithComments({"ARTIST=x", "BPM=127.5"}), "a.flac");
    REQUIRE(bpm.available);
    REQUIRE(bpm.bpm == Approx(127.5)); // not rounded to 128
    REQUIRE(bpm.metadataKey == "BPM");
    REQUIRE(bpm.metadataFormat == "Vorbis comment (FLAC)");
    REQUIRE_FALSE(bpm.integerSemantics); // Vorbis comments are free text; a fraction is native

    const AudioTempo tempo = readBytes(flacWithComments({"TEMPO=90"}), "b.flac");
    REQUIRE(tempo.available);
    REQUIRE(tempo.bpm == Approx(90.0));
    REQUIRE(tempo.metadataKey == "TEMPO");

    // Vorbis keys are case-insensitive by the spec, and real files use every casing.
    const AudioTempo lower = readBytes(flacWithComments({"bpm=174"}), "c.flac");
    REQUIRE(lower.available);
    REQUIRE(lower.bpm == Approx(174.0));

    // Same container, comments present, just no tempo key.
    const AudioTempo none = readBytes(flacWithComments({"ARTIST=x", "TITLE=y"}), "d.flac");
    REQUIRE_FALSE(none.available);
    REQUIRE(none.source == TempoProvenance::None);
}

TEST_CASE("BPM wins over TEMPO when a FLAC carries both", "[tempo][metadata]") {
    // TEMPO is also used for prose ("allegro"); BPM is the numeric field, so it is preferred
    // whichever order they appear in.
    const AudioTempo a = readBytes(flacWithComments({"TEMPO=90", "BPM=128"}), "e.flac");
    REQUIRE(a.available);
    REQUIRE(a.bpm == Approx(128.0));
    REQUIRE(a.metadataKey == "BPM");

    const AudioTempo b = readBytes(flacWithComments({"BPM=128", "TEMPO=90"}), "f.flac");
    REQUIRE(b.bpm == Approx(128.0));
    REQUIRE(b.metadataKey == "BPM");

    // A non-numeric TEMPO with no BPM is simply ignored.
    REQUIRE_FALSE(readBytes(flacWithComments({"TEMPO=allegro"}), "g.flac").available);
}

// ---- WAV / AIFF ------------------------------------------------------------------------------------

TEST_CASE("a WAV carries tempo only in a genuine ID3 chunk", "[tempo][metadata]") {
    const AudioTempo tagged = readBytes(wav(id3Tag(4, textFrame(4, "TBPM", "128"))), "tagged.wav");
    REQUIRE(tagged.available);
    REQUIRE(tagged.bpm == Approx(128.0));
    REQUIRE(tagged.metadataKey == "TBPM");
    REQUIRE(tagged.metadataFormat == "ID3v2.4 (RIFF 'id3 ' chunk)");

    // Upper-case chunk id is equally common in the wild.
    const AudioTempo upper =
        readBytes(wav(id3Tag(4, textFrame(4, "TBPM", "100")), "ID3 "), "upper.wav");
    REQUIRE(upper.available);
    REQUIRE(upper.bpm == Approx(100.0));

    // A plain fmt+data WAV -- the shape of both files in assets/audio -- has no tempo, and no
    // filename or convention invents one.
    REQUIRE_FALSE(readBytes(wav({}), "MySong_128BPM.wav").available);
}

TEST_CASE("a filename is never metadata", "[tempo][metadata]") {
    // The brief's explicit non-goal, made a test so it cannot be quietly added later.
    for (const char* name : {"MySong_128BPM.wav", "track-174bpm.mp3", "90 BPM loop.flac"}) {
        INFO("name: " << name);
        REQUIRE_FALSE(readBytes(wav({}), name).available);
    }
    // And the same bytes with a real tag do yield one, so the above is not vacuous.
    REQUIRE(readBytes(wav(id3Tag(4, textFrame(4, "TBPM", "128"))), "untitled.wav").available);
}

TEST_CASE("a comment saying the tempo is not the tempo", "[tempo][metadata]") {
    // A COMM/comment field is prose. Only the dedicated numeric fields count.
    Bytes frames = textFrame(4, "COMM", "this track is 128 BPM");
    REQUIRE_FALSE(readBytes(id3Tag(4, frames), "prose.mp3").available);
    REQUIRE_FALSE(readBytes(flacWithComments({"COMMENT=this track is 128 BPM",
                                              "DESCRIPTION=128 bpm"}),
                            "prose.flac")
                      .available);
}

TEST_CASE("an AIFF carries tempo in its ID3 chunk", "[tempo][metadata]") {
    const AudioTempo tagged = readBytes(aiff(id3Tag(3, textFrame(3, "TBPM", "137"))), "tagged.aiff");
    REQUIRE(tagged.available);
    REQUIRE(tagged.bpm == Approx(137.0));
    REQUIRE(tagged.metadataFormat == "ID3v2.3 (AIFF 'ID3 ' chunk)");

    REQUIRE_FALSE(readBytes(aiff({}), "plain.aiff").available);
}

// ---- robustness ---------------------------------------------------------------------------------

TEST_CASE("truncated and corrupt containers yield no tempo and do not run away",
          "[tempo][metadata]") {
    // Every prefix of a valid tagged file must terminate. A length field read out of a truncated
    // buffer is the classic way a hand-written parser spins or reads past the end.
    const Bytes full = wav(id3Tag(4, textFrame(4, "TBPM", "128")));
    for (std::size_t n = 0; n < full.size(); ++n) {
        const Bytes prefix(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(n));
        static_cast<void>(readBytes(prefix, "truncated.wav")); // must simply return
    }
    REQUIRE(readBytes(full, "full.wav").available);

    const Bytes flac = flacWithComments({"BPM=128"});
    for (std::size_t n = 0; n < flac.size(); ++n) {
        static_cast<void>(
            readBytes(Bytes(flac.begin(), flac.begin() + static_cast<std::ptrdiff_t>(n)), "t.flac"));
    }
    REQUIRE(readBytes(flac, "full.flac").available);

    REQUIRE_FALSE(readBytes({}, "empty").available);
    REQUIRE_FALSE(readBytes(Bytes(64, 0x00u), "zeros").available);
    REQUIRE_FALSE(readBytes(Bytes(64, 0xFFu), "ones").available);
}

// ---- through a real file on disk ------------------------------------------------------------------

TEST_CASE("embedded tempo is read from a file on disk, not only from a buffer",
          "[tempo][metadata]") {
    // ADR-387: the span overload passing proves the parser; this proves the path callers use.
    const auto dir = avgen::testsupport::processTempDir() / "tempo-metadata";
    std::filesystem::create_directories(dir);

    const auto write = [&](const std::string& name, const Bytes& bytes) {
        const auto path = dir / name;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out.close();
        return path;
    };

    const auto tagged = write("tagged.wav", wav(id3Tag(4, textFrame(4, "TBPM", "128.25"))));
    const auto plain = write("plain.wav", wav({}));

    const auto a = avgen::audio::readEmbeddedTempo(tagged);
    REQUIRE(a.has_value());
    REQUIRE(a->available);
    REQUIRE(a->bpm == Approx(128.25));
    REQUIRE(a->metadataKey == "TBPM");

    const auto b = avgen::audio::readEmbeddedTempo(plain);
    REQUIRE(b.has_value());   // not an error
    REQUIRE_FALSE(b->available); // just no tempo

    // A file that is not there IS an error, unlike a file with no tempo.
    REQUIRE_FALSE(avgen::audio::readEmbeddedTempo(dir / "absent.wav").has_value());

    std::filesystem::remove_all(dir);
}

// ---- the capability boundary -----------------------------------------------------------------------

TEST_CASE("the containers with no reader are exactly the ones the engine cannot decode",
          "[tempo][metadata]") {
    // ADR-394's capability rule, made falsifiable. If miniaudio ever gains an MP4 or Ogg decoder
    // this test starts failing, which is the point: the gap must be revisited when it closes, not
    // discovered years later.
    const auto dir = avgen::testsupport::processTempDir() / "tempo-capability";
    std::filesystem::create_directories(dir);
    const auto write = [&](const std::string& name, const Bytes& bytes) {
        const auto path = dir / name;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        return path;
    };

    // A minimal MP4 carrying an iTunes 'tmpo' atom (128). Well formed enough that a tmpo reader
    // would find it -- so this asserts the *absence* of the reader, not a broken fixture.
    Bytes mp4;
    appendBeU32(mp4, 20);
    appendAscii(mp4, "ftyp");
    appendAscii(mp4, "M4A ");
    appendBeU32(mp4, 0);
    appendAscii(mp4, "M4A ");
    Bytes tmpo;
    appendBeU32(tmpo, 26);
    appendAscii(tmpo, "tmpo");
    appendBeU32(tmpo, 18);
    appendAscii(tmpo, "data");
    appendBeU32(tmpo, 21); // well-known type: integer
    appendBeU32(tmpo, 0);
    tmpo.push_back(0x00);
    tmpo.push_back(0x80); // 128
    mp4.insert(mp4.end(), tmpo.begin(), tmpo.end());
    const auto m4a = write("tagged.m4a", mp4);

    // No tempo is extracted...
    const auto tempo = avgen::audio::readEmbeddedTempo(m4a);
    REQUIRE(tempo.has_value());
    REQUIRE_FALSE(tempo->available);
    // ...and the reason that is acceptable: the engine cannot open the file at all, so no such
    // file can ever reach the importer. A reader here would have no way to be fed.
    REQUIRE_FALSE(avgen::audio::AudioFile::load(m4a).has_value());

    std::filesystem::remove_all(dir);
}

// ---- tempo map -------------------------------------------------------------------------------------

TEST_CASE("a single embedded BPM is one tempo event at zero", "[tempo][metadata]") {
    const AudioTempo t = readBytes(mp3WithTbpm("120"), "x.mp3");
    const TempoMap map = TempoMap::fromAudioTempo(t);
    REQUIRE(map.events.size() == 1);
    REQUIRE(map.events[0].timeSeconds == Approx(0.0));
    REQUIRE(map.events[0].bpm == Approx(120.0));
    REQUIRE(map.isConstant());
    REQUIRE(map.bpmAt(0.0) == Approx(120.0));
    REQUIRE(map.bpmAt(90.0) == Approx(120.0)); // constant: the same everywhere
    REQUIRE(map.secondsPerBeatAt(10.0) == Approx(0.5));

    // No tempo means an empty map, not a map of zero.
    const TempoMap none = TempoMap::fromAudioTempo(AudioTempo{});
    REQUIRE(none.empty());
    REQUIRE(none.bpmAt(0.0) == Approx(0.0));
}

TEST_CASE("a multi-event tempo map already answers per-position", "[tempo][metadata]") {
    // Nothing produces one yet. The type is exercised so that the day something does -- a DAW
    // export, ID3 SYTC, the user drawing one -- the lookup is not written from scratch under a
    // deadline, and callers that cannot cope can see isConstant() go false.
    TempoMap map;
    map.events = {{0.0, 120.0}, {30.0, 140.0}, {60.0, 90.0}};
    REQUIRE_FALSE(map.isConstant());
    REQUIRE(map.bpmAt(-1.0) == Approx(120.0));
    REQUIRE(map.bpmAt(0.0) == Approx(120.0));
    REQUIRE(map.bpmAt(29.9) == Approx(120.0));
    REQUIRE(map.bpmAt(30.0) == Approx(140.0));
    REQUIRE(map.bpmAt(59.0) == Approx(140.0));
    REQUIRE(map.bpmAt(1000.0) == Approx(90.0));
    REQUIRE(map.secondsPerBeatAt(30.0) == Approx(60.0 / 140.0));
}

TEST_CASE("an embedded BPM gives seconds per beat and nothing more", "[tempo][metadata]") {
    // The honest surface. AudioTempo has no phase, downbeat, bar, time-signature or swing field,
    // and this test exists so that adding one without a way to know it is noticed.
    const AudioTempo t = readBytes(mp3WithTbpm("128"), "y.mp3");
    REQUIRE(t.secondsPerBeat() == Approx(60.0 / 128.0));
    REQUIRE(t.beatsPerSecond() == Approx(128.0 / 60.0));
    REQUIRE(AudioTempo{}.secondsPerBeat() == Approx(0.0));
}

TEST_CASE("provenance tokens round-trip", "[tempo][metadata]") {
    using avgen::audio::tempoProvenanceFromToken;
    using avgen::audio::tempoProvenanceToken;
    for (const TempoProvenance p : {TempoProvenance::None, TempoProvenance::EmbeddedMetadata,
                                    TempoProvenance::Detected, TempoProvenance::UserOverride}) {
        REQUIRE(tempoProvenanceFromToken(tempoProvenanceToken(p)) == p);
    }
    REQUIRE(tempoProvenanceFromToken("nonsense") == TempoProvenance::None);
    // Metadata and analysis never share a token -- the whole point of the enum.
    REQUIRE(tempoProvenanceToken(TempoProvenance::EmbeddedMetadata) !=
            tempoProvenanceToken(TempoProvenance::Detected));
    REQUIRE(avgen::audio::tempoProvenanceName(TempoProvenance::EmbeddedMetadata) ==
            "Embedded Metadata");
}
