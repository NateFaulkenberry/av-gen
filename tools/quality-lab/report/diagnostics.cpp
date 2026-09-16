#include "report/diagnostics.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::quality {
namespace {

// Black -> red -> yellow -> white. Chosen over a rainbow because it is monotone in luminance, so it
// survives being looked at in greyscale and does not invent a boundary where the data has none.
void ramp(double t, std::uint8_t* out) {
    const double c = std::clamp(t, 0.0, 1.0);
    const double r = std::clamp(c * 3.0, 0.0, 1.0);
    const double g = std::clamp(c * 3.0 - 1.0, 0.0, 1.0);
    const double b = std::clamp(c * 3.0 - 2.0, 0.0, 1.0);
    out[0] = static_cast<std::uint8_t>(std::lround(r * 255.0));
    out[1] = static_cast<std::uint8_t>(std::lround(g * 255.0));
    out[2] = static_cast<std::uint8_t>(std::lround(b * 255.0));
    out[3] = 255;
}

} // namespace

Frame differenceImage(const Frame& a, const Frame& b, double amplification) {
    Frame out;
    if (!a.valid() || !b.valid() || !a.sameShapeAs(b)) {
        return out;
    }
    out.width = a.width;
    out.height = a.height;
    out.rgba.assign(a.rgba.size(), 255);
    for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            const double d = std::abs(static_cast<double>(a.rgba[i + c]) -
                                      static_cast<double>(b.rgba[i + c])) *
                             amplification;
            out.rgba[i + c] = static_cast<std::uint8_t>(std::clamp(d, 0.0, 255.0));
        }
        out.rgba[i + 3] = 255;
    }
    return out;
}

Frame heatmapImage(const std::vector<float>& values, std::uint32_t width, std::uint32_t height,
                   double fullScale) {
    Frame out;
    if (values.size() != static_cast<std::size_t>(width) * height || width == 0 || height == 0) {
        return out;
    }
    out.width = width;
    out.height = height;
    out.rgba.assign(values.size() * 4, 255);
    const double scale = fullScale > 0.0 ? fullScale : 1.0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        ramp(static_cast<double>(values[i]) / scale, out.rgba.data() + i * 4);
    }
    return out;
}

Frame maskImage(const std::vector<std::uint8_t>& mask, std::uint32_t width, std::uint32_t height) {
    Frame out;
    if (mask.size() != static_cast<std::size_t>(width) * height || width == 0 || height == 0) {
        return out;
    }
    out.width = width;
    out.height = height;
    out.rgba.assign(mask.size() * 4, 255);
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const std::uint8_t v = mask[i] != 0 ? 255 : 0;
        out.rgba[i * 4] = v;
        out.rgba[i * 4 + 1] = v;
        out.rgba[i * 4 + 2] = v;
        out.rgba[i * 4 + 3] = 255;
    }
    return out;
}

std::vector<float> laplacianPlane(const Frame& frame) {
    std::vector<float> out(static_cast<std::size_t>(frame.width) * frame.height, 0.0f);
    if (!frame.valid() || frame.width < 3 || frame.height < 3) {
        return out;
    }
    const std::vector<float> y = luma(frame);
    const std::uint32_t w = frame.width;
    // Borders are skipped, not clamped: a clamped edge pixel reports a Laplacian that is an artifact
    // of the clamp (artifact-detection.md §2).
    for (std::uint32_t j = 1; j + 1 < frame.height; ++j) {
        for (std::uint32_t i = 1; i + 1 < w; ++i) {
            const std::size_t k = static_cast<std::size_t>(j) * w + i;
            const double value = 4.0 * static_cast<double>(y[k]) - static_cast<double>(y[k - 1]) -
                                 static_cast<double>(y[k + 1]) - static_cast<double>(y[k - w]) -
                                 static_cast<double>(y[k + w]);
            out[k] = static_cast<float>(std::abs(value));
        }
    }
    return out;
}

TileMap tileMap(const std::vector<float>& values, std::uint32_t width, std::uint32_t height,
                std::uint32_t tileSize) {
    TileMap map;
    if (values.size() != static_cast<std::size_t>(width) * height || tileSize == 0) {
        return map;
    }
    map.tileSize = tileSize;
    map.tilesX = (width + tileSize - 1) / tileSize;
    map.tilesY = (height + tileSize - 1) / tileSize;
    map.means.assign(static_cast<std::size_t>(map.tilesX) * map.tilesY, 0.0);
    for (std::uint32_t ty = 0; ty < map.tilesY; ++ty) {
        for (std::uint32_t tx = 0; tx < map.tilesX; ++tx) {
            double total = 0.0;
            std::size_t counted = 0;
            for (std::uint32_t y = ty * tileSize; y < std::min(height, (ty + 1) * tileSize); ++y) {
                for (std::uint32_t x = tx * tileSize; x < std::min(width, (tx + 1) * tileSize);
                     ++x) {
                    total += static_cast<double>(values[static_cast<std::size_t>(y) * width + x]);
                    ++counted;
                }
            }
            const double mean = counted > 0 ? total / static_cast<double>(counted) : 0.0;
            map.means[static_cast<std::size_t>(ty) * map.tilesX + tx] = mean;
            if (mean > map.worstTileMean) {
                map.worstTileMean = mean;
                map.worstTileX = tx * tileSize;
                map.worstTileY = ty * tileSize;
            }
        }
    }
    return map;
}

} // namespace avgen::quality
