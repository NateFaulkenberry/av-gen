// Several audio files on one timeline, through the engine (ADR-103).
//
// `tests/unit/test_arrangement.cpp` checks the mix itself. This checks that the engine treats the
// mixdown as *the* audio -- the project's length, the analysis, the transport, the saved file --
// and that a project with one audio file behaves exactly as it did before any of this existed.

#include "app/engine.hpp"
#include "audio/arrangement.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace avgen;
using audio::AudioClip;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

namespace {

struct Scratch {
    fs::path dir;
    explicit Scratch(const char* name) {
        dir = fs::temp_directory_path() / (std::string("avgen_engine_audio_") + name);
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // A tone rather than silence: the analyser has to have something to find, or "the analysis
    // followed the arrangement" is a claim about two empty things agreeing.
    fs::path tone(const char* name, double seconds, float hz) {
        constexpr std::uint32_t rate = 48000;
        auto mono = testsupport::sine(hz, rate, static_cast<std::size_t>(rate * seconds));
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, rate);
        const fs::path out = dir / name;
        REQUIRE(file.writeWav(out).has_value());
        return out;
    }
};

} // namespace

TEST_CASE("A project made of several audio files is as long as all of them",
          "[integration][audio][arrangement]") {
    Scratch scratch("length");
    const fs::path a = scratch.tone("a.wav", 2.0, 220.0f);
    const fs::path b = scratch.tone("b.wav", 3.0, 440.0f);

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setAudioClips({AudioClip{.file = a, .startSeconds = 0.0},
                                  AudioClip{.file = b, .startSeconds = 5.0}})
                .has_value());

    CHECK(engine.audioMix().clipsMixed == 2);
    CHECK(engine.hasAudio());
    // Two seconds, a three-second gap, then three more: eight, not two and not five.
    CHECK_THAT(engine.audioDurationSeconds(), WithinAbs(8.0, 0.01));
    CHECK_THAT(engine.durationSeconds(), WithinAbs(8.0, 0.01));
    // And the transport can play all of it, which is the thing that could not be expressed before.
    CHECK_THAT(engine.transport().playEndSeconds(), WithinAbs(8.0, 0.01));
}

TEST_CASE("The analysis follows the arrangement, not one of its files",
          "[integration][audio][arrangement]") {
    Scratch scratch("analysis");
    const fs::path quiet = scratch.tone("quiet.wav", 2.0, 220.0f);
    const fs::path loud = scratch.tone("loud.wav", 2.0, 220.0f);

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setAudioClips({AudioClip{.file = quiet, .startSeconds = 0.0, .gain = 0.1f},
                                  AudioClip{.file = loud, .startSeconds = 4.0, .gain = 1.0f}})
                .has_value());
    REQUIRE(engine.track() != nullptr);

    const auto rmsAt = [&](double seconds) {
        FixedStepClock clock(30.0);
        clock.restartAt(seconds);
        engine.seekSeconds(seconds);
        const FrameTime time = engine.tick(clock);
        engine.update(time);
        REQUIRE(engine.hasFrame());
        return static_cast<double>(engine.latestFrame().rms);
    };

    const double first = rmsAt(1.0);
    const double gap = rmsAt(3.0);
    const double second = rmsAt(5.0);
    // The three sections of the arrangement are three different signals, in the order the clips were
    // placed in -- which is only true if the analyser saw the mixdown rather than one of the files.
    CHECK(gap < first);
    CHECK(first < second);
    CHECK(gap < 1e-3); // the gap really is silence
}

TEST_CASE("One audio file behaves exactly as it did before arrangements existed",
          "[integration][audio][arrangement]") {
    // `loadAudio` now goes through the mixer. This is the check that says nobody can tell.
    Scratch scratch("single");
    const fs::path wav = scratch.tone("one.wav", 2.0, 330.0f);

    app::Engine engine(app::EngineMode::Offline);
    const auto duration = engine.loadAudio(wav);
    REQUIRE(duration.has_value());
    CHECK_THAT(*duration, WithinAbs(2.0, 0.01));
    CHECK(engine.hasAudio());
    CHECK(engine.audioPath() == wav);          // still the file, for the project and the UI
    CHECK(engine.audioClips().size() == 1);
    REQUIRE(engine.audioFile() != nullptr);

    // Bit-identical to the file, through the engine's own path.
    const auto source = audio::AudioFile::load(wav);
    REQUIRE(source.has_value());
    const auto a = source->interleaved();
    const auto b = engine.audioFile()->interleaved();
    REQUIRE(a.size() == b.size());
    std::size_t differences = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        differences += a[i] == b[i] ? 0 : 1;
    }
    CHECK(differences == 0);
}

TEST_CASE("An arrangement is saved with the project, and one file still is not",
          "[integration][audio][arrangement][project]") {
    Scratch scratch("project");
    const fs::path a = scratch.tone("a.wav", 1.0, 220.0f);
    const fs::path b = scratch.tone("b.wav", 1.0, 440.0f);

    const auto read = [](const fs::path& p) {
        std::ifstream in(p);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };

    // One plain file is still written as `assets.audio` and gains no clip list, so every project
    // written before this existed round-trips unchanged.
    const fs::path single = scratch.dir / "single.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(a).has_value());
        REQUIRE(engine.saveProject(single).has_value());
    }
    const std::string singleText = read(single);
    CHECK(singleText.find("\"audioClips\"") == std::string::npos);
    CHECK(singleText.find("\"audio\"") != std::string::npos);

    const fs::path many = scratch.dir / "many.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.setAudioClips({AudioClip{.file = a, .name = "intro"},
                                      AudioClip{.file = b,
                                                .startSeconds = 4.0,
                                                .inSeconds = 0.25,
                                                .durationSeconds = 0.5,
                                                .gain = 0.5f,
                                                .fadeInSeconds = 0.1}})
                    .has_value());
        REQUIRE(engine.saveProject(many).has_value());
    }
    CHECK(read(many).find("\"audioClips\"") != std::string::npos);

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(many).has_value());
    REQUIRE(engine.audioClips().size() == 2);
    const audio::AudioClip& second = engine.audioClips()[1];
    CHECK(second.file == b);
    CHECK_THAT(second.startSeconds, WithinAbs(4.0, 1e-9));
    CHECK_THAT(second.inSeconds, WithinAbs(0.25, 1e-9));
    CHECK_THAT(second.durationSeconds, WithinAbs(0.5, 1e-9));
    CHECK_THAT(static_cast<double>(second.gain), WithinAbs(0.5, 1e-6));
    CHECK_THAT(second.fadeInSeconds, WithinAbs(0.1, 1e-9));
    CHECK(engine.audioClips()[0].name == "intro");
    // And it plays: the piece is as long as the arrangement says, not as long as one file.
    CHECK_THAT(engine.durationSeconds(), WithinAbs(4.5, 0.02));
    // `audioPath_` names nothing, because no one file is the project's audio any more.
    CHECK(engine.audioPath().empty());
}

TEST_CASE("Editing one clip re-mixes without re-decoding the others",
          "[integration][audio][arrangement]") {
    Scratch scratch("edit");
    const fs::path a = scratch.tone("a.wav", 1.0, 220.0f);
    const fs::path b = scratch.tone("b.wav", 1.0, 440.0f);

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setAudioClips({AudioClip{.file = a}, AudioClip{.file = b, .startSeconds = 2.0}})
                .has_value());
    const auto heldA = engine.clipSource(a);
    REQUIRE(heldA != nullptr);
    CHECK_THAT(engine.durationSeconds(), WithinAbs(3.0, 0.01));

    // Move the second clip. The first's decoded samples are the same object afterwards -- dragging a
    // clip must not re-decode a hundred megabytes of wav.
    auto clips = engine.audioClips();
    clips[1].startSeconds = 10.0;
    REQUIRE(engine.setAudioClips(clips).has_value());
    CHECK(engine.clipSource(a) == heldA);
    CHECK_THAT(engine.durationSeconds(), WithinAbs(11.0, 0.01));

    // Removing a clip releases the file nothing names any more.
    REQUIRE(engine.setAudioClips({AudioClip{.file = a}}).has_value());
    CHECK(engine.clipSource(b) == nullptr);
    CHECK(engine.clipSource(a) == heldA);
    CHECK_THAT(engine.durationSeconds(), WithinAbs(1.0, 0.01));

    // And an empty arrangement is "no audio", not a device playing nothing.
    REQUIRE(engine.setAudioClips({}).has_value());
    CHECK_FALSE(engine.hasAudio());
    CHECK(engine.clipSource(a) == nullptr);
}

TEST_CASE("The audio revision changes whenever the installed audio does",
          "[integration][audio][arrangement]") {
    // What every cache of something derived from the audio keys on -- the sequencer's waveform
    // summary and its beat grid. They used to key on the *address* of the `AudioFile`, which is
    // freed and reallocated on every re-mix: an allocator handing back the same address made a cache
    // conclude "nothing changed" about a different mix, and the panel drew the old waveform under
    // the new clips. A counter cannot be reused.
    Scratch scratch("revision");
    const fs::path a = scratch.tone("a.wav", 1.0, 220.0f);
    const fs::path b = scratch.tone("b.wav", 1.0, 440.0f);

    app::Engine engine(app::EngineMode::Offline);
    const std::uint64_t empty = engine.audioRevision();

    REQUIRE(engine.loadAudio(a).has_value());
    const std::uint64_t loaded = engine.audioRevision();
    CHECK(loaded != empty);

    // A re-mix that changes the audio changes the revision, even though the clip count does not.
    REQUIRE(engine.setAudioClips({AudioClip{.file = a, .startSeconds = 3.0}}).has_value());
    const std::uint64_t moved = engine.audioRevision();
    CHECK(moved != loaded);

    REQUIRE(engine.setAudioClips({AudioClip{.file = a}, AudioClip{.file = b, .startSeconds = 2.0}})
                .has_value());
    CHECK(engine.audioRevision() != moved);

    // Audio going away is a change too: a cache holding the last summary would otherwise draw a
    // waveform for a project that has none.
    const std::uint64_t two = engine.audioRevision();
    REQUIRE(engine.setAudioClips({}).has_value());
    CHECK(engine.audioRevision() != two);
    CHECK_FALSE(engine.hasAudio());

    // And it only moves forward, so a cache can compare rather than merely detect inequality.
    CHECK(engine.audioRevision() > empty);
}
