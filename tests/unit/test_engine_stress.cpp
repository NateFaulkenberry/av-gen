// Concurrency stress: main-thread control edits (seek, params, routes, volume, transport) while
// the audio callback and analysis thread run. Needs an output device; SKIPs otherwise.
#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/rng.hpp"
#include "core/time.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace avgen;

TEST_CASE("Engine survives rapid control edits during live playback", "[audio][device][stress]") {
    constexpr std::uint32_t rate = 48000;
    auto mono = testsupport::mix(testsupport::clickTrack(120.0f, rate, rate * 4), testsupport::sine(60.0f, rate, rate * 4, 0.3f));
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, rate);
    const auto wav = std::filesystem::temp_directory_path() / "avgen_stress.wav";
    REQUIRE(file.writeWav(wav).has_value());

    app::Engine engine(app::EngineMode::Live);
    auto loaded = engine.loadAudio(wav);
    if (!loaded) {
        SKIP("no audio output device: " << loaded.error().message);
    }
    engine.setVolume(0.0f);
    REQUIRE(engine.play().has_value());

    RealtimeClock clock;
    Rng rng(12345);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    int frames = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        auto& params = engine.params();
        const float roll = rng.nextFloat();
        if (roll < 0.4f && params.size() > 0) {
            auto* p = params.ordered()[static_cast<std::size_t>(rng.nextFloat() * static_cast<float>(params.size())) % params.size()];
            for (std::size_t c = 0; c < p->componentCount(); ++c) {
                p->setBaseComponent(c, rng.range(p->softMin(c), p->softMax(c)));
            }
        } else if (roll < 0.6f) {
            engine.seekSeconds(static_cast<double>(rng.nextFloat()) * engine.durationSeconds());
        } else if (roll < 0.7f) {
            engine.setVolume(0.0f);
        } else if (roll < 0.85f) {
            for (auto& r : engine.modulator().routes()) {
                r.amount = rng.range(-2.0f, 4.0f);
            }
        } else if (roll < 0.9f) {
            engine.togglePlay();
        } else if (roll < 0.93f) {
            engine.seekSeconds(engine.durationSeconds());
            (void)engine.play();
        } else {
            engine.stop();
            (void)engine.play();
        }
        engine.update(engine.tick(clock));
        ++frames;
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    CHECK(frames > 100);
    engine.stop();
    std::filesystem::remove(wav);
}
