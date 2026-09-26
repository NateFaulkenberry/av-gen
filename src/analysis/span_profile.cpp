#include "analysis/span_profile.hpp"

#include "analysis/analysis_track.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::analysis {

const std::array<const char*, 5>& defaultBandNames() {
    static const std::array<const char*, 5> kNames{"bass", "lowMid", "mid", "highMid", "treble"};
    return kNames;
}

SpanProfile profileSpan(const AnalysisTrack& track, double startSeconds, double endSeconds) {
    SpanProfile out;
    const auto& frames = track.frames();
    if (frames.empty() || !(endSeconds > startSeconds)) {
        return out;
    }
    const auto first = std::lower_bound(frames.begin(), frames.end(), startSeconds,
                                        [](const AnalysisFrame& f, double t) { return f.timeSeconds < t; });
    const auto last = std::lower_bound(first, frames.end(), endSeconds,
                                       [](const AnalysisFrame& f, double t) { return f.timeSeconds < t; });
    if (first == last) {
        return out;
    }
    double energy = 0.0;
    double width = 0.0;
    double centroid = 0.0;
    std::size_t audible = 0;
    std::size_t stereo = 0;
    std::size_t hits = 0;
    std::size_t kicks = 0;
    std::size_t snares = 0;
    std::size_t hats = 0;
    std::size_t broadband = 0;
    bool percussive = false;
    double lastHit = -1e30;
    std::array<double, kMaxBands> power{};
    for (auto it = first; it != last; ++it) {
        const AnalysisFrame& f = *it;
        ++out.frames;
        out.bandCount = std::max<std::size_t>(out.bandCount, f.bandCount);
        for (std::size_t b = 0; b < f.bandCount && b < kMaxBands; ++b) {
            power[b] += static_cast<double>(f.bandsRaw[b]) * static_cast<double>(f.bandsRaw[b]);
        }
        if (f.rms > 1e-7f && f.centroidHz > 0.0f) {
            centroid += f.centroidHz;
            ++audible;
        }
        if (f.stereo) {
            width += f.width;
            ++stereo;
        }
        percussive = percussive || f.onsetRateIsPercussive;
        kicks += f.lowOnset ? 1u : 0u;
        snares += f.midOnset ? 1u : 0u;
        hats += f.highOnset ? 1u : 0u;
        broadband += f.onset ? 1u : 0u;
        if ((f.lowOnset || f.midOnset || f.highOnset) && f.timeSeconds - lastHit >= 0.03) {
            ++hits;
            lastHit = f.timeSeconds;
        }
    }
    const double n = static_cast<double>(out.frames);
    const double seconds = endSeconds - startSeconds;
    out.onsetRate = static_cast<float>(static_cast<double>(percussive ? hits : broadband) / seconds);
    // The span's energy is the composite of its frames' level-free terms with the span's OWN onset
    // rate as the density term -- not the mean of the frames' smoothed `energy`, whose one-pole lags a
    // short section (a 7 s riser climbs for most of its length) and whose leaky rate carries the
    // previous section's density into this one.
    for (auto it = first; it != last; ++it) {
        EnergyTerms terms;
        terms.highRatioDb = it->highRatioDb;
        terms.centroidHz = it->centroidHz;
        terms.relativeFlux = it->relativeFlux;
        terms.onsetRate = out.onsetRate;
        terms.hasOnsetRate = percussive;
        terms.width = it->width;
        terms.hasWidth = it->stereo;
        terms.silent = !(it->rms > 1e-7f);
        energy += energyComposite(terms);
    }
    out.energy = static_cast<float>(energy / n);
    out.kickRate = static_cast<float>(static_cast<double>(kicks) / seconds);
    out.snareRate = static_cast<float>(static_cast<double>(snares) / seconds);
    out.hatRate = static_cast<float>(static_cast<double>(hats) / seconds);
    out.brightnessHz = audible > 0 ? static_cast<float>(centroid / static_cast<double>(audible)) : 0.0f;
    for (std::size_t b = 0; b < out.bandCount; ++b) {
        const double mean = power[b] / n / kHannEnergyGain; // sine-amplitude units, as the levels are
        out.bandDb[b] = mean > 0.0 ? static_cast<float>(10.0 * std::log10(mean)) : -120.0f;
        out.bandLevels[b] = bandLevelFromPower(mean);
    }
    out.stereo = stereo > 0;
    out.width = stereo > 0 ? static_cast<float>(width / static_cast<double>(stereo)) : 0.0f;
    return out;
}

nlohmann::json spanProfileToJson(const SpanProfile& p) {
    nlohmann::json bands = nlohmann::json::object();
    nlohmann::json levels = nlohmann::json::object();
    const auto& names = defaultBandNames();
    for (std::size_t b = 0; b < p.bandCount && b < names.size(); ++b) {
        bands[names[b]] = p.bandDb[b];
        levels[names[b]] = p.bandLevels[b];
    }
    nlohmann::json j = {{"energy", p.energy},       {"onsetRate", p.onsetRate}, {"kickRate", p.kickRate},
                        {"snareRate", p.snareRate}, {"hatRate", p.hatRate},     {"brightnessHz", p.brightnessHz},
                        {"bandsDb", std::move(bands)}, {"bandLevels", std::move(levels)}, {"frames", p.frames}};
    if (p.stereo) {
        j["width"] = p.width;
    }
    return j;
}

SpanProfile spanProfileFromJson(const nlohmann::json& j) {
    SpanProfile p;
    if (!j.is_object()) {
        return p;
    }
    p.energy = j.value("energy", 0.0f);
    p.onsetRate = j.value("onsetRate", 0.0f);
    p.kickRate = j.value("kickRate", 0.0f);
    p.snareRate = j.value("snareRate", 0.0f);
    p.hatRate = j.value("hatRate", 0.0f);
    p.brightnessHz = j.value("brightnessHz", 0.0f);
    p.frames = j.value("frames", std::size_t{0});
    const auto& names = defaultBandNames();
    if (j.contains("bandsDb") && j["bandsDb"].is_object()) {
        for (std::size_t b = 0; b < names.size(); ++b) {
            if (j["bandsDb"].contains(names[b])) {
                p.bandDb[b] = j["bandsDb"][names[b]].get<float>();
                p.bandCount = std::max(p.bandCount, b + 1);
            }
        }
    }
    if (j.contains("bandLevels") && j["bandLevels"].is_object()) {
        for (std::size_t b = 0; b < names.size(); ++b) {
            if (j["bandLevels"].contains(names[b])) {
                p.bandLevels[b] = j["bandLevels"][names[b]].get<float>();
            }
        }
    }
    if (j.contains("width")) {
        p.width = j["width"].get<float>();
        p.stereo = true;
    }
    return p;
}

} // namespace avgen::analysis
