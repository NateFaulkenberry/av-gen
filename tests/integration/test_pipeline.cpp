// End-to-end: synthetic audio -> Analyzer -> SignalBus -> Modulator -> ParameterSet -> OrbScene.
// GPU-free. Uses the Engine in Offline mode so the exact production path is exercised with a
// FixedStepClock and a precomputed AnalysisTrack (ADR-012).
#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <vector>

using namespace avgen;

namespace {

std::filesystem::path writeFixture(const char* name, const std::vector<float>& mono, std::uint32_t rate) {
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, rate);
    const auto path = std::filesystem::temp_directory_path() / (std::string("avgen_pipeline_") + name + ".wav");
    REQUIRE(file.writeWav(path).has_value());
    return path;
}

// 4 s: silence for 1 s, then a loud 60 Hz bass tone, then a 8 kHz treble tone, with an impulse
// train (onsets) during the treble section.
std::vector<float> fixtureSignal(std::uint32_t rate) {
    const std::size_t second = rate;
    std::vector<float> out(4 * second, 0.0f);
    auto bass = testsupport::sine(60.0f, rate, second, 0.9f);
    auto treble = testsupport::sine(8000.0f, rate, second, 0.5f);
    auto clicks = testsupport::impulseTrain(second, second / 4, 1000, 0.9f);
    for (std::size_t i = 0; i < second; ++i) {
        out[second + i] = bass[i];
        out[2 * second + i] = treble[i] + clicks[i];
    }
    return out;
}

struct RunResult {
    std::vector<float> scale;
    std::vector<float> emissive;
    std::vector<float> brightness;
    std::vector<float> impulse;
    std::vector<glm::mat4> orbMatrix;
    std::vector<float> bass;
    std::vector<float> treble;
    int onsetEvents = 0;
};

RunResult runOffline(const std::filesystem::path& path, double fps, int frames) {
    app::Engine engine(app::EngineMode::Offline);
    auto duration = engine.loadAudio(path);
    REQUIRE(duration.has_value());
    FixedStepClock clock(fps);
    RunResult result;
    const auto& sig = engine.audioSignals();
    for (int i = 0; i < frames; ++i) {
        const FrameTime time = engine.tick(clock);
        engine.update(time);
        result.scale.push_back(engine.orbScene().scale().value());
        result.emissive.push_back(engine.orbScene().emissive().value());
        result.brightness.push_back(engine.orbScene().brightness().value());
        result.impulse.push_back(engine.orbScene().impulse().value());
        result.orbMatrix.push_back(engine.scene().entities[0].transform.matrix());
        result.bass.push_back(engine.signals().value(sig.bass));
        result.treble.push_back(engine.signals().value(sig.treble));
        if (engine.latestFrame().onset && engine.hasFrame()) {
            ++result.onsetEvents;
        }
    }
    return result;
}

float average(const std::vector<float>& v, std::size_t from, std::size_t to) {
    float sum = 0.0f;
    for (std::size_t i = from; i < to && i < v.size(); ++i) {
        sum += v[i];
    }
    return to > from ? sum / static_cast<float>(to - from) : 0.0f;
}

} // namespace

TEST_CASE("Audio drives scene parameters through the modulation system", "[integration][pipeline]") {
    constexpr std::uint32_t rate = 48000;
    const auto path = writeFixture("sections", fixtureSignal(rate), rate);
    constexpr double fps = 60.0;
    const auto r = runOffline(path, fps, 240); // 4 s

    const auto sec = [](double s) { return static_cast<std::size_t>(s * fps); };
    const float scaleSilence = average(r.scale, sec(0.3), sec(0.9));
    const float scaleBass = average(r.scale, sec(1.3), sec(1.9));
    const float scaleTreble = average(r.scale, sec(2.5), sec(2.9));
    const float emissiveBass = average(r.emissive, sec(1.3), sec(1.9));
    const float emissiveTreble = average(r.emissive, sec(2.3), sec(2.9));
    const float brightnessSilence = average(r.brightness, sec(0.3), sec(0.9));
    const float brightnessBass = average(r.brightness, sec(1.3), sec(1.9));

    INFO("scale silence=" << scaleSilence << " bass=" << scaleBass << " treble=" << scaleTreble);
    INFO("emissive bass=" << emissiveBass << " treble=" << emissiveTreble);
    // Base values while silent.
    CHECK_THAT(scaleSilence, Catch::Matchers::WithinAbs(1.0, 1e-3));
    CHECK_THAT(brightnessSilence, Catch::Matchers::WithinAbs(1.0, 1e-3));
    // Bass -> scale, and treble does not inflate scale.
    CHECK(scaleBass > 1.5f);
    CHECK(scaleTreble < 1.1f); // treble does not inflate scale
    // Treble -> emissive.
    CHECK(emissiveTreble > emissiveBass + 1.0f);
    // RMS -> brightness.
    CHECK(brightnessBass > brightnessSilence + 0.3f);
    // Onset -> impulse: the click train fires events and the envelope rises.
    CHECK(r.onsetEvents >= 3);
    float maxImpulse = 0.0f;
    for (std::size_t i = sec(2.0); i < r.impulse.size(); ++i) {
        maxImpulse = std::max(maxImpulse, r.impulse[i]);
    }
    CHECK(maxImpulse > 0.2f);
    // Signals themselves reached the bus.
    CHECK(average(r.bass, sec(1.3), sec(1.9)) > 0.8f);
    CHECK(average(r.treble, sec(2.3), sec(2.9)) > 0.8f);
    std::filesystem::remove(path);
}

TEST_CASE("Offline pipeline is deterministic frame for frame", "[integration][determinism]") {
    constexpr std::uint32_t rate = 44100;
    const auto path = writeFixture("determinism", fixtureSignal(rate), rate);
    const auto a = runOffline(path, 30.0, 120);
    const auto b = runOffline(path, 30.0, 120);
    REQUIRE(a.scale.size() == b.scale.size());
    for (std::size_t i = 0; i < a.scale.size(); ++i) {
        REQUIRE(a.scale[i] == b.scale[i]);
        REQUIRE(a.emissive[i] == b.emissive[i]);
        REQUIRE(a.orbMatrix[i] == b.orbMatrix[i]);
    }
    CHECK(a.onsetEvents == b.onsetEvents);
    std::filesystem::remove(path);
}

TEST_CASE("Rotation integrates the modulated speed with the injected clock", "[integration][time]") {
    constexpr std::uint32_t rate = 48000;
    // 2 s of a mid-band tone: mid -> rotationSpeed.
    const auto path = writeFixture("rotation", testsupport::sine(1000.0f, rate, 2 * rate, 0.8f), rate);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(path).has_value());
    FixedStepClock clock(50.0);
    float previous = engine.orbScene().currentAngle();
    bool monotonic = true;
    for (int i = 0; i < 100; ++i) {
        engine.update(engine.tick(clock));
        const float angle = engine.orbScene().currentAngle();
        if (angle < previous) {
            monotonic = false;
        }
        previous = angle;
    }
    CHECK(monotonic);
    // Base speed is 0.4 rad/s; with modulation over 2 s the angle must exceed the unmodulated value.
    CHECK(engine.orbScene().currentAngle() > 0.4f * 2.0f * 0.98f);
    CHECK(engine.orbScene().rotationSpeed().value() > 0.4f);
    std::filesystem::remove(path);
}

TEST_CASE("Engine reports errors for missing audio and stays usable", "[integration][errors]") {
    app::Engine engine(app::EngineMode::Offline);
    auto r = engine.loadAudio("/definitely/not/here.wav");
    REQUIRE_FALSE(r.has_value());
    CHECK_FALSE(engine.hasAudio());
    FixedStepClock clock(60.0);
    for (int i = 0; i < 5; ++i) {
        engine.update(engine.tick(clock)); // silence path
    }
    CHECK_THAT(engine.orbScene().scale().value(), Catch::Matchers::WithinAbs(1.0, 1e-6));
}
