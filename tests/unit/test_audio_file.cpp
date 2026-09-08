#include "audio/audio_file.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>

using namespace avgen;
using namespace avgen::audio;

namespace {
std::filesystem::path tempPath(const char* name) {
    return std::filesystem::temp_directory_path() / (std::string("avgen_test_") + name);
}
} // namespace

TEST_CASE("AudioFile round-trips a stereo sine through WAV", "[audio][file]") {
    constexpr std::uint32_t rate = 44100;
    const auto mono = testsupport::sine(440.0f, rate, rate / 2, 0.5f);
    auto file = AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, rate);
    CHECK(file.channels() == 2);
    CHECK(file.sampleRate() == rate);
    CHECK(file.frameCount() == rate / 2);
    CHECK_THAT(file.durationSeconds(), Catch::Matchers::WithinAbs(0.5, 1e-9));

    const auto path = tempPath("roundtrip.wav");
    REQUIRE(file.writeWav(path).has_value());

    auto loaded = AudioFile::load(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->channels() == 2);
    CHECK(loaded->sampleRate() == rate);
    CHECK(loaded->frameCount() == rate / 2);
    CHECK_THAT(loaded->durationSeconds(), Catch::Matchers::WithinAbs(0.5, 1e-9));
    REQUIRE(loaded->mono().size() == mono.size());
    for (std::size_t i = 0; i < mono.size(); i += 997) {
        CHECK_THAT(static_cast<double>(loaded->mono()[i]), Catch::Matchers::WithinAbs(static_cast<double>(mono[i]), 1e-6));
    }
    std::filesystem::remove(path);
}

TEST_CASE("AudioFile mono downmix averages channels", "[audio][file]") {
    std::vector<float> interleaved = {1.0f, 0.0f, 0.5f, 0.5f, -1.0f, 1.0f};
    auto file = AudioFile::fromInterleaved(interleaved, 2, 48000);
    REQUIRE(file.frameCount() == 3);
    CHECK(file.mono()[0] == 0.5f);
    CHECK(file.mono()[1] == 0.5f);
    CHECK(file.mono()[2] == 0.0f);
}

TEST_CASE("AudioFile readFrames zero-pads past the end", "[audio][file]") {
    std::vector<float> interleaved = {1.0f, 2.0f, 3.0f, 4.0f};
    auto file = AudioFile::fromInterleaved(interleaved, 2, 48000);
    std::vector<float> out(8, 9.0f);
    CHECK(file.readFrames(1, out) == 1);
    CHECK(out[0] == 3.0f);
    CHECK(out[1] == 4.0f);
    CHECK(out[2] == 0.0f);
    CHECK(out[7] == 0.0f);
    CHECK(file.readFrames(10, out) == 0);
    CHECK(out[0] == 0.0f);
}

TEST_CASE("AudioFile rejects missing and invalid files", "[audio][file]") {
    auto missing = AudioFile::load(tempPath("does_not_exist.wav"));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message.find("not found") != std::string::npos);

    const auto garbage = tempPath("garbage.wav");
    {
        std::ofstream out(garbage, std::ios::binary);
        out << "this is not audio data at all, not even close";
    }
    auto invalid = AudioFile::load(garbage);
    REQUIRE_FALSE(invalid.has_value());
    CHECK_FALSE(invalid.error().message.empty());
    std::filesystem::remove(garbage);
}

TEST_CASE("AudioFile handles odd sample rates and mono sources", "[audio][file]") {
    constexpr std::uint32_t rate = 22050;
    auto file = AudioFile::fromInterleaved(testsupport::sine(100.0f, rate, 2205), 1, rate);
    const auto path = tempPath("mono22k.wav");
    REQUIRE(file.writeWav(path).has_value());
    auto loaded = AudioFile::load(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->sampleRate() == 22050);
    CHECK(loaded->channels() == 1);
    CHECK_THAT(loaded->durationSeconds(), Catch::Matchers::WithinAbs(0.1, 1e-9));
    std::filesystem::remove(path);
}
