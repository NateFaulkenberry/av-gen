#include "sonic/character.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::sonic {

namespace {

constexpr std::array<const char*, kFeatureCount> kFeatureNames{
    "loudness", "centroid",      "bandwidth",     "rolloff",   "flatness", "lowRatio",  "highRatio",
    "flux",     "pitch",         "pitchConfidence", "harmonicity", "inharmonicity", "tonalness", "dissonance",
    "peaks",    "fundamentals",  "width",         "transient", "levelSlope", "centroidDeviation", "fluxAverage"};

constexpr std::array<const char*, kDimensionCount> kDimensionNames{
    "energy",     "brightness", "warmth",    "roughness", "sharpness", "smoothness", "harmonicity", "inharmonicity",
    "density",    "complexity", "stability", "movement",  "organic",   "mechanical", "spatial"};

Term f(Feature feature, float lo, float hi, float weight = 1.0f, bool log = false, bool invert = false) {
    Term t;
    t.feature = feature;
    t.lo = lo;
    t.hi = hi;
    t.weight = weight;
    t.log = log;
    t.invert = invert;
    return t;
}

Term d(Dimension dimension, float weight = 1.0f, bool invert = false) {
    Term t;
    t.dimension = dimension;
    t.lo = 0.0f;
    t.hi = 1.0f;
    t.weight = weight;
    t.invert = invert;
    return t;
}

} // namespace

const char* featureName(Feature feature) {
    const auto i = static_cast<std::size_t>(feature);
    return i < kFeatureCount ? kFeatureNames[i] : "loudness";
}

std::optional<Feature> featureFromName(std::string_view name) {
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        if (name == kFeatureNames[i]) {
            return static_cast<Feature>(i);
        }
    }
    return std::nullopt;
}

const char* dimensionName(Dimension dimension) {
    const auto i = static_cast<std::size_t>(dimension);
    return i < kDimensionCount ? kDimensionNames[i] : "energy";
}

std::optional<Dimension> dimensionFromName(std::string_view name) {
    for (std::size_t i = 0; i < kDimensionCount; ++i) {
        if (name == kDimensionNames[i]) {
            return static_cast<Dimension>(i);
        }
    }
    return std::nullopt;
}

CharacterSpec CharacterSpec::defaults() {
    // First cut, tuned against tools/make_sonic_material.py's four sounds (PROGRESS.md records the readings).
    // Each list says what the dimension means in measurements; the art agent may replace any of them.
    CharacterSpec s;
    auto& dims = s.dimensions;
    auto at = [&](Dimension dim) -> DimensionSpec& { return dims[static_cast<std::size_t>(dim)]; };

    // How much sound there is: level, plus how hard and how changeful it is.
    at(Dimension::Energy) = {{f(Feature::Loudness, -48.0f, -10.0f, 2.0f), f(Feature::Transient, 0.0f, 0.8f, 0.5f),
                              f(Feature::Flux, 0.04f, 0.3f, 0.5f)},
                             0.03f, 0.3f, 3.0f, false};
    // Where the spectrum's weight sits, and how far up it reaches.
    at(Dimension::Brightness) = {{f(Feature::Centroid, 250.0f, 4000.0f, 1.0f, true),
                                  f(Feature::Rolloff, 600.0f, 10000.0f, 1.0f, true),
                                  f(Feature::HighRatio, -36.0f, -6.0f, 1.0f)},
                                 0.1f, 0.4f, 4.0f, true};
    // Low and low-mid weight, a low centroid, and a harmonic (not noisy) spectrum.
    at(Dimension::Warmth) = {{f(Feature::LowRatio, -24.0f, -3.0f, 1.0f),
                              f(Feature::Centroid, 250.0f, 3000.0f, 1.0f, true, true),
                              f(Feature::Harmonicity, 0.2f, 0.8f, 0.5f), f(Feature::Flatness, 0.001f, 0.3f, 0.5f, true, true)},
                             0.15f, 0.5f, 5.0f, true};
    // Beating, intermodulation and noise: sensory dissonance, flatness, partials that fit no series.
    at(Dimension::Roughness) = {{f(Feature::Dissonance, 0.01f, 0.12f, 1.5f), f(Feature::Flatness, 0.001f, 0.3f, 1.0f, true),
                                 f(Feature::Inharmonicity, 0.15f, 0.7f, 1.0f)},
                                0.08f, 0.4f, 4.0f, true};
    // Attack: fast level rises, spectral change, and upper-spectrum edge.
    at(Dimension::Sharpness) = {{f(Feature::Transient, 0.0f, 0.7f, 1.5f), f(Feature::Flux, 0.04f, 0.3f, 1.0f),
                                 f(Feature::HighRatio, -30.0f, -6.0f, 0.5f)},
                                0.02f, 0.35f, 3.0f, true};
    // The absence of all of the above.
    at(Dimension::Smoothness) = {{f(Feature::Transient, 0.0f, 0.6f, 1.0f, false, true),
                                  f(Feature::FluxAverage, 0.03f, 0.2f, 1.0f, false, true),
                                  f(Feature::Dissonance, 0.01f, 0.12f, 0.5f, false, true),
                                  f(Feature::Flatness, 0.001f, 0.3f, 0.5f, true, true)},
                                 0.15f, 0.25f, 4.0f, true};
    // Energy in harmonic series, confidence of a pitch, and a spectrum made of partials.
    at(Dimension::Harmonicity) = {{f(Feature::Harmonicity, 0.15f, 0.8f, 2.0f), f(Feature::PitchConfidence, 0.1f, 0.7f, 0.5f),
                                   f(Feature::Tonalness, 0.3f, 0.9f, 0.5f)},
                                  0.1f, 0.4f, 5.0f, true};
    // Partials that fit no series, counted where there are partials at all.
    at(Dimension::Inharmonicity) = {{f(Feature::Inharmonicity, 0.1f, 0.7f, 2.0f), f(Feature::Tonalness, 0.2f, 0.8f, 0.5f),
                                     f(Feature::Harmonicity, 0.15f, 0.8f, 0.5f, false, true)},
                                    0.1f, 0.4f, 5.0f, true};
    // How full the spectrum is: partials, spread, simultaneous fundamentals.
    at(Dimension::Density) = {{f(Feature::Peaks, 4.0f, 40.0f, 1.0f, true), f(Feature::Bandwidth, 300.0f, 3500.0f, 1.0f, true),
                               f(Feature::Fundamentals, 1.0f, 4.0f, 0.5f)},
                              0.15f, 0.5f, 5.0f, true};
    // Many unrelated components, changing.
    at(Dimension::Complexity) = {{f(Feature::Peaks, 4.0f, 40.0f, 1.0f, true), f(Feature::Inharmonicity, 0.1f, 0.7f, 1.0f),
                                  f(Feature::CentroidDeviation, 0.05f, 0.6f, 0.5f), f(Feature::FluxAverage, 0.03f, 0.2f, 0.5f)},
                                 0.15f, 0.5f, 5.0f, true};
    // A spectrum that stays where it is.
    at(Dimension::Stability) = {{f(Feature::CentroidDeviation, 0.03f, 0.5f, 1.0f, false, true),
                                 f(Feature::FluxAverage, 0.03f, 0.2f, 1.0f, false, true)},
                                0.2f, 0.5f, 5.0f, true};
    // A spectrum that moves.
    at(Dimension::Movement) = {{f(Feature::FluxAverage, 0.03f, 0.2f, 1.0f), f(Feature::CentroidDeviation, 0.03f, 0.5f, 1.0f)},
                               0.1f, 0.4f, 4.0f, true};
    // Composites of the above: soft, warm and harmonic with some life in it...
    at(Dimension::Organic) = {{d(Dimension::Smoothness), d(Dimension::Warmth), d(Dimension::Harmonicity, 0.75f),
                               d(Dimension::Movement, 0.5f)},
                              0.2f, 0.6f, 5.0f, true};
    // ...against hard, precise and inharmonic.
    at(Dimension::Mechanical) = {{d(Dimension::Sharpness), d(Dimension::Inharmonicity), d(Dimension::Stability, 0.5f),
                                  d(Dimension::Smoothness, 0.5f, true)},
                                 0.2f, 0.6f, 5.0f, true};
    // Width of the image.
    at(Dimension::Spatial) = {{f(Feature::Width, 0.03f, 0.5f, 1.0f)}, 0.2f, 0.6f, 5.0f, true};
    return s;
}

float mapTerm(const Term& term, float value) {
    float x = 0.0f;
    if (term.log) {
        if (value > 0.0f && term.lo > 0.0f && term.hi > 0.0f && term.hi != term.lo) {
            x = (std::log(value) - std::log(term.lo)) / (std::log(term.hi) - std::log(term.lo));
        }
    } else if (term.hi != term.lo) {
        x = (value - term.lo) / (term.hi - term.lo);
    }
    x = std::clamp(x, 0.0f, 1.0f);
    return term.invert ? 1.0f - x : x;
}

std::array<float, kDimensionCount> evaluateCharacter(const CharacterSpec& spec,
                                                     const std::array<float, kFeatureCount>& features) {
    std::array<float, kDimensionCount> out{};
    for (std::size_t i = 0; i < kDimensionCount; ++i) {
        double sum = 0.0;
        double weight = 0.0;
        for (const Term& t : spec.dimensions[i].terms) {
            float value = 0.0f;
            if (t.feature) {
                value = features[static_cast<std::size_t>(*t.feature)];
            } else if (t.dimension && static_cast<std::size_t>(*t.dimension) < i) {
                value = out[static_cast<std::size_t>(*t.dimension)];
            } else {
                continue; // a dimension not yet computed: ignored rather than read as zero
            }
            const double w = std::max(0.0f, t.weight);
            sum += w * static_cast<double>(mapTerm(t, value));
            weight += w;
        }
        out[i] = weight > 0.0 ? static_cast<float>(sum / weight) : 0.0f;
    }
    return out;
}

Result<void> CharacterSpec::applyJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'sonic.character' must be an object");
    }
    gateDb = j.value("gateDb", gateDb);
    transientThreshold = j.value("transientThreshold", transientThreshold);
    transientRefractory = j.value("transientRefractory", transientRefractory);
    const auto dims = j.find("dimensions");
    if (dims == j.end()) {
        return {};
    }
    if (!dims->is_object()) {
        return fail("'sonic.character.dimensions' must be an object");
    }
    for (const auto& [name, body] : dims->items()) {
        const auto dim = dimensionFromName(name);
        if (!dim) {
            return fail("sonic.character: unknown dimension '{}'", name);
        }
        if (!body.is_object()) {
            return fail("sonic.character.dimensions.{} must be an object", name);
        }
        DimensionSpec& spec = dimensions[static_cast<std::size_t>(*dim)];
        spec.attack = body.value("attack", spec.attack);
        spec.release = body.value("release", spec.release);
        spec.slow = body.value("slow", spec.slow);
        spec.gated = body.value("gated", spec.gated);
        if (const auto terms = body.find("terms"); terms != body.end()) {
            if (!terms->is_array()) {
                return fail("sonic.character.dimensions.{}.terms must be an array", name);
            }
            std::vector<Term> parsed;
            for (const auto& t : *terms) {
                Term term;
                if (t.contains("feature")) {
                    term.feature = featureFromName(t.at("feature").get<std::string>());
                    if (!term.feature) {
                        return fail("sonic.character.{}: unknown feature '{}'", name, t.at("feature").get<std::string>());
                    }
                } else if (t.contains("dimension")) {
                    term.dimension = dimensionFromName(t.at("dimension").get<std::string>());
                    if (!term.dimension || *term.dimension >= *dim) {
                        return fail("sonic.character.{}: a term's dimension must be one listed before it", name);
                    }
                } else {
                    return fail("sonic.character.{}: a term needs a 'feature' or a 'dimension'", name);
                }
                term.lo = t.value("lo", term.dimension ? 0.0f : term.lo);
                term.hi = t.value("hi", 1.0f);
                term.log = t.value("log", false);
                term.invert = t.value("invert", false);
                term.weight = t.value("weight", 1.0f);
                parsed.push_back(term);
            }
            spec.terms = std::move(parsed);
        }
    }
    return {};
}

nlohmann::json CharacterSpec::toJson() const {
    nlohmann::json dims = nlohmann::json::object();
    for (std::size_t i = 0; i < kDimensionCount; ++i) {
        const DimensionSpec& spec = dimensions[i];
        nlohmann::json terms = nlohmann::json::array();
        for (const Term& t : spec.terms) {
            nlohmann::json term = nlohmann::json::object();
            if (t.feature) {
                term["feature"] = featureName(*t.feature);
            } else if (t.dimension) {
                term["dimension"] = dimensionName(*t.dimension);
            }
            term["lo"] = t.lo;
            term["hi"] = t.hi;
            if (t.log) term["log"] = true;
            if (t.invert) term["invert"] = true;
            term["weight"] = t.weight;
            terms.push_back(std::move(term));
        }
        dims[kDimensionNames[i]] = {{"attack", spec.attack}, {"release", spec.release}, {"slow", spec.slow},
                                    {"gated", spec.gated}, {"terms", std::move(terms)}};
    }
    return {{"gateDb", gateDb}, {"transientThreshold", transientThreshold},
            {"transientRefractory", transientRefractory}, {"dimensions", std::move(dims)}};
}

} // namespace avgen::sonic
