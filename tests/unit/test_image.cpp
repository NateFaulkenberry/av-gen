#include "assets/image.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinRel;

namespace {

std::filesystem::path tempPath(const char* name) {
    return std::filesystem::temp_directory_path() / (std::string("avgen_image_test_") + name);
}

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const char* name)
        : path(tempPath(name)) {
        std::filesystem::remove(path);
    }
    ~TempFile() { std::filesystem::remove(path); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

// 3x2 RGBA8 pattern with distinct values in every channel, including alpha.
constexpr std::uint32_t kWidth = 3;
constexpr std::uint32_t kHeight = 2;
const std::vector<std::uint8_t> kPixels = {
    255, 0,  0,  255, 0,   255, 0,   200, 0,   0,   255, 128, //
    10,  20, 30, 40,  250, 240, 230, 0,   128, 128, 128, 255, //
};

} // namespace

TEST_CASE("encodePng / loadImageFromMemory round-trips pixels exactly", "[assets][image]") {
    const auto png = assets::encodePng(kWidth, kHeight, kPixels);
    REQUIRE(png.has_value());
    REQUIRE(png->size() > 8);
    // PNG signature
    CHECK((*png)[0] == 0x89);
    CHECK((*png)[1] == 'P');

    SECTION("srgb flag selects the format") {
        const auto srgb = assets::loadImageFromMemory(*png, true, "pattern");
        REQUIRE(srgb.has_value());
        CHECK(srgb->name == "pattern");
        CHECK(srgb->width == kWidth);
        CHECK(srgb->height == kHeight);
        CHECK(srgb->format == scene::TextureFormat::Rgba8Srgb);
        CHECK(srgb->valid());
        CHECK(srgb->data == kPixels);
        CHECK(assets::floatPixels(*srgb).empty());

        const auto linear = assets::loadImageFromMemory(*png, false, "pattern");
        REQUIRE(linear.has_value());
        CHECK(linear->format == scene::TextureFormat::Rgba8Unorm);
        CHECK(linear->data == kPixels);
    }
}

TEST_CASE("writePng / loadImage round-trips through a file", "[assets][image]") {
    TempFile file("roundtrip.png");
    REQUIRE(assets::writePng(file.path, kWidth, kHeight, kPixels).has_value());
    const auto loaded = assets::loadImage(file.path, true);
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == "avgen_image_test_roundtrip.png");
    CHECK(loaded->width == kWidth);
    CHECK(loaded->height == kHeight);
    CHECK(loaded->format == scene::TextureFormat::Rgba8Srgb);
    CHECK(loaded->data == kPixels);
}

TEST_CASE("writeHdr / loadImage yields Rgba32Float within RGBE precision", "[assets][image]") {
    TempFile file("roundtrip.hdr");
    constexpr std::uint32_t w = 4;
    constexpr std::uint32_t h = 2;
    std::vector<float> pixels;
    for (std::uint32_t i = 0; i < w * h; ++i) {
        const float base = 0.25f + static_cast<float>(i) * 1.75f; // 0.25 .. 12.5, spans several exponents
        pixels.push_back(base);
        pixels.push_back(base * 0.5f);
        pixels.push_back(base * 2.0f);
        pixels.push_back(1.0f);
    }
    REQUIRE(assets::writeHdr(file.path, w, h, pixels).has_value());

    const auto loaded = assets::loadImage(file.path, true); // srgb ignored for HDR
    REQUIRE(loaded.has_value());
    CHECK(loaded->format == scene::TextureFormat::Rgba32Float);
    CHECK(loaded->isHdr());
    CHECK(loaded->width == w);
    CHECK(loaded->height == h);
    CHECK(loaded->bytesPerPixel() == 16);
    CHECK(loaded->valid());
    const auto floats = assets::floatPixels(*loaded);
    REQUIRE(floats.size() == pixels.size());
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        if (i % 4 == 3) {
            CHECK(floats[i] == 1.0f); // alpha is not stored; stb fills 1
            continue;
        }
        CHECK_THAT(static_cast<double>(floats[i]), WithinRel(static_cast<double>(pixels[i]), 0.01));
    }
}

TEST_CASE("image loading reports errors", "[assets][image]") {
    SECTION("missing file") {
        const auto result = assets::loadImage(tempPath("does_not_exist.png"), true);
        REQUIRE_FALSE(result.has_value());
        CHECK_FALSE(result.error().message.empty());
    }
    SECTION("garbage bytes in memory") {
        const std::array<std::uint8_t, 16> garbage = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        const auto result = assets::loadImageFromMemory(garbage, false, "garbage");
        REQUIRE_FALSE(result.has_value());
        CHECK_FALSE(result.error().message.empty());
        CHECK(result.error().message.find("garbage") != std::string::npos);
    }
    SECTION("empty buffer") {
        const auto result = assets::loadImageFromMemory({}, false, "empty");
        REQUIRE_FALSE(result.has_value());
    }
    SECTION("garbage file") {
        TempFile file("garbage.png");
        {
            std::ofstream out(file.path, std::ios::binary);
            out << "definitely not a png";
        }
        const auto result = assets::loadImage(file.path, true);
        REQUIRE_FALSE(result.has_value());
        CHECK_FALSE(result.error().message.empty());
    }
    SECTION("encoder rejects a mismatched buffer") {
        CHECK_FALSE(assets::encodePng(2, 2, kPixels).has_value()); // 2x2 needs 16 bytes, given 24
        CHECK_FALSE(assets::encodePng(0, 2, {}).has_value());
        CHECK_FALSE(assets::writeHdr(tempPath("bad.hdr"), 2, 2, std::vector<float>(3)).has_value());
    }
}
