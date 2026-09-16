// `avgen_quality` -- the Quality Lab's CLI (docs/quality-lab/architecture.md §4).
//
//   analyze   --candidate <dir> --reference <dir> --out <run-dir> [...]
//   compare   --runs <run-dir>... [--out <dir>]
//   validate  --ladder
//   version
//
// **`render` and `experiment` are deliberately not subcommands.** Rendering is
// `avgen --project ... --render ...` and batching is `--queue`; both exist, are tested, and already
// emit the per-frame and sequence hashes every full-reference comparison depends on. A second
// rendering entry point would be a second thing to keep correct.
//
// **`validate` is a first-class subcommand rather than a test-only path**, because ADR-182 makes
// metric validation a product feature here: the product is measurement, so anyone must be able to
// ask the tool to demonstrate that its metrics can fail -- including by watching the tool fail its
// own ladder when a metric is deliberately broken.

#include "artifacts/masks.hpp"
#include "capture/sequence.hpp"
#include "external/vmaf.hpp"
#include "metrics/spatial.hpp"
#include "metrics/temporal.hpp"
#include "report/diagnostics.hpp"
#include "report/vector.hpp"
#include "validation/ladder.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace avgen;
using namespace avgen::quality;

namespace {

constexpr const char* kVersion = "1.0.0";
constexpr const char* kSchemaVersion = "1.0.0";

struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::vector<std::string> runs;
    std::string candidate;
    std::string reference;
    std::string aovDir;
    std::string out;
    std::string profile;
    std::string scene;
    std::string configuration;
    std::string candidateHash;
    std::string referenceHash;
    std::size_t stride = 1;
    double fps = 30.0;
    bool ladder = false;
    bool noExternal = false;
    bool json = false;
};

std::string capture(const std::string& command) {
    std::string output;
    std::FILE* pipe = ::popen((command + " 2>/dev/null").c_str(), "r");
    if (pipe == nullptr) {
        return output;
    }
    std::array<char, 1024> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    ::pclose(pipe);
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    return output;
}

std::string nowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::array<char, 32> buffer{};
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer.data();
}

// ADR-170: a timing is evidence only if the device was also quiet, so the witness is taken beside
// the numbers rather than assumed. It is recorded even when no timing is reported, because "the
// machine was busy" is also the explanation for a render that looks different.
std::string contentionWitness() {
    const std::string others = capture("pgrep -x avgen | wc -l");
    const std::string load = capture("uptime");
    return fmt::format("avgen processes: {}; {}", others.empty() ? "?" : others, load);
}

bool parseArgs(int argc, char** argv, Args& args, std::string& error) {
    if (argc < 2) {
        error = "no subcommand";
        return false;
    }
    args.command = argv[1];
    const auto next = [&](int& i) -> std::string {
        if (i + 1 >= argc) {
            return {};
        }
        return argv[++i];
    };
    for (int i = 2; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (flag == "--candidate") {
            args.candidate = next(i);
        } else if (flag == "--reference") {
            args.reference = next(i);
        } else if (flag == "--aov-dir") {
            args.aovDir = next(i);
        } else if (flag == "--out") {
            args.out = next(i);
        } else if (flag == "--profile") {
            args.profile = next(i);
        } else if (flag == "--scene") {
            args.scene = next(i);
        } else if (flag == "--configuration") {
            args.configuration = next(i);
        } else if (flag == "--candidate-hash") {
            args.candidateHash = next(i);
        } else if (flag == "--reference-hash") {
            args.referenceHash = next(i);
        } else if (flag == "--stride") {
            args.stride = static_cast<std::size_t>(std::max(1, std::atoi(next(i).c_str())));
        } else if (flag == "--fps") {
            args.fps = std::atof(next(i).c_str());
        } else if (flag == "--runs") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                args.runs.emplace_back(argv[++i]);
            }
        } else if (flag == "--ladder") {
            args.ladder = true;
        } else if (flag == "--no-external") {
            args.noExternal = true;
        } else if (flag == "--json") {
            args.json = true;
        } else if (flag.starts_with("--")) {
            error = fmt::format("unknown flag '{}'", flag);
            return false;
        } else {
            args.positional.emplace_back(flag);
        }
    }
    return true;
}

void usage() {
    std::cout << R"(avgen_quality -- the Render Quality Lab's metric engine

  analyze   --candidate <dir> --reference <dir> --out <run-dir>
            [--aov-dir <dir>] [--profile <target-profile.json>] [--stride N]
            [--fps F] [--scene NAME] [--configuration NAME] [--no-external]
            [--candidate-hash H] [--reference-hash H]

  compare   --runs <run-dir> <run-dir> [...] [--out <dir>]

  validate  --ladder [--json]
            Runs the distortion ladder and the temporal control arms, and then runs
            the ladder again against a deliberately broken metric to show it fails.

  version

The candidate is rendered at production settings with --aov; the reference is rendered
--tier offline --render-limits unlimited --supersample 2.0 with no AOVs. Neither is
rendered by this tool: use avgen --project ... --render ... under tools/gpu-lock.sh.
)";
}

// ---- analyze -----------------------------------------------------------------------------------

struct FrameSeries {
    std::vector<double> psnr;
    std::vector<double> ssim;
    std::vector<double> msSsim;
    std::vector<double> ciedeMean;
    std::vector<double> ciedeP95;
    std::vector<double> laplacianCandidate;
    std::vector<double> laplacianReference;
    std::vector<double> laplacianRatio;
    std::vector<double> quantisationCandidate;
    std::vector<double> alternation;
    std::vector<double> alternationPeak;
    std::vector<double> residual;
    std::vector<double> residualP95;
    std::vector<double> disocclusion;
    std::vector<double> specularResidual;
    std::vector<double> specularCoverage;
    std::vector<double> shadingResidual;
    std::vector<double> shadingCoverage;
    std::vector<double> idChurn;
};

Metric makeMetric(std::string name, std::string unit, std::string source, std::string pooling,
                  const std::vector<double>& values, bool higherIsWorse,
                  std::vector<std::string> limitations) {
    Metric metric;
    metric.name = std::move(name);
    metric.unit = std::move(unit);
    metric.source = std::move(source);
    metric.pooling = std::move(pooling);
    metric.limitations = std::move(limitations);
    if (values.empty()) {
        metric.available = false;
        metric.unavailableReason = "no frames contributed a value";
        return metric;
    }
    metric.pooled = pool(values, higherIsWorse);
    metric.value = metric.pooled->mean;
    return metric;
}

int analyze(const Args& args) {
    if (args.candidate.empty() || args.reference.empty() || args.out.empty()) {
        std::cerr << "analyze needs --candidate, --reference and --out\n";
        return 2;
    }
    auto candidate = discoverSequence(args.candidate);
    if (!candidate) {
        std::cerr << candidate.error().message << "\n";
        return 1;
    }
    auto reference = discoverSequence(args.reference);
    if (!reference) {
        std::cerr << reference.error().message << "\n";
        return 1;
    }
    if (candidate->size() != reference->size()) {
        // reference-rendering.md §3.4: two arms that should describe the same moments must be the
        // same length, or the comparison silently pairs different moments.
        std::cerr << fmt::format(
            "candidate has {} frames and reference has {}. A full-reference comparison of "
            "different lengths compares different moments.\n",
            candidate->size(), reference->size());
        return 1;
    }
    // metrics.md §5: every frame under 300, every second frame above, and the sampling recorded.
    std::size_t stride = args.stride;
    if (stride == 1 && candidate->size() > 300) {
        stride = 2;
    }

    const fs::path outDir = args.out;
    std::error_code ec;
    fs::create_directories(outDir / "diagnostics", ec);

    const fs::path aovRoot = args.aovDir.empty() ? fs::path(args.candidate) : fs::path(args.aovDir);
    const auto aovFor = [&](std::size_t frameIndex, std::string_view name) {
        return aovPathFor(aovRoot / candidate->frames[frameIndex].filename(), name);
    };
    const auto loadAov = [&](std::size_t frameIndex, std::string_view name) -> std::optional<Plane> {
        const fs::path path = aovFor(frameIndex, name);
        if (!fs::is_regular_file(path, ec)) {
            return std::nullopt;
        }
        auto plane = readPlane(path);
        if (!plane) {
            return std::nullopt;
        }
        return *plane;
    };

    FrameSeries series;
    std::vector<std::size_t> analysed;
    std::vector<std::string> limitations;
    IdentifierSurvey idSurvey;
    bool sawVelocity = false;
    bool sawIdentifier = false;
    bool sawDepth = false;
    bool sawEmission = false;
    bool sawNormal = false;

    Frame previousCandidate;
    Frame twoBackCandidate;
    std::optional<Plane> previousDepth;
    std::optional<Plane> previousId;
    std::optional<Plane> previousNormal;

    for (std::size_t i = 0; i < candidate->size(); i += stride) {
        auto candidateFrame = readFrame(candidate->frames[i]);
        if (!candidateFrame) {
            std::cerr << candidateFrame.error().message << "\n";
            return 1;
        }
        auto referenceFrame = readFrame(reference->frames[i]);
        if (!referenceFrame) {
            std::cerr << referenceFrame.error().message << "\n";
            return 1;
        }
        if (!candidateFrame->sameShapeAs(*referenceFrame)) {
            std::cerr << fmt::format(
                "frame {}: candidate is {}x{} and reference is {}x{}. reference-rendering.md §4: "
                "the reference matches the candidate's OUTPUT resolution and differs only in "
                "--supersample, --tier and --render-limits.\n",
                i, candidateFrame->width, candidateFrame->height, referenceFrame->width,
                referenceFrame->height);
            return 1;
        }
        analysed.push_back(i);
        series.psnr.push_back(psnr(*candidateFrame, *referenceFrame));
        series.ssim.push_back(ssim(*candidateFrame, *referenceFrame));
        series.msSsim.push_back(msSsim(*candidateFrame, *referenceFrame));
        const ColourDifference colour = ciede2000(*candidateFrame, *referenceFrame);
        series.ciedeMean.push_back(colour.mean);
        series.ciedeP95.push_back(colour.p95);
        const double lapCandidate = spatialLaplacian(*candidateFrame);
        const double lapReference = spatialLaplacian(*referenceFrame);
        series.laplacianCandidate.push_back(lapCandidate);
        series.laplacianReference.push_back(lapReference);
        series.laplacianRatio.push_back(lapCandidate / std::max(1e-9, lapReference));
        series.quantisationCandidate.push_back(quantisationSteps(*candidateFrame));

        std::optional<Plane> velocity = loadAov(i, "velocity");
        std::optional<Plane> depth = loadAov(i, "depth");
        std::optional<Plane> identifier = loadAov(i, "id");
        std::optional<Plane> emission = loadAov(i, "emission");
        std::optional<Plane> normal = loadAov(i, "normal");
        sawVelocity = sawVelocity || velocity.has_value();
        sawDepth = sawDepth || depth.has_value();
        sawIdentifier = sawIdentifier || identifier.has_value();
        sawEmission = sawEmission || emission.has_value();
        sawNormal = sawNormal || normal.has_value();
        if (identifier.has_value() && idSurvey.distinctValues == 0) {
            idSurvey = surveyIdentifiers(*identifier);
        }

        if (previousCandidate.valid() && velocity.has_value()) {
            MotionInputs inputs;
            inputs.previous = &previousCandidate;
            inputs.current = &*candidateFrame;
            inputs.velocity = &*velocity;
            if (depth.has_value() && previousDepth.has_value()) {
                inputs.depthPrevious = &*previousDepth;
                inputs.depthCurrent = &*depth;
            }
            if (identifier.has_value() && previousId.has_value()) {
                inputs.idPrevious = &*previousId;
                inputs.idCurrent = &*identifier;
            }
            const MotionResidual residual = motionCompensatedResidual(inputs);
            if (residual.width > 0) {
                series.residual.push_back(residual.residual);
                series.residualP95.push_back(residual.residualP95);
                series.disocclusion.push_back(residual.disocclusionFraction);
                const Mask specular =
                    specularMask(emission.has_value() ? &*emission : nullptr,
                                 normal.has_value() ? &*normal : nullptr);
                if (!specular.empty()) {
                    const auto gated = residual.over(specular);
                    series.specularResidual.push_back(gated.residual);
                    series.specularCoverage.push_back(gated.coverage);
                }
                if (normal.has_value() && previousNormal.has_value()) {
                    const Mask shading = normalUnchangedMask(*previousNormal, *normal);
                    if (!shading.empty()) {
                        const auto gated = residual.over(shading);
                        series.shadingResidual.push_back(gated.residual);
                        series.shadingCoverage.push_back(gated.coverage);
                    }
                }
                if (identifier.has_value() && previousId.has_value()) {
                    const Mask churn = identifierChurnMask(
                        *previousId, *identifier, *velocity,
                        previousDepth.has_value() ? &*previousDepth : nullptr,
                        depth.has_value() ? &*depth : nullptr);
                    series.idChurn.push_back(coverage(churn));
                }
            }
        }
        if (twoBackCandidate.valid() && previousCandidate.valid()) {
            const AlternationStats stats =
                temporalAlternation(twoBackCandidate, previousCandidate, *candidateFrame);
            series.alternation.push_back(stats.mean);
            series.alternationPeak.push_back(stats.peak);
        }

        twoBackCandidate = previousCandidate;
        previousCandidate = *candidateFrame;
        previousDepth = depth;
        previousId = identifier;
        previousNormal = normal;
    }

    Report report;
    report.schemaVersion = kSchemaVersion;
    report.run.id = nowIso8601();
    report.run.timestamp = report.run.id;
    report.run.gitCommit = capture("git rev-parse HEAD");
    report.run.scene = args.scene;
    report.run.targetProfile = args.profile;
    report.run.candidateDirectory = fs::absolute(args.candidate).string();
    report.run.referenceDirectory = fs::absolute(args.reference).string();
    report.run.framesAnalysed = analysed.size();
    report.run.frameStride = stride;
    report.run.candidateSequenceHash = args.candidateHash;
    report.run.referenceSequenceHash = args.referenceHash;
    report.renderer.configuration = args.configuration;
    report.renderer.contentionWitness = contentionWitness();

    const std::string pngPair = "candidate PNG vs reference PNG (display-referred sRGB)";
    report.add(makeMetric("spatial.msSsim", "ratio 0..1", pngPair, "mean and p5 over frames",
                          series.msSsim, false,
                          {"responds identically to more aliasing and to more genuine detail; must "
                           "not decide an anti-aliasing comparison alone"}));
    report.add(makeMetric("spatial.ssim", "ratio 0..1", pngPair, "mean over frames", series.ssim,
                          false, {"superseded by msSsim; reported for continuity with external "
                                  "tooling"}));
    report.add(makeMetric(
        "spatial.psnr", "dB", pngPair, "mean over frames", series.psnr, false,
        {"NOT a quality metric here. Its job is the alignment test: identical renders give "
         "infinity, and a 5% exposure grade collapses it while the picture is unchanged"}));
    report.add(makeMetric("color.ciede2000Mean", "dE00", pngPair, "mean over frames",
                          series.ciedeMean, true,
                          {"defined for surface colour under a reference illuminant and used here "
                           "on tone-mapped emissive content: comparable between arms only"}));
    report.add(makeMetric("color.ciede2000P95", "dE00", pngPair, "p95 over pixels then frames",
                          series.ciedeP95, true, {"as above"}));
    report.add(makeMetric(
        "detail.spatialLaplacian", "mean |4c-l-r-u-d| on 0..255 luma", "candidate PNG",
        "mean over frames", series.laplacianCandidate, true,
        {"cannot separate aliasing from detail. Meaningful only between arms of one view; never "
         "between scenes; never as an absolute bar (ADR-243, tools/spatial_stats.py)"}));
    report.add(makeMetric("detail.spatialLaplacianRatio", "ratio candidate/reference", pngPair,
                          "mean over frames", series.laplacianRatio, true,
                          {"above 1 means the candidate carries MORE high-frequency energy than a "
                           "better-sampled render of the same thing, which is aliasing or "
                           "sharpening and not detail"}));
    report.add(makeMetric("banding.quantisationSteps", "fraction of pixels", "candidate PNG",
                          "mean over frames", series.quantisationCandidate, true,
                          {"detects COARSE posterisation (a 3-to-12 luma step in a flat "
                           "neighbourhood) and is blind to ordinary single-code 8-bit banding",
                           "AND IT SCORES DITHER AS BANDING: on the validation ladder's ramp it "
                           "reports 0.000 for the banded frame and 0.031 for its dithered twin, "
                           "while CAMBI reports 22.99 and 0.00. Never take a banding decision on "
                           "this number alone; where banding.cambi is available it is the banding "
                           "number and this is the coarse-band companion (ADR-252)"}));

    if (!series.alternation.empty()) {
        report.add(makeMetric(
            "temporal.temporalAlternation", "luma steps 0..255", "candidate PNG sequence",
            "mean over frames", series.alternation, true,
            {"ADR-243: anti-correlated with human judgement on spatial aliasing over moving "
             "geometry. Never reported without detail.spatialLaplacian beside it, and never used "
             "alone to choose work",
             "authored smooth motion produces a LARGE value: the second difference in time of "
             "translating content is not zero"}));
        report.add(makeMetric("temporal.temporalAlternationPeak", "luma steps 0..255",
                              "candidate PNG sequence", "peak over pixels, mean over frames",
                              series.alternationPeak, true,
                              {"as above; the peak is reported because a three-frame pop is "
                               "invisible in a mean"}));
    } else {
        report.add(Metric::unavailable("temporal.temporalAlternation",
                                       "fewer than three analysed frames"));
    }

    if (!series.residual.empty()) {
        report.add(makeMetric(
            "temporal.motionCompensatedResidual", "luma steps 0..255",
            "candidate PNG sequence + velocity/depth/id AOVs", "mean over frame pairs",
            series.residual, true,
            {"inherits every velocity-buffer defect: where motion vectors are wrong this "
             "attributes the error to the image",
             "bilinear prediction is itself a resample, so the number has a floor above zero; "
             "compare against the warp floor, not against zero",
             "a blended (transparent) pixel's velocity is the background's, so transparent regions "
             "are measured against the wrong history (ADR-035's auxMask)"}));
        report.add(makeMetric("temporal.motionCompensatedResidualP95", "luma steps 0..255",
                              "as above", "p95 over pixels, mean over frame pairs",
                              series.residualP95, true, {"as above"}));
        report.add(makeMetric(
            "temporal.disocclusionFraction", "fraction", "velocity/depth/id AOVs",
            "mean over frame pairs", series.disocclusion, true,
            {"A DIAGNOSTIC, NOT A QUALITY NUMBER. It says how much of the frame the residual "
             "covers. A residual computed over 4% of the frame is not a statement about the "
             "frame"}));
    } else {
        const std::string reason =
            sawVelocity ? "the velocity AOV was present but no frame pair produced a residual"
                        : "no velocity AOV beside the candidate frames (render with --aov "
                          "velocity,depth,id)";
        report.add(Metric::unavailable("temporal.motionCompensatedResidual", reason));
        report.add(Metric::unavailable("temporal.disocclusionFraction", reason));
    }

    if (!series.specularResidual.empty()) {
        report.add(makeMetric(
            "perClass.specularResidual", "luma steps 0..255",
            "motion-compensated residual over the emission/roughness mask", "mean over frame pairs",
            series.specularResidual, true,
            {"EMISSION IS NOT SPECULAR: a rough emissive surface is in this mask and should not be",
             "roughness is the better half of the mask and is stored at half precision"}));
        report.add(makeMetric("perClass.specularCoverage", "fraction", "emission/roughness mask",
                              "mean over frame pairs", series.specularCoverage, false,
                              {"the mask's share of the frame; the residual above means nothing "
                               "without it"}));
    } else {
        report.add(Metric::unavailable("perClass.specularResidual",
                                       sawEmission || sawNormal
                                           ? "the emission and normal AOVs produced an empty mask"
                                           : "no emission or normal AOV (render with --aov "
                                             "emission,normal)"));
    }
    if (!series.shadingResidual.empty()) {
        report.add(makeMetric(
            "perClass.shadingResidual", "luma steps 0..255",
            "motion-compensated residual over pixels whose normal did not change",
            "mean over frame pairs", series.shadingResidual, true,
            {"a normal that changed by less than the half-float epsilon reads as unchanged; the "
             "threshold is a decision, not a fact"}));
        report.add(makeMetric("perClass.shadingCoverage", "fraction", "normal AOV",
                              "mean over frame pairs", series.shadingCoverage, false, {}));
    } else {
        report.add(Metric::unavailable(
            "perClass.shadingResidual",
            sawNormal ? "the normal AOV produced an empty mask" : "no normal AOV"));
    }
    if (!series.idChurn.empty()) {
        report.add(makeMetric(
            "perClass.lodIdentifierChurn", "fraction of pixels",
            "id AOV where velocity says static and depth says the same surface",
            "mean over frame pairs", series.idChurn, true,
            {"REPORT AS A CANDIDATE, NOT A DETECTION: a genuine object change at a silhouette is "
             "indistinguishable from a level swap without more information"}));
    } else {
        report.add(Metric::unavailable("perClass.lodIdentifierChurn",
                                       sawIdentifier ? "no frame pair produced a churn mask"
                                                     : "no id AOV"));
    }

    // Named absences. These are the answer, not a gap in the implementation.
    report.add(Metric::unavailable(
        "perClass.shadowStability",
        "no shadow AOV. An approximation over 'regions the lighting model says are shadowed' is "
        "refused: it would be a number whose name promised more than it knew (ADR-242, ADR-250)"));
    report.add(Metric::unavailable(
        "perClass.vegetationResidual",
        "no material-id to class mapping exists. The mask mechanism is built and tested; the "
        "mapping is a human decision"));

    // ---- optional external metrics --------------------------------------------------------
    if (!args.noExternal) {
        const ExternalTool tool = findVmafTool();
        if (!tool.available) {
            report.add(Metric::unavailable("delivery.vmafMean", tool.reason));
            report.add(Metric::unavailable("spatial.psnrHvs", tool.reason));
            report.add(Metric::unavailable("banding.cambi", tool.reason));
            report.limitations.push_back(fmt::format("libvmaf metrics unavailable: {}", tool.reason));
        } else {
            VmafRequest request;
            for (const std::size_t index : analysed) {
                request.candidateFrames.push_back(candidate->frames[index]);
                request.referenceFrames.push_back(reference->frames[index]);
            }
            request.workDirectory = outDir;
            request.fps = args.fps;
            auto vmafResult = runVmaf(tool, request);
            if (!vmafResult) {
                report.limitations.push_back(vmafResult.error().message);
            } else {
                const VmafResult& v = *vmafResult;
                const std::vector<std::string> vmafLimits = {
                    "trained on compression and scaling of CAMERA-CAPTURED video. Scoped to "
                    "compression resilience; MAY NOT arbitrate a renderer configuration (ADR-250)",
                    "the default model has enhancement gain -- measured here scoring a posterised "
                    "gradient 100.0 against an identical pair's 97.3. This run uses " + v.model,
                    "frame 0 is scored with a motion feature of zero because there is no previous "
                    "frame, so it is not comparable with the rest of the sequence",
                    "chroma is subsampled to yuv420p before measurement, which is the models' "
                    "training domain and a blind spot for a chroma-only defect"};
                if (v.vmaf.available) {
                    Metric mean;
                    mean.name = "delivery.vmafMean";
                    mean.unit = "VMAF 0..100";
                    mean.source = "candidate PNG vs reference PNG through ffmpeg libvmaf";
                    mean.pooling = "mean over frames";
                    mean.value = v.vmaf.mean;
                    mean.limitations = vmafLimits;
                    report.add(std::move(mean));
                    Metric p5;
                    p5.name = "delivery.vmafP5";
                    p5.unit = "VMAF 0..100";
                    p5.source = mean.source;
                    p5.pooling = "5th percentile over frames";
                    p5.value = v.vmaf.p5;
                    p5.limitations = vmafLimits;
                    report.add(std::move(p5));
                    Metric worst;
                    worst.name = "delivery.vmafMin";
                    worst.unit = "VMAF 0..100";
                    worst.source = mean.source;
                    worst.pooling = "minimum over frames";
                    worst.value = v.vmaf.min;
                    worst.limitations = vmafLimits;
                    report.add(std::move(worst));
                } else {
                    report.add(Metric::unavailable("delivery.vmafMean", v.vmaf.reason));
                }
                if (v.psnrHvs.available) {
                    Metric m;
                    m.name = "spatial.psnrHvs";
                    m.unit = "dB";
                    m.source = "ffmpeg libvmaf feature psnr_hvs";
                    m.pooling = "mean over frames";
                    m.value = v.psnrHvs.mean;
                    m.limitations = {"assumes a viewing condition it is not told; no decision "
                                     "authority",
                                     "libvmaf writes null for an infinite value, so an identical "
                                     "pair contributes no frames to this mean"};
                    report.add(std::move(m));
                } else {
                    report.add(Metric::unavailable("spatial.psnrHvs", v.psnrHvs.reason));
                }
                if (v.cambi.available) {
                    Metric m;
                    m.name = "banding.cambi";
                    m.unit = "CAMBI 0..~24";
                    m.source = "ffmpeg libvmaf feature cambi, on the candidate";
                    m.pooling = "mean over frames";
                    m.value = v.cambi.mean;
                    m.limitations = {
                        "luma only, so chroma banding in saturated gradients is out of scope",
                        "frame-local, so a band that CRAWLS scores the same as a static one",
                        "detects SUBTLE banding: measured here, quantising a gradient to steps of "
                        "4 codes or more takes CAMBI to ~0, because steps above its "
                        "max_log_contrast read as genuine edges (ADR-252)",
                        "its thresholds assume a display brightness, which must be pinned in the "
                        "target profile"};
                    report.add(std::move(m));
                } else {
                    report.add(Metric::unavailable("banding.cambi", v.cambi.reason));
                }
                report.limitations.push_back("libvmaf command: " + v.commandLine);
            }
        }
    }

    // ---- limitations and named absences ----------------------------------------------------
    report.limitations.emplace_back(
        "the reference is a better-SAMPLED render, not ground truth and not band-limited: "
        "--supersample resolves through the tonemap's bilinear tap and there is no resolve pass "
        "(reference-rendering.md §3.1)");
    report.limitations.emplace_back(
        "PSNR, MS-SSIM, CIEDE2000 and the Laplacian are read from display-referred PNG, which is "
        "post-exposure, post-tonemap, post-vignette and post-grain. A difference here may be the "
        "tone mapper's and not the renderer's");
    if (sawIdentifier) {
        report.limitations.push_back(fmt::format(
            "the id AOV carries {} distinct values over {:.1f}% background; every id-gated number "
            "is vacuous if that first figure is 1 (ADR-242)",
            idSurvey.distinctValues, idSurvey.backgroundFraction * 100.0));
        if (idSurvey.exceedsExactFloatRange) {
            report.limitations.emplace_back(
                "at least one identifier exceeds 2^24 and is therefore NOT exactly representable "
                "in the float EXR the AOV is written as; equality tests on it are approximate");
        }
    }
    if (stride != 1) {
        report.limitations.push_back(fmt::format(
            "every {}th frame was analysed. metrics.md §5: a subsampled run may NOT be cited as a "
            "regression check",
            stride));
    }
    if (report.run.candidateSequenceHash.empty() || report.run.referenceSequenceHash.empty()) {
        report.limitations.emplace_back(
            "no sequence hashes were supplied (--candidate-hash / --reference-hash). Two arms "
            "expected to differ that hash identically are vacuous, and the experiment is void "
            "rather than measured (ADR-182)");
    } else if (report.run.candidateSequenceHash == report.run.referenceSequenceHash) {
        report.findings.push_back(
            Finding{"run.sequenceHash",
                    "the candidate and the reference hash identically: these are the same render, "
                    "so every full-reference number below is an identity check and not a "
                    "comparison",
                    0.0, std::nullopt});
    }

    // ---- findings, which are measurements and nothing else ----------------------------------
    if (const Metric* ratio = report.find("detail.spatialLaplacianRatio");
        ratio != nullptr && ratio->pooled.has_value()) {
        report.findings.push_back(Finding{
            "detail.spatialLaplacianRatio",
            fmt::format("the candidate carries {:.3f}x the reference's high-frequency energy; "
                        "worst frame index {}",
                        ratio->pooled->mean, analysed[ratio->pooled->worstFrameIndex]),
            ratio->pooled->mean, analysed[ratio->pooled->worstFrameIndex]});
    }
    if (const Metric* disocclusion = report.find("temporal.disocclusionFraction");
        disocclusion != nullptr && disocclusion->available && disocclusion->pooled.has_value() &&
        disocclusion->pooled->mean > 0.25) {
        report.findings.push_back(Finding{
            "temporal.disocclusionFraction",
            fmt::format("{:.1f}% of pixels were excluded, so the residual describes a minority of "
                        "the frame",
                        disocclusion->pooled->mean * 100.0),
            disocclusion->pooled->mean, std::nullopt});
    }

    // ---- diagnostics, for the worst frame of the metric most likely to have found something ---
    std::size_t worstIndex = analysed.empty() ? 0 : analysed.front();
    if (const Metric* ms = report.find("spatial.msSsim"); ms != nullptr && ms->pooled.has_value()) {
        worstIndex = analysed[ms->pooled->worstFrameIndex];
    }
    {
        auto candidateFrame = readFrame(candidate->frames[worstIndex]);
        auto referenceFrame = readFrame(reference->frames[worstIndex]);
        if (candidateFrame && referenceFrame) {
            const auto write = [&](const std::string& name, const Frame& frame) {
                if (!frame.valid()) {
                    return;
                }
                if (writeFrame(outDir / "diagnostics" / name, frame)) {
                    report.diagnostics.push_back("diagnostics/" + name);
                }
            };
            write("candidate.png", *candidateFrame);
            write("reference.png", *referenceFrame);
            write("difference-x8.png", differenceImage(*candidateFrame, *referenceFrame, 8.0));
            const std::vector<float> lap = laplacianPlane(*candidateFrame);
            write("edge-map-candidate.png",
                  heatmapImage(lap, candidateFrame->width, candidateFrame->height, 64.0));
            write("edge-map-reference.png",
                  heatmapImage(laplacianPlane(*referenceFrame), referenceFrame->width,
                               referenceFrame->height, 64.0));
            const TileMap tiles = tileMap(lap, candidateFrame->width, candidateFrame->height);
            report.findings.push_back(Finding{
                "detail.spatialLaplacian",
                fmt::format("worst 8x8 tile of the worst frame is at ({}, {}) with mean {:.2f}",
                            tiles.worstTileX, tiles.worstTileY, tiles.worstTileMean),
                tiles.worstTileMean, worstIndex});
            if (worstIndex > 0) {
                auto previousFrame = readFrame(candidate->frames[worstIndex - 1]);
                std::optional<Plane> velocity = loadAov(worstIndex, "velocity");
                std::optional<Plane> depthNow = loadAov(worstIndex, "depth");
                std::optional<Plane> depthWas = loadAov(worstIndex - 1, "depth");
                std::optional<Plane> idNow = loadAov(worstIndex, "id");
                std::optional<Plane> idWas = loadAov(worstIndex - 1, "id");
                if (previousFrame && velocity.has_value()) {
                    MotionInputs inputs;
                    inputs.previous = &*previousFrame;
                    inputs.current = &*candidateFrame;
                    inputs.velocity = &*velocity;
                    if (depthNow.has_value() && depthWas.has_value()) {
                        inputs.depthPrevious = &*depthWas;
                        inputs.depthCurrent = &*depthNow;
                    }
                    if (idNow.has_value() && idWas.has_value()) {
                        inputs.idPrevious = &*idWas;
                        inputs.idCurrent = &*idNow;
                    }
                    const MotionResidual residual = motionCompensatedResidual(inputs);
                    if (residual.width > 0) {
                        write("temporal-residual.png",
                              heatmapImage(residual.residualMap, residual.width, residual.height,
                                           32.0));
                        std::vector<std::uint8_t> invalid(residual.valid.size(), 0);
                        for (std::size_t k = 0; k < residual.valid.size(); ++k) {
                            invalid[k] = residual.valid[k] != 0 ? 0 : 1;
                        }
                        write("disocclusion-mask.png",
                              maskImage(invalid, residual.width, residual.height));
                    }
                }
            }
        }
    }

    const std::string violation = report.pairingViolation();
    if (!violation.empty()) {
        std::cerr << "refusing to write the report: " << violation << "\n";
        return 1;
    }
    const std::string json = report.toJson();
    std::ofstream out(outDir / "metrics.json");
    out << json << "\n";
    out.close();

    std::cout << fmt::format("analysed {} of {} frames (stride {}) -> {}\n", analysed.size(),
                             candidate->size(), stride, (outDir / "metrics.json").string());
    std::cout << fmt::format("for the HTML report: python3 tools/quality-lab/report.py {}\n",
                             outDir.string());
    for (const Metric& metric : report.metrics) {
        if (!metric.available) {
            std::cout << fmt::format("  {:<42} unavailable: {}\n", metric.name,
                                     metric.unavailableReason);
        } else if (metric.pooled.has_value()) {
            std::cout << fmt::format("  {:<42} mean {:>10.4f}  p5 {:>10.4f}  p95 {:>10.4f}  worst "
                                     "frame {}\n",
                                     metric.name, metric.pooled->mean, metric.pooled->p5,
                                     metric.pooled->p95, analysed[metric.pooled->worstFrameIndex]);
        } else if (metric.value.has_value()) {
            std::cout << fmt::format("  {:<42} {:>10.4f}\n", metric.name, *metric.value);
        }
    }
    return 0;
}

// ---- compare -----------------------------------------------------------------------------------

int compare(const Args& args) {
    if (args.runs.size() < 2) {
        std::cerr << "compare needs at least two --runs directories\n";
        return 2;
    }
    std::vector<nlohmann::json> reports;
    std::vector<std::string> names;
    for (const std::string& run : args.runs) {
        const fs::path path = fs::is_directory(run) ? fs::path(run) / "metrics.json" : fs::path(run);
        std::ifstream stream(path);
        if (!stream) {
            std::cerr << fmt::format("cannot read '{}'\n", path.string());
            return 1;
        }
        nlohmann::json report;
        try {
            stream >> report;
        } catch (const std::exception& e) {
            std::cerr << fmt::format("'{}' did not parse: {}\n", path.string(), e.what());
            return 1;
        }
        reports.push_back(std::move(report));
        names.push_back(run);
    }

    // benchmark-scenes.md §5: two numbers taken under different target profiles are not comparable
    // and the report must refuse to rank them. Same for two different scenes -- a reference is a
    // pair-wise instrument, and "scene A scores 0.94 and scene B 0.89" is not a statement about the
    // renderer.
    const auto field = [](const nlohmann::json& report, const char* key) -> std::string {
        return report.contains("run") && report["run"].contains(key) &&
                       report["run"][key].is_string()
                   ? report["run"][key].get<std::string>()
                   : std::string{};
    };
    bool comparable = true;
    for (std::size_t i = 1; i < reports.size(); ++i) {
        if (field(reports[i], "targetProfile") != field(reports[0], "targetProfile")) {
            std::cerr << fmt::format(
                "REFUSING TO RANK: '{}' and '{}' were analysed under different target profiles "
                "('{}' and '{}'). VMAF's viewing-distance model and CAMBI's thresholds are "
                "different instruments per profile.\n",
                names[0], names[i], field(reports[0], "targetProfile"),
                field(reports[i], "targetProfile"));
            comparable = false;
        }
        if (field(reports[i], "scene") != field(reports[0], "scene")) {
            std::cerr << fmt::format(
                "REFUSING TO RANK: '{}' and '{}' are different scenes ('{}' and '{}'). A "
                "full-reference metric is a pair-wise instrument.\n",
                names[0], names[i], field(reports[0], "scene"), field(reports[i], "scene"));
            comparable = false;
        }
    }
    if (!comparable) {
        return 1;
    }

    std::vector<std::string> metricNames;
    for (const auto& report : reports) {
        for (const auto& metric : report["metrics"]) {
            const std::string name = metric["name"].get<std::string>();
            if (std::find(metricNames.begin(), metricNames.end(), name) == metricNames.end()) {
                metricNames.push_back(name);
            }
        }
    }

    std::cout << fmt::format("{:<42}", "metric");
    for (const std::string& name : names) {
        std::cout << fmt::format("{:>18}", fs::path(name).filename().string());
    }
    std::cout << "\n";
    nlohmann::ordered_json out;
    out["schemaVersion"] = kSchemaVersion;
    out["runs"] = names;
    out["note"] = "a vector, not a ranking. No composite score is computed and no weights exist "
                  "(ADR-250). Where two arms disagree, the disagreement is the result";
    for (const std::string& metricName : metricNames) {
        std::cout << fmt::format("{:<42}", metricName);
        nlohmann::ordered_json row = nlohmann::ordered_json::array();
        for (const auto& report : reports) {
            std::string cell = "-";
            nlohmann::ordered_json value = nullptr;
            for (const auto& metric : report["metrics"]) {
                if (metric["name"].get<std::string>() != metricName) {
                    continue;
                }
                if (!metric["available"].get<bool>()) {
                    cell = "unavailable";
                } else if (!metric["value"].is_null()) {
                    value = metric["value"];
                    cell = fmt::format("{:.4f}", metric["value"].get<double>());
                }
            }
            std::cout << fmt::format("{:>18}", cell);
            row.push_back(value);
        }
        std::cout << "\n";
        out["metrics"][metricName] = std::move(row);
    }
    if (!args.out.empty()) {
        std::error_code ec;
        fs::create_directories(args.out, ec);
        std::ofstream file(fs::path(args.out) / "comparison.json");
        file << out.dump(2) << "\n";
    }
    return 0;
}

// ---- validate ----------------------------------------------------------------------------------

void printLadder(const LadderResult& result) {
    for (const Arm& arm : result.arms) {
        std::cout << fmt::format("\n  {} [{}]\n    {}\n", arm.name, arm.passed() ? "PASS" : "FAIL",
                                 arm.construction);
        for (const Check& check : arm.checks) {
            std::cout << fmt::format("      [{}] {:<8} {}\n        {}   observed {:.5f} vs "
                                     "{:.5f}\n",
                                     check.passed ? "ok" : "XX",
                                     check.mustMove ? "must move" : "control", check.description,
                                     check.expectation, check.observed, check.reference);
        }
    }
}

nlohmann::ordered_json ladderJson(const LadderResult& result) {
    nlohmann::ordered_json arms = nlohmann::ordered_json::array();
    for (const Arm& arm : result.arms) {
        nlohmann::ordered_json a;
        a["name"] = arm.name;
        a["construction"] = arm.construction;
        a["passed"] = arm.passed();
        nlohmann::ordered_json checks = nlohmann::ordered_json::array();
        for (const Check& check : arm.checks) {
            nlohmann::ordered_json c;
            c["description"] = check.description;
            c["expectation"] = check.expectation;
            c["role"] = check.mustMove ? "must move" : "control";
            c["observed"] = check.observed;
            c["reference"] = check.reference;
            c["passed"] = check.passed;
            checks.push_back(std::move(c));
        }
        a["checks"] = std::move(checks);
        arms.push_back(std::move(a));
    }
    return arms;
}

int validate(const Args& args) {
    if (!args.ladder) {
        std::cerr << "validate needs --ladder\n";
        return 2;
    }
    const LadderResult spatial = runSpatialLadder();
    const LadderResult temporal = runTemporalLadder();
    const fs::path workDirectory =
        args.out.empty() ? fs::temp_directory_path() / "avgen-quality-validate" : fs::path(args.out);
    const LadderResult external = runExternalLadder(workDirectory);
    const ExternalTool externalTool = findVmafTool();

    // **The meta-control.** A ladder that cannot fail proves nothing about the metrics it runs, so
    // the same arms are run again against a metric that returns a constant. ADR-182's rule applied
    // to the validator: if this passes, the ladder is not asserting anything.
    MetricBindings broken = realMetrics();
    broken.spatialLaplacian = [](const Frame&) { return 1.0; };
    const LadderResult sabotaged = runSpatialLadder(broken);
    const bool sabotageDetected = !sabotaged.passed();

    if (args.json) {
        nlohmann::ordered_json out;
        out["version"] = kVersion;
        out["spatial"] = ladderJson(spatial);
        out["temporal"] = ladderJson(temporal);
        if (external.arms.empty()) {
            out["external"] = {{"available", false}, {"reason", externalTool.reason}};
        } else {
            out["external"] = ladderJson(external);
        }
        out["metaControl"] = {
            {"description",
             "the spatial ladder re-run with spatialLaplacian replaced by a constant"},
            {"ladderDetectedTheBrokenMetric", sabotageDetected},
            {"failedChecks", sabotaged.failedChecks()}};
        out["passed"] =
            spatial.passed() && temporal.passed() && external.passed() && sabotageDetected;
        std::cout << out.dump(2) << "\n";
    } else {
        std::cout << "SPATIAL LADDER (docs/quality-lab/metrics.md 4.1)";
        printLadder(spatial);
        std::cout << "\nTEMPORAL CONTROL ARMS (docs/quality-lab/artifact-detection.md 1)";
        printLadder(temporal);
        std::cout << "\nEXTERNAL METRICS (ffmpeg libvmaf -- optional, ADR-252)";
        if (external.arms.empty()) {
            std::cout << fmt::format("\n  unavailable: {}\n", externalTool.reason.empty()
                                                                  ? "libvmaf produced no arms"
                                                                  : externalTool.reason);
        } else {
            printLadder(external);
        }
        std::cout << fmt::format(
            "\nMETA-CONTROL: the spatial ladder re-run with spatialLaplacian replaced by a "
            "constant\n  the ladder {} the broken metric ({} of {} checks failed)\n",
            sabotageDetected ? "CAUGHT" : "DID NOT CATCH", sabotaged.failedChecks(),
            sabotaged.totalChecks());
        std::cout << fmt::format(
            "\nspatial {}/{} checks passed; temporal {}/{}; external {}/{}; meta-control {}\n",
            spatial.totalChecks() - spatial.failedChecks(), spatial.totalChecks(),
            temporal.totalChecks() - temporal.failedChecks(), temporal.totalChecks(),
            external.totalChecks() - external.failedChecks(), external.totalChecks(),
            sabotageDetected ? "ok" : "FAILED");
    }
    return spatial.passed() && temporal.passed() && external.passed() && sabotageDetected ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    std::string error;
    if (!parseArgs(argc, argv, args, error)) {
        if (!error.empty() && error != "no subcommand") {
            std::cerr << error << "\n";
        }
        usage();
        return 2;
    }
    if (args.command == "analyze") {
        return analyze(args);
    }
    if (args.command == "compare") {
        return compare(args);
    }
    if (args.command == "validate") {
        return validate(args);
    }
    if (args.command == "version") {
        const ExternalTool tool = findVmafTool();
        std::cout << fmt::format("avgen_quality {} (report schema {})\n", kVersion, kSchemaVersion);
        std::cout << fmt::format("  libvmaf metrics: {}\n",
                                 tool.available ? tool.version : "unavailable -- " + tool.reason);
        return 0;
    }
    std::cerr << fmt::format("unknown subcommand '{}'\n", args.command);
    usage();
    return 2;
}
