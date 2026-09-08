#include "audio/audio_file.hpp"
#include "audio/audio_player.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace avgen;
using namespace avgen::audio;

namespace {

constexpr std::uint32_t kRate = 44100;

std::shared_ptr<const AudioFile> stereoSine(double seconds, std::uint32_t rate = kRate) {
    const auto frames = static_cast<std::size_t>(seconds * rate);
    const auto mono = testsupport::sine(440.0f, rate, frames, 0.25f);
    return std::make_shared<const AudioFile>(
        AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, rate));
}

void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Installs the source or skips the test when there is no usable output device (CI).
void requireSource(AudioPlayer& player, std::shared_ptr<const AudioFile> file) {
    auto r = player.setSource(std::move(file));
    if (!r) {
        SKIP("no audio output device: " << r.error().message);
    }
    player.setVolume(0.0f); // keep the test run silent; the analysis stream is unaffected
}

template <typename Pred>
bool waitFor(Pred pred, int timeoutMs, int stepMs = 5) {
    for (int elapsed = 0; elapsed <= timeoutMs; elapsed += stepMs) {
        if (pred()) {
            return true;
        }
        sleepMs(stepMs);
    }
    return pred();
}

} // namespace

TEST_CASE("AudioPlayer volume clamps to [0, 1]", "[audio][player]") {
    AudioPlayer player;
    CHECK(player.volume() == 1.0f);
    player.setVolume(2.0f);
    CHECK(player.volume() == 1.0f);
    player.setVolume(-3.0f);
    CHECK(player.volume() == 0.0f);
    player.setVolume(0.25f);
    CHECK(player.volume() == 0.25f);
}

TEST_CASE("AudioPlayer without a source is inert", "[audio][player]") {
    AudioPlayer player;
    CHECK_FALSE(player.hasSource());
    CHECK(player.source() == nullptr);
    CHECK_FALSE(player.play().has_value());
    CHECK(player.positionFrames() == 0);
    CHECK(player.positionSeconds() == 0.0);
    CHECK(player.durationSeconds() == 0.0);
    CHECK(player.sampleRate() == 0);
    CHECK(player.deviceSampleRate() == 0);
    CHECK(player.deviceName().empty());
    player.seekSeconds(1.0);
    CHECK(player.positionFrames() == 0);
    CHECK(player.setSource(nullptr).has_value());
    CHECK_FALSE(player.hasSource());
}

TEST_CASE("AudioPlayer advances position while playing and freezes on pause", "[audio][device]") {
    AudioPlayer player;
    requireSource(player, stereoSine(2.0));
    CHECK(player.hasSource());
    CHECK(player.sampleRate() == kRate);
    CHECK(player.deviceSampleRate() > 0);
    CHECK_FALSE(player.deviceName().empty());
    CHECK_THAT(player.durationSeconds(), Catch::Matchers::WithinAbs(2.0, 1e-9));
    CHECK(player.positionFrames() == 0);
    CHECK_FALSE(player.isPlaying());

    REQUIRE(player.play().has_value());
    CHECK(player.isPlaying());
    sleepMs(150);
    const auto afterPlay = player.positionFrames();
    CHECK(afterPlay > 0);
    CHECK(player.positionSeconds() < player.durationSeconds());

    player.pause();
    CHECK_FALSE(player.isPlaying());
    sleepMs(50); // let any in-flight callback finish
    const auto frozen = player.positionFrames();
    sleepMs(100);
    CHECK(player.positionFrames() == frozen);
    CHECK(frozen >= afterPlay);
}

TEST_CASE("AudioPlayer seek moves the position and marks the analysis stream", "[audio][device]") {
    AudioPlayer player;
    requireSource(player, stereoSine(2.0));
    auto& stream = player.analysisStream();
    std::vector<float> buf(4096);

    REQUIRE(player.play().has_value());
    sleepMs(100);
    player.pause();
    sleepMs(50);

    // Discard everything produced so far.
    while (stream.read(buf).count > 0) {
    }

    player.seekSeconds(0.5);
    CHECK(player.positionFrames() == kRate / 2);
    CHECK_THAT(player.positionSeconds(), Catch::Matchers::WithinAbs(0.5, 1e-9));

    REQUIRE(player.play().has_value());
    sleepMs(100);
    player.pause();
    sleepMs(50);
    CHECK(player.positionFrames() > kRate / 2);

    bool sawSeekMarker = false;
    std::size_t samplesAfterMarker = 0;
    for (int i = 0; i < 1000; ++i) {
        auto r = stream.read(buf);
        if (r.discontinuity && r.startFrame == kRate / 2) {
            sawSeekMarker = true;
        }
        if (sawSeekMarker) {
            samplesAfterMarker += r.count;
        }
        if (r.count == 0) {
            break;
        }
    }
    CHECK(sawSeekMarker);
    CHECK(samplesAfterMarker > 0);

    // seekFrames clamps to the file length.
    player.seekFrames(1'000'000'000);
    CHECK(player.positionFrames() == player.source()->frameCount());
    player.seekSeconds(-5.0);
    CHECK(player.positionFrames() == 0);
}

TEST_CASE("AudioPlayer reaches the end and restarts from zero on play", "[audio][device]") {
    AudioPlayer player;
    requireSource(player, stereoSine(0.3));
    const auto frames = player.source()->frameCount();

    REQUIRE(player.play().has_value());
    CHECK(waitFor([&] { return player.atEnd(); }, 1000));
    CHECK(player.atEnd());
    CHECK_FALSE(player.isPlaying());
    CHECK(player.positionFrames() == frames);

    REQUIRE(player.play().has_value());
    CHECK(player.positionFrames() == 0);
    CHECK(player.isPlaying());
    sleepMs(60);
    const auto pos = player.positionFrames();
    CHECK(pos > 0);
    CHECK(pos < frames);
    CHECK_FALSE(player.atEnd());

    player.stop();
    sleepMs(50);
    CHECK_FALSE(player.isPlaying());
    CHECK(player.positionFrames() == 0);
}

TEST_CASE("AudioPlayer releases the device on setSource(nullptr)", "[audio][device]") {
    AudioPlayer player;
    requireSource(player, stereoSine(0.5));
    CHECK(player.hasSource());
    REQUIRE(player.play().has_value());
    sleepMs(30);
    REQUIRE(player.setSource(nullptr).has_value());
    CHECK_FALSE(player.hasSource());
    CHECK_FALSE(player.isPlaying());
    CHECK(player.positionFrames() == 0);
    CHECK(player.deviceSampleRate() == 0);
    CHECK_FALSE(player.play().has_value());
}

TEST_CASE("AudioPlayer accepts a 22050 Hz mono source", "[audio][device]") {
    AudioPlayer player;
    constexpr std::uint32_t rate = 22050;
    const auto mono = testsupport::sine(220.0f, rate, rate / 2, 0.25f);
    auto file = std::make_shared<const AudioFile>(AudioFile::fromInterleaved(mono, 1, rate));
    requireSource(player, file);
    CHECK(player.sampleRate() == rate);
    CHECK(player.source()->channels() == 1);

    REQUIRE(player.play().has_value());
    sleepMs(100);
    player.pause();
    sleepMs(50);
    const auto pos = player.positionFrames();
    CHECK(pos > 0);
    CHECK(pos < rate / 2);
    CHECK_THAT(player.positionSeconds(), Catch::Matchers::WithinAbs(static_cast<double>(pos) / rate, 1e-9));

    // The analysis stream carries the source samples, stamped from frame 0.
    auto& stream = player.analysisStream();
    std::vector<float> buf(1024);
    auto r = stream.read(buf);
    REQUIRE(r.count > 0);
    CHECK(r.discontinuity);
    CHECK(r.startFrame == 0);
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < r.count; ++i) {
        if (buf[i] != mono[i]) {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}
