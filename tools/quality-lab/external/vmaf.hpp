#pragma once

// The optional external metrics: VMAF, PSNR-HVS and CAMBI, through ffmpeg's `libvmaf` filter.
//
// **ffmpeg is invoked as a separate process and is never linked** (docs/dependencies.md, ADR-250
// §2). Nothing in `src/` knows this file exists, `avgen`'s link line does not change, and the
// licence of whatever build the user has stays theirs.
//
// **Degradation is a designed behaviour.** A machine without ffmpeg, or with an ffmpeg built
// without libvmaf, gets `available: false` and a reason that lands in the report's `limitations`.
// The run succeeds and the vector is shorter. That is the only way a metric depending on an
// optional tool can be honest.
//
// ---------------------------------------------------------------------------------------------
// **Read this before using a number from here.** Three properties were measured on this machine
// (ffmpeg 9.0.1, libvmaf, 640x360 gradient arms, ten frames each) and every one of them changes how
// the output may be read. They are ADR-252.
//
//  1. **The default model has enhancement gain, and it is not subtle.** `vmaf_v0.6.1` scored a
//     posterised gradient **100.0** while scoring an identical copy of the reference **97.3** --
//     it ranked a defect above the reference itself, because ADM reads added contrast as added
//     detail (adm2 = 1.2235, above unity). `vmaf_v0.6.1neg` exists for exactly this and fixes it
//     (the same arm scores 90.9), so **neg is the default here** and the plain model is opt-in.
//  2. **VMAF does not reach 100 on an identical pair** when the motion feature is zero: a static
//     sequence differenced against itself scores 97.3, and the first frame of ANY sequence is
//     scored with motion 0 because there is no previous frame. "100 means identical" is false.
//  3. **Even with `neg`, VMAF ranks the remedy for banding below the banding.** Dithering a banded
//     gradient took CAMBI from 18.95 to 0.00 -- the artifact removed, by the instrument built for
//     it -- and VMAF scored the dithered frame 90.7, worse than the banded one. VMAF dislikes
//     noise, which is what dither is. This is ADR-250's scoping restated as a measurement:
//     **VMAF is for compression resilience and may not arbitrate a renderer configuration.**
//
// And one for CAMBI, also measured: it detects **subtle** banding and is blind to coarse
// posterisation. Quantising a gradient to steps of 4 or more takes CAMBI to ~0, because steps above
// its `max_log_contrast` are read as genuine edges. metrics.md's ladder arm was written the other
// way round; see ADR-252 and the corrected arm in validation/ladder.hpp.
// ---------------------------------------------------------------------------------------------

#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace avgen::quality {

// What was found, or why nothing was. `reason` is written to be read in a report by someone who was
// not here, so it says what to do about it.
struct ExternalTool {
    bool available = false;
    std::string reason;
    std::filesystem::path path;
    std::string version;
};

// ffmpeg via `AVGEN_FFMPEG`, then `PATH`, then Homebrew -- `assets::findFfmpeg`, deliberately the
// same search the video writer already does, so the Lab and the app never disagree about which
// binary is "the" ffmpeg. Then a probe that the build actually has `libvmaf`, because an ffmpeg
// without it fails at filter-graph time with an error nobody reads.
[[nodiscard]] ExternalTool findVmafTool();

struct VmafRequest {
    // The frames, in the order the Lab determined -- not a printf pattern. They are linked into a
    // scratch directory under canonical names before ffmpeg sees them, so a subsampled analysis and
    // a full one feed libvmaf the same frames the native metrics saw, in the same order.
    std::vector<std::filesystem::path> candidateFrames;
    std::vector<std::filesystem::path> referenceFrames;
    std::filesystem::path workDirectory;
    double fps = 30.0;
    // `neg` by default. See property 1 in the header: the plain model ranked a posterised gradient
    // above an identical pair on this machine.
    std::string model = "version=vmaf_v0.6.1neg";
    bool wantPsnrHvs = true;
    bool wantCambi = true;
    bool keepLog = false;
};

struct VmafSeries {
    bool available = false;
    std::string reason;
    std::vector<double> perFrame; // NaN where libvmaf reported null (an infinite PSNR-HVS, say)
    double mean = 0.0;
    double min = 0.0;
    double max = 0.0;
    double p5 = 0.0;
};

struct VmafResult {
    bool available = false;
    std::string reason;
    std::string model;
    std::string ffmpegVersion;
    std::string commandLine; // recorded verbatim, so a number can be reproduced by hand
    VmafSeries vmaf;
    VmafSeries psnrHvs;
    VmafSeries cambi;
};

// Runs one comparison. Returns an unavailable result rather than an error for every condition a
// caller can do nothing about (no tool, no libvmaf, mismatched frame counts); returns an error only
// when the request itself is malformed.
[[nodiscard]] Result<VmafResult> runVmaf(const ExternalTool& tool, const VmafRequest& request);

} // namespace avgen::quality
