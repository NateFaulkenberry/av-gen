// Embedded Tempo through the engine (ADR-394).
//
// test_tempo_metadata.cpp proves the parser. This proves the four things a parser cannot:
//   * the tempo REACHES the transport, which is what reads it (ADR-387)
//   * a user override is never silently overwritten by an import
//   * an import never permanently locks the tempo
//   * the override and its provenance survive save -> load -> save (ADR-225 / ADR-350)
// and it measures the claim that seeding the estimator with a known BPM is worth doing.

#include "analysis/beat_tracker.hpp"
#include "app/engine.hpp"
#include "audio/tempo_metadata.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using avgen::audio::AudioTempo;
using avgen::audio::TempoProvenance;
using Catch::Approx;

namespace {

using Bytes = std::vector<std::uint8_t>;

void beU32(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 24));
    b.push_back(static_cast<std::uint8_t>(v >> 16));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}
void leU32(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v >> 16));
    b.push_back(static_cast<std::uint8_t>(v >> 24));
}
void leU16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void ascii(Bytes& b, std::string_view s) {
    b.insert(b.end(), s.begin(), s.end());
}
void syncsafe(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>((v >> 21) & 0x7Fu));
    b.push_back(static_cast<std::uint8_t>((v >> 14) & 0x7Fu));
    b.push_back(static_cast<std::uint8_t>((v >> 7) & 0x7Fu));
    b.push_back(static_cast<std::uint8_t>(v & 0x7Fu));
}

// An ID3v2.4 tag carrying one TBPM frame.
Bytes id3TbpmTag(std::string_view bpm) {
    Bytes body;
    body.push_back(0x03); // UTF-8
    ascii(body, bpm);
    body.push_back(0x00);

    Bytes frame;
    ascii(frame, "TBPM");
    syncsafe(frame, static_cast<std::uint32_t>(body.size()));
    frame.push_back(0);
    frame.push_back(0);
    frame.insert(frame.end(), body.begin(), body.end());

    Bytes tag;
    ascii(tag, "ID3");
    tag.push_back(4);
    tag.push_back(0);
    tag.push_back(0);
    syncsafe(tag, static_cast<std::uint32_t>(frame.size()));
    tag.insert(tag.end(), frame.begin(), frame.end());
    return tag;
}

// A real, decodable 16-bit PCM WAV of a click track at `bpm`, optionally carrying `tbpm` in an
// 'id3 ' chunk. Real audio matters here: the engine analyses whatever it loads, so a fixture of
// two silent frames would not exercise the seeding path at all.
std::filesystem::path writeClickWav(const std::filesystem::path& path, double bpm, double seconds,
                                    std::string_view tbpm) {
    constexpr std::uint32_t kRate = 44100;
    const auto frames = static_cast<std::size_t>(seconds * kRate);
    const std::vector<float> mono =
        avgen::testsupport::clickTrack(static_cast<float>(bpm), static_cast<float>(kRate), frames);

    Bytes pcm;
    pcm.reserve(mono.size() * 2);
    for (const float v : mono) {
        const auto s = static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
        leU16(pcm, static_cast<std::uint16_t>(s));
    }

    Bytes chunks;
    ascii(chunks, "fmt ");
    leU32(chunks, 16);
    leU16(chunks, 1); // PCM
    leU16(chunks, 1); // mono
    leU32(chunks, kRate);
    leU32(chunks, kRate * 2);
    leU16(chunks, 2);
    leU16(chunks, 16);
    if (!tbpm.empty()) {
        const Bytes tag = id3TbpmTag(tbpm);
        ascii(chunks, "id3 ");
        leU32(chunks, static_cast<std::uint32_t>(tag.size()));
        chunks.insert(chunks.end(), tag.begin(), tag.end());
        if (tag.size() % 2 != 0) {
            chunks.push_back(0);
        }
    }
    ascii(chunks, "data");
    leU32(chunks, static_cast<std::uint32_t>(pcm.size()));
    chunks.insert(chunks.end(), pcm.begin(), pcm.end());

    Bytes out;
    ascii(out, "RIFF");
    leU32(out, static_cast<std::uint32_t>(chunks.size() + 4));
    ascii(out, "WAVE");
    out.insert(out.end(), chunks.begin(), chunks.end());

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return path;
}

struct Fixture {
    std::filesystem::path dir;
    explicit Fixture(std::string_view name)
        : dir(avgen::testsupport::processTempDir() / std::string(name)) {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }
    ~Fixture() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

avgen::app::Engine makeEngine() {
    return avgen::app::Engine(avgen::app::EngineMode::Offline);
}

} // namespace

TEST_CASE("an imported file's embedded tempo reaches the transport", "[tempo][engine]") {
    // ADR-387: that the value was parsed and stored proves nothing. The transport is what the
    // bars/beats readout and beat stepping read, so the assertion is made there.
    Fixture fx("embedded-tempo-reach");
    const auto tagged = writeClickWav(fx.dir / "tagged.wav", 128.0, 6.0, "128");
    const auto plain = writeClickWav(fx.dir / "plain.wav", 128.0, 6.0, "");

    auto engine = makeEngine();
    REQUIRE(engine.loadAudio(tagged).has_value());

    const AudioTempo t = engine.tempo();
    REQUIRE(t.available);
    REQUIRE(t.source == TempoProvenance::EmbeddedMetadata);
    REQUIRE(t.bpm == Approx(128.0));
    REQUIRE(t.metadataKey == "TBPM");
    // The number the engine resolved is the number the transport carries.
    REQUIRE(engine.transport().tempoBpm() == Approx(128.0));
    REQUIRE(engine.transport().snapshot().tempoBpm == Approx(128.0));
    // And seconds-per-beat, the one derived quantity a BPM legitimately gives.
    REQUIRE(t.secondsPerBeat() == Approx(60.0 / 128.0));

    // The ADR-182 sibling: the same audio, same path, no tag. If this arm alone existed, a reader
    // that always returned nothing would pass.
    auto bare = makeEngine();
    REQUIRE(bare.loadAudio(plain).has_value());
    REQUIRE_FALSE(bare.embeddedTempo().available);
    REQUIRE(bare.tempo().source != TempoProvenance::EmbeddedMetadata);
}

TEST_CASE("an import never silently overwrites a user override", "[tempo][engine]") {
    Fixture fx("embedded-tempo-override");
    const auto tagged = writeClickWav(fx.dir / "tagged.wav", 128.0, 6.0, "128");

    auto engine = makeEngine();
    engine.setTempoOverride(130.0);
    REQUIRE(engine.tempo().bpm == Approx(130.0));

    // Import a file that has an authoritative BPM of its own.
    REQUIRE(engine.loadAudio(tagged).has_value());

    // The artist's number survives, and the file's is still known -- not discarded, just outranked.
    REQUIRE(engine.tempo().source == TempoProvenance::UserOverride);
    REQUIRE(engine.tempo().bpm == Approx(130.0));
    REQUIRE(engine.embeddedTempo().available);
    REQUIRE(engine.embeddedTempo().bpm == Approx(128.0));
    REQUIRE(engine.transport().tempoBpm() == Approx(130.0));
}

TEST_CASE("an embedded tempo never locks the project tempo", "[tempo][engine]") {
    // The other half of the previous assertion, and a separate one: an import that wins must not
    // become permanent. These are two different failures and both need an arm.
    Fixture fx("embedded-tempo-unlock");
    const auto tagged = writeClickWav(fx.dir / "tagged.wav", 128.0, 6.0, "128");

    auto engine = makeEngine();
    REQUIRE(engine.loadAudio(tagged).has_value());
    REQUIRE(engine.tempo().source == TempoProvenance::EmbeddedMetadata);
    REQUIRE(engine.tempo().bpm == Approx(128.0));

    // The artist changes it afterwards. This must work.
    engine.setTempoOverride(90.0);
    REQUIRE(engine.tempo().source == TempoProvenance::UserOverride);
    REQUIRE(engine.tempo().bpm == Approx(90.0));
    REQUIRE(engine.transport().tempoBpm() == Approx(90.0));

    // And changing their mind returns the project to the file's number rather than to nothing.
    engine.clearTempoOverride();
    REQUIRE(engine.tempo().source == TempoProvenance::EmbeddedMetadata);
    REQUIRE(engine.tempo().bpm == Approx(128.0));
    REQUIRE(engine.transport().tempoBpm() == Approx(128.0));
}

TEST_CASE("clearing the audio clears the tempo it brought, but not the override",
          "[tempo][engine]") {
    // A tempo attributed to a file that is no longer loaded, still outranking the analyzer for
    // whatever is loaded next. The override is a property of the project and must survive.
    Fixture fx("embedded-tempo-clear");
    const auto tagged = writeClickWav(fx.dir / "tagged.wav", 128.0, 6.0, "128");

    auto engine = makeEngine();
    REQUIRE(engine.loadAudio(tagged).has_value());
    REQUIRE(engine.embeddedTempo().available);

    REQUIRE(engine.setAudioClips({}).has_value());
    REQUIRE_FALSE(engine.embeddedTempo().available);
    REQUIRE_FALSE(engine.tempo().available);

    engine.setTempoOverride(112.0);
    REQUIRE(engine.loadAudio(tagged).has_value());
    REQUIRE(engine.setAudioClips({}).has_value());
    REQUIRE(engine.tempoOverride().available);
    REQUIRE(engine.tempo().bpm == Approx(112.0));
}

TEST_CASE("tempo precedence is override > embedded > detected > none", "[tempo][engine]") {
    Fixture fx("embedded-tempo-precedence");
    const auto tagged = writeClickWav(fx.dir / "tagged.wav", 128.0, 6.0, "128");
    const auto plain = writeClickWav(fx.dir / "plain.wav", 128.0, 6.0, "");

    // None: nothing loaded, nothing set.
    auto empty = makeEngine();
    REQUIRE_FALSE(empty.tempo().available);
    REQUIRE(empty.tempo().source == TempoProvenance::None);

    // Embedded beats detected: the same audio analysed either way, only the tag differs.
    auto withTag = makeEngine();
    REQUIRE(withTag.loadAudio(tagged).has_value());
    REQUIRE(withTag.tempo().source == TempoProvenance::EmbeddedMetadata);

    // Override beats embedded.
    withTag.setTempoOverride(101.0);
    REQUIRE(withTag.tempo().source == TempoProvenance::UserOverride);

    // A metadata tempo and an analysis tempo are never given the same provenance, whatever their
    // values -- that is the requirement the enum exists for.
    REQUIRE(TempoProvenance::EmbeddedMetadata != TempoProvenance::Detected);
    REQUIRE(withTag.embeddedTempo().source == TempoProvenance::EmbeddedMetadata);
}

TEST_CASE("an out-of-range tempo override is refused, a valid one is not", "[tempo][engine]") {
    auto engine = makeEngine();
    for (const double bad : {0.0, -1.0, -128.0, 10000.0, 1000.0,
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        engine.setTempoOverride(bad);
        INFO("bpm: " << bad);
        REQUIRE_FALSE(engine.tempoOverride().available);
    }
    engine.setTempoOverride(127.5);
    REQUIRE(engine.tempoOverride().available);
    REQUIRE(engine.tempoOverride().bpm == Approx(127.5)); // fractional, unrounded
}

TEST_CASE("a tempo override and its provenance survive save, load and save again",
          "[tempo][engine]") {
    // ADR-225 / ADR-350: a setting the application does not keep is not a setting. The second save
    // is the half that catches a reader whose value never reaches the writer.
    Fixture fx("embedded-tempo-roundtrip");
    const auto project = fx.dir / "p.avgen";

    {
        auto engine = makeEngine();
        engine.setTempoOverride(127.5);
        REQUIRE(engine.saveProject(project).has_value());
    }

    nlohmann::json first;
    {
        std::ifstream in(project);
        in >> first;
    }
    REQUIRE(first["transport"]["tempo"]["bpm"].get<double>() == Approx(127.5));
    REQUIRE(first["transport"]["tempo"]["source"].get<std::string>() == "user");

    auto engine = makeEngine();
    REQUIRE(engine.loadProject(project).has_value());
    REQUIRE(engine.tempoOverride().available);
    REQUIRE(engine.tempoOverride().bpm == Approx(127.5));
    REQUIRE(engine.tempoOverride().source == TempoProvenance::UserOverride);
    // Loaded is not the same as reaching the transport.
    REQUIRE(engine.transport().tempoBpm() == Approx(127.5));

    // Save again from the loaded engine: the value must come back out, not be dropped by a writer
    // that only ever saw a freshly-set field.
    const auto again = fx.dir / "q.avgen";
    REQUIRE(engine.saveProject(again).has_value());
    nlohmann::json second;
    {
        std::ifstream in(again);
        in >> second;
    }
    REQUIRE(second["transport"]["tempo"]["bpm"].get<double>() == Approx(127.5));
    REQUIRE(second["transport"]["tempo"]["source"].get<std::string>() == "user");
}

TEST_CASE("a project with no override writes no tempo, and loading one clears it",
          "[tempo][engine]") {
    Fixture fx("embedded-tempo-default");

    // Byte stability: an untouched project gains no key.
    const auto bare = fx.dir / "bare.avgen";
    {
        auto engine = makeEngine();
        REQUIRE(engine.saveProject(bare).has_value());
    }
    nlohmann::json doc;
    {
        std::ifstream in(bare);
        in >> doc;
    }
    const bool hasTempoKey = doc.contains("transport") && doc["transport"].contains("tempo");
    REQUIRE_FALSE(hasTempoKey);

    // And a project with no tempo block must clear an override from whatever was open before,
    // rather than letting another piece's tempo quietly govern this one.
    auto engine = makeEngine();
    engine.setTempoOverride(140.0);
    REQUIRE(engine.tempoOverride().available);
    REQUIRE(engine.loadProject(bare).has_value());
    REQUIRE_FALSE(engine.tempoOverride().available);
}

TEST_CASE("a detected tempo is not persisted as if it were a decision", "[tempo][engine]") {
    // Saving the analyzer's estimate would make a measurement look like something a person chose,
    // and would then outrank a re-analysis of replaced audio. Only the override is written.
    Fixture fx("embedded-tempo-nodetect");
    const auto tagged = writeClickWav(fx.dir / "tagged.wav", 128.0, 6.0, "128");
    const auto project = fx.dir / "p.avgen";

    auto engine = makeEngine();
    REQUIRE(engine.loadAudio(tagged).has_value());
    REQUIRE(engine.tempo().source == TempoProvenance::EmbeddedMetadata);
    REQUIRE(engine.saveProject(project).has_value());

    nlohmann::json doc;
    {
        std::ifstream in(project);
        in >> doc;
    }
    // The tempo is in the audio file; the project does not keep a stale copy of it.
    const bool hasTempoKey = doc.contains("transport") && doc["transport"].contains("tempo");
    REQUIRE_FALSE(hasTempoKey);
}

// ---- the analysis tension, measured -------------------------------------------------------------

TEST_CASE("the beat grid still exists when the tempo came from a tag", "[tempo][engine]") {
    // The regression the feature could most easily cause: skipping analysis because a tag supplied
    // a number would leave every beat-driven route with nothing, and would pass every other test
    // here. So: with a tag present, beats must still be found.
    Fixture fx("embedded-tempo-grid");
    const auto tagged = writeClickWav(fx.dir / "tagged.wav", 128.0, 8.0, "128");

    auto engine = makeEngine();
    REQUIRE(engine.loadAudio(tagged).has_value());
    REQUIRE(engine.tempo().source == TempoProvenance::EmbeddedMetadata);

    const auto track = engine.track();
    REQUIRE(track != nullptr);
    REQUIRE_FALSE(track->frames().empty());
    // A grid, not just a number: several beats, at times a BPM alone could not have supplied.
    REQUIRE(track->beats().beatTimes.size() > 4);
    REQUIRE(track->beats().tempoBpm > 0.0f);
}

TEST_CASE("seeding the tempogram with a known BPM removes the octave error", "[tempo][engine]") {
    // The measurement behind the decision to seed rather than skip.
    //
    // The estimator's failure mode is the octave error: a 64 BPM pulse and a 128 BPM pulse share
    // most of their autocorrelation structure, and the default prior (centred on 120, one octave
    // wide) is what breaks the tie when nothing else can. A tag can break it exactly.
    using avgen::analysis::estimateTempo;
    constexpr float kHop = 512.0f / 48000.0f;

    // An onset envelope whose true period is 172 BPM but which has a strong sub-harmonic at 86 --
    // the shape of a track with a half-time feel, and the case the default prior gets wrong
    // because 86 sits much closer to its centre of 120 than 172 does.
    const auto envelope = [&](double bpm, std::size_t hops) {
        std::vector<float> e(hops, 0.02f);
        const double period = 60.0 / bpm / static_cast<double>(kHop);
        for (std::size_t i = 0; i < hops; ++i) {
            const double beat = static_cast<double>(i) / period;
            const double frac = beat - std::floor(beat);
            if (frac < 0.12) {
                // Every other beat louder: a half-time accent, which is what feeds the octave error.
                e[i] = (static_cast<int>(std::floor(beat)) % 2 == 0) ? 1.0f : 0.45f;
            }
        }
        return e;
    };
    const std::vector<float> onsets = envelope(172.0, 900);

    avgen::analysis::BeatTrackerConfig plain; // preferredBpm 120, width 1.0 octave
    const auto [unseededBpm, unseededConf] = estimateTempo(onsets, kHop, plain);

    avgen::analysis::BeatTrackerConfig seeded = plain;
    seeded.preferredBpm = 172.0f; // what a TBPM of 172 supplies
    seeded.priorWidthOctaves = avgen::audio::kSeededPriorWidthOctaves;
    const auto [seededBpm, seededConf] = estimateTempo(onsets, kHop, seeded);

    INFO("unseeded " << unseededBpm << " bpm (confidence " << unseededConf << "), seeded "
                     << seededBpm << " bpm (confidence " << seededConf << "), true 172");
    // The seeded estimate lands on the true tempo.
    REQUIRE(seededBpm == Approx(172.0).margin(3.0));
    // And it is at least as close as the unseeded one -- the claim being made, stated as an
    // inequality rather than an assumption about which octave the default prior picks.
    REQUIRE(std::abs(seededBpm - 172.0f) <= std::abs(unseededBpm - 172.0f));

    // Seeding constrains; it does not dictate. A tag that is simply wrong must not be echoed back
    // as if the audio agreed: with a 300 BPM tag on the same 172 BPM material the estimate stays
    // in the plausible range rather than becoming the tag.
    avgen::analysis::BeatTrackerConfig wrong = plain;
    wrong.preferredBpm = 300.0f;
    wrong.priorWidthOctaves = avgen::audio::kSeededPriorWidthOctaves;
    const auto [wrongBpm, wrongConf] = estimateTempo(onsets, kHop, wrong);
    INFO("mis-seeded estimate " << wrongBpm << " bpm (confidence " << wrongConf << ")");
    REQUIRE(wrongBpm <= plain.maxBpm);
    REQUIRE(wrongBpm > 0.0f);
}
