// ADR-1400: a hidden diagnostic, not a test -- prints a scene's resonance at a second of a real song, so a field can
// be tuned with numbers instead of renders. Run:
//   CW_SCENE=examples/cymatic-world/cymatic-world.scene.json CW_WAV=assets/audio/trench.wav CW_T=130 \
//   CW_PROBE=0,12,0 CW_RADIUS=10 ./build/release/tests/avgen_tests "[.resonance-probe]"
#include "analysis/analysis_track.hpp"
#include "analysis/audio_history_builder.hpp"
#include "audio/audio_file.hpp"
#include "spatial/field.hpp"
#include "spatial/resonance.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

using namespace avgen;

TEST_CASE("resonance probe (diagnostic)", "[.resonance-probe]") {
    const char* scenePath = std::getenv("CW_SCENE");
    const char* wav = std::getenv("CW_WAV");
    if (scenePath == nullptr || wav == nullptr) {
        SKIP("set CW_SCENE and CW_WAV");
    }
    const double t = std::getenv("CW_T") ? std::atof(std::getenv("CW_T")) : 30.0;
    glm::vec3 probe(0.0f, 12.0f, 0.0f);
    if (const char* p = std::getenv("CW_PROBE")) {
        std::sscanf(p, "%f,%f,%f", &probe.x, &probe.y, &probe.z);
    }
    const float radius = std::getenv("CW_RADIUS") ? static_cast<float>(std::atof(std::getenv("CW_RADIUS"))) : 10.0f;
    std::ifstream in(scenePath);
    REQUIRE(in);
    const nlohmann::json scene = nlohmann::json::parse(in);
    spatial::FieldSet set;
    for (const auto& n : scene.at("nodes")) {
        if (n.value("kind", "") != "field") {
            continue;
        }
        nlohmann::json f = n.at("field");
        f["name"] = n.at("name");
        if (n.contains("position")) {
            f["position"] = n.at("position");
        }
        auto spec = spatial::FieldSpec::fromJson(f);
        REQUIRE(spec.has_value());
        set.fields.push_back(*spec);
    }
    auto file = audio::AudioFile::load(wav);
    REQUIRE(file.has_value());
    const auto track = analysis::AnalysisTrack::analyze(*file, analysis::AnalyzerConfig{});
    set.audio = std::make_shared<const spatial::AudioHistory>(analysis::buildAudioHistory(track));
    spatial::ResonanceCache cache;
    {
        // the CPU cost a frame pays: 300 frames played at 30 fps through one cache (after one warm frame)
        spatial::resolveResonance(set, cache, t - 10.0);
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 1; k <= 300; ++k) {
            spatial::resolveResonance(set, cache, t - 10.0 + k / 30.0);
        }
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 300.0;
        std::printf("resolveResonance: %.1f us a frame (played, cached)\n", us);
        spatial::ResonanceCache cold;
        const auto t1 = std::chrono::steady_clock::now();
        spatial::resolveResonance(set, cold, t);
        std::printf("resolveResonance after a seek (cold cache): %.1f us\n",
                    std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t1).count());
    }
    spatial::resolveResonance(set, cache, t);
    for (const auto& f : set.fields) {
        if (!f.resonanceState) {
            continue;
        }
        const auto& s = *f.resonanceState;
        std::printf("resonator %s at %.2f s: %d modes, limiter %.3f\n", f.name.c_str(), t, s.count, s.limiterGain);
        for (int i = 0; i < s.count; ++i) {
            const auto& m = s.modes[static_cast<std::size_t>(i)];
            std::printf("  mode %2d u %.2f f %7.1f Hz lambda %6.2f m src %d m %.0f | driven %.3f mem %.3f | e %.3f s %.3f\n", i,
                        m.drive.z, s.frequency[static_cast<std::size_t>(i)], 6.2831853f / m.shape.x,
                        s.source[static_cast<std::size_t>(i)], m.shape.y, s.envelope[static_cast<std::size_t>(i)],
                        s.memory[static_cast<std::size_t>(i)], m.drive.y, m.drive.w);
        }
    }
    // CW_LINE=x,z,y0,y1: every field along a vertical line (a tuned structure's axis)
    if (const char* line = std::getenv("CW_LINE")) {
        float x = 0.0f, z = 0.0f, y0 = 0.0f, y1 = 10.0f;
        std::sscanf(line, "%f,%f,%f,%f", &x, &z, &y0, &y1);
        for (int k = 0; k <= 8; ++k) {
            const glm::vec3 p(x, y0 + (y1 - y0) * static_cast<float>(k) / 8.0f, z);
            std::printf("  y %6.2f:", p.y);
            for (const auto& f : set.fields) {
                std::printf(" %s %.3f", f.name.c_str(), static_cast<double>(spatial::sampleScalar(f, p, t, &set)));
            }
            std::printf("\n");
        }
    }
    // every reader's value statistics over a ball round the probe
    for (const auto& f : set.fields) {
        double sum = 0.0;
        double sum2 = 0.0;
        double top = 0.0;
        const int n = 4000;
        for (int k = 0; k < n; ++k) {
            const float a = 2.399963f * static_cast<float>(k);
            const float z = 1.0f - 2.0f * (static_cast<float>(k) + 0.5f) / static_cast<float>(n);
            const float r = radius * std::cbrt(static_cast<float>((k * 7919) % n + 1) / static_cast<float>(n));
            const float rr = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const glm::vec3 p = probe + r * glm::vec3(rr * std::cos(a), z, rr * std::sin(a));
            const double v = f.type() == spatial::FieldType::Vector ? glm::length(spatial::sampleVector(f, p, t, &set))
                                                                    : spatial::sampleScalar(f, p, t, &set);
            sum += v;
            sum2 += v * v;
            top = std::max(top, std::abs(v));
        }
        const double mean = sum / n;
        std::printf("field %-10s mean %8.4f rms %8.4f max %8.4f\n", f.name.c_str(), mean, std::sqrt(sum2 / n), top);
    }
}

namespace {

// The scene's field nodes as a FieldSet (the probe's loader).
spatial::FieldSet loadFieldSet(const char* scenePath) {
    std::ifstream in(scenePath);
    REQUIRE(in);
    const nlohmann::json scene = nlohmann::json::parse(in);
    spatial::FieldSet set;
    for (const auto& n : scene.at("nodes")) {
        if (n.value("kind", "") != "field") {
            continue;
        }
        nlohmann::json f = n.at("field");
        f["name"] = n.at("name");
        if (n.contains("position")) {
            f["position"] = n.at("position");
        }
        auto spec = spatial::FieldSpec::fromJson(f);
        REQUIRE(spec.has_value());
        set.fields.push_back(*spec);
    }
    return set;
}

double correlation(const std::vector<float>& a, const std::vector<float>& b) {
    double ma = 0.0, mb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= static_cast<double>(a.size());
    mb /= static_cast<double>(b.size());
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return saa > 0.0 && sbb > 0.0 ? sab / std::sqrt(saa * sbb) : 1.0;
}

} // namespace

// ADR-1403 (Cymatic World motion): how much the ground's STRUCTURE changes, not how much it moves. A reader's value
// (CW_FIELD, default "relief") on a grid over the visible ground, every CW_DT seconds from CW_T0 to CW_T1; for each
// lag the mean of 1 - corr(H(t), H(t - lag)) -- a normalised correlation, so a figure that only grows and shrinks in
// place scores 0 however much it moves, and a figure that reorganises scores towards 1. Also the rms (the motion's
// size, in the reader's units) and the share of the grid whose displacement changed sign over the lag (nodal lines
// crossing ground). Run:
//   CW_SCENE=examples/cymatic-world/cymatic-world.scene.json CW_WAV=assets/audio/hyperborea.wav CW_T0=40 CW_T1=80 \
//   ./build/release/tests/avgen_tests "[.resonance-motion]"
TEST_CASE("resonance motion (diagnostic)", "[.resonance-motion]") {
    const char* scenePath = std::getenv("CW_SCENE");
    const char* wav = std::getenv("CW_WAV");
    if (scenePath == nullptr || wav == nullptr) {
        SKIP("set CW_SCENE and CW_WAV");
    }
    const auto env = [](const char* k, double d) { return std::getenv(k) ? std::atof(std::getenv(k)) : d; };
    const double t0 = env("CW_T0", 40.0);
    const double t1 = env("CW_T1", 80.0);
    const double dt = env("CW_DT", 0.25);
    const float half = static_cast<float>(env("CW_HALF", 40.0)); // the grid's half-width (m), about the origin
    const float y = static_cast<float>(env("CW_Y", -3.0));
    const char* fieldName = std::getenv("CW_FIELD") ? std::getenv("CW_FIELD") : "relief";
    spatial::FieldSet set = loadFieldSet(scenePath);
    auto file = audio::AudioFile::load(wav);
    REQUIRE(file.has_value());
    const auto track = analysis::AnalysisTrack::analyze(*file, analysis::AnalyzerConfig{});
    set.audio = std::make_shared<const spatial::AudioHistory>(analysis::buildAudioHistory(track));
    const spatial::FieldSpec* reader = nullptr;
    for (const auto& f : set.fields) {
        if (f.name == fieldName) {
            reader = &f;
        }
    }
    REQUIRE(reader != nullptr);
    constexpr int kGrid = 81;
    spatial::ResonanceCache cache;
    std::vector<std::vector<float>> frames;
    std::vector<double> times;
    for (double t = t0; t <= t1 + 1e-9; t += dt) {
        spatial::resolveResonance(set, cache, t);
        std::vector<float> h(kGrid * kGrid);
        for (int j = 0; j < kGrid; ++j) {
            for (int i = 0; i < kGrid; ++i) {
                const glm::vec3 p(-half + 2.0f * half * static_cast<float>(i) / (kGrid - 1), y,
                                  -half + 2.0f * half * static_cast<float>(j) / (kGrid - 1));
                h[static_cast<std::size_t>(j * kGrid + i)] = spatial::sampleScalar(*reader, p, t, &set);
            }
        }
        frames.push_back(std::move(h));
        times.push_back(t);
    }
    // CW_BLUR (metres): a Gaussian low-pass first, so the measure reads the structure the eye reads at that scale
    // (fine sand flickering decorrelates a raw grid while the large figure stands still)
    const float blur = static_cast<float>(env("CW_BLUR", 0.0));
    if (blur > 0.0f) {
        const float cell = 2.0f * half / (kGrid - 1);
        const int rad = static_cast<int>(std::ceil(2.5f * blur / cell));
        std::vector<float> wts(static_cast<std::size_t>(2 * rad + 1));
        float wsum = 0.0f;
        for (int k = -rad; k <= rad; ++k) {
            const float x = static_cast<float>(k) * cell / blur;
            wts[static_cast<std::size_t>(k + rad)] = std::exp(-0.5f * x * x);
            wsum += wts[static_cast<std::size_t>(k + rad)];
        }
        for (auto& h : frames) {
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<float> o(h.size());
                for (int j = 0; j < kGrid; ++j) {
                    for (int i = 0; i < kGrid; ++i) {
                        float acc = 0.0f;
                        for (int k = -rad; k <= rad; ++k) {
                            const int ii = std::clamp(pass == 0 ? i + k : i, 0, kGrid - 1);
                            const int jj = std::clamp(pass == 0 ? j : j + k, 0, kGrid - 1);
                            acc += wts[static_cast<std::size_t>(k + rad)] * h[static_cast<std::size_t>(jj * kGrid + ii)];
                        }
                        o[static_cast<std::size_t>(j * kGrid + i)] = acc / wsum;
                    }
                }
                h.swap(o);
            }
        }
    }
    const double amount = env("CW_AMOUNT", 1.0); // the deformer's amount: metres per reader unit
    const auto rmsOf = [](const std::vector<float>& h) {
        double m = 0.0, s = 0.0;
        for (float v : h) {
            m += v;
        }
        m /= static_cast<double>(h.size());
        for (float v : h) {
            s += (v - m) * (v - m);
        }
        return std::sqrt(s / static_cast<double>(h.size()));
    };
    std::printf("motion of '%s' over [%.1f, %.1f] s, grid %d x %d over +-%.0f m at y %.1f, blur %.1f m\n", fieldName, t0,
                t1, kGrid, kGrid, half, y, static_cast<double>(blur));
    for (double lag : {0.25, 1.0, 4.0, 10.0}) {
        const int step = std::max(1, static_cast<int>(std::lround(lag / dt)));
        double change = 0.0, flips = 0.0, moved = 0.0;
        int n = 0;
        for (std::size_t k = static_cast<std::size_t>(step); k < frames.size(); ++k) {
            change += 1.0 - correlation(frames[k], frames[k - static_cast<std::size_t>(step)]);
            int f = 0;
            const auto& a = frames[k];
            const auto& b = frames[k - static_cast<std::size_t>(step)];
            double ma = 0.0, mb = 0.0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                ma += a[i];
                mb += b[i];
            }
            ma /= static_cast<double>(a.size());
            mb /= static_cast<double>(b.size());
            for (std::size_t i = 0; i < a.size(); ++i) {
                f += ((a[i] - ma) > 0.0) != ((b[i] - mb) > 0.0) ? 1 : 0;
            }
            flips += static_cast<double>(f) / static_cast<double>(a.size());
            double dm = 0.0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                dm += std::abs(a[i] - b[i]);
            }
            moved += amount * dm / static_cast<double>(a.size());
            ++n;
        }
        if (n > 0) {
            std::printf("  lag %5.2f s: structural change (1 - corr) %.3f, sign flips %.3f, mean |dH| %.3f m\n", lag, change / n,
                        flips / n, moved / n);
        }
    }
    double rms = 0.0, rmsMin = 1e30, rmsMax = 0.0;
    for (const auto& h : frames) {
        const double r = rmsOf(h);
        rms += r;
        rmsMin = std::min(rmsMin, r);
        rmsMax = std::max(rmsMax, r);
    }
    std::printf("  rms (m, mean / min / max): %.3f / %.3f / %.3f\n", amount * rms / static_cast<double>(frames.size()),
                amount * rmsMin, amount * rmsMax);
    if (const char* every = std::getenv("CW_SERIES")) { // a time series, one line per CW_SERIES seconds
        const int step = std::max(1, static_cast<int>(std::lround(std::atof(every) / dt)));
        const int lag = std::max(1, static_cast<int>(std::lround(1.0 / dt)));
        for (std::size_t k = static_cast<std::size_t>(lag); k < frames.size(); k += static_cast<std::size_t>(step)) {
            std::printf("  t %6.2f rms %.3f change/1s %.3f\n", times[k], rmsOf(frames[k]),
                        1.0 - correlation(frames[k], frames[k - static_cast<std::size_t>(lag)]));
        }
    }
}
