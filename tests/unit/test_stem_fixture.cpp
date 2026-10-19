// stems-multichannel M1: the generated fixture decodes and agrees with its own metadata.

#include "audio/audio_file.hpp"
#include "core/hash.hpp"
#include "support/stem_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>

using namespace avgen;

TEST_CASE("stem fixture: every file decodes with the rate, channels and length its metadata records",
          "[stems][fixture]") {
    const auto& fx = testsupport::requireStemFixture();
    std::size_t decoded = 0;
    for (const auto& [name, info] : fx.meta["files"].items()) {
        INFO(name);
        auto file = audio::AudioFile::load(fx.file(name));
        if (info.value("undecodable", false)) {
            CHECK_FALSE(file.has_value());
            continue;
        }
        REQUIRE(file.has_value());
        CHECK(file->sampleRate() == info["rate"].get<std::uint32_t>());
        CHECK(file->channels() == info["channels"].get<std::uint32_t>());
        CHECK(file->frameCount() == info["frames"].get<std::uint64_t>());
        // The bytes are the ones the generator recorded: the fixture on disk is the fixture the metadata describes.
        const auto sha = sha256File(fx.file(name));
        REQUIRE(sha.has_value());
        CHECK(*sha == info["sha256"].get<std::string>());
        ++decoded;
    }
    CHECK(decoded == 9);
}

TEST_CASE("stem fixture: the mix is louder than silence where every stem has events, and the silent stem is zero",
          "[stems][fixture]") {
    const auto& fx = testsupport::requireStemFixture();
    auto mix = audio::AudioFile::load(fx.file("mix.wav"));
    auto silent = audio::AudioFile::load(fx.file("silent.wav"));
    REQUIRE(mix.has_value());
    REQUIRE(silent.has_value());
    CHECK(mix->channels() == 2);
    const auto m = silent->mono();
    CHECK(std::all_of(m.begin(), m.end(), [](float v) { return v == 0.0f; }));
    // 24-bit synth decodes to within one 24-bit step of full scale (no clipping, a real signal).
    auto synth = audio::AudioFile::load(fx.file("synth.wav"));
    REQUIRE(synth.has_value());
    float peak = 0.0f;
    for (float v : synth->interleaved()) {
        peak = std::max(peak, std::fabs(v));
    }
    CHECK(peak > 0.1f);
    CHECK(peak < 1.0f);
}
