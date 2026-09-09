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

} // namespace avgen::assets
