#pragma once

// The Sonic Character model (ADR-1020, brief §10): "what does the sound feel like", as fifteen 0..1 dimensions
// derived from the raw timbre measurements.
//
// Every dimension is DATA: a weighted mean of terms, each term one measurement mapped linearly (or on a log
// scale) from a fixed range [lo, hi] to 0..1, optionally inverted. The defaults are starting points informed by
// RESEARCH.md, not truths; a project overrides any dimension's terms and time constants from its `sonic.character`
// block. A term may also read an earlier dimension's instantaneous value ("organic" is built from "warmth",
// "smoothness" and "harmonicity"), so the composite dimensions are data too.
//
// Ranges are absolute, never a running maximum: normalising each sound against itself would erase exactly the
// differences between sounds this exists to show (RESEARCH.md §3.2).
//
// Temporal tiers (brief §11/§23). Each dimension has a MEDIUM tier (an asymmetric one-pole: its own attack and
// release, a few hundred ms) and a SLOW tier (a one-pole of the medium over seconds). Timbre dimensions are
// `gated`: below the loudness gate they hold, so a sound keeps its identity between notes rather than decaying
// to "silence-coloured" values; energy is not gated and falls. The FAST tier is the transient event.

#include "core/error.hpp"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::sonic {

struct TimbreFeatures;

// The measurements a term can read: the TimbreFeatures, plus the runtime's temporal statistics.
enum class Feature : std::uint8_t {
    Loudness,          // dBFS
    Centroid,          // Hz
    Bandwidth,         // Hz
    Rolloff,           // Hz
    Flatness,          // 0..1
    LowRatio,          // dB
    HighRatio,         // dB
    Flux,              // relative flux
    Pitch,             // f0, Hz
    PitchConfidence,   // 0..1
    Harmonicity,       // 0..1
    Inharmonicity,     // 0..1
    Tonalness,         // 0..1
    Dissonance,        // amplitude-normalised Plomp-Levelt sum
    Peaks,             // prominent peak count
    Fundamentals,      // f0 count
    Width,             // stereo side/mid
    Transient,         // 0..1: the level's rise over its 150 ms follower, 0..18 dB
    LevelSlope,        // dB per second of the follower (negative: decaying)
    CentroidDeviation, // octaves: the centroid's standard deviation over about a second
    FluxAverage,       // the relative flux through a 0.5 s one-pole
    Count
};
[[nodiscard]] const char* featureName(Feature f);
[[nodiscard]] std::optional<Feature> featureFromName(std::string_view name);

enum class Dimension : std::uint8_t {
    Energy,
    Brightness,
    Warmth,
    Roughness,
    Sharpness,
    Smoothness,
    Harmonicity,
    Inharmonicity,
    Density,
    Complexity,
    Stability,
    Movement,
    Organic,
    Mechanical,
    Spatial,
    Count
};
constexpr std::size_t kFeatureCount = static_cast<std::size_t>(Feature::Count);
constexpr std::size_t kDimensionCount = static_cast<std::size_t>(Dimension::Count);
[[nodiscard]] const char* dimensionName(Dimension d); // "energy", "brightness", ...
[[nodiscard]] std::optional<Dimension> dimensionFromName(std::string_view name);

struct Term {
    // Exactly one of the two: a measurement, or an earlier dimension's instantaneous value (0..1).
    std::optional<Feature> feature;
    std::optional<Dimension> dimension;
    float lo = 0.0f;
    float hi = 1.0f;
    bool log = false;    // map on log(x) (frequencies, counts)
    bool invert = false; // 1 - mapped
    float weight = 1.0f;
};

struct DimensionSpec {
    std::vector<Term> terms;
    float attack = 0.08f;  // seconds: the medium tier rising
    float release = 0.35f; // seconds: the medium tier falling
    float slow = 4.0f;     // seconds: the slow tier's time constant
    bool gated = true;     // hold below the loudness gate
};

struct CharacterSpec {
    std::array<DimensionSpec, kDimensionCount> dimensions;
    float gateDb = -50.0f;            // timbre dimensions hold below this loudness
    float transientThreshold = 0.35f; // sonic.transient fires when the transient crosses this, rising
    float transientRefractory = 0.06f; // seconds between two transient events

    [[nodiscard]] static CharacterSpec defaults();
    // Overrides from a project's `sonic.character` block: {"gateDb", "transientThreshold", "dimensions": {name:
    // {"attack", "release", "slow", "gated", "terms": [{"feature"|"dimension", "lo", "hi", "log", "invert",
    // "weight"}]}}}. A dimension named there replaces only the fields given; "terms" replaces the whole list.
    [[nodiscard]] Result<void> applyJson(const nlohmann::json& j);
    [[nodiscard]] nlohmann::json toJson() const;
};

// One term on a value, 0..1.
[[nodiscard]] float mapTerm(const Term& term, float value);

// Every dimension's instantaneous (unsmoothed) value from one feature vector, in dimension order, so a term may
// read any dimension listed before its own.
[[nodiscard]] std::array<float, kDimensionCount> evaluateCharacter(const CharacterSpec& spec,
                                                                   const std::array<float, kFeatureCount>& features);

} // namespace avgen::sonic
