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
