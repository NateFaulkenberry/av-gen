#include "assets/exr.hpp"

#include <tinyexr.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace avgen::assets {

namespace {

std::string takeError(const char* err, const char* fallback) {
    std::string message = err != nullptr ? err : fallback;
    if (err != nullptr) {
        FreeEXRErrorMessage(err);
    }
    return message;
}

} // namespace

Result<void> writeExr(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                      std::span<const float> rgba, bool half) {
    if (width == 0 || height == 0) {
        return fail("writeExr: empty image ({}x{})", width, height);
    }
    if (rgba.size() != static_cast<std::size_t>(width) * height * 4) {
        return fail("writeExr: expected {} floats for {}x{} RGBA, got {}", static_cast<std::size_t>(width) * height * 4,
                    width, height, rgba.size());
    }
    const char* err = nullptr;
    const int ret = SaveEXR(rgba.data(), static_cast<int>(width), static_cast<int>(height), 4, half ? 1 : 0,
                            path.string().c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        return fail("cannot write '{}': {} (tinyexr {})", path.string(), takeError(err, "unknown error"), ret);
    }
    return {};
}

Result<scene::TextureData> readExr(const std::filesystem::path& path) {
    float* pixels = nullptr;
    int width = 0;
    int height = 0;
    const char* err = nullptr;
    const int ret = LoadEXR(&pixels, &width, &height, path.string().c_str(), &err);
    if (ret != TINYEXR_SUCCESS || pixels == nullptr) {
        return fail("cannot read '{}': {} (tinyexr {})", path.string(), takeError(err, "unknown error"), ret);
    }
    std::unique_ptr<float, decltype(&std::free)> owned(pixels, &std::free);
    if (width <= 0 || height <= 0) {
        return fail("cannot read '{}': bad size {}x{}", path.string(), width, height);
    }
    scene::TextureData tex;
    tex.name = path.filename().string();
    tex.width = static_cast<std::uint32_t>(width);
    tex.height = static_cast<std::uint32_t>(height);
    tex.format = scene::TextureFormat::Rgba32Float;
    const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 * sizeof(float);
    tex.data.resize(bytes);
    std::memcpy(tex.data.data(), pixels, bytes);
    return tex;
}


Result<void> writeExrLayers(const std::filesystem::path& path, std::uint32_t width,
                            std::uint32_t height, std::span<const ExrChannel> channels) {
    if (width == 0 || height == 0) return fail("exr: {}x{} is not an image", width, height);
    if (channels.empty()) return fail("exr: no channels given for '{}'", path.string());
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    for (std::size_t i = 0; i < channels.size(); ++i) {
        if (channels[i].name.empty()) return fail("exr: channel {} has no name", i);
        if (channels[i].name.size() >= 255) return fail("exr: channel name '{}' is too long", channels[i].name);
        if (channels[i].data.size() != pixels) {
            return fail("exr: channel '{}' has {} values but the image is {}x{}", channels[i].name,
                        channels[i].data.size(), width, height);
        }
        for (std::size_t j = i + 1; j < channels.size(); ++j) {
            if (channels[i].name == channels[j].name) {
                return fail("exr: duplicate channel name '{}'", channels[i].name);
            }
        }
    }

    EXRHeader header;
    InitEXRHeader(&header);
    EXRImage image;
    InitEXRImage(&image);

    const int n = static_cast<int>(channels.size());
    image.num_channels = n;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);

    // tinyexr wants planar data, one pointer per channel. The spans are already planar, so this is
    // only an array of pointers -- no copy.
    std::vector<const float*> planes(static_cast<std::size_t>(n));
    for (int c = 0; c < n; ++c) planes[static_cast<std::size_t>(c)] = channels[static_cast<std::size_t>(c)].data.data();
    image.images = reinterpret_cast<unsigned char**>(const_cast<float**>(planes.data()));

    std::vector<EXRChannelInfo> infos(static_cast<std::size_t>(n));
    std::vector<int> pixelTypes(static_cast<std::size_t>(n));
    std::vector<int> requested(static_cast<std::size_t>(n));
    for (int c = 0; c < n; ++c) {
        const auto ci = static_cast<std::size_t>(c);
        std::memset(infos[ci].name, 0, sizeof(infos[ci].name));
        std::strncpy(infos[ci].name, channels[ci].name.c_str(), sizeof(infos[ci].name) - 1);
        // The caller always hands us float; `requested_pixel_types` is what lands in the file, so
        // this is where per-channel half lives -- something writeExr cannot express at all.
        pixelTypes[ci] = TINYEXR_PIXELTYPE_FLOAT;
        requested[ci] = channels[ci].half ? TINYEXR_PIXELTYPE_HALF : TINYEXR_PIXELTYPE_FLOAT;
    }
    header.num_channels = n;
    header.channels = infos.data();
    header.pixel_types = pixelTypes.data();
    header.requested_pixel_types = requested.data();
    header.compression_type = TINYEXR_COMPRESSIONTYPE_ZIP;

    const char* err = nullptr;
    const int rc = SaveEXRImageToFile(&image, &header, path.string().c_str(), &err);
    if (rc != TINYEXR_SUCCESS) {
        const std::string message = err != nullptr ? err : "unknown error";
        FreeEXRErrorMessage(err);
        return fail("exr: could not write '{}': {}", path.string(), message);
    }
    return {};
}

} // namespace avgen::assets
