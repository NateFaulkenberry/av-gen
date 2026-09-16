// The audio arrangement (ADR-103): several files placed on one timeline, mixed to the single buffer
// every other part of the audio path already consumes.
//
// The load-bearing test in this file is the first one. Routing the engine's existing single-file
// path through the mixer is only safe if a one-clip arrangement is *bit-identical* to the file, and
// nothing downstream -- the player, the analyzer, the waveform, the transport -- can tell the
// difference. Everything else here is placement arithmetic.

#include "audio/arrangement.hpp"
#include "audio/audio_file.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>

using namespace avgen;
using audio::AudioClip;
using audio::ClipSources;
using Catch::Matchers::WithinAbs;

namespace fs = std::filesystem;

namespace {

struct Scratch {
    fs::path dir;
    explicit Scratch(const char* name) {
        dir = fs::temp_directory_path() / (std::string("avgen_arrangement_") + name);
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // A file whose samples are a known ramp, so a mix can be checked sample by sample rather than by
    // its length: "the clip landed at 2.0 s" is a claim a length cannot support.
    fs::path ramp(const char* name, std::size_t frames, std::uint32_t rate = 48000,
                  std::uint32_t channels = 2, float scale = 1.0f) {
        std::vector<float> interleaved(frames * channels);
        for (std::size_t f = 0; f < frames; ++f) {
            for (std::uint32_t c = 0; c < channels; ++c) {
                interleaved[f * channels + c] = scale * (static_cast<float>(f) + static_cast<float>(c) * 0.5f);
            }
        }
        auto file = audio::AudioFile::fromInterleaved(std::move(interleaved), channels, rate);
        const fs::path out = dir / name;
        REQUIRE(file.writeWav(out).has_value());
        return out;
    }
};

audio::AudioFile mix(const std::vector<AudioClip>& clips, ClipSources& sources,
                     audio::MixReport* report = nullptr) {
    const auto failures = sources.sync(clips);
    if (!failures.empty()) {
        FAIL(failures.front());
    }
    auto mixed = audio::mixArrangement(clips, sources, report);
    REQUIRE(mixed.has_value());
    return std::move(*mixed);
}

} // namespace

TEST_CASE("One clip mixes to exactly the file it names", "[audio][arrangement]") {
    // The property the whole design rests on. If this ever fails, routing the engine's single-file
    // path through the mixer silently changes every project that has one audio file.
    Scratch scratch("identity");
    const fs::path wav = scratch.ramp("one.wav", 5000);
    const auto source = audio::AudioFile::load(wav);
    REQUIRE(source.has_value());

    ClipSources sources;
    const audio::AudioFile mixed = mix({AudioClip{.file = wav}}, sources);

    REQUIRE(mixed.sampleRate() == source->sampleRate());
    REQUIRE(mixed.channels() == source->channels());
    REQUIRE(mixed.frameCount() == source->frameCount());
    const auto a = source->interleaved();
    const auto b = mixed.interleaved();
    REQUIRE(a.size() == b.size());
    std::size_t differences = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        differences += a[i] == b[i] ? 0 : 1;
    }
    CHECK(differences == 0); // bit-identical, not merely close

    // The negative control for the comparison itself: a gain of 0.5 must be visible to it, or the
    // check above proves nothing.
    const audio::AudioFile quieter = mix({AudioClip{.file = wav, .gain = 0.5f}}, sources);
    std::size_t quieterDifferences = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        quieterDifferences += a[i] == quieter.interleaved()[i] ? 0 : 1;
    }
    CHECK(quieterDifferences > a.size() / 2);
}

TEST_CASE("Clips land where the timeline says, with the gaps between them", "[audio][arrangement]") {
    Scratch scratch("placement");
    constexpr std::uint32_t rate = 48000;
    const fs::path first = scratch.ramp("a.wav", rate, rate, 1, 1.0f);  // one second, 0..47999
    const fs::path second = scratch.ramp("b.wav", rate, rate, 1, -1.0f); // one second, 0..-47999

    ClipSources sources;
    audio::MixReport report;
    // A one-second gap between them: a piece with a silence in the middle is the thing a single
    // file cannot express at all.
    const audio::AudioFile mixed = mix({AudioClip{.file = first, .startSeconds = 0.0},
                                        AudioClip{.file = second, .startSeconds = 2.0}},
                                       sources, &report);
    CHECK(report.clipsMixed == 2);
    CHECK_THAT(report.durationSeconds, WithinAbs(3.0, 1e-6));
    REQUIRE(mixed.frameCount() == 3 * rate);

    const auto s = mixed.interleaved();
    CHECK(s[0] == 0.0f);
    CHECK(s[100] == 100.0f);                    // inside the first clip
    CHECK(s[rate - 1] == static_cast<float>(rate - 1));
    CHECK(s[rate] == 0.0f);                     // the gap
    CHECK(s[rate + 1000] == 0.0f);
    CHECK(s[2 * rate] == 0.0f);                 // the second clip's first sample is its own 0
    CHECK(s[2 * rate + 100] == -100.0f);        // and it is the second file, not the first
}

TEST_CASE("Overlapping clips sum", "[audio][arrangement]") {
    Scratch scratch("overlap");
    constexpr std::uint32_t rate = 48000;
    const fs::path a = scratch.ramp("a.wav", rate, rate, 1, 1.0f);
    const fs::path b = scratch.ramp("b.wav", rate, rate, 1, 1.0f);

    ClipSources sources;
    const audio::AudioFile mixed =
        mix({AudioClip{.file = a, .startSeconds = 0.0}, AudioClip{.file = b, .startSeconds = 0.5}},
            sources);
    const auto s = mixed.interleaved();
    const std::size_t half = rate / 2;
    CHECK(s[100] == 100.0f);                       // only the first clip
    CHECK(s[half + 100] == static_cast<float>(half + 100) + 100.0f); // both, summed
    CHECK_THAT(static_cast<double>(s[rate + 100]), WithinAbs(static_cast<double>(half + 100), 1.0));
}

TEST_CASE("A clip plays the part of the file it was trimmed to", "[audio][arrangement]") {
    Scratch scratch("trim");
    constexpr std::uint32_t rate = 48000;
    const fs::path wav = scratch.ramp("a.wav", 4 * rate, rate, 1, 1.0f); // four seconds

    ClipSources sources;
    // From one second in, for one second, placed at ten seconds.
    const audio::AudioFile mixed = mix(
        {AudioClip{.file = wav, .startSeconds = 10.0, .inSeconds = 1.0, .durationSeconds = 1.0}}, sources);
    REQUIRE(mixed.frameCount() == 11 * rate);
    const auto s = mixed.interleaved();
    CHECK(s[10 * rate] == static_cast<float>(rate));           // the file's second 1, not its second 0
    CHECK(s[10 * rate + 100] == static_cast<float>(rate + 100));
    CHECK(s[11 * rate - 1] == static_cast<float>(2 * rate - 1)); // and it stops after one second
}

TEST_CASE("A trim past the end of the file plays what there is", "[audio][arrangement]") {
    Scratch scratch("overtrim");
    constexpr std::uint32_t rate = 48000;
    const fs::path wav = scratch.ramp("a.wav", rate, rate, 1, 1.0f); // one second

    ClipSources sources;
    // Asked for ten seconds from a one-second file. It plays one, rather than padding nine seconds
    // of silence into the middle of the piece.
    audio::MixReport report;
    const audio::AudioFile mixed =
        mix({AudioClip{.file = wav, .durationSeconds = 10.0}}, sources, &report);
    CHECK(mixed.frameCount() == rate);
    CHECK_THAT(report.durationSeconds, WithinAbs(1.0, 1e-6));

    // An in-point past the end plays nothing at all, and says so rather than mixing silence.
    audio::MixReport second;
    const audio::AudioFile empty = mix({AudioClip{.file = wav, .inSeconds = 5.0}}, sources, &second);
    CHECK(second.clipsMixed == 0);
    CHECK(second.clipsSkipped == 1);
    CHECK_FALSE(second.warnings.empty());
    CHECK(empty.frameCount() == 0);
}

TEST_CASE("Fades are applied at the clip's own edges", "[audio][arrangement]") {
    Scratch scratch("fades");
    constexpr std::uint32_t rate = 48000;
    // A constant file, so a fade is the only thing that can change a sample.
    std::vector<float> flat(rate, 1.0f);
    auto file = audio::AudioFile::fromInterleaved(std::move(flat), 1, rate);
    const fs::path wav = scratch.dir / "flat.wav";
    REQUIRE(file.writeWav(wav).has_value());

    ClipSources sources;
    const audio::AudioFile mixed =
        mix({AudioClip{.file = wav, .fadeInSeconds = 0.1, .fadeOutSeconds = 0.1}}, sources);
    const auto s = mixed.interleaved();
    CHECK(s[0] == 0.0f);
    CHECK_THAT(static_cast<double>(s[rate / 20]), WithinAbs(0.5, 0.02));  // halfway up the fade in
    CHECK_THAT(static_cast<double>(s[rate / 2]), WithinAbs(1.0, 1e-6));   // full in the middle
    CHECK_THAT(static_cast<double>(s[rate - rate / 20]), WithinAbs(0.5, 0.02));
    CHECK(s[rate - 1] < 0.05f);

    // The control: with no fades the same file is flat all the way through, which is what makes the
    // default safe for the identity property.
    const audio::AudioFile plain = mix({AudioClip{.file = wav}}, sources);
    CHECK(plain.interleaved()[0] == 1.0f);
    CHECK(plain.interleaved()[rate - 1] == 1.0f);
}

TEST_CASE("A disabled clip is not in the mix", "[audio][arrangement]") {
    Scratch scratch("disabled");
    constexpr std::uint32_t rate = 48000;
    const fs::path wav = scratch.ramp("a.wav", rate, rate, 1, 1.0f);

    ClipSources sources;
    audio::MixReport report;
    const audio::AudioFile mixed = mix({AudioClip{.file = wav, .enabled = false}}, sources, &report);
    CHECK(report.clipsMixed == 0);
    CHECK(mixed.frameCount() == 0);
}

TEST_CASE("Mixed sample rates resample, and say so", "[audio][arrangement]") {
    Scratch scratch("rates");
    const fs::path low = scratch.ramp("low.wav", 24000, 24000, 1, 1.0f);  // one second at 24 kHz
    const fs::path high = scratch.ramp("high.wav", 48000, 48000, 1, 1.0f); // one second at 48 kHz

    ClipSources sources;
    audio::MixReport report;
    const audio::AudioFile mixed =
        mix({AudioClip{.file = high}, AudioClip{.file = low, .startSeconds = 1.0}}, sources, &report);
    // The output takes the *highest* rate, so the clip that already matches is copied untouched and
    // only the odd one out is interpolated.
    CHECK(report.sampleRate == 48000);
    CHECK(mixed.frameCount() == 96000);
    CHECK(report.warnings.size() == 1);
    CHECK(report.warnings.front().find("24000 Hz") != std::string::npos);

    // The resampled second is still a ramp over the same range, at half the slope.
    const auto s = mixed.interleaved();
    CHECK_THAT(static_cast<double>(s[48000 + 1000]), WithinAbs(500.0, 1.0));
    CHECK_THAT(static_cast<double>(s[95999]), WithinAbs(23999.0, 2.0));
}

TEST_CASE("A mono clip is spread across the arrangement's channels", "[audio][arrangement]") {
    Scratch scratch("channels");
    constexpr std::uint32_t rate = 48000;
    const fs::path stereo = scratch.ramp("stereo.wav", rate, rate, 2, 1.0f);
    const fs::path mono = scratch.ramp("mono.wav", rate, rate, 1, 1.0f);

    ClipSources sources;
    const audio::AudioFile mixed =
        mix({AudioClip{.file = stereo}, AudioClip{.file = mono, .startSeconds = 1.0}}, sources);
    REQUIRE(mixed.channels() == 2);
    const auto s = mixed.interleaved();
    // The mono clip is in both channels, not only the left -- otherwise a mono cue would arrive on
    // one side of the picture.
    CHECK(s[(rate + 100) * 2] == 100.0f);
    CHECK(s[(rate + 100) * 2 + 1] == 100.0f);
}

TEST_CASE("A missing file is reported and the rest of the piece still mixes", "[audio][arrangement]") {
    Scratch scratch("missing");
    constexpr std::uint32_t rate = 48000;
    const fs::path good = scratch.ramp("good.wav", rate, rate, 1, 1.0f);

    const std::vector<AudioClip> clips{AudioClip{.file = good},
                                       AudioClip{.file = scratch.dir / "gone.wav", .startSeconds = 2.0}};
    ClipSources sources;
    const auto failures = sources.sync(clips);
    CHECK(failures.size() == 1);
    CHECK(failures.front().find("gone.wav") != std::string::npos);

    audio::MixReport report;
    auto mixed = audio::mixArrangement(clips, sources, &report);
    REQUIRE(mixed.has_value()); // a missing file does not take the piece down with it
    CHECK(report.clipsMixed == 1);
    CHECK(report.clipsSkipped == 1);
    CHECK(mixed->frameCount() == rate);
}

TEST_CASE("Sources are loaded once and released when nothing names them", "[audio][arrangement]") {
    Scratch scratch("sources");
    const fs::path a = scratch.ramp("a.wav", 1000);
    const fs::path b = scratch.ramp("b.wav", 1000);

    ClipSources sources;
    // The same file twice is one decode: an arrangement that cuts between two parts of one take
    // must not hold two copies of it.
    CHECK(sources.sync(std::vector<AudioClip>{{.file = a}, {.file = a, .startSeconds = 1.0}}).empty());
    CHECK(sources.size() == 1);
    const auto held = sources.find(a);
    REQUIRE(held != nullptr);

    CHECK(sources.sync(std::vector<AudioClip>{{.file = a}, {.file = b}}).empty());
    CHECK(sources.size() == 2);
    CHECK(sources.find(a) == held); // the one already held was not decoded again

    CHECK(sources.sync(std::vector<AudioClip>{{.file = b}}).empty());
    CHECK(sources.size() == 1);
    CHECK(sources.find(a) == nullptr);
}

TEST_CASE("An absurd arrangement is refused rather than allocated", "[audio][arrangement]") {
    Scratch scratch("absurd");
    const fs::path wav = scratch.ramp("a.wav", 1000);
    ClipSources sources;
    const std::vector<AudioClip> clips{{.file = wav, .startSeconds = 1e9}};
    REQUIRE(sources.sync(clips).empty());
    const auto mixed = audio::mixArrangement(clips, sources);
    CHECK_FALSE(mixed.has_value()); // a hand-edited start time must not be the last allocation
}

TEST_CASE("Mixing a piece costs what a load costs", "[audio][arrangement][performance]") {
    // The price of the mixdown design: a pass over every sample whenever the arrangement is edited.
    // Worth measuring rather than assuming, because it happens on the UI thread when a clip is
    // dragged.
    Scratch scratch("cost");
    constexpr std::uint32_t rate = 48000;
    const fs::path wav = scratch.ramp("a.wav", 60 * rate, rate, 2, 1.0f); // one minute, stereo

    ClipSources sources;
    std::vector<AudioClip> clips;
    for (int i = 0; i < 6; ++i) { // six minutes of arrangement
        clips.push_back(AudioClip{.file = wav, .startSeconds = static_cast<double>(i) * 60.0});
    }
    REQUIRE(sources.sync(clips).empty());

    audio::MixReport report;
    const auto mixed = audio::mixArrangement(clips, sources, &report);
    REQUIRE(mixed.has_value());
    CHECK_THAT(report.durationSeconds, WithinAbs(360.0, 1e-6));
    WARN("six-minute stereo arrangement of six clips mixed in " << report.millis << " ms");
    // Generous, because this is a correctness guard against an accidental quadratic rather than a
    // performance target: a second here would mean something is wrong with the loop, not that the
    // machine is slow.
    CHECK(report.millis < 2000.0);
}
