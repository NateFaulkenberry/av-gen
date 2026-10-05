// GPU World research spike, Phase 3 (art) -- DISPOSABLE. See art_audio.hpp.
#include "art_audio.hpp"

#include "analysis/analysis_track.hpp"
#include "audio/audio_file.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace gpuworld {
namespace {

constexpr std::uint32_t kCacheMagic = 0x47575033u; // "GWP3"
constexpr std::uint32_t kCacheVersion = 4;

template <typename T>
void putVec(std::ofstream& f, const std::vector<T>& v) {
    const std::uint64_t n = v.size();
    f.write(reinterpret_cast<const char*>(&n), sizeof(n));
    f.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(n * sizeof(T)));
}
template <typename T>
bool getVec(std::ifstream& f, std::vector<T>& v) {
    std::uint64_t n = 0;
    if (!f.read(reinterpret_cast<char*>(&n), sizeof(n))) return false;
    v.resize(n);
    return static_cast<bool>(f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n * sizeof(T))));
}

bool readCache(const std::string& path, SongAnalysis& s) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::uint32_t magic = 0, version = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    if (magic != kCacheMagic || version != kCacheVersion) return false;
    f.read(reinterpret_cast<char*>(&s.hopRate), 4);
    f.read(reinterpret_cast<char*>(&s.t0), 4);
    f.read(reinterpret_cast<char*>(&s.hops), 4);
    f.read(reinterpret_cast<char*>(&s.duration), 8);
    return getVec(f, s.spec) && getVec(f, s.env0) && getVec(f, s.env1) && getVec(f, s.kickT) && getVec(f, s.kickS) &&
           getVec(f, s.snareT) && getVec(f, s.snareS) && getVec(f, s.hatT) && getVec(f, s.hatS);
}

void writeCache(const std::string& path, const SongAnalysis& s) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;
    f.write(reinterpret_cast<const char*>(&kCacheMagic), 4);
    f.write(reinterpret_cast<const char*>(&kCacheVersion), 4);
    f.write(reinterpret_cast<const char*>(&s.hopRate), 4);
    f.write(reinterpret_cast<const char*>(&s.t0), 4);
    f.write(reinterpret_cast<const char*>(&s.hops), 4);
    f.write(reinterpret_cast<const char*>(&s.duration), 8);
    putVec(f, s.spec); putVec(f, s.env0); putVec(f, s.env1); putVec(f, s.kickT); putVec(f, s.kickS);
    putVec(f, s.snareT); putVec(f, s.snareS); putVec(f, s.hatT); putVec(f, s.hatS);
}

float percentile(std::vector<float> v, float q) {
    if (v.empty()) return 0.0f;
    const auto k = static_cast<std::size_t>(std::clamp(q, 0.0f, 1.0f) * static_cast<float>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

} // namespace

SongAnalysis loadSong(const std::string& path, const std::string& cachePath) {
    SongAnalysis s;
    if (!cachePath.empty() && readCache(cachePath, s)) {
        return s;
    }
    const auto t0 = std::chrono::steady_clock::now();
    auto file = avgen::audio::AudioFile::load(path);
    if (!file) {
        std::fprintf(stderr, "audio: %s\n", file.error().message.c_str());
        std::exit(6);
    }
    avgen::analysis::AnalyzerConfig cfg;
    cfg.sampleRate = file->sampleRate();
    const auto track = avgen::analysis::AnalysisTrack::analyze(*file, cfg);
    const auto& frames = track.frames();
    s.hops = static_cast<int>(frames.size());
    s.hopRate = static_cast<float>(cfg.sampleRate) / static_cast<float>(cfg.hopSize);
    s.t0 = frames.empty() ? 0.0f : static_cast<float>(frames.front().timeSeconds);
    s.duration = file->durationSeconds();
    const float binHz = static_cast<float>(cfg.sampleRate) / static_cast<float>(cfg.windowSize);

    // 64 log-spaced bins, 32 Hz .. 16 kHz. A bin narrower than an FFT bin reads the magnitude
    // interpolated at its centre, so the low end is not a staircase of empty rows.
    constexpr int B = SongAnalysis::kBins;
    std::vector<float> edges(B + 1);
    for (int k = 0; k <= B; ++k) edges[k] = 32.0f * std::pow(16000.0f / 32.0f, static_cast<float>(k) / B);
    std::vector<float> db(static_cast<std::size_t>(s.hops) * B);
    for (int h = 0; h < s.hops; ++h) {
        const auto& mag = frames[h].magnitude;
        const int nb = static_cast<int>(mag.size());
        for (int k = 0; k < B; ++k) {
            const float lo = edges[k] / binHz, hi = edges[k + 1] / binHz;
            double p = 0.0;
            int count = 0;
            for (int j = static_cast<int>(std::ceil(lo)); j < hi && j < nb; ++j) {
                p += static_cast<double>(mag[j]) * mag[j];
                ++count;
            }
            if (count == 0) {
                const float c = 0.5f * (lo + hi);
                const int j0 = std::min(static_cast<int>(c), nb - 2);
                const float a = c - static_cast<float>(j0);
                const float m = mag[j0] * (1.0f - a) + mag[j0 + 1] * a;
                p = static_cast<double>(m) * m;
            } else {
                p /= count;
            }
            db[static_cast<std::size_t>(h) * B + k] = static_cast<float>(10.0 * std::log10(p + 1e-12));
        }
    }
    // Per-bin normalisation over the whole track (p8 -> 0, p99.5 -> 1): every bin uses its full
    // range, so the hats light the field as surely as the kick does.
    s.spec.assign(db.size(), 0.0f);
    std::vector<float> col(static_cast<std::size_t>(s.hops));
    for (int k = 0; k < B; ++k) {
        for (int h = 0; h < s.hops; ++h) col[h] = db[static_cast<std::size_t>(h) * B + k];
        const float lo = percentile(col, 0.08f), hi = percentile(col, 0.995f);
        float y = 0.0f;
        const float release = std::exp(-1.0f / (0.11f * s.hopRate));
        for (int h = 0; h < s.hops; ++h) {
            const float x = std::clamp((col[h] - lo) / std::max(hi - lo, 1e-3f), 0.0f, 1.0f);
            y = std::max(x, y * release); // instant attack, ~110 ms release
            s.spec[static_cast<std::size_t>(h) * B + k] = y;
        }
    }
    // Per-bin normalisation forgets loudness: the quiet intro would light the field as hard as the
    // release. Fold the song's dynamics back in: a 0.6 s RMS envelope against the track's p98 scales
    // every bin (0.25 at silence-ish, 1 at full).
    {
        std::vector<float> rmsS(static_cast<std::size_t>(s.hops));
        float y = 0.0f;
        const float k = 1.0f - std::exp(-1.0f / (0.6f * s.hopRate));
        for (int h = 0; h < s.hops; ++h) { y += (frames[h].rms - y) * k; rmsS[h] = y; }
        const float ref = std::max(percentile(rmsS, 0.98f), 1e-4f);
        for (int h = 0; h < s.hops; ++h) {
            const float g = 0.25f + 0.75f * std::pow(std::clamp(rmsS[h] / ref, 0.0f, 1.0f), 1.5f);
            for (int k2 = 0; k2 < B; ++k2) s.spec[static_cast<std::size_t>(h) * B + k2] *= g;
        }
    }
    // Band envelopes from the normalised bins (bass <150 Hz, lowMid <400, mid <2k, high <6k, air).
    auto binOf = [&](float hz) { return std::clamp(static_cast<int>(std::log(hz / 32.0f) / std::log(16000.0f / 32.0f) * B), 0, B); };
    const int cuts[6] = {0, binOf(150), binOf(400), binOf(2000), binOf(6000), B};
    s.env0.resize(s.hops);
    s.env1.resize(s.hops);
    float e[5] = {0, 0, 0, 0, 0};
    float rmsPeak = 1e-4f;
    for (const auto& f : frames) rmsPeak = std::max(rmsPeak, f.rms);
    const float att = 1.0f - std::exp(-1.0f / (0.012f * s.hopRate));
    const float rel = 1.0f - std::exp(-1.0f / (0.22f * s.hopRate));
    for (int h = 0; h < s.hops; ++h) {
        for (int b = 0; b < 5; ++b) {
            float m = 0.0f;
            for (int k = cuts[b]; k < cuts[b + 1]; ++k) m += s.spec[static_cast<std::size_t>(h) * B + k];
            m /= static_cast<float>(std::max(1, cuts[b + 1] - cuts[b]));
            e[b] += (m - e[b]) * (m > e[b] ? att : rel);
        }
        s.env0[h] = glm::vec4(e[0], e[1], e[2], e[3]);
        s.env1[h] = glm::vec4(e[4], frames[h].energy, frames[h].beatPhase, frames[h].rms / rmsPeak);
        if (frames[h].lowOnset) { s.kickT.push_back(static_cast<float>(frames[h].timeSeconds)); s.kickS.push_back(std::max(0.25f, frames[h].lowOnsetStrength)); }
        if (frames[h].midOnset) { s.snareT.push_back(static_cast<float>(frames[h].timeSeconds)); s.snareS.push_back(std::max(0.25f, frames[h].midOnsetStrength)); }
        if (frames[h].highOnset) { s.hatT.push_back(static_cast<float>(frames[h].timeSeconds)); s.hatS.push_back(std::max(0.25f, frames[h].highOnsetStrength)); }
    }
    s.analyseSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!cachePath.empty()) writeCache(cachePath, s);
    return s;
}

AudioAtT sampleSong(const SongAnalysis& s, double t) {
    AudioAtT a;
    a.hopF = static_cast<float>((t - s.t0) * s.hopRate);
    if (s.hops > 0) {
        const int h0 = std::clamp(static_cast<int>(std::floor(a.hopF)), 0, s.hops - 1);
        const int h1 = std::min(h0 + 1, s.hops - 1);
        const float f = std::clamp(a.hopF - std::floor(a.hopF), 0.0f, 1.0f);
        a.env0 = glm::mix(s.env0[h0], s.env0[h1], f);
        a.env1 = glm::mix(s.env1[h0], s.env1[h1], f);
        a.env1.z = s.env1[h0].z; // beat phase is a sawtooth: do not interpolate across the wrap
        a.rms = a.env1.w;
    }
    auto lastN = [&](const std::vector<float>& T, const std::vector<float>& S, float* outT, float* outS, int n, float decay) {
        const auto it = std::upper_bound(T.begin(), T.end(), static_cast<float>(t));
        int idx = static_cast<int>(it - T.begin()) - 1;
        float env = 0.0f;
        for (int k = 0; k < n; ++k, --idx) {
            outT[k] = idx >= 0 ? T[idx] : -1e4f;
            outS[k] = idx >= 0 ? S[idx] : 0.0f;
            if (idx >= 0) env += S[idx] * std::exp(-decay * static_cast<float>(t - T[idx]));
        }
        return env;
    };
    a.kickEnv = std::min(1.5f, lastN(s.kickT, s.kickS, a.kickT, a.kickS, 8, 7.0f));
    a.snareEnv = std::min(1.5f, lastN(s.snareT, s.snareS, a.snareT, a.snareS, 4, 9.0f));
    float ht[4], hs[4];
    a.hatEnv = std::min(1.5f, lastN(s.hatT, s.hatS, ht, hs, 4, 14.0f));
    return a;
}

} // namespace gpuworld
