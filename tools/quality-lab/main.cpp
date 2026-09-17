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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
    std::string perFrame;
    std::string candidateHash;
    std::string referenceHash;
    std::size_t stride = 1;
    std::size_t band = 2;
    long long object = -1;
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
        } else if (flag == "--per-frame") {
            args.perFrame = next(i);
        } else if (flag == "--band") {
            args.band = static_cast<std::size_t>(std::max(1, std::atoi(next(i).c_str())));
        } else if (flag == "--object") {
            args.object = std::atoll(next(i).c_str());
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
            [--candidate-hash H] [--reference-hash H] [--per-frame <file.csv>]

  compare   --runs <run-dir> <run-dir> [...] [--out <dir>]

  control   --runs <arm-dir> <arm-dir> [...] [--aov-dir <dir>] [--object N]
            [--band N] [--out <dir>]
            Splits one object out of the identifier AOV into INTERIOR and SILHOUETTE-BAND
            pixels and reports how far each arm's luma moves from the first arm's over each,
            separately. A scene that calls a shape "the control" is making a claim about
            where a difference may appear; this is the thing that checks it. Without
            --object it surveys the identifier plane and prints what it found.

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
    std::vector<double> shadowResidual;
    std::vector<double> shadowCoverage;
    std::vector<double> vegetationResidual;
    std::vector<double> vegetationCoverage;
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
    bool sawShadow = false;

    // ADR-256: the surface classes of THIS render, written by the run that produced the AOVs.
    // Read rather than authored, for the reason the ADR is named after -- a list of integers
    // maintained beside the scene is silently wrong the moment the scene gains an object, and the
    // mask it produces is well-formed, the residual over it correct, and the surfaces the wrong
    // ones.
    std::vector<std::uint32_t> vegetationObjects;
    std::string manifestReason = "no materials.json beside the frames (render with --aov id)";
    {
        const fs::path manifestPath = aovRoot / "materials.json";
        std::ifstream manifest(manifestPath);
        if (manifest) {
            nlohmann::json doc = nlohmann::json::parse(manifest, nullptr, false);
            if (doc.is_discarded() || !doc.contains("objects")) {
                manifestReason = fmt::format("'{}' did not parse", manifestPath.string());
            } else if (doc.value("stable", true) == false) {
                // The render itself said its object set moved. A mapping that does not describe
                // the frames beside it is worse than no mapping, because it looks like one.
                manifestReason = fmt::format(
                    "materials.json is marked UNSTABLE: {}",
                    doc.value("reason", std::string("the scene changed during the render")));
            } else {
                std::size_t classified = 0;
                for (const auto& object : doc["objects"]) {
                    const std::string surface = object.value("class", std::string("unclassified"));
                    if (surface != "unclassified") {
                        ++classified;
                    }
                    if (surface == "vegetation" && object.contains("objectId")) {
                        vegetationObjects.push_back(object["objectId"].get<std::uint32_t>());
                    }
                }
                manifestReason =
                    vegetationObjects.empty()
                        ? fmt::format("materials.json classifies no object as vegetation ({} of {} "
                                      "object(s) carry any class at all)",
                                      classified, doc["objects"].size())
                        : std::string{};
            }
        }
    }

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
                // ADR-255. The shadow AOV is a RECOMPUTED term, not the one the lit pass used --
                // measured, not assumed: 0.43% of pixels differ. The limitation travels with the
                // number below rather than living only in a document.
                if (std::optional<Plane> shadow = loadAov(i, "shadow"); shadow.has_value()) {
                    sawShadow = true;
                    const Mask shadowed = shadowedMask(*shadow);
                    if (!shadowed.empty()) {
                        const auto gated = residual.over(shadowed);
                        series.shadowResidual.push_back(gated.residual);
                        series.shadowCoverage.push_back(gated.coverage);
                    }
                }
                // ADR-256.
                if (identifier.has_value() && !vegetationObjects.empty()) {
                    const Mask vegetation = objectClassMask(*identifier, vegetationObjects);
                    if (!vegetation.empty()) {
                        const auto gated = residual.over(vegetation);
                        series.vegetationResidual.push_back(gated.residual);
                        series.vegetationCoverage.push_back(gated.coverage);
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

    // metrics.md §5 pools and never reports a mean alone, and the pooled fields answer "how bad is
    // the worst frame". They do not answer "is arm A worse than arm B on THIS frame", which is a
    // different and stronger question: two arms of one experiment are rendered from the same
    // camera at the same times, so their metrics are PAIRED, and a difference of means that does
    // not survive the pairing is a difference nobody can act on. `--per-frame` writes the series
    // the pooling consumed, so the pairing can be done outside this tool.
    if (!args.perFrame.empty()) {
        std::ofstream csv(args.perFrame);
        if (!csv) {
            std::cerr << fmt::format("cannot write '{}'\n", args.perFrame);
            return 1;
        }
        csv << "frame,spatialLaplacian,spatialLaplacianRatio,msSsim,temporalAlternation\n";
        for (std::size_t i = 0; i < analysed.size(); ++i) {
            csv << analysed[i] << ',' << fmt::format("{:.6f}", series.laplacianCandidate[i]) << ','
                << fmt::format("{:.6f}", series.laplacianRatio[i]) << ','
                << fmt::format("{:.6f}", series.msSsim[i]) << ',';
            // alternation needs three frames, so its first value belongs to the third analysed one
            if (i >= 2 && (i - 2) < series.alternation.size()) {
                csv << fmt::format("{:.6f}", series.alternation[i - 2]);
            }
            csv << '\n';
        }
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
         "between scenes; never as an absolute bar (ADR-243, tools/spatial_stats.py)",
         "ADR-257: this is the one metric here validated against a person. A blind reviewer ranked "
         "three anti-aliasing arms on a moving camera in this metric's order, in both directions, "
         "and it holds frame by frame on 60 of 60 frames",
         "its sensitivity has ONE calibration point in each direction and is a bracket, not a "
         "threshold: a 6.5% difference was invisible at 1x playback on a moving camera (visible in "
         "a frozen close-up) and a 16.9% difference was 'immediately obvious'"}));
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
            {"ADR-243: anti-correlated with human judgement on a STATIC camera over "
             "wind-animated sub-pixel geometry, where it ranked both remedies backwards. Never "
             "reported without detail.spatialLaplacian beside it, and never used alone to choose "
             "work",
             "ADR-257: on a MOVING camera it does not invert -- and it does not discriminate "
             "either. On the three aliasing-dolly arms at 1280x720 it separated FXAA-off from the "
             "baseline on 58 of 58 frames by 0.6%, and called the supersampled arm -- the one a "
             "blind reviewer called 'immediately obvious' and best -- WORSE on 26 of 58. Where the "
             "eye was most certain this number is a coin flip",
             "its effect size is a property of the render size: the same arms separate 4x more "
             "strongly at 640x360 than at 1280x720, so a direction check passed at preview "
             "resolution has been passed for preview resolution (ADR-253's follow-up)",
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

    // ADR-255: shadow. No longer a named absence -- and no longer quite what its name promises
    // either, which is why the qualification is in the metric and not only in an ADR.
    if (!series.shadowResidual.empty()) {
        report.add(makeMetric(
            "perClass.shadowStability", "luma steps 0..255",
            "candidate PNG sequence + velocity/depth/id AOVs, masked by the shadow AOV",
            "mean over frame pairs", series.shadowResidual, true,
            {"⚠ THE SHADOW AOV IS A SECOND OPINION, NOT A CAPTURE. At the tiers an offline render "
             "uses the engine builds no shadow mask at all -- the lit pass computes the term "
             "inline -- so --aov shadow runs a dedicated pass that RECOMPUTES it (ADR-255). "
             "Measured rather than assumed: a frame whose lit pass consumes this term differs from "
             "one that computes it inline on 0.43% of pixels, peak 22 of 255, against a control "
             "(the shadow atlas halved) that moves 1.86%. The likely cause is that this pass "
             "reconstructs its normal from the depth buffer and the lit pass uses the shading "
             "normal, which moves the shadow lookup's bias -- a hypothesis, not a finding",
             "the mask is the KEY light's visibility below 0.5, and a penumbra is a continuum: "
             "where a soft edge stops being shadow is a threshold somebody chose",
             "the screen-space contact march is deliberately not in this term (ADR-087), so a "
             "contact shadow is not in this mask",
             "inherits every limitation of the motion-compensated residual it is a gating of"}));
        report.add(makeMetric("perClass.shadowCoverage", "fraction", "the shadow AOV",
                              "mean over frame pairs", series.shadowCoverage, false,
                              {"read it beside shadowStability, always: a residual over 2% of the "
                               "frame is a statement about 2% of the frame"}));
    } else {
        report.add(Metric::unavailable("perClass.shadowStability",
                                       sawShadow ? "the shadow AOV carried no shadowed pixel at "
                                                   "the 0.5 visibility threshold"
                                                 : "no shadow AOV beside the frames (render with "
                                                   "--aov shadow; ADR-255)"));
    }

    // ADR-256: vegetation. The mask mechanism was always here; the mapping is what was missing,
    // and it now ships with the frames rather than beside the scene.
    if (!series.vegetationResidual.empty()) {
        report.add(makeMetric(
            "perClass.vegetationResidual", "luma steps 0..255",
            "candidate PNG sequence + velocity/depth/id AOVs, masked by materials.json",
            "mean over frame pairs", series.vegetationResidual, true,
            {"the class comes from materials.json, written by the render that produced these "
             "frames (ADR-256). It is keyed on the identifier's LOW 16 bits: the high half is the "
             "object's ordinal within its pick space and not a material index, whatever its name",
             "a scatter layer whose asset carries no library category falls back to a keyword "
             "table over its filename, which is inspectable and wrong in ways an author can see -- "
             "on Glowmere it reads 'pebbles' and 'beacons' as vegetation",
             "inherits every limitation of the motion-compensated residual it is a gating of"}));
        report.add(makeMetric("perClass.vegetationCoverage", "fraction",
                              "the id AOV + materials.json", "mean over frame pairs",
                              series.vegetationCoverage, false,
                              {"read it beside vegetationResidual, always"}));
    } else {
        report.add(Metric::unavailable("perClass.vegetationResidual",
                                       manifestReason.empty()
                                           ? "no id AOV beside the frames to mask with"
                                           : manifestReason));
    }

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

// ---- control -----------------------------------------------------------------------------------
//
// **A scene that calls a shape "the control" is making a claim, and the claim is testable.**
// `examples/quality/aliasing*.scene.json` calls its large smooth orb a control -- "it cannot alias,
// so a metric that moves on this scene's arms must not be moving here" -- and half of that is
// wrong: a sphere's *interior shading* is unaffected by anti-aliasing and its *silhouette* is a
// curved edge like any other, affected exactly as much as the fence is. The half that is true is
// the half this subcommand can isolate, so it isolates it rather than arguing about it.
//
// The split comes from the identifier AOV, which is exact (R32Uint), and is built ONCE from the
// first arm and applied unchanged to every arm -- the arms differ only in how the renderer filtered
// the same geometry, so a mask rebuilt per arm would be a different mask per arm and the comparison
// would no longer be a comparison.
//
// Two guards, both ADR-182: the interior mask and the band mask must each be non-empty and neither
// may be the whole frame, and a **temporal control** is reported beside every row -- the same masks
// applied to consecutive frames of the first arm, which must be clearly non-zero. An interior that
// cannot differ between arms because nothing in it can differ at all is the reassuring null result
// this repository keeps writing ADRs about.

struct RegionRow {
    std::vector<double> interiorMean;
    std::vector<double> interiorMax;
    std::vector<double> bandMean;
    std::vector<double> bandMax;
    std::vector<double> elsewhereMean;
};

double meanOf(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    return sum / static_cast<double>(values.size());
}

double maxOf(const std::vector<double>& values) {
    double m = 0.0;
    for (const double v : values) {
        m = std::max(m, v);
    }
    return m;
}

int control(const Args& args) {
    if (args.runs.size() < 2 && args.object >= 0) {
        std::cerr << "control needs at least two --runs directories to compare\n";
        return 2;
    }
    if (args.runs.empty()) {
        std::cerr << "control needs --runs\n";
        return 2;
    }
    std::vector<Sequence> sequences;
    for (const std::string& run : args.runs) {
        auto sequence = discoverSequence(run);
        if (!sequence) {
            std::cerr << sequence.error().message << "\n";
            return 1;
        }
        if (sequence->empty()) {
            std::cerr << fmt::format("'{}' has no frames\n", run);
            return 1;
        }
        sequences.push_back(std::move(*sequence));
    }
    std::size_t frames = sequences[0].size();
    for (const Sequence& sequence : sequences) {
        frames = std::min(frames, sequence.size());
    }

    // The identifier AOV comes from ONE arm and is applied to all of them.
    std::vector<fs::path> idFrames;
    if (args.aovDir.empty()) {
        for (std::size_t i = 0; i < frames; ++i) {
            idFrames.push_back(sequences[0].frames[i]);
        }
    } else {
        auto aovSequence = discoverSequence(args.aovDir);
        if (!aovSequence || aovSequence->size() < frames) {
            std::cerr << fmt::format("--aov-dir '{}' does not carry {} frames\n", args.aovDir,
                                     frames);
            return 1;
        }
        for (std::size_t i = 0; i < frames; ++i) {
            idFrames.push_back(aovSequence->frames[i]);
        }
    }
    if (!hasAov(idFrames[0], "id")) {
        std::cerr << fmt::format(
            "no id AOV beside '{}'. Render the arm the mask comes from with --aov id: without an "
            "identifier plane there is no object to split and every number below would be about "
            "the whole frame\n",
            idFrames[0].string());
        return 1;
    }

    // ---- the survey. Also the non-vacuity evidence: an identifier plane of one constant produces
    // a mask of everything or a mask of nothing, and both look like a working detector.
    auto firstId = readPlane(aovPathFor(idFrames[0], "id"));
    if (!firstId) {
        std::cerr << firstId.error().message << "\n";
        return 1;
    }
    const IdentifierSurvey survey = surveyIdentifiers(*firstId);
    if (!survey.usable()) {
        std::cerr << fmt::format("the identifier plane has {} distinct value(s): every mask built "
                                 "from it is everything or nothing (ADR-242's test, inherited)\n",
                                 survey.distinctValues);
        return 1;
    }
    if (args.object < 0) {
        struct ObjectExtent {
            std::size_t pixels = 0;
            std::uint32_t material = 0;
            std::uint32_t minX = 0xffffffffu;
            std::uint32_t minY = 0xffffffffu;
            std::uint32_t maxX = 0;
            std::uint32_t maxY = 0;
        };
        std::map<std::uint32_t, ObjectExtent> counts;
        for (std::uint32_t y = 0; y < firstId->height; ++y) {
            for (std::uint32_t x = 0; x < firstId->width; ++x) {
                const float packed = firstId->at(x, y)[0];
                if (packed == 0.0f) {
                    continue;
                }
                ObjectExtent& extent = counts[objectIdOf(packed)];
                ++extent.pixels;
                extent.material = materialIdOf(packed);
                extent.minX = std::min(extent.minX, x);
                extent.minY = std::min(extent.minY, y);
                extent.maxX = std::max(extent.maxX, x);
                extent.maxY = std::max(extent.maxY, y);
            }
        }
        std::vector<std::pair<std::uint32_t, ObjectExtent>> ordered(counts.begin(), counts.end());
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& a, const auto& b) { return a.second.pixels > b.second.pixels; });
        const double total = static_cast<double>(firstId->width) * firstId->height;
        std::cout << fmt::format(
            "{}: {} distinct identifiers, background {:.2f}% of the frame\n\n",
            aovPathFor(idFrames[0], "id").filename().string(), survey.distinctValues,
            survey.backgroundFraction * 100.0);
        // The bounding box and the fill are how you tell a compact object from a scattered one:
        // a sphere fills most of its box, and an array of 160 fence slats fills almost none of it.
        std::cout << fmt::format("{:>10}{:>10}{:>10}{:>9}{:>22}{:>7}\n", "object", "material",
                                 "pixels", "of frame", "bounding box", "fill");
        for (std::size_t i = 0; i < ordered.size() && i < 16; ++i) {
            const ObjectExtent& e = ordered[i].second;
            const double box = static_cast<double>(e.maxX - e.minX + 1) *
                               static_cast<double>(e.maxY - e.minY + 1);
            std::cout << fmt::format(
                "{:>10}{:>10}{:>10}{:>8.3f}%{:>22}{:>6.2f}\n", ordered[i].first, e.material,
                e.pixels, 100.0 * static_cast<double>(e.pixels) / total,
                fmt::format("({},{})-({},{})", e.minX, e.minY, e.maxX, e.maxY),
                static_cast<double>(e.pixels) / box);
        }
        std::cout << "\npass one of these to --object.\n";
        return 0;
    }

    const auto objectId = static_cast<std::uint32_t>(args.object);
    const int band = static_cast<int>(args.band);
    RegionRow zero;
    std::vector<RegionRow> rows(sequences.size(), zero);
    RegionRow temporalControl; // the first arm against its own previous frame, same masks
    std::vector<double> interiorCoverage;
    std::vector<double> bandCoverage;
    std::vector<double> objectCoverage;
    Frame previousBaseline;
    std::size_t framesMeasured = 0;
    std::size_t framesWithoutObject = 0;

    for (std::size_t i = 0; i < frames; ++i) {
        auto idPlane = readPlane(aovPathFor(idFrames[i], "id"));
        if (!idPlane) {
            std::cerr << idPlane.error().message << "\n";
            return 1;
        }
        std::vector<Frame> armFrames;
        for (const Sequence& sequence : sequences) {
            auto frame = readFrame(sequence.frames[i]);
            if (!frame) {
                std::cerr << frame.error().message << "\n";
                return 1;
            }
            if (frame->width != idPlane->width || frame->height != idPlane->height) {
                std::cerr << fmt::format(
                    "frame {} is {}x{} and the identifier plane is {}x{}: the mask would be "
                    "measuring different pixels than the arms\n",
                    i, frame->width, frame->height, idPlane->width, idPlane->height);
                return 1;
            }
            armFrames.push_back(std::move(*frame));
        }

        const SilhouetteSplit split = splitSilhouette(*idPlane, objectId, band);
        const double interiorFraction = coverage(split.interior);
        const double rimFraction = coverage(split.band);
        if (interiorFraction <= 0.0 || rimFraction <= 0.0) {
            ++framesWithoutObject;
            continue;
        }
        interiorCoverage.push_back(interiorFraction);
        bandCoverage.push_back(rimFraction);
        objectCoverage.push_back(coverage(split.object));

        for (std::size_t k = 0; k < sequences.size(); ++k) {
            const MaskedDifference inside =
                maskedLumaDifference(armFrames[k], armFrames[0], split.interior);
            const MaskedDifference edge =
                maskedLumaDifference(armFrames[k], armFrames[0], split.band);
            const MaskedDifference rest =
                maskedLumaDifference(armFrames[k], armFrames[0], split.elsewhere);
            rows[k].interiorMean.push_back(inside.mean);
            rows[k].interiorMax.push_back(inside.max);
            rows[k].bandMean.push_back(edge.mean);
            rows[k].bandMax.push_back(edge.max);
            rows[k].elsewhereMean.push_back(rest.mean);
        }
        if (previousBaseline.valid()) {
            const MaskedDifference inside =
                maskedLumaDifference(armFrames[0], previousBaseline, split.interior);
            const MaskedDifference edge =
                maskedLumaDifference(armFrames[0], previousBaseline, split.band);
            const MaskedDifference rest =
                maskedLumaDifference(armFrames[0], previousBaseline, split.elsewhere);
            temporalControl.interiorMean.push_back(inside.mean);
            temporalControl.interiorMax.push_back(inside.max);
            temporalControl.bandMean.push_back(edge.mean);
            temporalControl.bandMax.push_back(edge.max);
            temporalControl.elsewhereMean.push_back(rest.mean);
        }
        previousBaseline = armFrames[0];
        ++framesMeasured;
    }

    if (framesMeasured == 0) {
        std::cerr << fmt::format("object {} produced no frame with both an interior and a "
                                 "silhouette band at band={} px. Nothing was measured\n",
                                 objectId, band);
        return 1;
    }

    std::cout << fmt::format(
        "\nobject {} over {} frame(s){}, band {} px, mask from '{}'\n", objectId, framesMeasured,
        framesWithoutObject == 0
            ? std::string{}
            : fmt::format(" ({} skipped: the object had no interior or no band in them)",
                          framesWithoutObject),
        band, args.aovDir.empty() ? args.runs[0] : args.aovDir);
    std::cout << fmt::format(
        "coverage: object {:.3f}%   interior {:.3f}%   silhouette band {:.3f}%   of the frame\n",
        meanOf(objectCoverage) * 100.0, meanOf(interiorCoverage) * 100.0,
        meanOf(bandCoverage) * 100.0);
    std::cout << "every number is |luma difference| in steps 0..255, against the FIRST arm\n\n";
    std::cout << fmt::format("{:<26}{:>12}{:>12}{:>12}{:>12}{:>12}\n", "arm", "interior",
                             "int. max", "band", "band max", "elsewhere");
    for (std::size_t k = 0; k < sequences.size(); ++k) {
        std::cout << fmt::format("{:<26}{:>12.4f}{:>12.4f}{:>12.4f}{:>12.4f}{:>12.4f}\n",
                                 fs::path(args.runs[k]).filename().string(),
                                 meanOf(rows[k].interiorMean), maxOf(rows[k].interiorMax),
                                 meanOf(rows[k].bandMean), maxOf(rows[k].bandMax),
                                 meanOf(rows[k].elsewhereMean));
    }
    std::cout << fmt::format(
        "\n{:<26}{:>12.4f}{:>12.4f}{:>12.4f}{:>12.4f}{:>12.4f}\n", "CONTROL frame-to-frame",
        meanOf(temporalControl.interiorMean), maxOf(temporalControl.interiorMax),
        meanOf(temporalControl.bandMean), maxOf(temporalControl.bandMax),
        meanOf(temporalControl.elsewhereMean));
    std::cout << "  the first arm against its own previous frame, over the same masks. It is what\n"
                 "  makes an interior row of zero a statement about the arms rather than about a\n"
                 "  region where nothing can change (ADR-182).\n";

    if (!args.out.empty()) {
        nlohmann::ordered_json out;
        out["schemaVersion"] = kSchemaVersion;
        out["object"] = objectId;
        out["bandPixels"] = band;
        out["framesMeasured"] = framesMeasured;
        out["unit"] = "luma steps 0..255, |arm - first arm|";
        out["coverage"] = {{"object", meanOf(objectCoverage)},
                           {"interior", meanOf(interiorCoverage)},
                           {"silhouetteBand", meanOf(bandCoverage)}};
        out["note"] = "the mask is built from ONE arm's identifier AOV and applied unchanged to "
                      "every arm; a mask rebuilt per arm would be a different mask per arm";
        for (std::size_t k = 0; k < sequences.size(); ++k) {
            out["arms"][args.runs[k]] = {{"interiorMean", meanOf(rows[k].interiorMean)},
                                         {"interiorMax", maxOf(rows[k].interiorMax)},
                                         {"bandMean", meanOf(rows[k].bandMean)},
                                         {"bandMax", maxOf(rows[k].bandMax)},
                                         {"elsewhereMean", meanOf(rows[k].elsewhereMean)}};
        }
        out["frameToFrameControl"] = {{"interiorMean", meanOf(temporalControl.interiorMean)},
                                      {"interiorMax", maxOf(temporalControl.interiorMax)},
                                      {"bandMean", meanOf(temporalControl.bandMean)},
                                      {"bandMax", maxOf(temporalControl.bandMax)}};
        std::error_code ec;
        fs::create_directories(args.out, ec);
        std::ofstream file(fs::path(args.out) / "control.json");
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
    if (args.command == "control") {
        return control(args);
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
