#pragma once

// Offline render settings (milestone 1.0, ADR-020): what to render, at which size and rate,
// over which time range, and where. Stored in the project under "render" (optional) and
// overridable from the CLI. GPU-free; the RenderJob executes it.

#include "core/error.hpp"
#include "scene/detail_limits.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {
struct Scene;
}


namespace avgen::app {

// PngSequence: display-referred 8-bit PNGs after tone mapping. ExrSequence: scene-linear half
// EXRs from the HDR target before tone mapping (compositing/grading). Video: see VideoWriter.
enum class RenderOutput : std::uint8_t { PngSequence, Video, ExrSequence };
[[nodiscard]] const char* renderOutputName(RenderOutput output); // "sequence" | "video" | "exr"
[[nodiscard]] bool isSequence(RenderOutput output);


// ADR-255 + ADR-182: what `--aov shadow` needs before its output is a measurement rather than a
// constant. A free function, and here rather than on the render job, so the preconditions can be
// tested without a GPU -- a refusal nobody can exercise is a refusal nobody knows still works.
//
// Two conditions, and the second was found by running the arm rather than by reasoning about it:
//   * the scene must have an enabled directional light that CASTS. Without one the shadow-map term
//     is the constant 1.0, and a plane of 1.0 in a valid EXR of the right size is the exact failure
//     ADR-242 refused an approximated shadow AOV over.
//   * the shadow passes must not be disabled. `--disable shadows` skips the cascade passes and the
//     mask pass samples the atlas they would have drawn: an undrawn depth atlas reads as an
//     occluder in front of everything, so the exported plane came back marking 28.9% of the frame
//     shadowed against 4.6% in the same render with shadows on. Inverted, plausible, and silent.
[[nodiscard]] Result<void> shadowAovPreconditions(const scene::Scene& scene,
                                                  std::string_view disabledPasses);

struct RenderSettings {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    double fps = 60.0;
    double startSeconds = 0.0;
    double endSeconds = -1.0;          // < 0: the audio duration, else the timeline duration, else 10 s
    RenderOutput output = RenderOutput::PngSequence;
    std::filesystem::path outputPath;  // directory (sequence) or file (video); relative to the project
    std::string pattern = "frame_{:06d}.png"; // fmt pattern with the frame index (sequence)
    std::string codec = "h264";        // video codec id (see assets/video_writer.hpp)
    std::string backend = "auto";      // "auto" | "native" | "ffmpeg"
    int quality = 80;                  // 0..100
    bool muxAudio = true;              // include the project's audio in the video
    int encoderThreads = 0;            // 0 = hardware threads - 1, clamped to [1, 16]
    // ADR-147 / §5.9: a batch render is the deliverable, so it renders at the offline tier unless
    // told otherwise. Before this existed `RenderJob` never called `setQuality` at all and a batch
    // frame came out byte-identical to an interactive Realtime one -- the offline promise was
    // stated in the tier table and not kept by the path that produces the actual output.
    std::string tier = "offline";      // preview | realtime | high | offline
    // Phases switched off for this render, comma-separated, in `--disable`'s vocabulary. Empty is
    // the ordinary deliverable and is what every real render uses.
    //
    // It is here for the same reason `tier` is (ADR-147): the offline engine builds its own
    // renderer, so a toggle set on the interactive one never reached the path that produces the
    // output. `--disable water` on a `--render` came out byte-for-byte identical to the baseline --
    // an attribution arm that cannot fail, which is worse than no arm at all, because a null result
    // from it reads as "this subsystem is innocent".
    //
    // Not a deliverable feature: a render with a pass switched off is a diagnostic, and the job
    // says so in the log rather than letting a disabled frame be mistaken for a finished one.
    std::string disablePasses;
    // Quality arms applied to this render, comma-separated, in `--quality-arm`'s vocabulary. Same
    // reason and same defect as `disablePasses`: it was applied to the interactive renderer only,
    // so a quality arm on a `--render` was a third flag that validated and then did nothing.
    std::string qualityArms;
    // ADR-186: which distance-based detail reductions this render is under. "tier" (the default)
    // takes the tier's answer -- offline lifts them all, every other tier keeps live playback's;
    // "live" keeps them whatever the tier, which is what a quick proof render wants; "unlimited"
    // lifts them at any tier.
    //
    // Not a diagnostic like the two fields above it: this is a property of the deliverable, and a
    // render with it lifted is the *better* picture rather than a broken one. The far field draws
    // real geometry instead of billboards, distant characters are posed every frame instead of at
    // 20 Hz, and nothing past 120 m stands frozen.
    // How many times the output resolution the scene is rendered at, before being resolved back down
    // (ADR-212). 1 is off and is the default, so nothing changes for a render that does not ask.
    //
    // The renderer has supported this since ADR-137 -- `QualitySettings::renderScale` clamps to
    // [0.25, 2.0] and the resolve is already written -- but nothing could reach the top half of that
    // range: `--canvas-scale` refuses anything above 1, the settings slider stops at 1, and the
    // Offline tier pins `renderScale` to 1 under a comment about taking "no resolution shortcut".
    // That comment is about not going *down*. Going up is not a shortcut, it is spending more, which
    // is the one thing an offline render is for.
    //
    // Why it matters, measured on Glowmere through `--project`: neighbour-to-neighbour chroma noise
    // is 2.80% at 1280x720, 2.19% at 1920x1080 and 1.86% at 2560x1440. The artifact is undersampling
    // of sub-pixel foliage, it gets monotonically worse as the output shrinks, and a 720p deliverable
    // had no way to buy its way out of it.
    float supersample = 1.0f;

    std::string limits = "tier";

    // ---- AOV export (ADR-242) ----------------------------------------------------------------
    //
    // Comma-separated, from `aovNames()`, empty by default. The renderer has always written these
    // auxiliary targets every frame and nothing outside a debug view has ever read them; this is
    // the consumer. Each requested AOV is written as its own scene-linear EXR sequence beside the
    // beauty pass, so a compositor gets `frame_000123.exr` and `frame_000123.normal.exr` together.
    //
    // A separate file per AOV rather than one multi-layer EXR, because `writeExr` writes a single
    // RGBA part and separate sequences are what a compositor reads anyway. The layered form is a
    // better file and a bigger change; it is recorded as not done rather than half-built.
    std::string aovs;

    // Frame count for a resolved end time (endSeconds >= startSeconds); the last frame is the one
    // whose time is < end (end exclusive), at least 1.
    [[nodiscard]] std::uint64_t frameCount(double resolvedEndSeconds) const;
    [[nodiscard]] double resolvedEnd(double audioSeconds, double timelineSeconds) const;
    // Output file for frame `index` of a sequence.
    [[nodiscard]] std::filesystem::path frameFile(const std::filesystem::path& dir, std::uint64_t index) const;
    // Infers the output kind from a path's extension, keeping `fallback` when the extension says
    // nothing about it.
    //
    // A path is weak evidence and the setting is strong evidence. This used to answer PngSequence
    // for everything that was not a video -- for a directory, for an `.exr`, for the `.json` a save
    // dialog had appended -- so choosing an output file silently moved the radio button back to PNG
    // under someone who had just pressed Video.
    static RenderOutput outputForPath(const std::filesystem::path& path,
                                      RenderOutput fallback = RenderOutput::PngSequence);
    // The path a video would actually be written to: the same name with a container extension the
    // muxer understands, so a name typed without one (or with somebody else's) still names a movie.
    static std::filesystem::path withVideoExtension(const std::filesystem::path& path);
    // The default frame pattern of a sequence kind ("frame_{:06d}.png" / ".exr").
    static const char* defaultPattern(RenderOutput output);
    // Swaps a pattern that is still the other sequence kind's default for this kind's default, so
    // switching PNG <-> EXR does not leave ".png" names on EXR files.
    void normalisePattern();

    // The detail limits this render runs under, resolved against its own tier. The one place the
    // three words mean anything, so the job, the UI and a test cannot disagree about what "tier"
    // resolves to.
    [[nodiscard]] scene::DetailLimits resolvedLimits() const;

    // Every AOV this build can export, in the order `aovs` lists them for a canonical run.
    [[nodiscard]] static std::span<const std::string_view> aovNames();
    // `aovs` split and validated. An unknown name is an error rather than a skipped entry: a typo
    // that silently exports nothing is the failure this project keeps writing ADRs about.
    [[nodiscard]] Result<std::vector<std::string>> aovList() const;
    // Output file for one AOV of frame `index`: the frame pattern with ".<aov>.exr" in place of
    // its extension, so the AOV sorts beside its own beauty frame.
    [[nodiscard]] std::filesystem::path aovFile(const std::filesystem::path& dir, std::uint64_t index,
                                                std::string_view aov) const;

    [[nodiscard]] Result<void> validate() const; // sizes > 0 and even for video, fps > 0, quality range, pattern has {}
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<RenderSettings> fromJson(const nlohmann::json& j); // missing fields keep defaults
};

} // namespace avgen::app
