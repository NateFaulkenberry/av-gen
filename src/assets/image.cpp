#include "assets/image.hpp"

#include "assets/exr.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace avgen::assets {

namespace {

constexpr int kChannels = 4; // every decode/encode path is RGBA

struct StbiFree {
    void operator()(void* p) const noexcept { stbi_image_free(p); }
};
template <typename T>
using StbiPtr = std::unique_ptr<T, StbiFree>;

// Case-insensitive ".exr" test on the extension. An HDRI can arrive with any capitalisation and
// the one that prompted this arrived lower-case; matching only lower-case would have worked today
// and failed on the next asset.
bool isExrPath(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".exr";
}

// stb_image reports failure reasons through a global string; snapshot it immediately after a failed call.
std::string failureReason() {
    const char* reason = stbi_failure_reason();
    return reason != nullptr ? reason : "unknown error";
}

// Copies a tightly packed RGBA buffer decoded by stb into TextureData.
scene::TextureData makeTexture(std::string name, int width, int height, scene::TextureFormat format,
                               const void* pixels) {
    scene::TextureData tex;
    tex.name = std::move(name);
    tex.width = static_cast<std::uint32_t>(width);
    tex.height = static_cast<std::uint32_t>(height);
    tex.format = format;
    // The vector's storage comes from operator new and is therefore aligned for float, so floatPixels()
    // can reinterpret it in place without copying.
    const std::size_t bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * tex.bytesPerPixel();
    tex.data.resize(bytes);
    std::memcpy(tex.data.data(), pixels, bytes);
    return tex;
}

bool dimensionsOk(int width, int height) {
    // Guard the width*height*bpp product against overflow before allocating.
    if (width <= 0 || height <= 0) {
        return false;
    }
    constexpr std::size_t kMaxPixels = std::size_t{1} << 30; // 1 Gpixel; 16 GiB for float RGBA
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) <= kMaxPixels;
}

Result<scene::TextureData> decodeHdrFile(const std::filesystem::path& path, std::string name) {
    int width = 0;
    int height = 0;
    int channels = 0;
    StbiPtr<float> pixels(stbi_loadf(path.string().c_str(), &width, &height, &channels, kChannels));
    if (!pixels) {
        return fail("failed to decode HDR image '{}': {}", path.string(), failureReason());
    }
    if (!dimensionsOk(width, height)) {
        return fail("HDR image '{}' has unsupported dimensions {}x{}", path.string(), width, height);
    }
    return makeTexture(std::move(name), width, height, scene::TextureFormat::Rgba32Float, pixels.get());
}

Result<scene::TextureData> decodeLdrFile(const std::filesystem::path& path, bool srgb, std::string name) {
    int width = 0;
    int height = 0;
    int channels = 0;
    StbiPtr<stbi_uc> pixels(stbi_load(path.string().c_str(), &width, &height, &channels, kChannels));
    if (!pixels) {
        return fail("failed to decode image '{}': {}", path.string(), failureReason());
    }
    if (!dimensionsOk(width, height)) {
        return fail("image '{}' has unsupported dimensions {}x{}", path.string(), width, height);
    }
    return makeTexture(std::move(name), width, height,
                       srgb ? scene::TextureFormat::Rgba8Srgb : scene::TextureFormat::Rgba8Unorm,
                       pixels.get());
}

Result<int> checkedLength(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return fail("image buffer too large ({} bytes)", bytes.size());
    }
    return static_cast<int>(bytes.size());
}

Result<void> checkRgbaSize(std::uint32_t width, std::uint32_t height, std::size_t elements,
                           const char* what) {
    if (width == 0 || height == 0) {
        return fail("{}: image dimensions must be non-zero (got {}x{})", what, width, height);
    }
    if (width > static_cast<std::uint32_t>(std::numeric_limits<int>::max() / kChannels) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return fail("{}: image dimensions {}x{} exceed the encoder limit", what, width, height);
    }
    const std::size_t expected =
        static_cast<std::size_t>(width) * height * static_cast<std::size_t>(kChannels);
    if (elements != expected) {
        return fail("{}: expected {} RGBA elements for {}x{}, got {}", what, expected, width, height,
                    elements);
    }
    return {};
}

void appendToVector(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

Result<scene::TextureData> loadImage(const std::filesystem::path& path, bool srgb) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return fail("image file not found: '{}'", path.string());
    }
    std::string name = path.filename().string();
    // EXR goes to tinyexr, which this module already links for `writeExr`. stb_image has no EXR
    // decoder at all, so before this an `.exr` fell through to `decodeLdrFile` and came back
    // "unknown image type" -- which is what an `--env foo.exr` or a scene `environment.map`
    // pointing at an EXR did, and it failed *initialisation* rather than degrading. The asset
    // catalogue has classified `.exr` as an "environment" the whole time (asset_catalog.cpp),
    // so the format was advertised and not readable.
    if (isExrPath(path)) {
        auto exr = readExr(path);
        if (!exr) {
            return std::unexpected(exr.error());
        }
        exr->name = std::move(name);
        return exr;
    }
    if (stbi_is_hdr(path.string().c_str()) != 0) {
        return decodeHdrFile(path, std::move(name));
    }
    return decodeLdrFile(path, srgb, std::move(name));
}

Result<scene::TextureData> loadImageFromMemory(std::span<const std::uint8_t> bytes, bool srgb,
                                               std::string name) {
    if (bytes.empty()) {
        return fail("image '{}': empty buffer", name);
    }
    const auto length = checkedLength(bytes);
    if (!length) {
        return std::unexpected(length.error());
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    if (stbi_is_hdr_from_memory(bytes.data(), *length) != 0) {
        StbiPtr<float> pixels(
            stbi_loadf_from_memory(bytes.data(), *length, &width, &height, &channels, kChannels));
        if (!pixels) {
            return fail("failed to decode HDR image '{}': {}", name, failureReason());
        }
        if (!dimensionsOk(width, height)) {
            return fail("HDR image '{}' has unsupported dimensions {}x{}", name, width, height);
        }
        return makeTexture(std::move(name), width, height, scene::TextureFormat::Rgba32Float, pixels.get());
    }
    StbiPtr<stbi_uc> pixels(
        stbi_load_from_memory(bytes.data(), *length, &width, &height, &channels, kChannels));
    if (!pixels) {
        return fail("failed to decode image '{}': {}", name, failureReason());
    }
    if (!dimensionsOk(width, height)) {
        return fail("image '{}' has unsupported dimensions {}x{}", name, width, height);
    }
    return makeTexture(std::move(name), width, height,
                       srgb ? scene::TextureFormat::Rgba8Srgb : scene::TextureFormat::Rgba8Unorm,
                       pixels.get());
}

Result<void> writePng(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                      std::span<const std::uint8_t> rgba) {
    if (auto ok = checkRgbaSize(width, height, rgba.size(), "writePng"); !ok) {
        return ok;
    }
    const int stride = static_cast<int>(width) * kChannels;
    if (stbi_write_png(path.string().c_str(), static_cast<int>(width), static_cast<int>(height), kChannels,
                       rgba.data(), stride) == 0) {
        return fail("failed to write PNG '{}'", path.string());
    }
    return {};
}

Result<std::vector<std::uint8_t>> encodePng(std::uint32_t width, std::uint32_t height,
                                            std::span<const std::uint8_t> rgba) {
    if (auto ok = checkRgbaSize(width, height, rgba.size(), "encodePng"); !ok) {
        return std::unexpected(ok.error());
    }
    std::vector<std::uint8_t> out;
    const int stride = static_cast<int>(width) * kChannels;
    if (stbi_write_png_to_func(appendToVector, &out, static_cast<int>(width), static_cast<int>(height),
                               kChannels, rgba.data(), stride) == 0) {
        return fail("failed to encode PNG ({}x{})", width, height);
    }
    return out;
}

Result<void> writeHdr(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                      std::span<const float> rgba) {
    if (auto ok = checkRgbaSize(width, height, rgba.size(), "writeHdr"); !ok) {
        return ok;
    }
    if (stbi_write_hdr(path.string().c_str(), static_cast<int>(width), static_cast<int>(height), kChannels,
                       rgba.data()) == 0) {
        return fail("failed to write HDR '{}'", path.string());
    }
    return {};
}

std::span<const float> floatPixels(const scene::TextureData& texture) {
    // TextureData stores every format as bytes. The loaders fill `data` through std::vector's allocator
    // (operator new), whose storage satisfies alignof(float), so a float view of the same bytes is a
    // valid reinterpretation as long as the size is a whole number of floats.
    if (texture.format != scene::TextureFormat::Rgba32Float || texture.data.empty() ||
        texture.data.size() % sizeof(float) != 0) {
        return {};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const float*>(texture.data.data()), texture.data.size() / sizeof(float)};
}

} // namespace avgen::assets
