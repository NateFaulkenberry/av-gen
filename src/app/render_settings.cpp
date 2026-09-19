#include "app/render_settings.hpp"

#include "app/frame_range.hpp"

#include "scene/scene.hpp"

#include "rendering/render_quality.hpp"
#include "pathtrace/path_tracer.hpp"

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

// ADR-383: the range arithmetic has one home now. `RenderSettings` carried its own copy of these
// two, and a second renderer growing a third copy is how two deliverables of one project end up a
// frame apart. `frameRange()` is the seam; the behaviour is unchanged, and the assertions in
// `test_render_settings.cpp` that predate this are what say so.
FrameRange RenderSettings::frameRange() const {
    return FrameRange{startSeconds, endSeconds, fps};
}

std::uint64_t RenderSettings::frameCount(double resolvedEndSeconds) const {
    return frameRange().frameCount(resolvedEndSeconds);
}

double RenderSettings::resolvedEnd(double audioSeconds, double timelineSeconds) const {
    return frameRange().resolvedEnd(audioSeconds, timelineSeconds);
}

std::span<const std::string_view> RenderSettings::aovNames() {
    // Each name is a target the scene pass already writes every frame, and each answers a question a
    // compositor actually asks. Nothing is here because it was cheap to add -- the mandate's own rule
    // is that a target must correspond to meaningful renderer state, and a pass nobody consumes is
    // the failure mode it warns about from the other direction.
    //
    //   normal    RGB world-space normal, roughness in alpha -- relighting, and the one target the
    //             renderer has always written and nothing has ever read.
    //   emission  the emissive term before bloom and before tone mapping -- a glow pass in comp.
    //   depth     linear depth in metres against the far plane -- depth of field, fog, depth merge.
    //   velocity  screen-space motion in R and G -- motion blur in comp.
    //   id        the per-object identifier picking reads -- per-object mattes.
    //   shadow    ADR-255: the directional shadow-map visibility, rgb = lights 0/1/2, a = the view
    //             depth it was computed at. The one name here that is NOT a target the scene pass
    //             already writes: at `high` and `offline` -- the tiers an offline render uses --
    //             no shadow mask is built at all, because the lit pass computes the term inline.
    //             Asking for it runs a dedicated full-resolution pass that would not otherwise
    //             exist, and the exported term is therefore RECOMPUTED rather than captured.
    static constexpr std::string_view kNames[] = {"normal", "emission", "depth",
                                                  "velocity", "id", "shadow"};
    return kNames;
}

Result<std::vector<std::string>> RenderSettings::aovList() const {
    std::vector<std::string> out;
    const auto names = aovNames();
    std::size_t start = 0;
    while (start <= aovs.size()) {
        const std::size_t comma = aovs.find(',', start);
        std::string one = aovs.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        // Trim, because "normal, depth" is what a person types and refusing it would be pedantry.
        const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
        one.erase(one.begin(), std::find_if(one.begin(), one.end(), notSpace));
        one.erase(std::find_if(one.rbegin(), one.rend(), notSpace).base(), one.end());
        if (!one.empty()) {
            if (std::find(names.begin(), names.end(), one) == names.end()) {
                return fail("render aov '{}' is not one of normal, emission, depth, velocity, id, shadow",
                            one);
            }
            if (std::find(out.begin(), out.end(), one) == out.end()) {
                out.push_back(std::move(one));
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

std::filesystem::path RenderSettings::aovFile(const std::filesystem::path& dir, std::uint64_t index,
                                              std::string_view aov) const {
    // Built from the *frame* name so an AOV sorts next to the beauty frame it belongs to, whatever
    // the pattern is -- including a video render, where the beauty frame has no file of its own and
    // the pattern is still the thing that names an index.
    std::string name;
    try {
        name = fmt::format(fmt::runtime(pattern), index);
    } catch (const fmt::format_error&) {
        name = fmt::format(fmt::runtime(defaultPattern(RenderOutput::PngSequence)), index);
    }
    const std::size_t dot = name.rfind('.');
    if (dot != std::string::npos) {
        name.resize(dot);
    }
    return dir / fmt::format("{}.{}.exr", name, aov);
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
    return resolved == rendering::QualityTier::Offline ? scene::DetailLimits::offlineDefault()
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
    // ADR-212. The ceiling is the renderer's own: `QualitySettings::renderScale` clamps to 2, so a
    // larger number here would be silently truncated -- and a setting that quietly means something
    // other than what it says is worse than one that is refused.
    if (!(supersample >= 1.0f) || supersample > 2.0f) {
        return fail("render supersample {} must be 1 (off) to 2", supersample);
    }
    // Validated here rather than discovered in the job, so a project file with a typo in it fails
    // when it is loaded and not two hours into a sequence.
    if (auto list = aovList(); !list) {
        return std::unexpected(list.error());
    } else if (!list->empty()) {
        // ADR-242. The auxiliary targets are sized to the *scaled* resolution, so under
        // supersampling an AOV is 2x the beauty frame -- and it cannot simply be resolved down with
        // the beauty pass's filter. Averaging two normals is not a normal, averaging two object
        // identifiers is a third object, and averaging two depths across a silhouette is a surface
        // that is not there. Refused rather than written at a size that does not match, or resolved
        // by an operation that is wrong for three of the five.
        if (supersample != 1.0f) {
            return fail("render: aov export and supersample {} cannot be combined -- an identifier, "
                        "a normal and a depth edge have no correct downsample",
                        supersample);
        }
    }
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
                          {"supersample", supersample},
                          {"limits", limits},
                          {"aovs", aovs},
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
    if (j.contains("supersample")) {
        if (!j.at("supersample").is_number()) {
            return fail("render: 'supersample' must be a number");
        }
        s.supersample = j.at("supersample").get<float>();
    }
    if (auto r = text("limits", s.limits); !r) return std::unexpected(r.error());
    if (auto r = text("aovs", s.aovs); !r) return std::unexpected(r.error());
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

Result<void> shadowAovPreconditions(const scene::Scene& scene, std::string_view disabledPasses) {
    std::size_t casting = 0;
    for (const scene::PunctualLight& light : scene.lights) {
        if (light.type == scene::PunctualLight::Type::Directional && light.enabled &&
            light.castsShadow) {
            ++casting;
        }
    }
    if (casting == 0) {
        return avgen::fail(
            "render: --aov shadow needs a directional light that casts, and this scene has none. "
            "The shadow-map term of a scene lit only by point, spot or area lights -- or by a "
            "directional light with castsShadow off -- is the constant 1.0, and a constant that "
            "looks like a render is worse than a refusal (ADR-242, ADR-255)");
    }
    if (disabledPasses.find("shadows") != std::string_view::npos) {
        return avgen::fail(
            "render: --aov shadow and --disable shadows cannot be combined. The mask pass reads "
            "the shadow atlas the disabled passes would have drawn, and an undrawn atlas reads as "
            "an occluder in front of everything: measured, the exported plane marks 28.9% of the "
            "frame shadowed against 4.6% with shadows on (ADR-182, ADR-255)");
    }
    return {};
}



// ---- PathTraceSettings (ADR-366) -------------------------------------------------------------

Result<void> PathTraceSettings::validate() const {
    if (!(seconds >= 0.0) || !std::isfinite(seconds)) {
        return fail("pathtrace: 'seconds' must be a finite time at or after zero, got {}", seconds);
    }
    if (samplesPerPixel == 0) {
        return fail("pathtrace: 'samples' must be at least 1");
    }
    // The UI's slider stops at 1024 and the ceiling here is deliberately higher: a person who edits
    // the project file to ask for an overnight 4096-sample frame is not making a mistake. What is a
    // mistake is a number that cannot have been meant, so the refusal is at a value no render is.
    if (samplesPerPixel > 65536) {
        return fail("pathtrace: 'samples' of {} is not a render anybody asked for (max 65536)",
                    samplesPerPixel);
    }
    if (maxDepth > 64) {
        return fail("pathtrace: 'bounces' of {} is past any useful path length (max 64)", maxDepth);
    }
    if (threads > 1024) {
        return fail("pathtrace: 'threads' of {} is not a machine (max 1024; 0 means all of them)",
                    threads);
    }
    if (!std::isfinite(fps) || fps <= 0.0) {
        return fail("pathtrace: 'fps' must be a finite positive number, got {}", fps);
    }
    if (isSequence()) {
        if (auto ok = frameRange().validate(); !ok) {
            return ok;
        }
        // A range that would trace more frames than anyone meant. At 24 fps this is a little over
        // twelve minutes of footage, and a path tracer spends minutes per frame: a typo in an end
        // time should not commit the machine for a month without saying anything.
        if (frameRange().frameCount(endSeconds) > 18000) {
            return fail("pathtrace: {:.3f}s..{:.3f}s at {:g} fps is {} frames, which is not a range "
                        "anybody typed on purpose (max 18000)",
                        seconds, endSeconds, fps, frameRange().frameCount(endSeconds));
        }
    }
    if (!outputPath.empty() && outputPath.extension() != ".exr") {
        // The tracer writes scene-linear EXR and nothing else. A '.png' here would be accepted by
        // the filesystem and would contain float EXR bytes, which is the silently-corrupt output
        // the error-handling rule exists to prevent.
        return fail("pathtrace: the output is scene-linear EXR, so '{}' must end in .exr",
                    outputPath.generic_string());
    }
    return {};
}

FrameRange PathTraceSettings::frameRange() const {
    // A single frame is a range of exactly one: end = start + one frame time, which `frameCount`
    // resolves to 1. Expressing it that way rather than special-casing it means the sequence path
    // and the single-frame path are the same code, and a one-frame sequence is a real test of it.
    const double end = isSequence() ? endSeconds : seconds + 1.0 / std::max(fps, 1e-6);
    return FrameRange{seconds, end, fps};
}

nlohmann::json PathTraceSettings::toJson() const {
    return nlohmann::json{{"seconds", seconds},
                          {"endSeconds", endSeconds},
                          {"fps", fps},
                          {"samples", samplesPerPixel},
                          {"bounces", maxDepth},
                          {"seed", seed},
                          {"threads", threads},
                          {"denoise", denoise},
                          {"aovs", writeAovs},
                          {"albedoProbe", albedoProbe},
                          {"path", outputPath.generic_string()}};
}

Result<PathTraceSettings> PathTraceSettings::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'pathtrace' must be an object");
    }
    PathTraceSettings s;
    auto number = [&](const char* key, auto& out) -> Result<void> {
        if (const auto it = j.find(key); it != j.end()) {
            if (!it->is_number()) {
                return fail("pathtrace.{} must be a number", key);
            }
            out = it->get<std::remove_reference_t<decltype(out)>>();
        }
        return {};
    };
    auto boolean = [&](const char* key, bool& out) -> Result<void> {
        if (const auto it = j.find(key); it != j.end()) {
            if (!it->is_boolean()) {
                return fail("pathtrace.{} must be a boolean", key);
            }
            out = it->get<bool>();
        }
        return {};
    };
    if (const auto it = j.find("seconds"); it != j.end()) {
        if (!it->is_number()) {
            return fail("pathtrace.seconds must be a number");
        }
        s.seconds = it->get<double>();
    }
    if (const auto it = j.find("endSeconds"); it != j.end()) {
        if (!it->is_number()) {
            return fail("pathtrace.endSeconds must be a number");
        }
        s.endSeconds = it->get<double>();
    }
    if (const auto it = j.find("fps"); it != j.end()) {
        if (!it->is_number()) {
            return fail("pathtrace.fps must be a number");
        }
        s.fps = it->get<double>();
    }
    if (auto r = number("samples", s.samplesPerPixel); !r) return std::unexpected(r.error());
    if (auto r = number("bounces", s.maxDepth); !r) return std::unexpected(r.error());
    // The seed goes through the integer path rather than the double one every other field uses:
    // 0x853c49e6748fea9b does not survive a round trip through a double, and a seed that changes
    // when a project is saved is a determinism guarantee that quietly stops holding.
    if (const auto it = j.find("seed"); it != j.end()) {
        if (!it->is_number_unsigned() && !it->is_number_integer()) {
            return fail("pathtrace.seed must be an integer");
        }
        s.seed = it->get<std::uint64_t>();
    }
    if (auto r = number("threads", s.threads); !r) return std::unexpected(r.error());
    if (auto r = boolean("denoise", s.denoise); !r) return std::unexpected(r.error());
    if (auto r = boolean("aovs", s.writeAovs); !r) return std::unexpected(r.error());
    if (auto r = boolean("albedoProbe", s.albedoProbe); !r) return std::unexpected(r.error());
    if (const auto it = j.find("path"); it != j.end()) {
        if (!it->is_string()) {
            return fail("pathtrace.path must be a string");
        }
        s.outputPath = it->get<std::string>();
    }
    if (auto r = s.validate(); !r) {
        return std::unexpected(r.error());
    }
    return s;
}


pathtrace::TraceSettings traceSettingsFrom(const PathTraceSettings& settings, std::uint32_t width,
                                           std::uint32_t height) {
    pathtrace::TraceSettings t;
    t.width = std::max(16u, width);
    t.height = std::max(16u, height);
    t.samplesPerPixel = std::max(1u, settings.samplesPerPixel);
    t.maxDepth = settings.maxDepth;
    t.seed = settings.seed;
    t.threads = settings.threads;
    t.albedoProbe.enabled = settings.albedoProbe;
    // Derived, never authored: both consumers need the feature buffers and a project that could
    // record "denoise, but do not capture what the denoiser reads" would be recording a failure.
    t.captureFeatures = settings.denoise || settings.writeAovs;
    // Deliberately NOT carried across, and named so nobody later assumes the omission was an
    // oversight: `strategy`, `russianRouletteDepth` and `russianRouletteCompensation`. The last is
    // documented as an intentionally-wrong control arm that must never be set in production, and a
    // project file is exactly the place a wrong value would survive long enough to be believed.
    return t;
}

} // namespace avgen::app
