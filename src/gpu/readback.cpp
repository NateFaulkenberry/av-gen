#include "gpu/readback.hpp"

#include "gpu/context.hpp"

#include <cstring>
#include <fstream>

namespace avgen::gpu {

Result<Image8> readTexture8(Context& context, const wgpu::Texture& texture, std::uint32_t width,
                            std::uint32_t height, bool bgra) {
    if (width == 0 || height == 0) {
        return fail("readTexture8: empty texture");
    }
    constexpr std::uint32_t kBytesPerPixel = 4;
    const std::uint32_t unpaddedRow = width * kBytesPerPixel;
    const std::uint32_t paddedRow = (unpaddedRow + 255u) / 256u * 256u; // WebGPU row alignment
    const std::uint64_t bufferSize = static_cast<std::uint64_t>(paddedRow) * height;

    wgpu::BufferDescriptor bufferDesc{};
    bufferDesc.label = "readback";
    bufferDesc.size = bufferSize;
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    wgpu::Buffer staging = context.device().CreateBuffer(&bufferDesc);

    wgpu::TexelCopyTextureInfo source{};
    source.texture = texture;
    source.mipLevel = 0;
    source.origin = {0, 0, 0};
    wgpu::TexelCopyBufferInfo destination{};
    destination.buffer = staging;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = paddedRow;
    destination.layout.rowsPerImage = height;
    wgpu::Extent3D extent{width, height, 1};

    wgpu::CommandEncoder encoder = context.device().CreateCommandEncoder();
    encoder.CopyTextureToBuffer(&source, &destination, &extent);
    wgpu::CommandBuffer commands = encoder.Finish();
    context.queue().Submit(1, &commands);

    bool mapped = false;
    std::string mapError;
    auto future = staging.MapAsync(wgpu::MapMode::Read, 0, static_cast<std::size_t>(bufferSize),
                                   wgpu::CallbackMode::WaitAnyOnly,
                                   [&](wgpu::MapAsyncStatus status, wgpu::StringView message) {
                                       mapped = status == wgpu::MapAsyncStatus::Success;
                                       if (!mapped) {
                                           mapError = Context::toString(message);
                                       }
                                   });
    if (!context.waitFor(future) || !mapped) {
        return fail("readback map failed: {}", mapError.empty() ? "timeout" : mapError);
    }

    Image8 image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * kBytesPerPixel);
    const auto* data = static_cast<const std::uint8_t*>(staging.GetConstMappedRange(0, static_cast<std::size_t>(bufferSize)));
    if (data == nullptr) {
        staging.Unmap();
        return fail("readback: mapped range unavailable");
    }
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = data + static_cast<std::size_t>(y) * paddedRow;
        std::uint8_t* dst = image.rgba.data() + static_cast<std::size_t>(y) * unpaddedRow;
        if (bgra) {
            for (std::uint32_t x = 0; x < width; ++x) {
                dst[x * 4 + 0] = row[x * 4 + 2];
                dst[x * 4 + 1] = row[x * 4 + 1];
                dst[x * 4 + 2] = row[x * 4 + 0];
                dst[x * 4 + 3] = row[x * 4 + 3];
            }
        } else {
            std::memcpy(dst, row, unpaddedRow);
        }
    }
    staging.Unmap();
    return image;
}

std::uint64_t hashImage(const Image8& image) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto byte : image.rgba) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

Result<void> writePpm(const Image8& image, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return fail("cannot write '{}'", path.string());
    }
    out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        out.write(reinterpret_cast<const char*>(&image.rgba[i]), 3);
    }
    return {};
}

} // namespace avgen::gpu
