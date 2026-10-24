// Aurora Borealis (docs/prototypes/aurora-borealis/): the committed live project, checked as a file, and its signals
// checked against the Feline Footwear stems (02-research-signals.md s4.4, S1-S6). The scene it names is generated and
// gitignored (examples/aurora-borealis/build.py); nothing here needs it.
//
//   * S1: every musical input is a conditioned signal (ADR-1602); no route reads a stem, a role, the mix's response,
//     the raw analysis or a macro. Every route and signal parses.
//   * the stems are declared by role (drums, bass, synth), the beat and the hits are fed from the drum role, the three
//     part knobs rest at 1, and the aurora's own mix response is off (ADR-939: sensitivity and spectrum shape 0).
//   * with the stems (skipped where they are absent): the kick wave against the drum class (S2), no stuck accents
//     (S4), no over-triggering (S5), the context medians by section (S6).
#include "app/sonic_trace_cli.hpp"
#include "params/modulation.hpp"
#include "params/serialization.hpp"
#include "signals/conditioned_signal.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path projectPath() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "aurora-borealis" / "aurora-borealis.json";
}

json loadProject() {
    std::ifstream in(projectPath());
    REQUIRE(in.good());
    return json::parse(in);
}

struct Trace {
    std::vector<std::string> names;
    std::vector<std::vector<double>> rows;
    [[nodiscard]] std::size_t col(const std::string& n) const {
        const auto it = std::find(names.begin(), names.end(), n);
        REQUIRE(it != names.end());
        return static_cast<std::size_t>(it - names.begin());
    }
};

Trace readTrace(const fs::path& p) {
    Trace t;
    std::ifstream in(p);
    std::string line;
    REQUIRE(std::getline(in, line));
    {
        std::stringstream ss(line);
        for (std::string n; std::getline(ss, n, ',');) {
            t.names.push_back(n);
        }
    }
    while (std::getline(in, line)) {
        std::vector<double> row;
        row.reserve(t.names.size());
        std::stringstream ss(line);
        for (std::string c; std::getline(ss, c, ',');) {
            row.push_back(c.empty() ? 0.0 : std::stod(c));
        }
        t.rows.push_back(std::move(row));
    }
    return t;
}

// Times (s) at which column `c` crosses `level` upward.
std::vector<double> rises(const Trace& t, std::size_t c, double level) {
    std::vector<double> out;
    const std::size_t time = t.col("time");
    for (std::size_t i = 1; i < t.rows.size(); ++i) {
        if (t.rows[i][c] >= level && t.rows[i - 1][c] < level) {
            out.push_back(t.rows[i][time]);
        }
    }
    return out;
}

double median(std::vector<double> v) {
    REQUIRE_FALSE(v.empty());
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double sectionMedian(const Trace& t, const std::string& name, double from, double to) {
    const std::size_t c = t.col(name);
    const std::size_t time = t.col("time");
    std::vector<double> v;
    for (const auto& r : t.rows) {
        if (r[time] >= from && r[time] < to) {
            v.push_back(r[c]);
        }
    }
    return median(v);
}

} // namespace

TEST_CASE("Aurora Borealis: every musical input is a role-driven signal with a mix fallback",
          "[examples][aurora-borealis][adr1602][adr1630]") {
    const json p = loadProject();
    CHECK(p.at("sonic").at("live") == true);
    // S1: the chains live in the signals; routes carry amounts
    for (const json& r : p.at("routes")) {
        REQUIRE(avgen::params::routeFromJson(r).has_value());
        const std::string source = r.at("source");
        INFO(source);
        CHECK(source.starts_with("signal."));
        CHECK_FALSE(r.contains("chain"));
    }
    std::set<std::string> names;
    for (const json& src : p.at("sources")) {
        REQUIRE(src.at("kind") == "signal");
        avgen::signals::ConditionedSignalSource parsed(src.at("name"));
        REQUIRE(parsed.settingsFromJson(src.at("settings")).has_value());
        for (const auto& in : parsed.inputs()) {
            INFO(src.at("name").get<std::string>() << " <- " << in.source);
            CHECK_FALSE(in.source.starts_with("stem.")); // by role, never by key (ADR-1630)
            CHECK_FALSE(in.source.starts_with("macro."));
            if (in.source.starts_with("signal.")) {
                CHECK(names.contains(in.source.substr(7))); // listed after what it reads (same frame)
            }
        }
        names.insert(src.at("name").get<std::string>());
    }
    for (const char* n : {"arrangement", "synthBright", "synthBed", "hatDrive", "paletteAxis", "kickWave", "snareFold",
                          "bassHit", "bassBody", "swell", "auroraLight"}) {
        INFO(n);
        CHECK(names.contains(n));
    }
}

TEST_CASE("Aurora Borealis: the stems by role, drum feeds, part knobs at rest, no mix leak into the aurora",
          "[examples][aurora-borealis][adr1630][adr1612][adr939]") {
    const json p = loadProject();
    std::set<std::string> roles;
    for (const json& s : p.at("assets").at("stems")) {
        roles.insert(s.at("role").get<std::string>());
        CHECK(s.at("file").get<std::string>().find("feline-footwear (") != std::string::npos);
    }
    CHECK(roles == std::set<std::string>{"drums", "bass", "synth"});
    CHECK(p.at("feeds").at("beat") == "role.drums");
    CHECK(p.at("feeds").at("hits") == "role.drums");
    std::set<std::string> parts;
    for (const json& c : p.at("control").at("channels")) {
        CHECK(c.at("rest") == 1.0);
        parts.insert(c.at("name").get<std::string>());
    }
    CHECK(parts == std::set<std::string>{"drums", "bass", "synth"});
    bool found = false;
    for (const json& e : p.at("effects")) {
        if (e.at("type") != "aurora") {
            continue;
        }
        found = true;
        const json& par = e.at("parameters");
        CHECK(par.at("audio").at("sensitivity") == 0.0);
        CHECK(par.at("audio").at("spectrumShape") == 0.0);
        CHECK(par.at("shape").at("ribbons") == true);
        CHECK(par.at("shape").at("ribbonSteps").get<double>() >= 40.0); // 40-48 planes at Ultra (grain below)
    }
    CHECK(found);
    // E4: the aurora's speeds are never routed (the flow is elapsed x speed, so a routed speed jumps the pattern)
    for (const json& r : p.at("routes")) {
        const std::string target = r.at("target");
        CHECK(target.find("Speed") == std::string::npos);
        CHECK(target.find("ribbonSteps") == std::string::npos);
    }
}

TEST_CASE("Aurora Borealis: the signals on the Feline Footwear stems (S2, S4-S6)",
          "[examples][aurora-borealis][stems][adr1602]") {
    const fs::path root = fs::path(AVGEN_SOURCE_DIR) / "assets" / "audio";
    for (const char* f : {"feline-footwear.wav", "stems/feline-footwear (Drums).wav", "stems/feline-footwear (Bass).wav",
                          "stems/feline-footwear (Synth).wav"}) {
        if (!fs::exists(root / f)) {
            SKIP("the Feline Footwear stems are local and never committed: " << f);
        }
    }
    // The project's instrument (its sources, stems and feeds) on an orb: the trace needs no generated scene. The
    // trace samples after a frame's events are cleared, so the drum class is read through an envelope (0 attack).
    json p = loadProject();
    p["assets"]["scene"] = {{"kind", "orb"}};
    p["assets"]["audio"] = (root / "feline-footwear.wav").string();
    for (json& s : p["assets"]["stems"]) {
        s["file"] = (root / "stems" / fs::path(s.at("file").get<std::string>()).filename()).string();
    }
    p.erase("palette");
    p.erase("effects");
    p["routes"] = json::array();
    p["timeline"]["tracks"] = json::array();
    p["sources"].push_back({{"kind", "envelope"}, {"name", "eKick"}, {"settings", {{"trigger", "role.drums.kick"}}}});
    for (const auto& [k, v] : std::map<std::string, double>{
             {"attackMs", 0.0}, {"decayMs", 0.0}, {"sustain", 1.0}, {"holdMs", 0.0}, {"releaseMs", 60.0}}) {
        p["parameters"]["sources/eKick/" + k] = v;
    }
    const fs::path dir = avgen::testsupport::processTempDir() / "aurora-borealis-trace";
    fs::create_directories(dir);
    std::ofstream(dir / "project.json") << p.dump(1);
    REQUIRE(avgen::app::runSonicTraceCommand(dir / "project.json", dir / "trace.csv", 30.0) == 0);
    const Trace t = readTrace(dir / "trace.csv");

    // S2: the kick wave fires on the attack of the drum class's kicks: precise and nearly complete
    const std::vector<double> waves = rises(t, t.col("signal.kickWave"), 0.5);
    const std::vector<double> kicks = rises(t, t.col("env.eKick"), 0.5);
    REQUIRE(kicks.size() > 300);
    std::size_t matched = 0;
    for (double w : waves) {
        matched += std::any_of(kicks.begin(), kicks.end(), [w](double k) { return k - w >= -0.08 && k - w <= 0.1; })
                       ? 1
                       : 0;
    }
    std::size_t found = 0;
    for (double k : kicks) {
        found += std::any_of(waves.begin(), waves.end(), [k](double w) { return k - w >= -0.08 && k - w <= 0.1; })
                     ? 1
                     : 0;
    }
    const double precision = static_cast<double>(matched) / static_cast<double>(waves.size());
    const double recall = static_cast<double>(found) / static_cast<double>(kicks.size());
    INFO("kickWave: " << waves.size() << " fires, precision " << precision << ", recall " << recall);
    CHECK(precision >= 0.95);
    CHECK(recall >= 0.85);

    // S5: no over-triggering
    for (std::size_t i = 1; i < waves.size(); ++i) {
        CHECK(waves[i] - waves[i - 1] >= 0.19);
    }
    const std::vector<double> swells = rises(t, t.col("signal.swell"), 0.5);
    for (std::size_t i = 1; i < swells.size(); ++i) {
        CHECK(swells[i] - swells[i - 1] >= 5.9);
    }

    // S4: nothing sticks -- after the drums stop (183.3 s) the kick wave is gone, and every accent is back below 0.05
    // within a second of the song's end
    const std::size_t time = t.col("time");
    for (const auto& r : t.rows) {
        if (r[time] > 184.5 && r[time] < 210.0) {
            CHECK(r[t.col("signal.kickWave")] < 0.05);
            CHECK(r[t.col("signal.snareFold")] < 0.05);
        }
        if (r[time] > 212.0 && r[time] < 230.0) {
            for (const char* a : {"signal.bassHit", "signal.swell"}) {
                CHECK(r[t.col(a)] < 0.05);
            }
        }
    }

    // S6: the context reads the arrangement (02 s2.2's sections)
    CHECK(sectionMedian(t, "signal.arrangement", 47.9, 101.4) >= 0.85);   // groove A
    CHECK(sectionMedian(t, "signal.arrangement", 111.3, 151.0) >= 0.85);  // groove B
    CHECK(sectionMedian(t, "signal.arrangement", 0.2, 8.2) <= 0.45);      // intro
    CHECK(sectionMedian(t, "signal.arrangement", 194.6, 210.0) <= 0.45);  // synth alone
    CHECK(sectionMedian(t, "signal.synthBright", 151.0, 166.9) >= 0.8);   // the lift
    CHECK(sectionMedian(t, "signal.synthBright", 20.1, 39.9) <= 0.1);     // the calls
    // no flicker (12-research-pass2-signals.md s2.6): the brightness that moves the palette changes smoothly, frame
    // to frame, through groove B (it was p99 0.36 a frame with the raw rms as its depth)
    {
        const std::size_t c = t.col("signal.synthBright");
        std::vector<double> steps;
        for (std::size_t i = 1; i < t.rows.size(); ++i) {
            if (t.rows[i][time] >= 111.3 && t.rows[i][time] < 151.0) {
                steps.push_back(std::abs(t.rows[i][c] - t.rows[i - 1][c]));
            }
        }
        std::sort(steps.begin(), steps.end());
        REQUIRE_FALSE(steps.empty());
        CHECK(steps[steps.size() * 99 / 100] < 0.02);
    }
    // pass 2 (12 s6.2 point 7): the comet's five cues land on their frames, at most one alive
    {
        const std::vector<double> comets = rises(t, t.col("signal.comet"), 0.5);
        const std::vector<double> expected{48.27, 87.53, 114.6, 151.0, 185.33};
        std::vector<double> inSong;
        for (double c : comets) {
            if (c < 205.0) {
                inSong.push_back(c);
            }
        }
        REQUIRE(inSong.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            CHECK(std::abs(inSong[i] - expected[i]) <= 1.0 / 30.0 + 1e-6);
        }
        for (std::size_t i = 1; i < comets.size(); ++i) {
            CHECK(comets[i] - comets[i - 1] >= 8.0);
        }
    }
    // the river pulses: a ring of two, so never more than two fronts alive, each slot latched for its life
    {
        const std::size_t a = t.col("signal.pulseA");
        const std::size_t b = t.col("signal.pulseB");
        const std::vector<double> la = rises(t, a, 0.5);
        const std::vector<double> lb = rises(t, b, 0.5);
        CHECK(la.size() > 60);
        CHECK(lb.size() > 60);
        for (std::size_t i = 1; i < la.size(); ++i) {
            CHECK(la[i] - la[i - 1] >= 1.79);
        }
        for (std::size_t i = 1; i < lb.size(); ++i) {
            CHECK(lb[i] - lb[i - 1] >= 1.79);
        }
    }
    // the snare: attack 0 and a 150 ms refractory
    {
        const std::vector<double> sn = rises(t, t.col("signal.snareFold"), 0.3);
        for (std::size_t i = 1; i < sn.size(); ++i) {
            CHECK(sn[i] - sn[i - 1] >= 0.149);
        }
    }
    // the palette moves through its looks: low in the intro, high in the lift
    CHECK(sectionMedian(t, "signal.paletteAxis", 0.2, 8.2) < 0.3);
    CHECK(sectionMedian(t, "signal.paletteAxis", 151.0, 166.9) > 0.85);
}
