#include "gpu/readback.hpp"

#include "gpu/context.hpp"
#include "gpu/texture.hpp"

#include <cstring>
#include <fstream>

namespace avgen::gpu {

namespace {

// Copies a 2D texture into a fresh staging buffer, maps it and returns the tightly packed rows.
Result<std::vector<std::uint8_t>> readTextureRaw(Context& context, const wgpu::Texture& texture, std::uint32_t width,
                                                 std::uint32_t height, std::uint32_t bytesPerPixel,
                                                 std::uint32_t originX = 0, std::uint32_t originY = 0) {
    if (width == 0 || height == 0) {
        return fail("readTexture: empty texture");
    }
    const std::uint32_t unpaddedRow = width * bytesPerPixel;
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
    source.origin = {originX, originY, 0};
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
    const auto* data = static_cast<const std::uint8_t*>(staging.GetConstMappedRange(0, static_cast<std::size_t>(bufferSize)));
    if (data == nullptr) {
        staging.Unmap();
        return fail("readback: mapped range unavailable");
    }
    std::vector<std::uint8_t> rows(static_cast<std::size_t>(unpaddedRow) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(rows.data() + static_cast<std::size_t>(y) * unpaddedRow, data + static_cast<std::size_t>(y) * paddedRow,
                    unpaddedRow);
    }
    staging.Unmap();
    return rows;
}

} // namespace

Result<Image8> readTexture8(Context& context, const wgpu::Texture& texture, std::uint32_t width,
                            std::uint32_t height, bool bgra) {
    auto rows = readTextureRaw(context, texture, width, height, 4);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    Image8 image;
    image.width = width;
    image.height = height;
    image.rgba = std::move(*rows);
    if (bgra) {
        for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            std::swap(image.rgba[i], image.rgba[i + 2]);
        }
    }
    return image;
}

Result<ImageF> readTextureF16(Context& context, const wgpu::Texture& texture, std::uint32_t width,
                              std::uint32_t height) {
    auto rows = readTextureRaw(context, texture, width, height, 8);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    ImageF image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    std::vector<std::uint16_t> halves(image.rgba.size());
    std::memcpy(halves.data(), rows->data(), halves.size() * 2);
    halfToFloatArray(halves.data(), image.rgba.data(), halves.size());
    return image;
}

Result<std::vector<std::uint32_t>> readTextureR32Uint(Context& context, const wgpu::Texture& texture,
                                                      std::uint32_t width, std::uint32_t height) {
    auto rows = readTextureRaw(context, texture, width, height, 4);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    std::vector<std::uint32_t> values(static_cast<std::size_t>(width) * height);
    std::memcpy(values.data(), rows->data(), values.size() * sizeof(std::uint32_t));
    return values;
}

Result<std::vector<std::uint8_t>> readBuffer(Context& context, const wgpu::Buffer& buffer, std::uint64_t offset,
                                             std::uint64_t size) {
    if (size == 0 || size % 4 != 0 || offset % 4 != 0) {
        return fail("readBuffer: offset and size must be non-zero multiples of 4 (got {} + {})", offset, size);
    }
    const std::uint64_t paddedSize = size;
    wgpu::BufferDescriptor bufferDesc{};
    bufferDesc.label = "readback-buffer";
    bufferDesc.size = paddedSize;
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    wgpu::Buffer staging = context.device().CreateBuffer(&bufferDesc);

    wgpu::CommandEncoder encoder = context.device().CreateCommandEncoder();
    encoder.CopyBufferToBuffer(buffer, offset, staging, 0, paddedSize);
    wgpu::CommandBuffer commands = encoder.Finish();
    context.queue().Submit(1, &commands);

    bool mapped = false;
    std::string mapError;
    auto future = staging.MapAsync(wgpu::MapMode::Read, 0, static_cast<std::size_t>(paddedSize),
                                   wgpu::CallbackMode::WaitAnyOnly,
                                   [&](wgpu::MapAsyncStatus status, wgpu::StringView message) {
                                       mapped = status == wgpu::MapAsyncStatus::Success;
                                       if (!mapped) {
                                           mapError = Context::toString(message);
                                       }
                                   });
    if (!context.waitFor(future) || !mapped) {
        return fail("readBuffer map failed: {}", mapError.empty() ? "timeout" : mapError);
    }
    const auto* data = static_cast<const std::uint8_t*>(staging.GetConstMappedRange(0, static_cast<std::size_t>(paddedSize)));
    if (data == nullptr) {
        staging.Unmap();
        return fail("readBuffer: mapped range unavailable");
    }
    std::vector<std::uint8_t> bytes(data, data + size);
    staging.Unmap();
    return bytes;
}

std::uint64_t hashImage(const Image8& image) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto byte : image.rgba) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t hashImage(const ImageF& image) {
    // FNV-1a over the 32-bit float bit patterns (one step per value: a 1080p frame is 8M floats).
    std::uint64_t hash = 1469598103934665603ull;
    for (const float value : image.rgba) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        hash ^= bits;
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


Result<std::uint32_t> readTexelR32Uint(Context& context, const wgpu::Texture& texture, std::uint32_t x,
                                       std::uint32_t y) {
    // One texel, not one frame. A full-screen identifier readback is five megabytes and a GPU
    // stall; a click needs four bytes. The row still costs the WebGPU 256-byte alignment, which is
    // the smallest a copy can be and is not worth avoiding.
    auto raw = readTextureRaw(context, texture, 1, 1, 4, x, y);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    std::uint32_t value = 0;
    std::memcpy(&value, raw->data(), sizeof(value));
    return value;
}

Result<std::vector<std::uint32_t>> readTexelsR32(Context& context,
                                                std::span<const TexelRequest> requests) {
    if (requests.empty()) {
        return std::vector<std::uint32_t>{};
    }
    // WebGPU requires a texture-to-buffer copy's offset to be a multiple of 256, so each texel gets
    // its own 256-byte slice. The buffer is a couple of kilobytes for a realistic batch; the thing
    // being saved is the wait, not the memory.
    constexpr std::uint64_t kSlice = 256;
    const std::uint64_t bufferSize = kSlice * requests.size();

    wgpu::BufferDescriptor bufferDesc{};
    bufferDesc.label = "readback-texels";
    bufferDesc.size = bufferSize;
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    wgpu::Buffer staging = context.device().CreateBuffer(&bufferDesc);

    wgpu::CommandEncoder encoder = context.device().CreateCommandEncoder();
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const TexelRequest& r = requests[i];
        if (r.texture == nullptr || *r.texture == nullptr) {
            return fail("readTexelsR32: request {} names no texture", i);
        }
        wgpu::TexelCopyTextureInfo source{};
        source.texture = *r.texture;
        source.mipLevel = 0;
        source.origin = {r.x, r.y, 0};
        wgpu::TexelCopyBufferInfo destination{};
        destination.buffer = staging;
        destination.layout.offset = kSlice * i;
        destination.layout.bytesPerRow = static_cast<std::uint32_t>(kSlice);
        destination.layout.rowsPerImage = 1;
        wgpu::Extent3D extent{1, 1, 1};
        encoder.CopyTextureToBuffer(&source, &destination, &extent);
    }
    wgpu::CommandBuffer commands = encoder.Finish();
    // One submission, and below it one wait, however many texels were asked for. That is the
    // entire point of the function.
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
        return fail("readTexelsR32 map failed: {}", mapError.empty() ? "timeout" : mapError);
    }
    const auto* data = static_cast<const std::uint8_t*>(
        staging.GetConstMappedRange(0, static_cast<std::size_t>(bufferSize)));
    if (data == nullptr) {
        staging.Unmap();
        return fail("readTexelsR32: mapped range unavailable");
    }
    std::vector<std::uint32_t> out(requests.size());
    for (std::size_t i = 0; i < requests.size(); ++i) {
        std::memcpy(&out[i], data + kSlice * i, sizeof(std::uint32_t));
    }
    staging.Unmap();
    return out;
}

Result<float> readTexelR32Float(Context& context, const wgpu::Texture& texture, std::uint32_t x,
                                std::uint32_t y) {
    auto raw = readTextureRaw(context, texture, 1, 1, 4, x, y);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    float value = 0.0f;
    std::memcpy(&value, raw->data(), sizeof(value));
    return value;
}

} // namespace avgen::gpu
