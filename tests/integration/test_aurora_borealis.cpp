// Aurora Borealis (docs/prototypes/aurora-borealis/): the committed live projects, checked as files, and their signals
// checked against the songs' stems (pass 3: 21-research-pass3-audio.md, 23-plan-pass3.md). The scene they name is
// generated and gitignored (examples/aurora-borealis/build.py); nothing here needs it.
//
//   * every musical input is a conditioned signal (ADR-1602); routes read only signals and the two subset sources
//     (lfo, random), never a stem, the mix's response or a macro;
//   * Sabertooth (the default) reads its nine stems BY KEY, the beat from the Kick stem; the seven part knobs rest at
//     1; the aurora's own mix response is off and only Cloud Diving (and a crash's glow) reaches it;
//   * Feline Footwear (the second project) keeps its three role stems and drum feeds;
//   * with the Sabertooth stems (skipped where they are absent): every transient detector reproduces research A's
//     event list (12 crashes, 121 kicks, 48 snares, 28 tom notes, 176 tone-block accents) and the Moog's onsets;
//     at most two light balls are alive; the kicks sit on the 85.49 BPM grid with no drift.
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

fs::path projectPath(const char* name = "aurora-borealis.json") {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "aurora-borealis" / name;
}

json loadProject(const char* name = "aurora-borealis.json") {
    std::ifstream in(projectPath(name));
    REQUIRE(in.good());
    return json::parse(in);
}

const char* const kFeline = "aurora-borealis-feline.json";

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

// The project's instrument (sources, stems, feeds) on an orb, traced at `fps`: no generated scene needed. The trace
// samples after a frame's events are cleared, so each event signal is also read through a one-frame latch,
// `signal.probe_<name>`.
Trace traceInstrument(json p, const fs::path& dir, double fps) {
    const fs::path base = projectPath().parent_path();
    p["assets"]["scene"] = {{"kind", "orb"}};
    p["assets"]["audio"] = (base / p["assets"]["audio"].get<std::string>()).lexically_normal().string();
    for (json& s : p["assets"]["stems"]) {
        s["file"] = (base / s.at("file").get<std::string>()).lexically_normal().string();
    }
    p.erase("palette");
    p.erase("effects");
    p["routes"] = json::array();
    p["timeline"]["tracks"] = json::array();
    std::vector<std::string> events;
    for (const json& s : p["sources"]) {
        if (s.at("kind") == "signal" && s.at("settings").value("event", false)) {
            events.push_back(s.at("name"));
        }
    }
    for (const std::string& e : events) {
        p["sources"].push_back({{"kind", "signal"},
                                {"name", "probe_" + e},
                                {"settings",
                                 {{"inputs",
                                   json::array({{{"source", "signal." + e},
                                                 {"chain",
                                                  {{"attackMs", 0.0},
                                                   {"envelope", "linearfall"},
                                                   {"envelopeFallPerSecond", 1000.0}}}}})}}}});
    }
    fs::create_directories(dir);
    std::ofstream(dir / "project.json") << p.dump(1);
    REQUIRE(avgen::app::runSonicTraceCommand(dir / "project.json", dir / "trace.csv", fps) == 0);
    return readTrace(dir / "trace.csv");
}

bool stemsPresent(const json& p) {
    const fs::path base = projectPath().parent_path();
    if (!fs::exists(base / p["assets"]["audio"].get<std::string>())) {
        return false;
    }
    return std::all_of(p["assets"]["stems"].begin(), p["assets"]["stems"].end(),
                       [&](const json& s) { return fs::exists(base / s.at("file").get<std::string>()); });
}

// Each reference event matched by one fire within `tol` seconds, and no fire without a reference event.
void checkEvents(const std::vector<double>& fires, const std::vector<double>& ref, double tol) {
    INFO(fires.size() << " fires against " << ref.size() << " reference events");
    CHECK(fires.size() == ref.size());
    for (double r : ref) {
        INFO("reference event at " << r);
        CHECK(std::any_of(fires.begin(), fires.end(), [&](double f) { return std::abs(f - r) <= tol; }));
    }
    for (double f : fires) {
        INFO("fire at " << f);
        CHECK(std::any_of(ref.begin(), ref.end(), [&](double r) { return std::abs(f - r) <= tol; }));
    }
}

} // namespace

TEST_CASE("Aurora Borealis: every musical input is a conditioned signal; routes carry amounts",
          "[examples][aurora-borealis][adr1602]") {
    for (const char* name : {"aurora-borealis.json", kFeline}) {
        INFO(name);
        const json p = loadProject(name);
        CHECK(p.at("sonic").at("live") == true);
        std::set<std::string> names;
        std::set<std::string> subsets;
        for (const json& src : p.at("sources")) {
            const std::string kind = src.at("kind");
            if (kind == "lfo" || kind == "random") { // the glints' subset selectors (pure in time / per trigger)
                subsets.insert(kind + "." + src.at("name").get<std::string>());
                continue;
            }
            REQUIRE(kind == "signal");
            avgen::signals::ConditionedSignalSource parsed(src.at("name"));
            REQUIRE(parsed.settingsFromJson(src.at("settings")).has_value());
            for (const auto& in : parsed.inputs()) {
                INFO(src.at("name").get<std::string>() << " <- " << in.source);
                CHECK_FALSE(in.source.starts_with("macro.")); // a frame late
                // Sabertooth: no drum class (late and wrong on single-instrument stems, 21 s2.4); each stem's own onset
                const bool classOk = std::string(name) == kFeline || !in.source.starts_with("role.");
                CHECK(classOk);
                if (in.source.starts_with("signal.")) {
                    CHECK(names.contains(in.source.substr(7))); // listed after what it reads (same frame)
                }
            }
            names.insert(src.at("name").get<std::string>());
        }
        for (const json& r : p.at("routes")) {
            REQUIRE(avgen::params::routeFromJson(r).has_value());
            const std::string source = r.at("source");
            INFO(source);
            CHECK((source.starts_with("signal.") || subsets.contains(source)));
            CHECK_FALSE(r.contains("chain"));
        }
        for (const char* n : {"cloudSwell", "cloudHue", "crashStarEv", "ballAEv", "ballBEv", "kickBeamEv",
                              "snareBeamEv", "moogNote", "moogGlow", "tomHit", "tomHitEv", "tomBody", "blockTick",
                              "auroraLight"}) {
            INFO(n);
            CHECK(names.contains(n));
        }
    }
}

TEST_CASE("Aurora Borealis: Sabertooth's stems by key, the Kick beat feed, part knobs at rest, the aurora is Cloud "
          "Diving's",
          "[examples][aurora-borealis][adr1630][adr1612][adr939]") {
    const json p = loadProject();
    std::map<std::string, std::string> roles;
    for (const json& s : p.at("assets").at("stems")) {
        roles[s.at("key")] = s.at("role");
        CHECK(s.at("file").get<std::string>().find("stems/sabertooth/") != std::string::npos);
    }
    CHECK(roles == std::map<std::string, std::string>{{"kick", "drums"}, {"snare", "other"}, {"toms", "other"},
                                                      {"crash", "other"}, {"hh", "other"}, {"bass", "bass"},
                                                      {"cloud", "synth"}, {"moog", "other"}, {"toneblock", "other"}});
    CHECK(p.at("assets").at("audio").get<std::string>().ends_with("sabertooth/Sabertooth.wav"));
    CHECK(p.at("feeds") == json{{"beat", "stem.kick"}});
    std::set<std::string> parts;
    for (const json& c : p.at("control").at("channels")) {
        CHECK(c.at("rest").get<double>() == 1.0);
        parts.insert(c.at("name").get<std::string>());
    }
    CHECK(parts == std::set<std::string>{"cloud", "crash", "moog", "kick", "snare", "toms", "toneblock"});
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
    for (const json& r : p.at("routes")) {
        const std::string target = r.at("target");
        INFO(target);
        // E4: the aurora's speeds are never routed (the flow is elapsed x speed, so a routed speed jumps the
        // pattern); nor its quality (the live ladder owns ribbonSteps) or fold size (it swims far sheets)
        CHECK(target.find("Speed") == std::string::npos);
        CHECK(target.find("ribbonSteps") == std::string::npos);
        CHECK(target.find("ribbonScale") == std::string::npos);
        // the brief s4: other stems do not take over the aurora
        if (target.starts_with("fx/aurora/") || target == "palette/position") {
            const std::string source = r.at("source");
            CHECK((source.starts_with("signal.cloud") || source == "signal.crashGlow")); // Cloud Diving's, and a crash
        }
    }
    // Feline Footwear stays available: its three stems by role and the drum feeds
    const json f = loadProject(kFeline);
    std::set<std::string> fr;
    for (const json& s : f.at("assets").at("stems")) {
        fr.insert(s.at("role").get<std::string>());
        CHECK(s.at("file").get<std::string>().find("feline-footwear (") != std::string::npos);
    }
    CHECK(fr == std::set<std::string>{"drums", "bass", "synth"});
    CHECK(f.at("feeds").at("beat") == "role.drums");
    CHECK(f.at("effects") == p.at("effects")); // one scene, two songs

    // pass 3's effects: the river beams are ADR-1662 path waves on the water's own course, each on its own refractory
    // event and fading on its own envelope; the stars are two fast comets (a pool of two); the pass-2 perpendicular
    // pulses and slow comet are gone
    std::map<std::string, json> fx;
    for (const json& e : p.at("effects")) {
        fx[e.at("id")] = e;
    }
    CHECK_FALSE(fx.contains("riverPulseA"));
    CHECK_FALSE(fx.contains("comet"));
    for (const auto& [id, event] : std::map<std::string, std::string>{{"riverKick", "signal.kickBeamEv"},
                                                                      {"riverSnare", "signal.snareBeamEv"},
                                                                      {"riverToms", "signal.tomHitEv"}}) {
        INFO(id);
        REQUIRE(fx.contains(id));
        const json& e = fx.at(id);
        CHECK(e.at("trigger").at("name") == event);
        CHECK(e.at("trigger").at("threshold").get<double>() == 0.0); // an onset's strength is 0.05-1: take every one
        CHECK(e.at("timing").at("lifetime").get<double>() > 0.0);    // ADR-981: each hit its own fading front
        const json& prop = e.at("parameters").at("propagation");
        CHECK(prop.at("kind") == "path");
        CHECK(prop.at("path").at("node") == "river");
        CHECK(prop.at("path").at("feature") == "river");
        CHECK(prop.at("path").at("disturb").get<double>() > 0.0);    // the owner: the ripples are required
    }
    // one core each (the owner, pass 4: the snare's twin "looks like two beams"), told apart by colour, lane and timing
    for (const char* id : {"riverKick", "riverSnare", "riverToms"}) {
        CHECK(fx.at(id).at("parameters").at("propagation").at("path").at("coreOffset").get<double>() == 0.0);
    }
    // the lanes: kick left of the centre, snare right, toms jittered across the width (pass 4)
    CHECK(fx.at("riverKick").at("parameters").at("propagation").at("path").at("lane").get<double>() < 0.0);
    CHECK(fx.at("riverSnare").at("parameters").at("propagation").at("path").at("lane").get<double>() > 0.0);
    CHECK(fx.at("riverToms").at("parameters").at("propagation").at("path").at("laneJitter").get<double>() > 0.0);
    // the light balls replace the stars (pass 4, ADR-1663)
    CHECK_FALSE(fx.contains("starA"));
    for (const char* id : {"ballA", "ballB"}) {
        INFO(id);
        REQUIRE(fx.contains(id));
        CHECK(fx.at(id).at("parameters").at("propagation").at("kind") == "ball");
        CHECK(fx.at(id).at("trigger").at("name") == std::string("signal.") + id + "Ev");
        CHECK(fx.at(id).at("timing").at("lifetime").get<double>() > 0.0);
    }
}

TEST_CASE("Aurora Borealis: the detectors on the Sabertooth stems reproduce research A's event lists",
          "[examples][aurora-borealis][stems][adr1602]") {
    const json p = loadProject();
    if (!stemsPresent(p)) {
        SKIP("the Sabertooth stems are local and never committed (assets/audio/stems/sabertooth/)");
    }
    const Trace t = traceInstrument(p, avgen::testsupport::processTempDir() / "aurora-borealis-sabertooth", 240.0);
    const auto ev = [&](const char* n) { return rises(t, t.col(std::string("signal.probe_") + n), 1e-6); };
    // 21 s3.1: the 12 crashes (a 300 ms refractory; the raw onset double-fires at 25.325)
    const std::vector<double> crashes{2.808,  23.158, 24.567, 25.262, 35.788, 36.504,
                                      45.625, 47.021, 47.725, 56.846, 58.254, 58.958};
    checkEvents(ev("crashStarEv"), crashes, 0.006);
    // the pool of two balls (pass 4): every crash gets one, in slot A when A's last is 1.3 s old, else in B
    const std::vector<double> a = ev("ballAEv");
    const std::vector<double> b = ev("ballBEv");
    CHECK(a.size() + b.size() == crashes.size());
    for (std::size_t i = 1; i < a.size(); ++i) {
        CHECK(a[i] - a[i - 1] >= 1.3);
    }
    for (double tb : b) {
        INFO("ball B at " << tb);
        CHECK(std::any_of(a.begin(), a.end(), [&](double ta) { return tb - ta > 0.0 && tb - ta < 1.3; }));
    }
    // 21 s3.2-3.4: counts with no extras (a 100 ms refractory on kick, snare and toms, 80 ms on the tone block)
    CHECK(ev("kickBeamEv").size() == 121);
    CHECK(ev("snareBeamEv").size() == 48);
    const std::vector<double> toms{1.046, 1.229, 1.571, 1.921, 2.446, 2.625, 24.033, 24.387, 24.558, 24.908,
                                   35.254, 35.608, 35.788, 36.142, 36.312, 41.750, 41.933, 45.967, 46.146, 46.500,
                                   46.837, 47.362, 47.542, 68.950, 69.304, 69.475, 69.825, 70.008};
    checkEvents(ev("tomHitEv"), toms, 0.006);
    CHECK(ev("blockTickEv").size() == 176);
    // the Moog (pass 4, the owner: every trill): every onset of its stem, none in its opening silence (it plays from
    // 3.54 s), with the 50 ms refractory: 346 on Sabertooth; each its own subset
    {
        const std::vector<double> trills = ev("moogNoteEv");
        INFO(trills.size() << " Moog onsets");
        CHECK(trills.size() >= 330);
        CHECK(trills.size() <= 360);
        REQUIRE_FALSE(trills.empty());
        CHECK(trills.front() > 3.5);
        for (std::size_t i = 1; i < trills.size(); ++i) {
            CHECK(trills[i] - trills[i - 1] >= 0.049);
        }
    }
    // the kicks on the Kick-fed grid (85.49 BPM, every kick on a 16th): a constant offset, no drift (21 s7 risk 2)
    {
        const double sixteenth = 60.0 / 85.49 / 4.0;
        std::vector<double> first;
        std::vector<double> second;
        for (double k : ev("kickBeamEv")) {
            const double r = k - std::round(k / sixteenth) * sixteenth;
            (k < 35.0 ? first : second).push_back(r);
        }
        const double m1 = median(first);
        const double m2 = median(second);
        INFO("kick offset from the 16th grid: " << m1 * 1000.0 << " / " << m2 * 1000.0 << " ms");
        CHECK(std::abs(m1) <= 0.012);
        CHECK(std::abs(m1 - m2) <= 0.003);
    }
    // the swell is smooth (the brief s4: no jitter): frame-to-frame steps at 30 fps inside the swells, p99 < 0.06
    // (A s3.6: 0.022 at 300/1200 ms), and the hue slower still
    for (const auto& [n, lim] : std::map<std::string, double>{{"signal.cloudSwell", 0.06}, {"signal.cloudHue", 0.02}}) {
        const std::size_t c = t.col(n);
        std::vector<double> steps;
        for (std::size_t i = 8; i < t.rows.size(); i += 8) { // 240 -> 30 fps
            steps.push_back(std::abs(t.rows[i][c] - t.rows[i - 8][c]));
        }
        std::sort(steps.begin(), steps.end());
        INFO(n);
        CHECK(steps[steps.size() * 99 / 100] < lim);
    }
}

TEST_CASE("Aurora Borealis: the Feline Footwear version's beams follow the drum classes",
          "[examples][aurora-borealis][stems][adr1602]") {
    const json p = loadProject(kFeline);
    if (!stemsPresent(p)) {
        SKIP("the Feline Footwear stems are local and never committed");
    }
    const Trace t = traceInstrument(p, avgen::testsupport::processTempDir() / "aurora-borealis-feline", 60.0);
    const std::vector<double> kicks = rises(t, t.col("signal.probe_kickBeamEv"), 1e-6);
    const std::vector<double> snares = rises(t, t.col("signal.probe_snareBeamEv"), 1e-6);
    CHECK(kicks.size() > 300);
    CHECK(snares.size() > 100);
    for (const auto* v : {&kicks, &snares}) {
        for (std::size_t i = 1; i < v->size(); ++i) {
            CHECK((*v)[i] - (*v)[i - 1] >= 0.099); // the 100 ms refractory
        }
    }
}
