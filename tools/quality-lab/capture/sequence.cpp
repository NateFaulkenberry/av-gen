#include "capture/sequence.hpp"

#include "assets/exr.hpp"
#include "assets/image.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace avgen::quality {
namespace {

// The trailing digit run of a stem, as a number. Returns false when there is none, which is how a
// file that is not a numbered frame is rejected rather than sorted to zero.
bool trailingNumber(const std::string& stem, std::uint64_t& out) {
    std::size_t end = stem.size();
    std::size_t begin = end;
    while (begin > 0 && std::isdigit(static_cast<unsigned char>(stem[begin - 1])) != 0) {
        --begin;
    }
    if (begin == end) {
        return false;
    }
    out = 0;
    for (std::size_t i = begin; i < end; ++i) {
        out = out * 10 + static_cast<std::uint64_t>(stem[i] - '0');
    }
    return true;
}

} // namespace

Result<Frame> readFrame(const std::filesystem::path& path) {
    auto texture = assets::loadImage(path, /*srgb=*/true);
    if (!texture) {
        return std::unexpected(texture.error());
    }
    if (texture->format != scene::TextureFormat::Rgba8Unorm &&
        texture->format != scene::TextureFormat::Rgba8Srgb) {
        return fail("quality: '{}' is not an 8-bit image; a beauty frame must be the PNG the "
                    "render wrote, not an HDR file",
                    path.string());
    }
    Frame frame;
    frame.width = texture->width;
    frame.height = texture->height;
    frame.rgba = std::move(texture->data);
    if (!frame.valid()) {
        return fail("quality: '{}' decoded to {}x{} but {} bytes", path.string(), frame.width,
                    frame.height, frame.rgba.size());
    }
    return frame;
}

Result<Plane> readPlane(const std::filesystem::path& path) {
    auto texture = assets::readExr(path);
    if (!texture) {
        return std::unexpected(texture.error());
    }
    const std::span<const float> pixels = assets::floatPixels(*texture);
    if (pixels.empty()) {
        return fail("quality: '{}' did not decode as float RGBA", path.string());
    }
    Plane plane;
    plane.width = texture->width;
    plane.height = texture->height;
    plane.rgba.assign(pixels.begin(), pixels.end());
    if (!plane.valid()) {
        return fail("quality: '{}' decoded to {}x{} but {} floats", path.string(), plane.width,
                    plane.height, plane.rgba.size());
    }
    return plane;
}

Result<void> writeFrame(const std::filesystem::path& path, const Frame& frame) {
    if (!frame.valid()) {
        return fail("quality: refusing to write an invalid frame to '{}'", path.string());
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return assets::writePng(path, frame.width, frame.height, frame.rgba);
}

Result<Sequence> discoverSequence(const std::filesystem::path& directory,
                                  std::string_view extension) {
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return fail("quality: '{}' is not a directory", directory.string());
    }
    std::vector<std::pair<std::uint64_t, std::filesystem::path>> numbered;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path& path = entry.path();
        if (path.extension().string() != extension) {
            continue;
        }
        // `frame_000123.normal.exr` has stem `frame_000123.normal`, whose trailing digit run is
        // empty -- so the AOV files fall out here without a name list to keep in step with
        // `RenderSettings::aovNames()`.
        std::uint64_t index = 0;
        if (!trailingNumber(path.stem().string(), index)) {
            continue;
        }
        numbered.emplace_back(index, path);
    }
    std::sort(numbered.begin(), numbered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    Sequence sequence;
    sequence.directory = directory;
    sequence.frames.reserve(numbered.size());
    for (auto& [index, path] : numbered) {
        sequence.frames.push_back(std::move(path));
    }
    if (sequence.frames.empty()) {
        return fail("quality: no numbered '{}' frames in '{}'", extension, directory.string());
    }
    return sequence;
}

std::filesystem::path aovPathFor(const std::filesystem::path& frame, std::string_view aov) {
    std::filesystem::path out = frame;
    out.replace_extension();
    out += ".";
    out += std::string(aov);
    out += ".exr";
    return out;
}

bool hasAov(const std::filesystem::path& frame, std::string_view aov) {
    std::error_code ec;
    return std::filesystem::is_regular_file(aovPathFor(frame, aov), ec);
}

} // namespace avgen::quality
