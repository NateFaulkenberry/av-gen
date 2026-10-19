// ADR-1361: every live scene reads the Live panel's Response controls (`sonic/response/*`, ADR-1062). Each case loads
// a live project, plays the same synthetic groove into it, and shows a reaction that is a function of the controls
// moving the expected way: Sensitivity (and Sustain) the levels, Transients the hits, Release the envelopes. THE RIFT
// LIVE's cases are in test_rift.cpp (its world is generated). The neutral setting is the identity wherever the engine
// conditions (tests/unit/test_audio_response.cpp); for THE ASTRAL FORGE it is the very song it always conducted.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/composition.hpp"
#include "support/groove.hpp"
#include "support/project_assets.hpp"
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

TEST_CASE("Digital Mosh Live: Sensitivity drives the arc, Transients the fracture", "[live][digital-mosh][adr1361]") {
    Play p{.project = example("digital-mosh/digital-mosh-live.json")};
    p.response = {{"sensitivity", 0.2f}};
    const double dull = meanParam(p, "macros/energy");
    p.response = {{"sensitivity", 0.5f}};
    const double neutral = meanParam(p, "macros/energy");
    p.response = {{"sensitivity", 0.8f}};
    const double keen = meanParam(p, "macros/energy");
    INFO("macros/energy: " << dull << " / " << neutral << " / " << keen);
    CHECK(dull < neutral);
    // ADR-1510: a steady groove at neutral reads as a mix's body, which is full presence (the arc on a mix is the
    // offline one), so more Sensitivity cannot drive it faster; less slows it.
    CHECK(keen >= neutral);

    // A take 16 dB down (the limbs' bass lift, an auto-gained band times the response bass): since ADR-1510 the
    // response levels read against the take's own loudness, so a quieter channel playing the same music reads about
    // as the mix does (ADR-1361 had it read quieter, on a fixed window mastered music always topped), and Sensitivity
    // still lifts it.
    Play q = p;
    q.response = {{"sensitivity", 0.5f}};
    const double loud = meanSignal(q, "visual.hBass");
    q.level = 0.12f;
    const double quiet = meanSignal(q, "visual.hBass");
    q.response = {{"sensitivity", 0.9f}};
    const double lifted = meanSignal(q, "visual.hBass");
    INFO("visual.hBass: loud " << loud << ", quiet " << quiet << ", lifted " << lifted);
    CHECK(quiet > 0.2);
    CHECK(std::abs(quiet - loud) < 0.25 * loud);
    CHECK(lifted > quiet);

    // The kick's fracture light (the Dream's rhythm gate opened): more Transients, harder kicks.
    Play k{.project = example("digital-mosh/digital-mosh-live.json"), .hold = {{"macros/gRhythm", 1.0f}}};
    k.response = {{"transient", 0.2f}};
    const double soft = meanParam(k, "lights/fracture/intensity");
    k.response = {{"transient", 0.8f}};
    const double hard = meanParam(k, "lights/fracture/intensity");
    INFO("fracture: " << soft << " / " << hard);
    CHECK(soft < hard);
}

TEST_CASE("Sonic Live: the garden's loudness and strikes are the Response's", "[live][sonic][adr1361]") {
    Play p{.project = example("sonic-garden/sonic-live.json")};
    p.response = {{"sensitivity", 0.25f}};
    const double dull = meanSignal(p, "visual.energy");
    p.response = {{"sensitivity", 0.75f}};
    const double keen = meanSignal(p, "visual.energy");
    INFO("visual.energy: " << dull << " / " << keen);
    CHECK(dull < keen);
    // Sustain alone moves the levels too (the Transients slider does not).
    p.response = {{"sustain", 0.25f}};
    const double lowSustain = meanSignal(p, "visual.energy");
    p.response = {{"transient", 0.25f}};
    const double lowTransient = meanSignal(p, "visual.energy");
    p.response = {};
    const double neutral = meanSignal(p, "visual.energy");
    CHECK(lowSustain < neutral);
    CHECK(lowTransient == neutral);

    // At Sensitivity 0 the garden's loudness is off. Since ADR-1510 the waiting world is for silence: the response
    // level reads against the music's own loudness (a breakdown reads near 0), so silence also hears the character's
    // fixed-dB energy, and holds when the input is silent rather than when the slider is down.
    p.response = {{"sensitivity", 0.0f}};
    CHECK(meanSignal(p, "visual.energy") < 0.02);
    p.response = {};
    CHECK(meanSignal(p, "visual.silence") < 0.1);
    Play quiet = p;
    quiet.level = 0.0f; // a silent input
    CHECK(meanSignal(quiet, "visual.silence") > 0.9);

    // Strikes: Transients decides how many onsets strike.
    p.response = {{"transient", 0.2f}};
    const double fewer = meanSignal(p, "visual.strike");
    p.response = {{"transient", 0.9f}};
    const double more = meanSignal(p, "visual.strike");
    INFO("visual.strike: " << fewer << " / " << more);
    CHECK(fewer < more);
}

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

TEST_CASE("Digital Mosh Live: a seek lands the response-heard arc where play does", "[live][digital-mosh][adr1361][adr1168]") {
    // The arc leaves the Dream only on Trench (the owner's song, on no CI runner). The live drive is the product of two
    // outputs of an earlier interpret source and the response layer's level: a seek's control replay (ADR-1168) must
    // rebuild both, or a still mid-song lands in the Dream while play is in the Infection.
    testsupport::skipUnlessProjectAssetsPresent(example("digital-mosh/digital-mosh-trench.json"));
    app::Engine engine(app::EngineMode::Offline);
    engine.setLiveControl(false);
    REQUIRE(engine.loadProject(example("digital-mosh/digital-mosh-live.json")).has_value());
    REQUIRE(engine.loadAudio(fs::path(AVGEN_SOURCE_DIR) / "assets/audio/trench.wav").has_value());
    engine.seekSeconds(45.0);
    FrameTime t{};
    t.renderTime = 45.0;
    t.deltaTime = 0.0;
    engine.update(t);
    INFO("state after a seek to 45 s: '" << engine.states().current() << "'");
    CHECK(engine.states().current().starts_with("Infection")); // play: Infection from 37 s (the trace in the report)
    CHECK(engine.params().find("macros/energy")->finalComponent(0) > 0.3f);
}
