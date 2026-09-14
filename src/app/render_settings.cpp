#include "app/render_settings.hpp"

#include "rendering/render_quality.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <thread>

namespace avgen::app {

const char* renderOutputName(RenderOutput output) {
    switch (output) {
    case RenderOutput::Video: return "video";
    case RenderOutput::ExrSequence: return "exr";
    case RenderOutput::PngSequence: break;
    }
    return "sequence";
}

bool isSequence(RenderOutput output) {
    return output != RenderOutput::Video;
}

const char* RenderSettings::defaultPattern(RenderOutput output) {
    return output == RenderOutput::ExrSequence ? "frame_{:06d}.exr" : "frame_{:06d}.png";
}

void RenderSettings::normalisePattern() {
    if (output == RenderOutput::ExrSequence && pattern == defaultPattern(RenderOutput::PngSequence)) {
        pattern = defaultPattern(RenderOutput::ExrSequence);
    } else if (output == RenderOutput::PngSequence && pattern == defaultPattern(RenderOutput::ExrSequence)) {
        pattern = defaultPattern(RenderOutput::PngSequence);
    }
}

std::uint64_t RenderSettings::frameCount(double resolvedEndSeconds) const {
    const double span = std::max(0.0, resolvedEndSeconds - startSeconds);
    const auto n = static_cast<std::uint64_t>(std::ceil(span * fps - 1e-9));
    return std::max<std::uint64_t>(1, n);
}

double RenderSettings::resolvedEnd(double audioSeconds, double timelineSeconds) const {
    if (endSeconds >= 0.0) {
        return std::max(endSeconds, startSeconds);
    }
    if (audioSeconds > 0.0) {
        return audioSeconds;
    }
    if (timelineSeconds > 0.0) {
        return timelineSeconds;
    }
    return startSeconds + 10.0;
}

std::filesystem::path RenderSettings::frameFile(const std::filesystem::path& dir, std::uint64_t index) const {
    try {
        return dir / fmt::format(fmt::runtime(pattern), index);
    } catch (const fmt::format_error&) {
        return dir / fmt::format(fmt::runtime(defaultPattern(output)), index);
    }
}

namespace {

std::string lowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool isVideoExtension(std::string_view ext) {
    return ext == ".mov" || ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".m4v";
}

} // namespace

RenderOutput RenderSettings::outputForPath(const std::filesystem::path& path, RenderOutput fallback) {
    const std::string ext = lowerExtension(path);
    if (isVideoExtension(ext)) {
        return RenderOutput::Video;
    }
    if (ext == ".exr") {
        return RenderOutput::ExrSequence;
    }
    if (ext == ".png") {
        return RenderOutput::PngSequence;
    }
    return fallback;   // a directory, a bare name, or an extension that means nothing here
}

std::filesystem::path RenderSettings::withVideoExtension(const std::filesystem::path& path) {
    if (path.empty() || isVideoExtension(lowerExtension(path))) {
        return path;
    }
    // QuickTime, because it is the container ProRes needs and ProRes is the default codec; the
    // muxer accepts the rest and the person can type one if they want it.
    std::filesystem::path out = path;
    out.replace_extension(".mov");
    return out;
}

scene::DetailLimits RenderSettings::resolvedLimits() const {
    scene::DetailLimitMode mode = scene::DetailLimitMode::Tier;
    if (!limits.empty()) {
        // An unparseable value cannot reach here through `validate`, and if one does the answer is
        // the live picture: the conservative reading of a setting nobody understood.
        static_cast<void>(scene::detailLimitModeFromName(limits, mode));
    }
    if (mode == scene::DetailLimitMode::Live) {
        return scene::DetailLimits{};
    }
    if (mode == scene::DetailLimitMode::Unlimited) {
        return scene::DetailLimits::unlimited();
    }
    // "tier": the Offline tier is the one that promises no representation shortcut (§5.9), so it
    // is the one that lifts these. Every other tier is a preview of the live picture and keeps
    // them -- including a `--render` at `--tier realtime`, which exists precisely to be fast.
    rendering::QualityTier resolved = rendering::QualityTier::Offline;
    if (!rendering::qualityTierFromName(tier, resolved)) {
        resolved = rendering::QualityTier::Offline; // the default the job also falls back to
    }
    return resolved == rendering::QualityTier::Offline ? scene::DetailLimits::unlimited()
                                                       : scene::DetailLimits{};
}

Result<void> RenderSettings::validate() const {
    if (width == 0 || height == 0 || width > 16384 || height > 16384) {
        return fail("render size {}x{} is out of range", width, height);
    }
    if (output == RenderOutput::Video && (width % 2 != 0 || height % 2 != 0)) {
        return fail("video output needs even dimensions ({}x{})", width, height);
    }
    if (!(fps > 0.0) || fps > 1000.0) {
        return fail("render fps {} is out of range", fps);
    }
    if (startSeconds < 0.0) {
        return fail("render start {} must be >= 0", startSeconds);
    }
    if (endSeconds >= 0.0 && endSeconds < startSeconds) {
        return fail("render end {} is before start {}", endSeconds, startSeconds);
    }
    if (quality < 0 || quality > 100) {
        return fail("render quality {} must be 0..100", quality);
    }
    // Validated here rather than discovered in the job, so a project file with a typo in it fails
    // when it is loaded and not two hours into a sequence.
    if (scene::DetailLimitMode mode{}; !limits.empty() && !scene::detailLimitModeFromName(limits, mode)) {
        return fail("render limits '{}' is not 'tier', 'live' or 'unlimited'", limits);
    }
    if (isSequence(output)) {
        if (pattern.find('{') == std::string::npos || pattern.find('}') == std::string::npos) {
            return fail("frame pattern '{}' needs a {{}} placeholder for the frame index", pattern);
        }
        try {
            (void)fmt::format(fmt::runtime(pattern), std::uint64_t{0});
        } catch (const fmt::format_error& e) {
            return fail("frame pattern '{}' is invalid: {}", pattern, e.what());
        }
    }
    return {};
}

nlohmann::json RenderSettings::toJson() const {
    return nlohmann::json{{"width", width},
                          {"height", height},
                          {"fps", fps},
                          {"start", startSeconds},
                          {"end", endSeconds},
                          {"output", renderOutputName(output)},
                          {"path", outputPath.generic_string()},
                          {"pattern", pattern},
                          {"codec", codec},
                          {"backend", backend},
                          {"quality", quality},
                          {"tier", tier},
                          {"limits", limits},
                          {"muxAudio", muxAudio},
                          {"encoderThreads", encoderThreads}};
}

Result<RenderSettings> RenderSettings::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'render' must be an object");
    }
    RenderSettings s;
    auto number = [&](const char* key, auto& out) -> Result<void> {
        if (const auto it = j.find(key); it != j.end()) {
            if (!it->is_number()) {
                return fail("render.{} must be a number", key);
            }
            out = static_cast<std::remove_reference_t<decltype(out)>>(it->get<double>());
        }
        return {};
    };
    auto text = [&](const char* key, std::string& out) -> Result<void> {
        if (const auto it = j.find(key); it != j.end()) {
            if (!it->is_string()) {
                return fail("render.{} must be a string", key);
            }
            out = it->get<std::string>();
        }
        return {};
    };
    if (auto r = number("width", s.width); !r) return std::unexpected(r.error());
    if (auto r = number("height", s.height); !r) return std::unexpected(r.error());
    if (auto r = number("fps", s.fps); !r) return std::unexpected(r.error());
    if (auto r = number("start", s.startSeconds); !r) return std::unexpected(r.error());
    if (auto r = number("end", s.endSeconds); !r) return std::unexpected(r.error());
    if (auto r = number("quality", s.quality); !r) return std::unexpected(r.error());
    if (auto r = number("encoderThreads", s.encoderThreads); !r) return std::unexpected(r.error());
    std::string output;
    if (auto r = text("output", output); !r) return std::unexpected(r.error());
    if (!output.empty()) {
        if (output == "video") {
            s.output = RenderOutput::Video;
        } else if (output == "sequence" || output == "png") {
            s.output = RenderOutput::PngSequence;
        } else if (output == "exr") {
            s.output = RenderOutput::ExrSequence;
        } else {
            return fail("render.output '{}' is not 'sequence', 'exr' or 'video'", output);
        }
    }
    std::string path;
    if (auto r = text("path", path); !r) return std::unexpected(r.error());
    s.outputPath = path;
    if (auto r = text("pattern", s.pattern); !r) return std::unexpected(r.error());
    s.normalisePattern(); // a missing pattern follows the output kind
    if (auto r = text("codec", s.codec); !r) return std::unexpected(r.error());
    if (auto r = text("tier", s.tier); !r) return std::unexpected(r.error());
    if (auto r = text("limits", s.limits); !r) return std::unexpected(r.error());
    if (auto r = text("backend", s.backend); !r) return std::unexpected(r.error());
    if (const auto it = j.find("muxAudio"); it != j.end()) {
        if (!it->is_boolean()) {
            return fail("render.muxAudio must be a boolean");
        }
        s.muxAudio = it->get<bool>();
    }
    if (auto r = s.validate(); !r) {
        return std::unexpected(r.error());
    }
    return s;
}

} // namespace avgen::app
