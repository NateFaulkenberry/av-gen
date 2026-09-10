#include "gpu/texture.hpp"

#include "gpu/context.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace avgen::gpu {

namespace {

float srgbToLinear(std::uint8_t v) {
    const float c = static_cast<float>(v) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

std::uint8_t linearToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(s * 255.0f));
}

// Downsamples an RGBA8 level by 2 (box filter). Colour channels are filtered in linear space
// when srgb is set; alpha is always linear.
std::vector<std::uint8_t> downsample8(const std::vector<std::uint8_t>& src, std::uint32_t w, std::uint32_t h,
                                      std::uint32_t dw, std::uint32_t dh, bool srgb) {
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(dw) * dh * 4);
    for (std::uint32_t y = 0; y < dh; ++y) {
        for (std::uint32_t x = 0; x < dw; ++x) {
            float acc[4] = {0, 0, 0, 0};
            int n = 0;
            for (std::uint32_t sy = y * 2; sy < std::min(y * 2 + 2, h); ++sy) {
                for (std::uint32_t sx = x * 2; sx < std::min(x * 2 + 2, w); ++sx) {
                    const std::uint8_t* p = src.data() + (static_cast<std::size_t>(sy) * w + sx) * 4;
                    for (int c = 0; c < 3; ++c) {
                        acc[c] += srgb ? srgbToLinear(p[c]) : static_cast<float>(p[c]) / 255.0f;
                    }
                    acc[3] += static_cast<float>(p[3]) / 255.0f;
                    ++n;
                }
            }
            std::uint8_t* q = dst.data() + (static_cast<std::size_t>(y) * dw + x) * 4;
            const float inv = 1.0f / static_cast<float>(std::max(n, 1));
            for (int c = 0; c < 3; ++c) {
                q[c] = srgb ? linearToSrgb(acc[c] * inv)
                            : static_cast<std::uint8_t>(std::lround(std::clamp(acc[c] * inv, 0.0f, 1.0f) * 255.0f));
            }
            q[3] = static_cast<std::uint8_t>(std::lround(std::clamp(acc[3] * inv, 0.0f, 1.0f) * 255.0f));
        }
    }
    return dst;
}

std::vector<std::uint8_t> downsampleF(const std::vector<std::uint8_t>& src, std::uint32_t w, std::uint32_t h,
                                      std::uint32_t dw, std::uint32_t dh) {
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(dw) * dh * 16);
    const float* in = reinterpret_cast<const float*>(src.data());
    float* out = reinterpret_cast<float*>(dst.data());
    for (std::uint32_t y = 0; y < dh; ++y) {
        for (std::uint32_t x = 0; x < dw; ++x) {
            float acc[4] = {0, 0, 0, 0};
            int n = 0;
            for (std::uint32_t sy = y * 2; sy < std::min(y * 2 + 2, h); ++sy) {
                for (std::uint32_t sx = x * 2; sx < std::min(x * 2 + 2, w); ++sx) {
                    const float* p = in + (static_cast<std::size_t>(sy) * w + sx) * 4;
                    for (int c = 0; c < 4; ++c) {
                        acc[c] += p[c];
                    }
                    ++n;
                }
            }
            float* q = out + (static_cast<std::size_t>(y) * dw + x) * 4;
            for (int c = 0; c < 4; ++c) {
                q[c] = acc[c] / static_cast<float>(std::max(n, 1));
            }
        }
    }
    return dst;
}

void writeLevel(Context& context, const wgpu::Texture& texture, std::uint32_t level, std::uint32_t w, std::uint32_t h,
                std::uint32_t bytesPerPixel, const std::vector<std::uint8_t>& pixels) {
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = texture;
    dst.mipLevel = level;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = w * bytesPerPixel;
    layout.rowsPerImage = h;
    wgpu::Extent3D extent{w, h, 1};
    context.queue().WriteTexture(&dst, pixels.data(), pixels.size(), &layout, &extent);
}

} // namespace

std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height) {
    std::uint32_t levels = 1;
    std::uint32_t size = std::max(width, height);
    while (size > 1) {
        size /= 2;
        ++levels;
    }
    return levels;
}

wgpu::TextureFormat toWgpuFormat(scene::TextureFormat format) {
    switch (format) {
    case scene::TextureFormat::Rgba8Srgb: return wgpu::TextureFormat::RGBA8UnormSrgb;
    case scene::TextureFormat::Rgba32Float: return wgpu::TextureFormat::RGBA32Float;
    case scene::TextureFormat::Rgba8Unorm:
    default: return wgpu::TextureFormat::RGBA8Unorm;
    }
}

Result<GpuTexture> uploadTexture(Context& context, const scene::TextureData& data, bool mips) {
    if (!data.valid()) {
        return fail("texture '{}' is invalid ({}x{}, {} bytes)", data.name, data.width, data.height, data.data.size());
    }
    GpuTexture out;
    out.width = data.width;
    out.height = data.height;
    out.format = toWgpuFormat(data.format);
    out.mipLevels = mips ? mipLevelCount(data.width, data.height) : 1;
    const bool isFloat = data.isHdr();
    const std::uint32_t bpp = isFloat ? 16 : 4;

    wgpu::TextureDescriptor desc{};
    desc.label = data.name.c_str();
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {data.width, data.height, 1};
    desc.format = out.format;
    desc.mipLevelCount = out.mipLevels;
    out.texture = context.device().CreateTexture(&desc);
    if (!out.texture) {
        return fail("failed to create texture '{}'", data.name);
    }

    std::vector<std::uint8_t> level = data.data;
    std::uint32_t w = data.width;
    std::uint32_t h = data.height;
    writeLevel(context, out.texture, 0, w, h, bpp, level);
    for (std::uint32_t mip = 1; mip < out.mipLevels; ++mip) {
        const std::uint32_t dw = std::max(1u, w / 2);
        const std::uint32_t dh = std::max(1u, h / 2);
        level = isFloat ? downsampleF(level, w, h, dw, dh)
                        : downsample8(level, w, h, dw, dh, data.format == scene::TextureFormat::Rgba8Srgb);
        w = dw;
        h = dh;
        writeLevel(context, out.texture, mip, w, h, bpp, level);
    }
    wgpu::TextureViewDescriptor viewDesc{};
    viewDesc.label = data.name.c_str();
    viewDesc.dimension = wgpu::TextureViewDimension::e2D;
    viewDesc.mipLevelCount = out.mipLevels;
    out.view = out.texture.CreateView(&viewDesc);
    return out;
}

std::uint16_t floatToHalf(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7FFFFFu;
    if (((bits >> 23) & 0xFFu) == 0xFFu) { // inf / nan
        return static_cast<std::uint16_t>(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
    }
    if (exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7C00u); // overflow -> inf
    }
    if (exponent <= 0) {
        if (exponent < -10) {
            return static_cast<std::uint16_t>(sign);
        }
        mantissa |= 0x800000u;
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - exponent);
        std::uint32_t half = mantissa >> shift;
        const std::uint32_t rem = mantissa & ((1u << shift) - 1u);
        if (rem > (1u << (shift - 1)) || (rem == (1u << (shift - 1)) && (half & 1u))) {
            ++half;
        }
        return static_cast<std::uint16_t>(sign | half);
    }
    std::uint32_t half = (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13);
    const std::uint32_t rem = mantissa & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) {
        ++half; // may carry into the exponent, which is the correct rounding behaviour
    }
    return static_cast<std::uint16_t>(sign | half);
}

float halfToFloat(std::uint16_t half) {
    const std::uint32_t sign = static_cast<std::uint32_t>(half >> 15) << 31;
    const std::uint32_t exponent = (half >> 10) & 0x1Fu;
    std::uint32_t mantissa = half & 0x3FFu;
    std::uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign; // signed zero
        } else {
            // Subnormal half: normalise by shifting until the implicit bit is at bit 10.
            std::uint32_t shift = 0;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                ++shift;
            }
            mantissa &= 0x3FFu;
            bits = sign | ((113u - shift) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7F800000u | (mantissa << 13); // inf / nan
    } else {
        bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    }
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void halfToFloatArray(const std::uint16_t* halves, float* out, std::size_t count) {
    static const std::vector<float> table = [] {
        std::vector<float> t(65536);
        for (std::uint32_t i = 0; i < 65536; ++i) {
            t[i] = halfToFloat(static_cast<std::uint16_t>(i));
        }
        return t;
    }();
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = table[halves[i]];
    }
}

Result<GpuTexture> uploadTextureAsHalf(Context& context, const scene::TextureData& data, bool mips) {
    if (!data.valid() || !data.isHdr()) {
        return fail("uploadTextureAsHalf requires a valid Rgba32Float image ('{}')", data.name);
    }
    GpuTexture out;
    out.width = data.width;
    out.height = data.height;
    out.format = wgpu::TextureFormat::RGBA16Float;
    out.mipLevels = mips ? mipLevelCount(data.width, data.height) : 1;
    wgpu::TextureDescriptor desc{};
    desc.label = data.name.c_str();
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {data.width, data.height, 1};
    desc.format = out.format;
    desc.mipLevelCount = out.mipLevels;
    out.texture = context.device().CreateTexture(&desc);
    if (!out.texture) {
        return fail("failed to create half-float texture '{}'", data.name);
    }
    std::vector<std::uint8_t> level = data.data;
    std::uint32_t w = data.width;
    std::uint32_t h = data.height;
    auto upload = [&](std::uint32_t mip, std::uint32_t lw, std::uint32_t lh, const std::vector<std::uint8_t>& floats) {
        std::vector<std::uint8_t> halves(static_cast<std::size_t>(lw) * lh * 8);
        const float* in = reinterpret_cast<const float*>(floats.data());
        std::uint16_t* outHalf = reinterpret_cast<std::uint16_t*>(halves.data());
        // A real HDR sky's sun or moon disc runs past the half-float range -- Kloppenheim 02's
        // moon peaks at 1.0e5 at 4K -- and one +inf texel does not stay local: it survives
        // filtering, reaches the bloom pyramid and blackens a block of the frame hundreds of
        // pixels wide. Saturating costs nothing, because no tone map resolves 65504 from 1.0e5.
        constexpr float kHalfMax = 65504.0f;
        for (std::size_t i = 0; i < static_cast<std::size_t>(lw) * lh * 4; ++i) {
            const float v = in[i];
            outHalf[i] = floatToHalf(std::isnan(v) ? 0.0f : std::clamp(v, -kHalfMax, kHalfMax));
        }
        writeLevel(context, out.texture, mip, lw, lh, 8, halves);
    };
    upload(0, w, h, level);
    for (std::uint32_t mip = 1; mip < out.mipLevels; ++mip) {
        const std::uint32_t dw = std::max(1u, w / 2);
        const std::uint32_t dh = std::max(1u, h / 2);
        level = downsampleF(level, w, h, dw, dh);
        w = dw;
        h = dh;
        upload(mip, w, h, level);
    }
    wgpu::TextureViewDescriptor viewDesc{};
    viewDesc.label = data.name.c_str();
    viewDesc.dimension = wgpu::TextureViewDimension::e2D;
    viewDesc.mipLevelCount = out.mipLevels;
    out.view = out.texture.CreateView(&viewDesc);
    return out;
}

GpuTexture solidTexture(Context& context, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a, bool srgb,
                        const char* label) {
    scene::TextureData data;
    data.name = label;
    data.width = 1;
    data.height = 1;
    data.format = srgb ? scene::TextureFormat::Rgba8Srgb : scene::TextureFormat::Rgba8Unorm;
    data.data = {r, g, b, a};
    auto tex = uploadTexture(context, data, false);
    return tex ? *tex : GpuTexture{};
}

const wgpu::Sampler& SamplerCache::get(scene::WrapMode wrapU, scene::WrapMode wrapV, bool linear) {
    const std::uint32_t key = static_cast<std::uint32_t>(wrapU) | (static_cast<std::uint32_t>(wrapV) << 4) |
                              (linear ? 1u << 8 : 0u);
    if (auto it = samplers_.find(key); it != samplers_.end()) {
        return it->second;
    }
    auto toWgpu = [](scene::WrapMode m) {
        switch (m) {
        case scene::WrapMode::Clamp: return wgpu::AddressMode::ClampToEdge;
        case scene::WrapMode::Mirror: return wgpu::AddressMode::MirrorRepeat;
        default: return wgpu::AddressMode::Repeat;
        }
    };
    wgpu::SamplerDescriptor desc{};
    desc.label = "material-sampler";
    desc.addressModeU = toWgpu(wrapU);
    desc.addressModeV = toWgpu(wrapV);
    desc.addressModeW = wgpu::AddressMode::Repeat;
    desc.magFilter = linear ? wgpu::FilterMode::Linear : wgpu::FilterMode::Nearest;
    desc.minFilter = linear ? wgpu::FilterMode::Linear : wgpu::FilterMode::Nearest;
    desc.mipmapFilter = linear ? wgpu::MipmapFilterMode::Linear : wgpu::MipmapFilterMode::Nearest;
    desc.maxAnisotropy = linear ? 8 : 1;
    return samplers_.emplace(key, context_.device().CreateSampler(&desc)).first->second;
}

} // namespace avgen::gpu
