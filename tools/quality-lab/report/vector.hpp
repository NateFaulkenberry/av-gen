#pragma once

// The quality vector and its versioned JSON (docs/quality-lab/metrics.md §2,
// docs/quality-lab/architecture.md §8; mandate §8, §18, §19).
//
// Three rules from ADR-250 are enforced here by the types rather than by discipline:
//
// 1. **There is no composite score and no place to put one.** A `Report` holds a vector of named
//    metrics. Nothing sums them, nothing weights them, and no field exists that could.
// 2. **Measured fact and inferred cause are different arrays with different types.** `findings`
//    holds measurements; `hypotheses` carry evidence and a confidence and are labelled as
//    hypotheses. Nothing automatic promotes one to the other, because `Hypothesis` has no
//    conversion to `Finding`.
// 3. **`temporalAlternation` and `spatialLaplacian` are emitted together or not at all.** ADR-243 is
//    the reason and `pairingViolation()` is the enforcement: a report carrying one without the
//    other fails to serialise rather than being published with a caveat nobody reads.
//
// And one rule from the mandate: a metric that could not be computed reports `available: false`
// with a reason. It is never omitted and never defaulted to zero, because a zero in a quality
// column is a claim.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nlohmann {
} // namespace nlohmann

namespace avgen::quality {

// Pooled over a sequence. metrics.md §5: never a mean alone -- a three-frame pop in three hundred
// frames is invisible in a mean, and popping is an artifact class the Lab is hunting.
struct Pooled {
    double mean = 0.0;
    double min = 0.0;
    double max = 0.0;
    double p5 = 0.0;
    double p95 = 0.0;
    std::size_t worstFrameIndex = 0;
    std::size_t frames = 0;
};

// Pools `values`, taking `higherIsWorse` to decide which frame is the worst one -- because "the
// worst frame" is different for MS-SSIM and for a residual, and getting it backwards points every
// diagnostic at the best frame in the run.
[[nodiscard]] Pooled pool(const std::vector<double>& values, bool higherIsWorse);

struct Metric {
    std::string name;    // dotted, e.g. "spatial.msSsim"
    std::string unit;    // "ratio", "dB", "luma steps 0..255", "fraction"
    std::string source;  // WHICH FILE the number came from: "candidate PNG vs reference PNG".
                         // reference-rendering.md §5: a PSNR on EXRs and a PSNR on PNGs are
                         // different numbers about different images.
    std::string pooling; // "mean over frames", "single frame", ...
    std::vector<std::string> limitations;
    bool available = true;
    std::string unavailableReason;
    std::optional<double> value;
    std::optional<Pooled> pooled;

    static Metric unavailable(std::string name, std::string reason);
};

// A measurement. No cause, no adjective, no recommendation.
struct Finding {
    std::string metric;
    std::string statement; // what was measured, in numbers
    double value = 0.0;
    std::optional<std::size_t> frameIndex;
};

// An inference. Carries its evidence and its confidence and says in its own JSON that it is a
// hypothesis (mandate §40, §41).
struct Hypothesis {
    std::string statement;
    std::vector<std::string> evidence;
    double confidence = 0.0; // 0..1, and it is an author's estimate, which the JSON says
};

struct RunInfo {
    std::string id;
    std::string timestamp;
    std::string gitCommit;
    std::string scene;
    std::string targetProfile;
    std::string candidateDirectory;
    std::string referenceDirectory;
    std::size_t framesAnalysed = 0;
    std::size_t frameStride = 1;
    std::string candidateSequenceHash;
    std::string referenceSequenceHash;
};

struct RendererInfo {
    std::string configuration;
    std::string gpu;
    // ADR-170: a timing is evidence only when the device was also quiet, so the witness travels
    // with it and an absent witness downgrades the number to "a record of having taken it".
    std::string contentionWitness;
    std::optional<double> gpuFrameMsMin;
    std::optional<double> cpuPhaseMsMin;
};

struct Report {
    std::string schemaVersion = "1.0.0";
    RunInfo run;
    RendererInfo renderer;
    std::vector<Metric> metrics;
    std::vector<std::string> diagnostics; // relative paths, written beside the report
    std::vector<std::string> limitations;
    std::vector<Finding> findings;
    std::vector<Hypothesis> hypotheses;

    [[nodiscard]] const Metric* find(std::string_view name) const;
    void add(Metric metric);

    // ADR-250 §3's harness rule. Returns the empty string when the report is well-formed, and the
    // violation otherwise. `toJson` refuses rather than emitting.
    [[nodiscard]] std::string pairingViolation() const;

    // Serialises to the shape `tools/quality-lab/schemas/quality-report.v1.json` describes.
    [[nodiscard]] std::string toJson() const; // empty on a pairing violation
};

} // namespace avgen::quality
