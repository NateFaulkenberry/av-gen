// The resonance field (ADR-1400). The header states the model; this file is the CPU reference that
// shaders/fields.wgsl transliterates (resonanceModeShape, resonanceListen, resonanceSums and the
// channels, operation for operation).

#include "spatial/resonance.hpp"

#include "spatial/audio_history.hpp"
#include "spatial/detail.hpp"
#include "spatial/field.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::spatial {

using nlohmann::json;

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717958f;
// The spectrogram's axis (audio_history.hpp): bin b of kAudioBins sits at kAudioMinHz (max/min)^(b / (bins - 1)).
constexpr float kOctavesPerBin = 8.96578428f / static_cast<float>(kAudioBins - 1); // log2(16000 / 32) / 63
constexpr int kSelectivityTaps = 16; // the resonance curve is truncated at +-16 bins (2.3 octaves)
constexpr float kStrikeRise = 0.03f; // seconds: an onset's strike reaches its peak this fast
constexpr float kStrikeBurst = 0.25f; // seconds of full drive a strike is worth to the memory
// Where each onset source strikes (Hz) and how widely (octaves): the analyser's low, mid and high bands.
constexpr float kStrikeCentre[3] = {80.0f, 700.0f, 5000.0f};
constexpr float kStrikeOctaves = 1.3f;

float saturate(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

float smoothstepf(float e0, float e1, float x) {
    const float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

// A deterministic hash in [0, 1) of (seed, mode, salt).
float hash01(std::uint32_t seed, int mode, std::uint32_t salt) {
    std::uint32_t h = seed * 747796405u + static_cast<std::uint32_t>(mode) * 2891336453u + salt * 277803737u + 1u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return static_cast<float>(h >> 8) / 16777216.0f;
}

// One row through the resonance curve and the spectral contrast (the cache's unit of work).
void filterRow(const AudioHistory& audio, std::int64_t row, const std::array<float, 2 * kSelectivityTaps + 1>& kernel,
               float contrast, std::array<float, kAudioBins>& out) {
    std::array<float, kAudioBins> raw{};
    for (int b = 0; b < kAudioBins; ++b) {
        raw[static_cast<std::size_t>(b)] = audio.value(row, b);
    }
    float mean = 0.0f;
    for (int b = 0; b < kAudioBins; ++b) {
        float sum = 0.0f;
        float norm = 0.0f;
        for (int d = -kSelectivityTaps; d <= kSelectivityTaps; ++d) {
            const int bb = b + d;
            if (bb < 0 || bb >= kAudioBins) {
                continue;
            }
            const float g = kernel[static_cast<std::size_t>(d + kSelectivityTaps)];
            sum += g * raw[static_cast<std::size_t>(bb)];
            norm += g;
        }
        out[static_cast<std::size_t>(b)] = norm > 0.0f ? sum / norm : 0.0f;
        mean += out[static_cast<std::size_t>(b)];
    }
    mean /= static_cast<float>(kAudioBins);
    const float c = std::clamp(contrast, 0.0f, 0.95f);
    for (float& v : out) {
        v = std::max(v - c * mean, 0.0f) / (1.0f - c);
    }
}

std::array<float, 2 * kSelectivityTaps + 1> selectivityKernel(float q) {
    std::array<float, 2 * kSelectivityTaps + 1> k{};
    for (int d = -kSelectivityTaps; d <= kSelectivityTaps; ++d) {
        const float ratio = std::exp2(static_cast<float>(d) * kOctavesPerBin);
        const float x = q * (ratio - 1.0f / ratio);
        k[static_cast<std::size_t>(d + kSelectivityTaps)] = 1.0f / (1.0f + x * x);
    }
    return k;
}

const std::vector<ResonanceSource>& defaultSources() {
    static const std::vector<ResonanceSource> sources{ResonanceSource{}};
    return sources;
}

} // namespace

// ================================================================================================
// Names and the spec
// ================================================================================================

const char* resonanceChannelName(ResonanceChannel channel) {
    switch (channel) {
    case ResonanceChannel::Displacement:
        return "displacement";
    case ResonanceChannel::Amplitude:
        return "amplitude";
    case ResonanceChannel::Sand:
        return "sand";
    case ResonanceChannel::Force:
        return "force";
    case ResonanceChannel::Gradient:
        return "gradient";
    case ResonanceChannel::Relief:
        return "relief";
    case ResonanceChannel::Shading:
        return "shading";
    }
    return "displacement";
}

std::optional<ResonanceChannel> resonanceChannelFromName(std::string_view name) {
    for (const ResonanceChannel c : {ResonanceChannel::Displacement, ResonanceChannel::Amplitude, ResonanceChannel::Sand,
                                     ResonanceChannel::Force, ResonanceChannel::Gradient, ResonanceChannel::Relief,
                                     ResonanceChannel::Shading}) {
        if (name == resonanceChannelName(c)) {
            return c;
        }
    }
    return std::nullopt;
}

const char* resonanceSourceKindName(ResonanceSourceKind kind) {
    return kind == ResonanceSourceKind::Lattice ? "lattice" : "point";
}

std::optional<ResonanceSourceKind> resonanceSourceKindFromName(std::string_view name) {
    if (name == "point") {
        return ResonanceSourceKind::Point;
    }
    if (name == "lattice") {
        return ResonanceSourceKind::Lattice;
    }
    return std::nullopt;
}

Result<void> ResonanceSpec::validate(std::string_view field) const {
    const auto finite = [](float v) { return std::isfinite(v); };
    if (!(modes >= 1.0f && modes <= static_cast<float>(kMaxResonanceModes))) {
        return fail("field '{}': resonance.modes must be in [1, {}]", field, kMaxResonanceModes);
    }
    if (!(fLow > 0.0f && fHigh > fLow && finite(fHigh))) {
        return fail("field '{}': resonance needs 0 < fLow < fHigh", field);
    }
    if (!(lambdaMin > 0.0f && lambdaMax > 0.0f && finite(lambdaMax))) {
        return fail("field '{}': resonance wavelengths must be > 0", field);
    }
    if (!(scale > 0.0f && finite(scale))) {
        return fail("field '{}': resonance.scale must be > 0", field);
    }
    if (!(selectivity > 0.0f && finite(selectivity))) {
        return fail("field '{}': resonance.selectivity must be > 0", field);
    }
    if (!(contrast >= 0.0f && contrast <= 0.95f)) {
        return fail("field '{}': resonance.contrast must be in [0, 0.95]", field);
    }
    if (!(persistence > 0.0f && persistence <= 30.0f)) {
        return fail("field '{}': resonance.persistence must be in (0, 30] seconds", field);
    }
    if (!(competition >= 0.0f && competition <= 8.0f)) {
        return fail("field '{}': resonance.competition must be in [0, 8]", field);
    }
    if (!(memory >= 0.0f && memory <= 30.0f)) {
        return fail("field '{}': resonance.memory must be in [0, 30] seconds", field);
    }
    if (!(buildup >= 0.0f && buildup <= 1.0f)) {
        return fail("field '{}': resonance.buildup must be in [0, 1]", field);
    }
    if (!(finite(sustain) && finite(transient) && idle >= 0.0f && finite(idle) && breath >= 0.0f && finite(breath))) {
        return fail("field '{}': resonance gains must be finite and idle, breath >= 0", field);
    }
    if (!(limit > 0.0f && finite(limit))) {
        return fail("field '{}': resonance.limit must be > 0", field);
    }
    if (sources.size() > static_cast<std::size_t>(kMaxResonanceSources)) {
        return fail("field '{}': at most {} resonance sources", field, kMaxResonanceSources);
    }
    for (const ResonanceSource& s : sources) {
        if (s.order < 0 || s.order > 8 || (s.kind == ResonanceSourceKind::Lattice && s.order < 1)) {
            return fail("field '{}': a resonance source's order must be 0..8 (a lattice at least 1)", field);
        }
        if (!(s.bandLow >= 0.0f && s.bandHigh <= 1.0f && s.bandLow <= s.bandHigh)) {
            return fail("field '{}': a resonance source's band must be 0 <= bandLow <= bandHigh <= 1", field);
        }
        if (!(s.reach >= 0.0f && finite(s.reach) && finite(s.gain) && s.scale > 0.0f && finite(s.scale) && s.depth >= 0.0f &&
              finite(s.depth))) {
            return fail("field '{}': a resonance source's reach must be >= 0 and its gain finite", field);
        }
    }
    if (!(listenLow >= 0.0f && listenHigh <= 1.0f && listenLow <= listenHigh && listenWidth >= 0.0f)) {
        return fail("field '{}': resonance listening window must be 0 <= listenLow <= listenHigh <= 1, width >= 0",
                    field);
    }
    if (!(sharpness > 0.0f && finite(sharpness) && reliefLineSharpness > 0.0f && finite(reliefLine) &&
          finite(reliefVibration) && derivativeStep >= 0.0f && finite(derivativeStep))) {
        return fail("field '{}': resonance.sharpness must be > 0 and derivativeStep >= 0", field);
    }
    return {};
}

std::uint64_t ResonanceSpec::structuralHash() const {
    detail::Fnv h;
    for (const float v : {modes, fLow, fHigh, lambdaMax, lambdaMin, scale, selectivity, contrast, persistence, buildup,
                          memory, competition, sustain, transient, idle, breath, limit, listenLow, listenHigh, listenWidth, sharpness,
                          reliefLine, reliefLineSharpness, reliefVibration, derivativeStep}) {
        h.f32(v);
    }
    h.u8(static_cast<std::uint8_t>(channel));
    h.boolean(listenAlongAxis);
    h.u64(sources.size());
    for (const ResonanceSource& s : sources) {
        h.u8(static_cast<std::uint8_t>(s.kind));
        h.v3(s.position);
        h.i32(s.order);
        h.f32(s.bandLow);
        h.f32(s.bandHigh);
        h.f32(s.reach);
        h.f32(s.gain);
        h.f32(s.scale);
        h.f32(s.depth);
    }
    return h.value();
}

json ResonanceSpec::toJson() const {
    json j = json::object();
    j["modes"] = modes;
    j["fLow"] = fLow;
    j["fHigh"] = fHigh;
    j["lambdaMax"] = lambdaMax;
    j["lambdaMin"] = lambdaMin;
    j["scale"] = scale;
    j["selectivity"] = selectivity;
    j["contrast"] = contrast;
    j["persistence"] = persistence;
    j["buildup"] = buildup;
    j["memory"] = memory;
    j["competition"] = competition;
    j["sustain"] = sustain;
    j["transient"] = transient;
    j["idle"] = idle;
    j["breath"] = breath;
    j["limit"] = limit;
    json src = json::array();
    for (const ResonanceSource& s : sources) {
        src.push_back({{"kind", resonanceSourceKindName(s.kind)},
                       {"position", detail::vecToJson(s.position)},
                       {"order", s.order},
                       {"bandLow", s.bandLow},
                       {"bandHigh", s.bandHigh},
                       {"reach", s.reach},
                       {"gain", s.gain},
                       {"scale", s.scale},
                       {"depth", s.depth}});
    }
    j["sources"] = std::move(src);
    j["channel"] = resonanceChannelName(channel);
    j["listenLow"] = listenLow;
    j["listenHigh"] = listenHigh;
    j["listenWidth"] = listenWidth;
    j["listenAlongAxis"] = listenAlongAxis;
    j["sharpness"] = sharpness;
    if (derivativeStep > 0.0f) {
        j["derivativeStep"] = derivativeStep;
    }
    if (channel == ResonanceChannel::Relief) {
        j["reliefLine"] = reliefLine;
        j["reliefLineSharpness"] = reliefLineSharpness;
        j["reliefVibration"] = reliefVibration;
    }
    return j;
}

Result<ResonanceSpec> ResonanceSpec::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("'resonance' must be an object");
    }
    ResonanceSpec r;
    AVGEN_SPATIAL_READ(r.modes, "modes", detail::readFloat);
    AVGEN_SPATIAL_READ(r.fLow, "fLow", detail::readFloat);
    AVGEN_SPATIAL_READ(r.fHigh, "fHigh", detail::readFloat);
    AVGEN_SPATIAL_READ(r.lambdaMax, "lambdaMax", detail::readFloat);
    AVGEN_SPATIAL_READ(r.lambdaMin, "lambdaMin", detail::readFloat);
    AVGEN_SPATIAL_READ(r.scale, "scale", detail::readFloat);
    AVGEN_SPATIAL_READ(r.selectivity, "selectivity", detail::readFloat);
    AVGEN_SPATIAL_READ(r.contrast, "contrast", detail::readFloat);
    AVGEN_SPATIAL_READ(r.persistence, "persistence", detail::readFloat);
    AVGEN_SPATIAL_READ(r.buildup, "buildup", detail::readFloat);
    AVGEN_SPATIAL_READ(r.memory, "memory", detail::readFloat);
    AVGEN_SPATIAL_READ(r.competition, "competition", detail::readFloat);
    AVGEN_SPATIAL_READ(r.sustain, "sustain", detail::readFloat);
    AVGEN_SPATIAL_READ(r.transient, "transient", detail::readFloat);
    AVGEN_SPATIAL_READ(r.idle, "idle", detail::readFloat);
    AVGEN_SPATIAL_READ(r.breath, "breath", detail::readFloat);
    AVGEN_SPATIAL_READ(r.limit, "limit", detail::readFloat);
    AVGEN_SPATIAL_READ_ENUM(r.channel, "channel", &resonanceChannelFromName, "resonance channel");
    AVGEN_SPATIAL_READ(r.listenLow, "listenLow", detail::readFloat);
    AVGEN_SPATIAL_READ(r.listenHigh, "listenHigh", detail::readFloat);
    AVGEN_SPATIAL_READ(r.listenWidth, "listenWidth", detail::readFloat);
    AVGEN_SPATIAL_READ(r.listenAlongAxis, "listenAlongAxis", detail::readBool);
    AVGEN_SPATIAL_READ(r.sharpness, "sharpness", detail::readFloat);
    AVGEN_SPATIAL_READ(r.derivativeStep, "derivativeStep", detail::readFloat);
    AVGEN_SPATIAL_READ(r.reliefLine, "reliefLine", detail::readFloat);
    AVGEN_SPATIAL_READ(r.reliefLineSharpness, "reliefLineSharpness", detail::readFloat);
    AVGEN_SPATIAL_READ(r.reliefVibration, "reliefVibration", detail::readFloat);
    if (j.contains("sources")) {
        const json& arr = j.at("sources");
        if (!arr.is_array()) {
            return fail("'resonance.sources' must be an array");
        }
        for (const json& sj : arr) {
            if (!sj.is_object()) {
                return fail("a resonance source must be an object");
            }
            ResonanceSource s;
            {
                const json& j = sj; // the macros read `j`
                AVGEN_SPATIAL_READ_ENUM(s.kind, "kind", &resonanceSourceKindFromName, "resonance source kind");
                AVGEN_SPATIAL_READ(s.position, "position", detail::readVec3);
                AVGEN_SPATIAL_READ(s.order, "order", detail::readInt);
                AVGEN_SPATIAL_READ(s.bandLow, "bandLow", detail::readFloat);
                AVGEN_SPATIAL_READ(s.bandHigh, "bandHigh", detail::readFloat);
                AVGEN_SPATIAL_READ(s.reach, "reach", detail::readFloat);
                AVGEN_SPATIAL_READ(s.gain, "gain", detail::readFloat);
                AVGEN_SPATIAL_READ(s.scale, "scale", detail::readFloat);
                AVGEN_SPATIAL_READ(s.depth, "depth", detail::readFloat);
            }
            r.sources.push_back(s);
        }
    }
    return r;
}

// ================================================================================================
// The cache
// ================================================================================================

const std::deque<std::array<float, 64>>& ResonanceCache::rows(const std::string& resonator,
                                                              const std::shared_ptr<const AudioHistory>& audio,
                                                              std::int64_t lo, std::int64_t hi, float selectivity,
                                                              float contrast) {
    static_assert(kAudioBins == 64, "the cache's rows are kAudioBins wide");
    Entry& e = entries_[resonator];
    const bool same = audio && !e.audio.expired() && e.audio.lock().get() == audio.get() &&
                      e.revision == audio->revision() && e.selectivity == selectivity && e.contrast == contrast;
    const std::int64_t heldHi = e.lo + static_cast<std::int64_t>(e.rows.size()) - 1;
    if (!same || e.rows.empty() || hi < e.lo || lo > heldHi) {
        e.rows.clear();
        e.audio = audio;
        e.revision = audio ? audio->revision() : 0;
        e.selectivity = selectivity;
        e.contrast = contrast;
        e.lo = lo;
    }
    const auto kernel = selectivityKernel(selectivity);
    std::array<float, kAudioBins> row{};
    if (e.rows.empty()) {
        e.lo = lo;
        for (std::int64_t r = lo; r <= hi; ++r) {
            if (audio) {
                filterRow(*audio, r, kernel, contrast, row);
            }
            e.rows.push_back(row);
            ++filtered_;
        }
        return e.rows;
    }
    // Older rows wanted: prepend them; newer rows: append; then trim to [lo, hi].
    while (e.lo > lo) {
        --e.lo;
        filterRow(*audio, e.lo, kernel, contrast, row);
        e.rows.push_front(row);
        ++filtered_;
    }
    for (std::int64_t r = e.lo + static_cast<std::int64_t>(e.rows.size()); r <= hi; ++r) {
        filterRow(*audio, r, kernel, contrast, row);
        e.rows.push_back(row);
        ++filtered_;
    }
    while (e.lo < lo && !e.rows.empty()) {
        e.rows.pop_front();
        ++e.lo;
    }
    while (static_cast<std::int64_t>(e.rows.size()) > hi - lo + 1) {
        e.rows.pop_back();
    }
    return e.rows;
}

// ================================================================================================
// The resonator
// ================================================================================================

ResonanceState evaluateResonance(const FieldSpec& field, const std::shared_ptr<const AudioHistory>& audio, double clock,
                                 ResonanceCache* cache) {
    const ResonanceSpec& r = field.resonance;
    ResonanceState st;
    st.clock = clock;
    const float n = std::clamp(r.modes, 1.0f, static_cast<float>(kMaxResonanceModes));
    const int count = std::clamp(static_cast<int>(std::ceil(n - 1e-4f)), 1, kMaxResonanceModes);
    const float fade = n - std::floor(n + 1e-4f); // the last mode's share while `modes` is between integers
    const float span = std::max(n - 1.0f, 1e-3f);
    const float scale = std::max(r.scale, 1e-3f);
    st.count = count;
    st.lambdaMin = r.lambdaMin / scale;
    const std::vector<ResonanceSource>& sources = r.sources.empty() ? defaultSources() : r.sources;
    const AudioResponse response = audio ? audio->response() : AudioResponse{};
    const float attackScale = std::clamp(response.attack, 0.1f, 10.0f);
    const float releaseScale = std::clamp(response.release, 0.1f, 10.0f);
    const auto t = static_cast<float>(clock);

    std::array<float, kMaxResonanceModes> tauD{};
    std::array<float, kMaxResonanceModes> tauA{};
    std::array<float, kMaxResonanceModes> slowD{}; // the memory's kernel: the ring-down plus `memory`
    std::array<float, kMaxResonanceModes> slowA{};
    float maxTau = 0.0f;
    const float memory = std::max(r.memory, 0.0f) * releaseScale;
    for (int i = 0; i < count; ++i) {
        const float u = count == 1 ? 0.0f : std::min(static_cast<float>(i) / span, 1.0f);
        const float f = r.fLow * std::pow(r.fHigh / r.fLow, u);
        const float lambda = r.lambdaMax * std::pow(r.lambdaMin / r.lambdaMax, u);
        const float k = kTwoPi / lambda * scale;
        // The source: one of those whose band holds u (else the first), chosen by the seed.
        int candidates[kMaxResonanceSources];
        int nc = 0;
        for (int s = 0; s < static_cast<int>(sources.size()); ++s) {
            const ResonanceSource& src = sources[static_cast<std::size_t>(s)];
            if (u >= src.bandLow - 1e-5f && u <= src.bandHigh + 1e-5f) {
                candidates[nc++] = s;
            }
        }
        const int si = nc == 0 ? 0 : candidates[std::min(static_cast<int>(hash01(field.seed, i, 1u) * static_cast<float>(nc)), nc - 1)];
        const ResonanceSource& src = sources[static_cast<std::size_t>(si)];
        float m = 0.0f;
        float geometry = 0.0f;
        if (src.kind == ResonanceSourceKind::Lattice) {
            m = static_cast<float>(std::clamp(src.order, 1, 8));
            geometry = 1.0f;
        } else {
            // Low modes draw simple figures (m 0..1), high modes may draw up to the source's order.
            const int cap = std::min(src.order, 1 + static_cast<int>(std::floor(u * static_cast<float>(src.order) * 1.5f)));
            m = std::floor(hash01(field.seed, i, 2u) * static_cast<float>(cap + 1));
            m = std::min(m, static_cast<float>(cap));
        }
        // A lattice's modes share their source's orientation, so lattices of different wavelengths nest (a crystal)
        // instead of crossing into a moire; a point source's modes each turn their petals their own way.
        const float theta = src.kind == ResonanceSourceKind::Lattice
                                ? hash01(field.seed, 1000 + si, 3u) * kTwoPi
                                : hash01(field.seed, i, 3u) * kTwoPi;
        float weight = src.gain;
        if (i == count - 1 && fade > 1e-4f) {
            weight *= fade;
        }
        ResonanceMode& md = st.modes[static_cast<std::size_t>(i)];
        md.shape = glm::vec4(k * std::max(src.scale, 1e-3f), m, theta,
                             geometry + 2.0f * std::round(std::clamp(src.depth, 0.0f, 10000.0f)));
        md.place = glm::vec4(src.position, src.reach);
        md.drive = glm::vec4(0.0f, 0.0f, u, 0.0f);
        st.weight[static_cast<std::size_t>(i)] = weight;
        st.source[static_cast<std::size_t>(i)] = si;
        st.frequency[static_cast<std::size_t>(i)] = f;
        tauD[static_cast<std::size_t>(i)] = std::max(r.persistence * std::pow(r.fLow / f, 0.35f), 0.05f) * releaseScale;
        tauA[static_cast<std::size_t>(i)] =
            std::min(std::max(r.buildup * tauD[static_cast<std::size_t>(i)] / releaseScale * attackScale, 0.015f),
                     0.98f * tauD[static_cast<std::size_t>(i)]);
        st.lambdaMin = std::min(st.lambdaMin, kTwoPi / md.shape.x); // the finest mode drawn (the vector channels' step)
        slowD[static_cast<std::size_t>(i)] = tauD[static_cast<std::size_t>(i)] + memory;
        slowA[static_cast<std::size_t>(i)] = std::min(std::max(0.5f * memory * attackScale, tauA[static_cast<std::size_t>(i)]),
                                                      0.98f * slowD[static_cast<std::size_t>(i)]);
        maxTau = std::max(maxTau, slowD[static_cast<std::size_t>(i)]);
    }

    std::array<float, kMaxResonanceModes> a{};
    std::array<float, kMaxResonanceModes> slow{};
    if (audio) {
        const std::int64_t newest = audio->newestRow(clock);
        const double rate = audio->rowRate();
        if (newest >= 0 && rate > 0.0) {
            const double window = std::min(5.0 * static_cast<double>(maxTau), static_cast<double>(kAudioMaxDelayRows) / rate);
            const auto span_rows = static_cast<std::int64_t>(std::ceil(window * rate));
            const std::int64_t lo = std::max<std::int64_t>(newest - span_rows + 1, 0);
            const auto dt = static_cast<float>(1.0 / rate);
            std::deque<std::array<float, 64>> local;
            const std::deque<std::array<float, 64>>* rows = nullptr;
            if (cache != nullptr) {
                rows = &cache->rows(field.name, audio, lo, newest, r.selectivity, r.contrast);
            } else {
                const auto kernel = selectivityKernel(r.selectivity);
                std::array<float, kAudioBins> row{};
                for (std::int64_t rr = lo; rr <= newest; ++rr) {
                    filterRow(*audio, rr, kernel, r.contrast, row);
                    local.push_back(row);
                }
                rows = &local;
            }
            const auto nrows = static_cast<std::int64_t>(rows->size());
            for (int i = 0; i < count; ++i) {
                const auto si = static_cast<std::size_t>(i);
                const float f = st.frequency[si];
                const float pos = std::clamp(std::log2(f / kAudioMinHz) / kOctavesPerBin, 0.0f, static_cast<float>(kAudioBins - 1));
                const int b0 = std::min(static_cast<int>(std::floor(pos)), kAudioBins - 1);
                const int b1 = std::min(b0 + 1, kAudioBins - 1);
                const float fb = pos - static_cast<float>(b0);
                const float td = tauD[si];
                const float ta = tauA[si];
                // K(age) = (e^{-age/td} - e^{-age/ta}) / (td - ta), stepped one row older at a time.
                const float age0 = 0.5f * dt;
                float ed = std::exp(-age0 / td);
                float ea = std::exp(-age0 / ta);
                const float md = std::exp(-dt / td);
                const float ma = std::exp(-dt / ta);
                const float inv = 1.0f / (td - ta);
                // the memory's kernel, the same way
                const float sd = slowD[si];
                const float sa = slowA[si];
                float sed = std::exp(-age0 / sd);
                float sea = std::exp(-age0 / sa);
                const float smd = std::exp(-dt / sd);
                const float sma = std::exp(-dt / sa);
                const float sinv = 1.0f / (sd - sa);
                float sum = 0.0f;
                float slowSum = 0.0f;
                for (std::int64_t kRow = 0; kRow < nrows; ++kRow) {
                    const auto& row = (*rows)[static_cast<std::size_t>(nrows - 1 - kRow)];
                    const float s = row[static_cast<std::size_t>(b0)] + (row[static_cast<std::size_t>(b1)] - row[static_cast<std::size_t>(b0)]) * fb;
                    sum += s * (ed - ea);
                    slowSum += s * (sed - sea);
                    ed *= md;
                    ea *= ma;
                    sed *= smd;
                    sea *= sma;
                }
                float level = r.sustain * sum * inv * dt;
                float remembered = r.sustain * slowSum * sinv * dt;
                // Strikes: every heard onset in the window, into the modes its band reaches.
                float strikes = 0.0f;
                float slowStrikes = 0.0f;
                for (int src = 0; src < 3; ++src) {
                    const float oct = std::log2(f / kStrikeCentre[src]) / kStrikeOctaves;
                    const float reachW = std::exp(-oct * oct);
                    if (reachW < 1e-3f) {
                        continue;
                    }
                    const auto& list = audio->onsets(static_cast<OnsetSource>(src));
                    auto it = std::upper_bound(list.begin(), list.end(), clock,
                                               [](double c, const AudioOnset& o) { return c < o.time; });
                    while (it != list.begin()) {
                        --it;
                        const auto age = static_cast<float>(clock - it->time);
                        if (age > static_cast<float>(window)) {
                            break;
                        }
                        const float heard = response.hit(it->strength);
                        if (heard > 0.0f) {
                            strikes += heard * reachW * std::exp(-age / td) * (1.0f - std::exp(-age / kStrikeRise));
                            // the memory hears a strike as a short burst of drive
                            slowStrikes += heard * reachW * kStrikeBurst * (std::exp(-age / sd) - std::exp(-age / sa)) * sinv;
                        }
                    }
                }
                level += r.transient * strikes;
                remembered += r.transient * slowStrikes;
                a[si] = level;
                slow[si] = remembered;
            }
        }
    }
    // Idle: the three lowest modes keep breathing in silence (a pure function of the clock).
    for (int i = 0; i < std::min(count, 3); ++i) {
        const float fi = static_cast<float>(i);
        const float shape = (0.65f + 0.35f * std::sin(kTwoPi * t / (19.0f + 7.0f * fi) + 1.3f * fi)) / (1.0f + fi);
        a[static_cast<std::size_t>(i)] += r.idle * shape;
        slow[static_cast<std::size_t>(i)] += r.idle * shape;
    }
    for (int i = 0; i < count; ++i) { // the envelopes as driven (diagnostics and tests)
        st.envelope[static_cast<std::size_t>(i)] = a[static_cast<std::size_t>(i)];
        st.memory[static_cast<std::size_t>(i)] = slow[static_cast<std::size_t>(i)];
    }
    // Mode competition: within each source, the stronger modes suppress the weaker (a_i (a_i / a_max)^c), as a
    // Faraday layer selects one pattern -- each body keeps a legible figure when many modes are driven at once, and
    // the sources (separate bodies) keep theirs side by side. 0 = every mode independent.
    if (r.competition > 0.0f) {
        for (auto* env : {&a, &slow}) {
            std::array<float, kMaxResonanceSources> top{};
            for (int i = 0; i < count; ++i) {
                const auto si = static_cast<std::size_t>(i);
                const auto src = static_cast<std::size_t>(st.source[si]);
                top[src] = std::max(top[src], (*env)[si] * st.weight[si]);
            }
            for (int i = 0; i < count; ++i) {
                const auto si = static_cast<std::size_t>(i);
                const float t = top[static_cast<std::size_t>(st.source[si])];
                if (t > 1e-9f) {
                    float& v = (*env)[si];
                    v *= std::pow(std::max(v * st.weight[si], 0.0f) / t, r.competition);
                }
            }
        }
    }
    // The soft limiter: dense music rings more modes, not louder ones (the memory has its own).
    float total = 0.0f;
    float slowTotal = 0.0f;
    for (int i = 0; i < count; ++i) {
        const auto si = static_cast<std::size_t>(i);
        const float w = a[si] * st.weight[si];
        const float ws = slow[si] * st.weight[si];
        total += w * w;
        slowTotal += ws * ws;
    }
    st.limiterGain = 1.0f / std::sqrt(1.0f + total / (r.limit * r.limit));
    const float slowGain = 1.0f / std::sqrt(1.0f + slowTotal / (r.limit * r.limit));
    for (int i = 0; i < count; ++i) {
        const auto si = static_cast<std::size_t>(i);
        const float e = a[si] * st.limiterGain * st.weight[si];
        const float omega = kTwoPi * r.breath * std::pow(st.frequency[si] / r.fLow, 0.3f);
        const float psi = hash01(field.seed, i, 4u) * kTwoPi;
        // The phase in double: an evening's clock times Omega is far past float's exact range.
        const double phase = std::fmod(static_cast<double>(omega) * clock + static_cast<double>(psi), 6.283185307179586);
        st.modes[si].drive.x = e * static_cast<float>(std::cos(phase));
        st.modes[si].drive.y = e;
        st.modes[si].drive.w = slow[si] * slowGain * st.weight[si];
    }
    return st;
}

void resolveResonance(FieldSet& fields, ResonanceCache& cache, double time) {
    const double clock = fields.audio ? fields.audio->now(time) : time;
    for (FieldSpec& f : fields.fields) {
        if (!f.isResonance()) {
            continue;
        }
        if (f.reference.empty() && f.enabled) {
            f.resonanceState = std::make_shared<const ResonanceState>(evaluateResonance(f, fields.audio, clock, &cache));
        } else {
            f.resonanceState.reset();
        }
    }
}

const ResonanceState* resonanceStateOf(const FieldSpec& field, const FieldSet* set) {
    if (!field.isResonance()) {
        return nullptr;
    }
    if (field.reference.empty()) {
        return field.resonanceState.get();
    }
    if (set == nullptr) {
        return nullptr;
    }
    const FieldSpec* owner = set->find(field.reference);
    if (owner == nullptr || !owner->isResonance() || !owner->reference.empty() || !owner->enabled) {
        return nullptr;
    }
    return owner->resonanceState.get();
}

// ================================================================================================
// Sampling
// ================================================================================================

float resonanceModeShape(const ResonanceMode& mode, const glm::vec3& q, float* env2) {
    const float k = mode.shape.x;
    const float m = mode.shape.y;
    const float theta = mode.shape.z;
    const glm::vec3 d = q - glm::vec3(mode.place);
    const float reach = mode.place.w;
    const float lattice = std::fmod(mode.shape.w, 2.0f);
    const float depth = std::floor(mode.shape.w * 0.5f);
    float w = reach > 0.0f ? 1.0f / (1.0f + glm::dot(d, d) / (reach * reach)) : 1.0f;
    if (depth > 0.0f) {
        w *= std::exp(-std::abs(d.y) / depth);
    }
    float phi = 0.0f;
    float e2 = 0.0f;
    if (lattice < 0.5f) {
        // point: R_m(kr) cos(m (alpha - theta)), the asymptotic Bessel form with a regular core
        const float kr = k * glm::length(d);
        if (m < 0.5f) {
            const float x = std::sqrt(kr * kr + 1.0f) - 1.0f; // smooth through r = 0
            const float amp = 1.0f / std::sqrt(1.0f + x / kTwoPi);
            phi = 1.41421356f * std::cos(x - 0.25f * kPi) * amp * w;
            e2 = amp * amp * w * w;
        } else {
            const float amp = 1.0f / std::sqrt(1.0f + kr / kTwoPi);
            // The core is regular about the vertical axis through the centre (where the azimuth is undefined):
            // the mode vanishes there like rho^m.
            const float rho2 = d.x * d.x + d.z * d.z;
            const float krho2 = k * k * rho2;
            const float core = std::pow(krho2 / (krho2 + m * m), 0.5f * m);
            const float alpha = rho2 > 1e-12f ? std::atan2(d.z, d.x) : 0.0f;
            phi = 1.41421356f * std::cos(kr - (2.0f * m + 1.0f) * 0.25f * kPi) * amp * core * std::cos(m * (alpha - theta)) * w;
            e2 = 0.5f * (amp * core * w) * (amp * core * w);
        }
    } else {
        // lattice: n^-1/2 sum_j cos(k x.d_j), n directions half a turn apart (mean square 1)
        const int n = std::clamp(static_cast<int>(m + 0.5f), 1, 8);
        float s = 0.0f;
        for (int j = 0; j < n; ++j) {
            const float a = theta + static_cast<float>(j) * kPi / static_cast<float>(n);
            s += std::cos(k * (d.x * std::cos(a) + d.z * std::sin(a)));
        }
        phi = s * std::sqrt(2.0f / static_cast<float>(n)) * w;
        e2 = w * w;
    }
    if (env2 != nullptr) {
        *env2 = e2;
    }
    return phi;
}

float resonanceListen(const FieldSpec& reader, float u, const glm::vec3& q) {
    const ResonanceSpec& r = reader.resonance;
    const float width = std::max(r.listenWidth, 1e-4f);
    if (r.listenAlongAxis) {
        const glm::vec3 axis = glm::length(reader.axis) > 1e-6f ? glm::normalize(reader.axis) : glm::vec3(0.0f, 1.0f, 0.0f);
        const float along = saturate(glm::dot(q - reader.point, axis) / std::max(reader.length, 1e-6f) + 0.5f);
        const float centre = r.listenLow + along * (r.listenHigh - r.listenLow);
        const float x = (u - centre) / width;
        return std::exp(-x * x);
    }
    if (r.listenLow <= 0.0f && r.listenHigh >= 1.0f) {
        return 1.0f;
    }
    return smoothstepf(r.listenLow - width, r.listenLow, u) * (1.0f - smoothstepf(r.listenHigh, r.listenHigh + width, u));
}

ResonanceSums resonanceSums(const ResonanceState& state, const FieldSpec& reader, const glm::vec3& q, bool memory) {
    ResonanceSums s;
    for (int i = 0; i < state.count; ++i) {
        const ResonanceMode& md = state.modes[static_cast<std::size_t>(i)];
        const float w = resonanceListen(reader, md.drive.z, q);
        if (w <= 0.0f) {
            continue;
        }
        float e2 = 0.0f;
        const float phi = resonanceModeShape(md, q, &e2);
        const float e = w * (memory ? md.drive.w : md.drive.y);
        s.displacement += w * md.drive.x * phi;
        s.energy += e * phi; // the figure F, squared below
        s.fast += w * md.drive.y * phi;
        s.meanEnergy += e * e * e2;
    }
    s.energy *= s.energy;
    return s;
}

namespace {

float sandOf(const ResonanceSums& s, float sharpness) {
    if (!(s.meanEnergy > 1e-12f)) {
        return 0.0f;
    }
    const float k = std::max(sharpness, 1e-3f);
    return std::sqrt(s.meanEnergy) * std::exp(-s.energy / (k * k * s.meanEnergy));
}

float scalarChannel(const ResonanceSums& s, const ResonanceSpec& r) {
    const ResonanceChannel channel = r.channel;
    const float sharpness = r.sharpness;
    switch (channel) {
    case ResonanceChannel::Relief:
        return sandOf(s, sharpness) + r.reliefLine * sandOf(s, r.reliefLineSharpness) + r.reliefVibration * s.displacement;
    case ResonanceChannel::Displacement:
        return s.displacement;
    case ResonanceChannel::Amplitude:
        return std::sqrt(std::max(s.energy, 0.0f));
    case ResonanceChannel::Sand: {
        if (!(s.meanEnergy > 1e-12f)) {
            return 0.0f;
        }
        const float k = std::max(sharpness, 1e-3f);
        return std::sqrt(s.meanEnergy) * std::exp(-s.energy / (k * k * s.meanEnergy));
    }
    case ResonanceChannel::Force:
    case ResonanceChannel::Gradient:
        break;
    }
    return 0.0f;
}

float derivativeStepOf(const ResonanceState& state, const FieldSpec& reader) {
    const float h = reader.resonance.derivativeStep;
    return std::max(h > 0.0f ? h : state.lambdaMin / 24.0f, 1e-4f);
}

} // namespace

glm::vec3 resonanceShading(const ResonanceState& state, const FieldSpec& reader, const glm::vec3& q) {
    const ResonanceSums s = resonanceSums(state, reader, q, true);
    return glm::vec3(sandOf(s, reader.resonance.sharpness), sandOf(s, reader.resonance.reliefLineSharpness),
                     std::abs(s.fast));
}

float resonanceScalar(const ResonanceState& state, const FieldSpec& reader, const glm::vec3& q) {
    if (reader.resonance.channel == ResonanceChannel::Shading) { // a colour read as a scalar: its luminance
        const glm::vec3 c = resonanceShading(state, reader, q);
        return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    }
    if (resonanceChannelIsVector(reader.resonance.channel)) {
        return glm::length(resonanceVector(state, reader, q));
    }
    const bool memory = reader.resonance.channel == ResonanceChannel::Sand || reader.resonance.channel == ResonanceChannel::Relief;
    return scalarChannel(resonanceSums(state, reader, q, memory), reader.resonance);
}

glm::vec3 resonanceVector(const ResonanceState& state, const FieldSpec& reader, const glm::vec3& q) {
    const ResonanceChannel c = reader.resonance.channel;
    if (!resonanceChannelIsVector(c)) {
        return glm::vec3(0.0f);
    }
    const float h = derivativeStepOf(state, reader);
    const auto value = [&](const glm::vec3& p) {
        // the force reads the SLOW figure: its nodes move as the sand's do, so matter held on them settles
        const ResonanceSums s = resonanceSums(state, reader, p, c == ResonanceChannel::Force);
        return c == ResonanceChannel::Force ? s.energy : s.displacement;
    };
    const glm::vec3 g((value(q + glm::vec3(h, 0, 0)) - value(q - glm::vec3(h, 0, 0))) / (2.0f * h),
                      (value(q + glm::vec3(0, h, 0)) - value(q - glm::vec3(0, h, 0))) / (2.0f * h),
                      (value(q + glm::vec3(0, 0, h)) - value(q - glm::vec3(0, 0, h))) / (2.0f * h));
    return c == ResonanceChannel::Force ? -g : g;
}

} // namespace avgen::spatial
