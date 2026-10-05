#pragma once

// ADR-1090..1093: `avgen --live-profile`, the live scene profiler (docs/live-optimizer/02-plan.md, Stage 1).
//
// This header is the profiler's *logic*, and nothing in it touches a device, a window or a clock: the record a run
// fills, the statistics taken over it, the table that groups GPU timeline labels into the brief's categories, the
// critical-path verdict, the optimization-candidate rules and the two writers (JSON `avgen.liveprofile/1` and the
// brief's human text report). The frame loops that FILL a record are in `application.cpp` (headless: the fixed-step
// clock; live: the real editor loop with present and Fifo); both hand their samples to the one builder here, which is
// what "both modes share one record builder" means.
//
// Two rules run through every type below, and both are the owner's:
//   * a number is labelled with how it was obtained. `measured` was timed; `coarse` was timed but covers more than the
//     name says; `estimated` was computed from a model. An estimated saving and a measured saving are separate fields
//     and are never added, averaged or printed in one column.
//   * nothing is invented. A category the engine cannot time separately is reported as "not separately measurable"
//     rather than given a share of a neighbour's number.

#include "app/live_optimize.hpp"
#include "rendering/render_stats.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {

// ---- the command's options -----------------------------------------------------------------------------------------

enum class LiveProfileMode : std::uint8_t {
    Headless, // the fixed-step clock: deterministic, no present, no UI. For agents iterating on a scene.
    Live,     // the real editor loop: present, Fifo, the projection output. What predicts a touring frame.
};

struct LiveProfileOptions {
    bool enabled = false;
    LiveProfileMode mode = LiveProfileMode::Headless;
    double targetFps = 60.0;
    std::uint32_t outputWidth = 1920; // the OUTPUT in pixels (the projection's, in live mode)
    std::uint32_t outputHeight = 1080;
    double startSeconds = 0.0;
    double warmupSeconds = 3.0;  // a cap: measurement starts earlier when the frame settles
    double measureSeconds = 5.0;
    bool deep = false;
    std::string quality = "auto"; // auto | ultra..emergency | quality | balanced | performance | a tier name
    std::string camera;           // a named camera / shot; empty = the project's own
    bool audio = true;
    std::string midi;             // a MIDI input filter; empty = the machine's setting
    std::optional<std::filesystem::path> capture; // a PNG of the last measured frame
    std::optional<std::filesystem::path> json;    // the record, avgen.liveprofile/1
    bool text = true;                              // the human report on stdout
    int verifyCandidates = 0;                      // measure the top N candidates' savings (A/B), 0 = estimate only
    bool prewarm = true; // --no-prewarm: the pre-ADR-1102 behaviour (SDF variants compiled on the main thread at first
                         // use), kept so a before/after can be measured in one build
    // ---- Phase 5 (ADR-1110..1112), headless only ----
    std::string compare;          // --compare <lever,lever|project>: ORIGINAL vs OPTIMIZED, timed and imaged
    bool optimize = false;        // --optimize: the combination search to the target
    std::string heroPolicy = "protect";  // --hero-policy protect|strict (ADR-1109)
    std::string optimizeRisk = "medium"; // --optimize-risk low|medium|high: the riskiest lever the search may try
    double optimizeMargin = 0.9;  // --optimize-margin: the search's target is budget x margin (ADR-1107's 0.9)
    int optimizeCandidates = 8;   // --optimize-candidates: at most this many singles measured
    int abFrames = 12;            // --ab-frames: consecutive frames captured per arm (after 10 settling frames)
    std::optional<std::filesystem::path> abDir; // --ab-dir: where the ORIGINAL/OPTIMIZED frames are kept
    bool abCritic = false;        // --ab-critic: also ask the Creative Critic (optional; ADR-1112)
};

// Splits a command line into the profiler's own flags and everything else, which the ordinary parser then reads
// (`--project`, `--tier`, ...). The profiler's flags are only flags when `--live-profile` is present, so `--start`,
// `--measure` and the rest cannot collide with the editor's own. `--size`, `--capture` and `--midi` are taken over:
// with `--live-profile` they mean the OUTPUT size in pixels, the profile's capture and its MIDI input.
struct LiveProfileArgs {
    LiveProfileOptions options;
    std::vector<std::string> rest; // argv[0] first, then every argument the profiler did not take
    std::string error;             // non-empty: the command line is wrong, and this says how
};
[[nodiscard]] LiveProfileArgs parseLiveProfileArgs(const std::vector<std::string>& argv);
[[nodiscard]] bool hasLiveProfileFlag(int argc, char** argv);
[[nodiscard]] std::string liveProfileUsage();

// ---- steady state (1b) ---------------------------------------------------------------------------------------------

// "Warm-up frames until steady state": the rolling median of the last `window` frames has stopped moving -- it has
// stayed within `tolerance` (a fraction) of where it was `window` frames earlier, `confirmations` times in a row. A cap
// in seconds ends warm-up regardless, and the record says which ended it.
class SteadyStateDetector {
public:
    explicit SteadyStateDetector(int window = 30, double tolerance = 0.05, int confirmations = 2, int minFrames = 60);
    // One frame's cost (ms). Returns true once steady.
    bool note(double ms);
    [[nodiscard]] bool steady() const { return steady_; }
    [[nodiscard]] int frames() const { return static_cast<int>(samples_.size()); }
    [[nodiscard]] double lastMedian() const { return lastMedian_; }

private:
    int window_;
    double tolerance_;
    int confirmations_;
    int minFrames_;
    std::vector<double> samples_;
    double previousMedian_ = -1.0;
    double lastMedian_ = -1.0;
    int streak_ = 0;
    bool steady_ = false;
};

// ---- one frame's measurements --------------------------------------------------------------------------------------

struct LiveProfileFrame {
    double atSeconds = 0.0;   // wall seconds since measurement began
    double frameMs = 0.0;     // live: the measured frame interval; headless: the wall clock of the frame
    double gpuMs = -1.0;      // the GPU timeline's span (-1 = no timing this frame)
    double cpuWorkMs = 0.0;   // main-thread work, waits excluded
    double waitMs = 0.0;      // live: swapchain acquire + present waits; headless: the queue wait
    double outputsMs = 0.0;   // live: the projection/outputs present; headless 0
    // The CPU stages, per frame (probe2 + the renderer's CpuFrameBreakdown); medians are taken by the builder.
    double updControlMs = 0.0, updSignalsMs = 0.0, updModulationMs = 0.0, updControllerMs = 0.0, updOtherMs = 0.0;
    double analysisCatchupMs = 0.0, meshUploadMs = 0.0, textureUploadMs = 0.0, environmentMs = 0.0;
    double engineUpdateMs = 0.0, renderRecordMs = 0.0, uiMs = 0.0;
    rendering::CpuFrameBreakdown render;
    std::vector<gpu::TimelineInterval> passes; // this frame's passes, summed per label
};

// ---- what the record holds -----------------------------------------------------------------------------------------

struct LiveProfileConditions {
    std::string scene;
    std::string mode;          // "headless" | "live"
    std::string machine;       // hw.model
    std::string cpu;           // machdep.cpu.brand_string
    std::string os;            // product name and version
    std::string gpu;           // the adapter
    std::string backend;       // e.g. "WebGPU / Dawn / Metal"
    std::string buildType;
    std::string gitRevision;
    bool gitDirty = false;
    double displayRefreshHz = 0.0; // 0 = unknown / no display (headless)
    std::string window;            // "offscreen" | "windowed" | "fullscreen"
    std::uint32_t outputWidth = 0, outputHeight = 0;
    std::uint32_t internalWidth = 0, internalHeight = 0;
    double targetFps = 60.0;
    double budgetMs = 1000.0 / 60.0;      // the frame budget the target implies
    double qualityBudgetMs = 0.0;         // the live controller's GPU budget (ADR-1080's headroom)
    std::string qualityRequested;         // --quality as given
    std::string liveLevel;                // the live level in force at the end (Ultra..Emergency), or "tier"
    std::string strategy;                 // the project's live strategy (ADR-1084)
    std::string profile;                  // the named profile in force (Stage 2), or empty
    float renderScale = 1.0f;
    bool audio = true;
    std::string audioState;               // "file", "live input", "off", "none"
    std::string midi;                     // "off" or the filter
    std::uint64_t seed = 0;
    bool haveSeed = false;
    std::string camera;
    double startSeconds = 0.0;
    double warmupCapSeconds = 0.0, measureSeconds = 0.0;
    bool deep = false;
    std::string startedAt, sessionId;
};

struct LiveProfileCold {
    double loadMs = -1.0;       // process start to the first frame's start (the scene load and asset upload)
    double prewarmMs = -1.0;    // the pre-warm (Stage 4); -1 = none ran
    int prewarmVariants = 0;    // SDF variants / pipelines the pre-warm built
    double firstFrameMs = -1.0; // the first rendered frame's wall clock
    int warmupFrames = 0;
    double warmupSeconds = 0.0;
    bool steadyReached = false; // false: the cap ended warm-up and the frame was still moving
    int compilesDuringMeasure = 0; // pipelines/variants created while measuring: a touring frame must have 0
};

struct BudgetStats {
    double budgetMs = 0.0;
    std::size_t frames = 0;
    std::size_t overBudget = 0;
    double percentUnder = 0.0;
    // Live only: in vsync terms. A frame misses its deadline when it took at least one more refresh than the target's
    // frame period allows (a 60 target on a 120 Hz display: a frame of three or more vsyncs).
    double refreshMs = 0.0;     // 0 = not applicable (headless)
    std::size_t deadlineMisses = 0;
    std::size_t vsyncsMissed = 0; // the refreshes the misses cost, summed
    double percentMissed = 0.0;
};
[[nodiscard]] BudgetStats budgetStats(const std::vector<double>& frameMs, double budgetMs, double refreshMs);

struct WorstFrame {
    std::size_t index = 0;
    double atSeconds = 0.0;
    double frameMs = 0.0;
    double gpuMs = -1.0;
    double cpuWorkMs = 0.0;
    double waitMs = 0.0;
};
[[nodiscard]] std::vector<WorstFrame> worstFrames(const std::vector<LiveProfileFrame>& frames, std::size_t n);

// 1c: GPU, CPU or sync/present, from the measured medians.
struct CriticalPath {
    std::string verdict;     // "GPU" | "CPU" | "sync/present" | "balanced"
    double frameMs = 0.0;    // the frame median the shares are of
    double gpuMs = 0.0;
    double cpuWorkMs = 0.0;
    double waitMs = 0.0;
    std::string explanation;
};
[[nodiscard]] CriticalPath criticalPath(double frameMs, double gpuMs, double cpuWorkMs, double waitMs,
                                        double budgetMs, bool live);

// 1d.
struct CpuCategory {
    std::string name;
    double medianMs = -1.0; // -1 = not separately measurable
    std::string basis;      // "measured" | "coarse" | "not separately measurable"
    std::string covers;     // what the number contains, in words
};

// 1e: the GPU categories, through one table. Unmapped labels go to "other".
struct GpuCategoryRule {
    std::string_view pattern;  // an exact label, or a prefix ending in '*'
    std::string_view category;
};
[[nodiscard]] const std::vector<GpuCategoryRule>& gpuCategoryTable();
[[nodiscard]] std::string_view gpuCategoryOf(std::string_view label);
[[nodiscard]] const std::vector<std::string_view>& gpuCategoryOrder();
struct GpuCategory {
    std::string name;
    double medianMs = 0.0;  // the sum of its labels' medians
    std::vector<gpu::TimelineInterval> labels;
};
[[nodiscard]] std::vector<GpuCategory> groupGpuCategories(const std::vector<gpu::TimelineInterval>& passMedians);

// 1f.
struct LiveProfileTexture {
    std::string label;
    std::uint64_t bytes = 0;
    std::string detail; // format and size, as the driver reports them
};
struct LiveProfileResources {
    // Geometry (medians over the measured window)
    double draws = 0, shadowDraws = 0, triangles = 0, logicalTriangles = 0, visibleInstances = 0, culledInstances = 0;
    double entities = 0, lod[4] = {0, 0, 0, 0};
    double entityLodDrawables = 0, entityLodDemoted = 0;
    // Memory (Dawn's own accounting, read once after measurement: no per-frame cost)
    bool haveMemory = false;
    std::uint64_t textureBytes = 0, bufferBytes = 0, depthStencilBytes = 0, totalBytes = 0;
    std::uint64_t renderTargetBytes = 0; // textures created as render attachments
    std::uint64_t textureCount = 0, bufferCount = 0;
    std::vector<LiveProfileTexture> largestTextures;
    // Pipelines and shaders
    std::uint64_t renderPipelines = 0, computePipelines = 0, shaderModules = 0;
    std::uint64_t sdfVariants = 0;     // compiled SDF scene variants alive
    std::uint64_t materialPrograms = 0; // procedural material programs in the frame
    std::uint64_t materialProgramOps = 0;
    // Shadows
    double shadowCasters = 0, shadowViews = 0, cascades = 0, spotMaps = 0, pointMaps = 0, shadowResolution = 0;
    double shadowLights = 0;  // lights casting shadows
    bool shadowPassSkipped = false; // Stage 2: the shadow pass did not run because nothing visible is lit
    // Lights and materials
    double lights = 0, directionalLights = 0, clusteredLights = 0;
    double litDraws = -1, unlitDraws = -1; // -1 = not counted
    // Particles
    double particleSystems = 0, particleCapacity = 0, particlesEmitted = 0, particleDispatches = 0;
    double particleSimSteps = 0; // simulation steps per rendered frame
    // Post
    double postPasses = 0, bloomLevels = 0;
    std::uint32_t postWidth = 0, postHeight = 0; // the size the post chain ran at
    std::uint32_t volumeWidth = 0, volumeHeight = 0;
    std::uint32_t aoWidth = 0, aoHeight = 0;
    double volumeSteps = 0;
    double sdfRaymarchObjects = 0, sdfAvgSteps = 0;
    double computeDispatches = 0, gpuPasses = 0, transientTextures = 0;
    // ADR-1116..1120: the GPU procedural systems' own bytes and work, which Dawn's totals above contain but do
    // not name. Medians over the measured window, except the held bytes (the last frame's).
    double generatorObjects = 0, generatorCells = 0, generatorMs = -1;
    std::uint64_t generatorBytes = 0;
    bool audioBound = false;
    double audioRowsPerFrame = 0;
    std::uint64_t audioRingBytes = 0;
    double simGrids = 0, simAgents = 0, simStepsPerFrame = 0;
    std::uint64_t simStateBytes = 0, simCheckpointBytes = 0;
    std::uint32_t simCheckpoints = 0;
};

// 1h.
struct LiveProfileCandidate {
    std::string id;           // a stable key ("volume-resolution")
    std::string title;        // "Volumetric fog"
    double costMs = 0.0;      // what the thing costs now, measured (the GPU category or pass median)
    std::string suggestion;   // "quarter-resolution fog (volumeResolutionScale 0.25)"
    std::string lever;        // the A/B arm that applies it (a --quality-arm / --ab name), empty = none
    double estimatedLowMs = 0.0, estimatedHighMs = 0.0; // ESTIMATED: from the per-pass scaling model
    std::string estimateBasis;
    std::string risk;         // "low" | "medium" | "high"
    std::string heroEffect;   // ADR-1109: "exempt" | "image-wide" | "degrades"
    // MEASURED, only after --verify-candidates. Never merged with the estimate.
    bool verified = false;
    double measuredSavingMs = 0.0;
    double measuredSavingPercent = 0.0;
    double noiseFloorPercent = 0.0;
    bool measuredIsResult = false; // outside the noise floor
    bool measuredVoid = false;     // drift larger than the effect
    std::string measuredVerdict;
    int measuredPairs = 0;
};

// What the rules read: the record's measured costs, plus the quality state they would change.
struct CandidateInputs {
    std::vector<GpuCategory> gpu;
    std::vector<gpu::TimelineInterval> passes;
    LiveProfileResources resources;
    float renderScale = 1.0f;
    float volumeResolutionScale = 0.5f;
    float volumeStepScale = 1.0f;
    bool motionBlur = true;
    bool depthOfField = true;
    bool softShadows = true;
    float postEffectQuality = 1.0f;
    float lodBias = 1.0f;
    float drawDistanceScale = 1.0f;
    float shadowCasterMinPixels = 0.0f;
    float particleSpawnScale = 1.0f;
    double gpuMs = 0.0;
    double budgetMs = 16.67;
    double perPixelShare = -1.0; // the scene's resolution-dependent share if it is known, else -1
};
[[nodiscard]] std::vector<LiveProfileCandidate> optimizationCandidates(const CandidateInputs& in);

// 5 (groundwork only): per-entity screen-space data a later phase can rank contributors with.
struct LiveProfileEntity {
    std::string name;
    double projectedArea = 0.0; // fraction of the screen its bounds cover (0..1, clipped): the coverage
    double distance = 0.0;      // metres from the camera
    bool hero = false;
    std::string importance;     // "hero" | "foreground" | "normal" | "background" | "ambient"
    bool castsShadow = false;
    bool visible = false;
    // ADR-1108
    double radiusPx = 0.0;      // the bounding sphere's radius on screen, the caster floor's formula
    float leverWeight = 1.0f;   // scene::importanceLeverWeight
    double contribution = 0.0;  // coverage / weight; +inf for a hero
    bool onScreen = false;      // its centre projects inside the frame (or the camera is inside it)
    bool haveBox = false;       // a screen box (on screen, in front)
    std::uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct LiveProfileRecord {
    LiveProfileConditions conditions;
    LiveProfileCold cold;
    rendering::Distribution frameMs;   // live: frame interval; headless: wall clock
    rendering::Distribution gpuMs;
    rendering::Distribution cpuWorkMs;
    rendering::Distribution waitMs;
    std::string frameBasis;            // what frameMs is, in words
    BudgetStats budget;                // over frameMs
    BudgetStats gpuBudget;             // the GPU span against the same budget
    std::vector<WorstFrame> worst;
    CriticalPath critical;
    std::vector<CpuCategory> cpu;
    std::vector<GpuCategory> gpu;
    std::vector<gpu::TimelineInterval> passMedians;
    LiveProfileResources resources;
    std::vector<LiveProfileCandidate> candidates;
    std::vector<std::string> limits;   // what this run cannot see, said plainly
    std::vector<std::string> notes;
    std::vector<LiveProfileEntity> entities;
    ContributionReport contribution;         // ADR-1108
    std::vector<AbComparison> comparisons;   // ADR-1110 (--compare)
    AbVisual comparisonFloor;                // ORIGINAL vs ORIGINAL, for the comparisons
    OptimizationReport optimization;         // ADR-1111 (--optimize)
    std::string status;                // "UNDER BUDGET" | "OVER BUDGET" | "AT RISK"
    bool headroom = false;
    std::string verificationMode;      // how --verify-candidates measured ("headless interleaved A/B, ...")
};

// The one builder both modes call. `frames` are the measured frames only (warm-up already dropped).
void buildLiveProfile(LiveProfileRecord& record, const std::vector<LiveProfileFrame>& frames, bool live);

// The CPU categories from the measured frames (1d). Live mode adds the UI and the waits.
[[nodiscard]] std::vector<CpuCategory> cpuCategories(const std::vector<LiveProfileFrame>& frames, bool live);

[[nodiscard]] nlohmann::json liveProfileJson(const LiveProfileRecord& record);
[[nodiscard]] std::string liveProfileJsonText(const LiveProfileRecord& record);
[[nodiscard]] std::string liveProfileText(const LiveProfileRecord& record);

// The host description (1g): model, CPU, OS. Read with sysctl on macOS; empty fields elsewhere.
struct HostDescription {
    std::string machine, cpu, os;
};
[[nodiscard]] HostDescription describeHost();

} // namespace avgen::app
