// Rainbow World 2 (docs/prototypes/rainbow-world/16-effect-mapping.md): the committed live project and the offline
// version (29-brief-feline-video.md), checked as files -- the scenes they name are generated and gitignored, so
// nothing here needs them.
//
//   * every stem feature is read through a conditioned signal (ADR-1602) whose main-mix fallback stands aside while
//     its stem is active (firstActive), so the live scene plays on a stereo input and follows stems wherever they
//     exist; no route reads a stem, the mix's response or a macro the signals replaced;
//   * the live project carries no audio file and no stems; the offline version carries a mix (and no stems: it is
//     song-agnostic), its own scene, no live ceilings, and the live routes plus the world distortion's;
//   * the MIDI layout is the house one (CC 9 look, CC 10 look lock) plus the world's controls, unchanged;
//   * every route parses.

#include "params/modulation.hpp"
#include "params/serialization.hpp"
#include "signals/conditioned_signal.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using nlohmann::json;

namespace {

json load(const char* name) {
    const std::filesystem::path p = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "rainbow-world" / name;
    std::ifstream in(p);
    REQUIRE(in.good());
    return json::parse(in);
}

// A conditioned signal's settings (ADR-1602), by name.
json signalSettings(const json& project, const std::string& name) {
    for (const json& s : project.at("sources")) {
        if (s.at("kind") == "signal" && s.at("name") == name) {
            return s.at("settings");
        }
    }
    FAIL("no signal '" << name << "'");
    return {};
}

} // namespace

TEST_CASE("Rainbow World: every stem feature is a signal whose mix fallback stands aside for its stem",
          "[examples][rainbow-world][adr1570][adr1602]") {
    for (const char* file : {"rainbow-world.json", "rainbow-world-offline.json"}) {
        INFO(file);
        const json p = load(file);
        // No route reads a stem, the mix's response or a knob the signals replaced: the chains live in the signals.
        for (const json& r : p.at("routes")) {
            REQUIRE(avgen::params::routeFromJson(r).has_value());
            const std::string source = r.at("source");
            INFO(source);
            CHECK_FALSE(source.starts_with("stem."));
            CHECK_FALSE(source.starts_with("response."));
            CHECK_FALSE(source.starts_with("audio."));
            CHECK((!source.starts_with("macro.") || source == "macro.distort"));
        }
        int signals = 0;
        int twins = 0;
        std::set<std::string> stems;
        for (const json& src : p.at("sources")) {
            if (src.at("kind") != "signal") {
                continue;
            }
            ++signals;
            avgen::signals::ConditionedSignalSource parsed(src.at("name"));
            REQUIRE(parsed.settingsFromJson(src.at("settings")).has_value());
            const auto& in = parsed.inputs();
            // a stem twin: the stem first, the main mix after it, chosen by the stem's activity (ADR-1570's fallback)
            if (parsed.combine() == avgen::signals::ConditionedSignalSource::Combine::FirstActive && in.size() == 2 &&
                (in[0].source.starts_with("stem.") || in[0].gate.starts_with("stem."))) {
                ++twins;
                CHECK_FALSE(in[1].source.starts_with("stem."));
                CHECK(in[1].gate.empty());
                const std::string stem = in[0].gate.empty() ? in[0].source : in[0].gate;
                stems.insert(stem.substr(5, stem.find('.', 5) - 5));
            }
        }
        CHECK(signals == 14);
        CHECK(twins >= 9);
        CHECK(stems == std::set<std::string>{"drums", "bass", "synth"});
    }
}

TEST_CASE("Rainbow World: the live project has no audio; the offline version adds a mix, its scene and the distortion",
          "[examples][rainbow-world]") {
    const json live = load("rainbow-world.json");
    const json off = load("rainbow-world-offline.json");
    CHECK_FALSE(live.at("assets").contains("audio"));
    CHECK_FALSE(live.at("assets").contains("stems"));
    CHECK(live.at("sonic").at("live") == true);
    REQUIRE(live.contains("live")); // the live ceilings
    // the offline version: a file to play, song-agnostic (no stems, no baked timing), full quality
    REQUIRE(off.at("assets").contains("audio"));
    CHECK_FALSE(off.at("assets").contains("stems"));
    CHECK(off.at("sonic").at("live") == false);
    CHECK_FALSE(off.contains("live"));
    CHECK(off.at("assets").at("scene").at("path").get<std::string>().ends_with("rainbow-world-offline.scene.json"));
    CHECK(live.at("assets").at("scene").at("path").get<std::string>().ends_with("rainbow-world.scene.json"));
    CHECK(off.at("control") == live.at("control"));
    CHECK(off.at("parameters") == live.at("parameters"));
    CHECK(off.at("palette") == live.at("palette"));
    // The routes: the live project's, the same scans (the latched live trigger, no baked timeline), plus the world
    // distortion's (`macro.distort` onto the offline scene's deformers, which rest at 0).
    json offRest = json::array();
    int distort = 0;
    for (const json& r : off.at("routes")) {
        if (r.at("source") == "macro.distort") {
            CHECK(r.at("target").get<std::string>().find("/deform/") != std::string::npos);
            ++distort;
        } else {
            offRest.push_back(r);
        }
        CHECK_FALSE(r.at("source").get<std::string>().starts_with("timeline.scan"));
    }
    CHECK(offRest == live.at("routes"));
    CHECK(distort >= 3);
    // the sources: the live project's, then a macro bank holding only the `distort` knob (the live project has none:
    // its knobs became signals, ADR-1602)
    REQUIRE(off.at("sources").size() == live.at("sources").size() + 1);
    for (std::size_t i = 0; i < live.at("sources").size(); ++i) {
        CHECK(off.at("sources")[i] == live.at("sources")[i]);
        CHECK(live.at("sources")[i].at("kind") != "macro");
    }
    const json& bank = off.at("sources").back();
    CHECK(bank.at("kind") == "macro");
    REQUIRE(bank.at("settings").at("knobs").size() == 1);
    CHECK(bank.at("settings").at("knobs")[0].at("name") == "distort");
    CHECK(bank.at("settings").at("knobs")[0].at("default").get<double>() == 0.0);
}

TEST_CASE("Rainbow World: the live scan latches on a swell, on the synth stem or the mix, never re-firing within 12 s",
          "[examples][rainbow-world][adr1574][adr1575][adr1602]") {
    const json live = load("rainbow-world.json");
    for (const char* name : {"scan", "scanFront"}) {
        INFO(name);
        const json settings = signalSettings(live, name);
        CHECK(settings.value("combine", "firstActive") == "firstActive");
        std::vector<std::string> sources;
        for (const json& in : settings.at("inputs")) {
            sources.push_back(in.at("source"));
            const json& c = in.at("chain");
            CHECK(c.at("envelopeRefractoryMs").get<double>() >= 12000.0);
            CHECK(c.at("trendSeconds").get<double>() > 0.0); // ADR-1575: a swell over the bed, not a level
            CHECK(c.contains("envelopeRearm"));
            CHECK_FALSE((in.contains("depthSource") && in.at("depthSource") == "stem.synth.level")); // the gate-4 glitch
        }
        // the stem while the synth stem is active (its own .active), else the mix
        CHECK(sources == std::vector<std::string>{"stem.synth.rms", "response.sustain"});
    }
}

TEST_CASE("Rainbow World: the MIDI layout keeps CC 9 look and CC 10 look lock", "[examples][rainbow-world]") {
    const json live = load("rainbow-world.json");
    std::set<std::pair<int, std::string>> ccs;
    for (const json& b : live.at("control").at("midi").at("bindings")) {
        ccs.insert({b.at("number").get<int>(), b.at("signal").get<std::string>()});
    }
    const std::set<std::pair<int, std::string>> expected{{1, "glow"},   {2, "lens"},    {5, "haze"},      {6, "light"},
                                                         {7, "colour"}, {9, "palette"}, {10, "paletteLock"}};
    CHECK(ccs == expected);
}

TEST_CASE("Rainbow World: the trees' twirl and fireworks are gated by the scan, so a world at rest is as built",
          "[examples][rainbow-world]") {
    // 29 section 9: at rest the front sits 980 m round the moving camera, and trees 650-980 m out were caught part-way
    // through a stage (a frozen burst, a part-turned tree) and played their twirl and fireworks as the camera flew.
    // Every firework and twirl effector is enabled, and the sparks and flashes drawn, only while signal.scan is up.
    for (const char* file : {"rainbow-world.json", "rainbow-world-offline.json"}) {
        INFO(file);
        const json p = load(file);
        int effectors = 0;
        int nodes = 0;
        for (const json& r : p.at("routes")) {
            const std::string target = r.at("target");
            if (r.at("source") != "signal.scan" || !(target.ends_with("/enabled") || target.ends_with("/visible"))) {
                continue;
            }
            CHECK(r.at("op") == "replace");
            CHECK(r.at("chain").at("threshold") == "binary");
            CHECK(r.at("chain").at("thresholdLevel").get<double>() > 0.0);
            if (target.find("/effector/") != std::string::npos) {
                ++effectors;
            } else {
                CHECK(target.find("/fw_") != std::string::npos);
                ++nodes;
            }
        }
        CHECK(effectors >= 48); // four families x three parts x the twirl, glow and spin effectors
        CHECK(nodes == 8);      // a spark pool and a flash per family
    }
}

TEST_CASE("Rainbow World: the grass's kick pops ride the beat clock's phase, not the drum stem's late band",
          "[examples][rainbow-world][adr1602]") {
    // 29 section 9 ("the ground lights look completely off time"): the drum stem's low band peaked ~140 ms after the
    // kick; the phase lands within a frame of it.
    const json live = load("rainbow-world.json");
    const json kick = signalSettings(live, "kick");
    REQUIRE(kick.at("inputs").size() == 2); // with the drum stem (its level the depth) and without (the fallback)
    for (const json& in : kick.at("inputs")) {
        CHECK(in.at("source") == "beat.phase");
        // 29 section 10: a square flash on the beat -- on as the phase turns over, held an eighth of a beat, gone
        CHECK(in.at("chain").at("threshold") == "binary");
        CHECK(in.at("chain").at("attackMs").get<double>() == 0.0);
        CHECK(in.at("chain").at("decayMs").get<double>() < 100.0);
    }
    CHECK(kick.at("inputs")[0].at("gate") == "stem.drums.active");
    CHECK(kick.at("inputs")[0].at("depthSource") == "stem.drums.level");
    int routes = 0;
    for (const json& r : live.at("routes")) {
        if (r.at("source") == "signal.kick") {
            CHECK(r.at("target").get<std::string>().find("rw_grass") != std::string::npos);
            ++routes;
        }
    }
    CHECK(routes == 1);
}
