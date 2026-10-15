// ADR-1361: every live scene reads the Live panel's Response controls (`sonic/response/*`, ADR-1062). Each case loads
// a live project, plays the same synthetic groove into it, and shows a reaction that is a function of the controls
// moving the expected way: Sensitivity (and Sustain) the levels, Transients the hits, Release the envelopes. THE RIFT
// LIVE's cases are in test_rift.cpp (its world is generated). The neutral setting is the identity wherever the engine
// conditions (tests/unit/test_audio_response.cpp); for THE ASTRAL FORGE it is the very song it always conducted.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/composition.hpp"
#include "support/groove.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// A groove at a level: 0.8 is a mix near full scale, 0.12 a take about 16 dB down.
fs::path grooveWav(float level) {
    static std::map<float, fs::path> made;
    if (auto it = made.find(level); it != made.end()) {
        return it->second;
    }
    testsupport::GrooveSpec spec;
    spec.bars = 8;
    spec.level = level;
    const auto g = testsupport::makeGroove(spec);
    const fs::path p = testsupport::processTempDir() / ("live_response_" + std::to_string(level) + ".wav");
    REQUIRE(g.file.writeWav(p).has_value());
    made[level] = p;
    return p;
}

fs::path example(const char* rel) {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / rel;
}

struct Play {
    fs::path project;
    float level = 0.8f;
    std::map<std::string, float> response; // sonic/response/<leaf> -> value
    double seconds = 8.0;
    std::map<std::string, float> hold;     // parameters held each frame (a stage's gate, opened for the test)
    std::function<void(app::Engine&, double)> each; // read the frame
};

void play(const Play& p) {
    app::Engine engine(app::EngineMode::Offline);
    engine.setLiveControl(false);
    auto loaded = engine.loadProject(p.project);
    INFO(p.project.string() << (loaded ? "" : ": " + loaded.error().message));
    REQUIRE(loaded.has_value());
    REQUIRE(engine.loadAudio(grooveWav(p.level)).has_value());
    for (const auto& [leaf, v] : p.response) {
        auto* param = engine.params().findAs<float>("sonic/response/" + leaf);
        REQUIRE(param != nullptr); // the project's `sonic` block registered the Response controls
        param->setBase(v);
    }
    FixedStepClock clock(30.0);
    const int frames = static_cast<int>(p.seconds * 30.0);
    for (int i = 0; i < frames; ++i) {
        for (const auto& [path, v] : p.hold) {
            if (auto* q = engine.params().findAs<float>(path)) {
                q->setBase(v);
            }
        }
        const FrameTime t = engine.tick(clock);
        engine.update(t);
        if (p.each && t.renderTime >= 1.0) { // the first second: the analysis and the followers find their feet
            p.each(engine, t.renderTime);
        }
    }
}

// The mean of a parameter's modulated value, or of a bus signal, over the played frames.
double meanParam(Play p, const std::string& path) {
    double sum = 0.0;
    int n = 0;
    p.each = [&](app::Engine& e, double) {
        const auto* q = e.params().find(path);
        REQUIRE(q != nullptr);
        sum += q->finalComponent(0);
        ++n;
    };
    play(p);
    return n > 0 ? sum / n : 0.0;
}

double meanSignal(Play p, const std::string& name) {
    double sum = 0.0;
    int n = 0;
    p.each = [&](app::Engine& e, double) {
        const auto id = e.signals().find(name);
        REQUIRE(id.has_value());
        sum += e.signals().value(*id);
        ++n;
    };
    play(p);
    return n > 0 ? sum / n : 0.0;
}

} // namespace

TEST_CASE("THE ASTRAL FORGE LIVE: the conductor's song is heard through the Response; neutral is the song itself",
          "[live][astral][adr1361]") {
    struct Heard {
        std::shared_ptr<const scene::AstralSong> song, heard;
    };
    auto run = [](std::map<std::string, float> response) {
        Heard h;
        Play p{.project = example("astral-forge/astral-forge-live.json"), .response = std::move(response),
               .seconds = 2.0};
        p.each = [&](app::Engine& e, double) {
            const scene::Composition* comp = e.composition();
            REQUIRE(comp != nullptr);
            h.song = comp->astralSong();
            h.heard = comp->astralHeardSong();
        };
        play(p);
        REQUIRE(h.song != nullptr);
        REQUIRE(h.heard != nullptr);
        return h;
    };
    // Neutral: the conductor conducts the very song it always did (the same object -- offline renders unchanged).
    const Heard neutral = run({});
    CHECK(neutral.heard == neutral.song);

    // Transients down: weak kicks are not heard and the rest land softer (the collapse's 0.5, the blast).
    const Heard soft = run({{"transient", 0.15f}});
    REQUIRE(soft.heard != soft.song);
    CHECK(soft.heard->song.kickT.size() < soft.song->song.kickT.size());
    // Sensitivity up: the spectrum the god's matter reads is lifted.
    const Heard keen = run({{"sensitivity", 0.85f}});
    double before = 0.0, after = 0.0;
    for (std::size_t i = 0; i < keen.song->song.spec.size(); ++i) {
        before += keen.song->song.spec[i];
        after += keen.heard->song.spec[i];
    }
    CHECK(after > before);
    // Release: the hit envelopes' decay is lengthened.
    const Heard slow = run({{"release", 3.0f}});
    CHECK(slow.heard->song.release == 3.0f);
}
