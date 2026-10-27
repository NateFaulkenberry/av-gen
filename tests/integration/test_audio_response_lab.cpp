// ADR-1720: the Audio Response Lab (examples/audio-response-lab, written by tools/audio_lab/make_lab_scene.py) is a board
// of indicators, each a route from a production signal. What makes it a diagnostic rather than a picture is that an
// indicator shows exactly the value the bus published, on that frame, and shows NOTHING for a signal the bus does
// not carry. These cases hold it to that on the stem fixture (tests/support/stem_fixture.hpp), whose source keys
// (drums, bass, synth, pad) are the lab's.
//
// The fixture categories, the ground truth and the musical metrics are tools/audio_lab/lab.py's (01-plan.md); this
// file guards the scene and the trace the lab reads.

#include "app/engine.hpp"
#include "app/sonic_trace_cli.hpp"
#include "core/time.hpp"
#include "params/parameter.hpp"
#include "support/stem_fixture.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path labDir() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "audio-response-lab";
}

nlohmann::json labProject() {
    std::ifstream in(labDir() / "audio-response-lab.json");
    return nlohmann::json::parse(in);
}

// The lab project with the stem fixture's mix and, when `stems`, its four stems keyed as the lab expects.
fs::path fixtureProject(const testsupport::StemFixture& fx, bool stems, const std::string& name) {
    nlohmann::json p = labProject();
    p["assets"]["scene"]["path"] = (labDir() / p["assets"]["scene"]["path"].get<std::string>()).string();
    p["assets"]["audio"] = fx.file("mix.wav").string();
    if (stems) {
        nlohmann::json list = nlohmann::json::array();
        for (const auto& [key, role] : std::vector<std::pair<std::string, std::string>>{
                 {"drums", "drums"}, {"bass", "bass"}, {"synth", "synth"}, {"pad", "other"}}) {
            list.push_back({{"key", key}, {"label", key}, {"role", role}, {"file", fx.file(key + ".wav").string()}});
        }
        p["assets"]["stems"] = list;
    }
    p["sonic"]["live"] = false;
    const fs::path dir = testsupport::processTempDir() / "audio-response-lab";
    fs::create_directories(dir);
    const fs::path out = dir / (name + ".json");
    std::ofstream(out) << p.dump(1);
    return out;
}

struct Trace {
    std::map<std::string, std::vector<double>> col;
};

Trace readTrace(const fs::path& csv) {
    Trace t;
    std::ifstream in(csv);
    std::string header;
    REQUIRE(std::getline(in, header));
    std::vector<std::string> names;
    {
        std::stringstream ss(header);
        for (std::string n; std::getline(ss, n, ',');) {
            names.push_back(n);
        }
    }
    for (std::string row; std::getline(in, row);) {
        std::stringstream ss(row);
        std::size_t i = 0;
        for (std::string cell; std::getline(ss, cell, ',') && i < names.size(); ++i) {
            t.col[names[i]].push_back(std::stod(cell));
        }
    }
    return t;
}

} // namespace

TEST_CASE("Audio Response Lab: a live project the index lists as a diagnostic", "[audiolab][adr1720]") {
    const nlohmann::json p = labProject();
    CHECK(p["sonic"]["live"].get<bool>());
    // Every indicator the composer and the pixel checks read is a route in the project.
    const auto& layout = p["audioLab"]["layout"];
    REQUIRE(layout.size() >= 30);
    std::map<std::string, std::string> routed; // target -> source
    for (const auto& r : p["routes"]) {
        routed[r["target"].get<std::string>()] = r["source"].get<std::string>();
    }
    for (const auto& [name, ind] : layout.items()) {
        INFO(name);
        const std::string param = ind["param"].get<std::string>();
        REQUIRE(routed.contains(param));
        CHECK(routed[param] == ind["signal"].get<std::string>());
    }
    // It loads, and every route that is not optional binds: no indicator silently reads nothing.
    app::Engine engine(app::EngineMode::Offline);
    engine.setLiveControl(false);
    auto r = engine.loadProject(labDir() / "audio-response-lab.json");
    INFO((r ? std::string() : r.error().message));
    REQUIRE(r.has_value());
    std::size_t optional = 0;
    for (const params::ModRoute& route : engine.modulator().routes()) {
        INFO(route.source << " -> " << route.target);
        if (route.optional) {
            ++optional;
            continue;
        }
        CHECK(route.targetParam != nullptr);
    }
    CHECK(optional > 0); // the per-source indicators: present only when the source is
}

TEST_CASE("Audio Response Lab: every indicator shows the published value on its frame", "[audiolab][stems][adr1720]") {
    const testsupport::StemFixture& fx = testsupport::requireStemFixture();
    const fs::path project = fixtureProject(fx, true, "stems");
    const fs::path csv = project.parent_path() / "stems.csv";
    REQUIRE(app::runSonicTraceCommand(project, csv, 30.0, true) == 0);
    const Trace t = readTrace(csv);
    const nlohmann::json layout = labProject()["audioLab"]["layout"];
    int meters = 0;
    int lamps = 0;
    for (const auto& [name, ind] : layout.items()) {
        const std::string kind = ind["kind"].get<std::string>();
        if (kind != "meter" && kind != "lamp") {
            continue;
        }
        INFO(name);
        const std::string signal = ind["signal"].get<std::string>();
        const std::string final =
            "final:" + ind["param"].get<std::string>() + "[" + std::to_string(ind.value("component", 0)) + "]";
        REQUIRE(t.col.contains(signal));
        REQUIRE(t.col.contains(final));
        const auto& v = t.col.at(signal);
        const auto& f = t.col.at(final);
        double worst = 0.0;
        double peak = 0.0;
        for (std::size_t i = 0; i < v.size(); ++i) {
            double want = 0.0;
            if (kind == "meter") {
                const double range = ind.value("range", 1.0);
                const double scale = ind["scale"].get<double>();
                want = 0.001 + std::clamp(v[i] / range, 0.0, 1.0) * (scale - 0.001);
                ++meters;
            } else {
                want = ind["base"].get<double>() + ind["gain"].get<double>() * v[i];
                ++lamps;
            }
            worst = std::max(worst, std::abs(want - f[i]));
            peak = std::max(peak, v[i]);
        }
        // The trace writes 4 decimals of the signal and 6 significant digits of the final.
        CHECK(worst < 1e-3 * std::max(1.0, ind.value("scale", 1.0)));
        // The silent pad excepted, every source indicator moved: the indicator is wired to something live.
        if (signal.rfind("stem.pad.", 0) != 0 && signal != "response.note" && signal != "audio.tempoConfidence") {
            CHECK(peak > 0.0);
        }
    }
    CHECK(meters > 0);
    CHECK(lamps > 0);
    // The clock lamps (`beat.source` == 1 MIDI, 2 file grid, 3 live tracker, 4 manual): lit exactly while the source
    // is theirs, 0.9 over their rest. Until 2026-10-27 the manual clock (ADR-1783) had no lamp.
    std::set<int> values;
    std::set<double> rests;
    for (const auto& [name, ind] : layout.items()) {
        if (ind["kind"].get<std::string>() != "state") {
            continue;
        }
        INFO(name);
        const int value = static_cast<int>(ind["value"].get<double>());
        values.insert(value);
        const std::string final = "final:" + ind["param"].get<std::string>() + "[0]";
        REQUIRE(t.col.contains("beat.source"));
        REQUIRE(t.col.contains(final));
        const auto& v = t.col.at("beat.source");
        const auto& f = t.col.at(final);
        // Every lamp rests at the same glow, so a lamp lit through the whole fixture is judged by the others' rest.
        REQUIRE(!v.empty());
        const double rest = f[0] - (std::abs(v[0] - value) < 0.5 ? 0.9 : 0.0);
        rests.insert(std::round(rest * 1000.0));
        for (std::size_t i = 0; i < v.size(); ++i) {
            const bool lit = std::abs(v[i] - value) < 0.5;
            CHECK(std::abs(f[i] - (rest + (lit ? 0.9 : 0.0))) < 1e-3);
        }
    }
    CHECK(values == std::set<int>{1, 2, 3, 4});
    CHECK(rests.size() == 1);
}

TEST_CASE("Audio Response Lab: an absent source reads as absent, never as a level", "[audiolab][stems][adr1720]") {
    const testsupport::StemFixture& fx = testsupport::requireStemFixture();
    // The mix alone: no stem is configured, so no stem.* signal exists on the bus.
    app::Engine engine(app::EngineMode::Offline);
    engine.setLiveControl(false);
    REQUIRE(engine.loadProject(fixtureProject(fx, false, "mix-only")).has_value());
    FixedStepClock clock(30.0);
    for (int i = 0; i < 60; ++i) {
        engine.update(engine.tick(clock));
    }
    for (const char* key : {"drums", "bass", "synth", "pad"}) {
        INFO(key);
        // The level meter stays at its authored empty height, the onset lamp at rest, and the ACTIVE lamp hidden so
        // the red no-data lamp behind it shows.
        auto* fill = engine.params().find(std::string("procedural/s") + key + "LevelFill/transform/scale");
        REQUIRE(fill != nullptr);
        CHECK_THAT(fill->finalComponent(1), Catch::Matchers::WithinAbs(0.001, 1e-6));
        auto* active = engine.params().find(std::string("procedural/s") + key + "Active/visible");
        REQUIRE(active != nullptr);
        CHECK(active->finalComponent(0) == 0.0f);
    }
}
