// OpenEXR output through tinyexr (ADR-020 follow-up): half and float round trips, errors.

#include "assets/exr.hpp"
#include "assets/image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

TEST_CASE("EXR round trip is exact for half-representable values and close for float", "[assets][exr]") {
    const fs::path dir = fs::temp_directory_path() / "avgen_exr_test";
    fs::create_directories(dir);
    constexpr std::uint32_t w = 7;
    constexpr std::uint32_t h = 5;
    // Every value here is exactly representable as a half: powers of two, small integers, and
    // values with few mantissa bits; the range spans well beyond what 8-bit output can hold.
    const std::vector<float> palette = {0.0f, 0.5f, 1.0f, 2.0f, 8.0f, 1024.0f, 0.25f, 0.125f, 3.5f, 65504.0f,
                                        -0.75f, 0.0009765625f, 1.0f / 1024.0f, 0.0625f, 12.5f};
    std::vector<float> rgba(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < rgba.size(); ++i) {
        rgba[i] = palette[(i * 7 + i / 4) % palette.size()];
    }
    SECTION("half") {
        const auto file = dir / "half.exr";
        REQUIRE(assets::writeExr(file, w, h, rgba, true).has_value());
        auto back = assets::readExr(file);
        REQUIRE(back.has_value());
        CHECK(back->width == w);
        CHECK(back->height == h);
        CHECK(back->isHdr());
        const auto px = assets::floatPixels(*back);
        REQUIRE(px.size() == rgba.size());
        for (std::size_t i = 0; i < rgba.size(); ++i) {
            INFO("index " << i);
            CHECK(px[i] == rgba[i]);
        }
    }
    SECTION("float") {
        const auto file = dir / "float.exr";
        std::vector<float> odd = rgba;
        odd[3] = 1.0f / 3.0f; // not half-representable: kept exactly only by the float path
        REQUIRE(assets::writeExr(file, w, h, odd, false).has_value());
        auto back = assets::readExr(file);
        REQUIRE(back.has_value());
        const auto px = assets::floatPixels(*back);
        REQUIRE(px.size() == odd.size());
        for (std::size_t i = 0; i < odd.size(); ++i) {
            INFO("index " << i);
            CHECK(px[i] == odd[i]);
        }
        // The half file rounds that value.
        REQUIRE(assets::writeExr(dir / "half2.exr", w, h, odd, true).has_value());
        auto half = assets::readExr(dir / "half2.exr");
        REQUIRE(half.has_value());
        CHECK(std::abs(assets::floatPixels(*half)[3] - odd[3]) < 1e-3f);
        CHECK(assets::floatPixels(*half)[3] != odd[3]);
    }
    SECTION("errors") {
        CHECK_FALSE(assets::writeExr(dir / "bad.exr", w, h, std::span<const float>(rgba.data(), 3)).has_value());
        CHECK_FALSE(assets::writeExr(dir / "empty.exr", 0, 0, {}).has_value());
        CHECK_FALSE(assets::writeExr(dir / "missing_dir" / "x" / "y.exr", w, h, rgba).has_value());
        CHECK_FALSE(assets::readExr(dir / "does_not_exist.exr").has_value());
    }
    fs::remove_all(dir);
}
