// Rainbow World 2 (docs/prototypes/rainbow-world/16-effect-mapping.md): the committed live project and the offline
// version (29-brief-feline-video.md), checked as files -- the scenes they name are generated and gitignored, so
// nothing here needs them.
//
//   * every stem route is optional, and every main-mix fallback stands aside while its stem is active (ADR-1570), so
//     the live scene plays on a stereo input and follows stems wherever they exist;
//   * the live project carries no audio file and no stems; the offline version carries a mix (and no stems: it is
//     song-agnostic), its own scene, no live ceilings, and the live routes plus the world distortion's;
//   * the MIDI layout is the house one (CC 9 look, CC 10 look lock) plus the world's controls, unchanged;
//   * every route parses.

#include "params/modulation.hpp"
#include "params/serialization.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using nlohmann::json;

namespace {

json load(const char* name) {
    const std::filesystem::path p = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "rainbow-world" / name;
    std::ifstream in(p);
    REQUIRE(in.good());
    return json::parse(in);
}

} // namespace

TEST_CASE("Rainbow World: stem routes are optional and each fallback stands aside for its stem",
          "[examples][rainbow-world][adr1570]") {
    for (const char* file : {"rainbow-world.json", "rainbow-world-offline.json"}) {
        INFO(file);
        const json p = load(file);
        int stemRoutes = 0;
        int fallbacks = 0;
        std::set<std::string> stemsRouted;
        for (const json& r : p.at("routes")) {
            const auto parsed = avgen::params::routeFromJson(r);
            REQUIRE(parsed.has_value());
            const std::string source = r.at("source");
            if (source.starts_with("stem.")) {
                ++stemRoutes;
                CHECK(parsed->optional);
                stemsRouted.insert(source.substr(5, source.find('.', 5) - 5));
            }
            if (parsed->depthSource.starts_with("stem.") && parsed->depthSource.ends_with(".active")) {
                ++fallbacks;
                CHECK_FALSE(source.starts_with("stem."));
                REQUIRE(parsed->depthDefault.has_value());
                CHECK(*parsed->depthDefault == 0.0f); // no such stem: full depth
                CHECK(parsed->depthMin == 1.0f);       // the stem inactive: full depth
                CHECK(parsed->depthMax == 0.0f);       // the stem active: the fallback stands aside
            }
        }
        CHECK(stemRoutes >= 10);
        CHECK(fallbacks >= 8);
        CHECK(stemsRouted == std::set<std::string>{"drums", "bass", "synth"});
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
    // the sources: the live project's, with the macro bank's `distort` knob
    REQUIRE(off.at("sources").size() == live.at("sources").size());
    for (std::size_t i = 0; i < live.at("sources").size(); ++i) {
        const json& a = live.at("sources")[i];
        json b = off.at("sources")[i];
        if (a.value("name", "") == "macros") {
            json knobs = json::array();
            bool hasDistort = false;
            for (const json& k : b.at("settings").at("knobs")) {
                if (k.at("name") == "distort") {
                    hasDistort = true;
                    CHECK(k.at("default").get<double>() == 0.0);
                } else {
                    knobs.push_back(k);
                }
            }
            CHECK(hasDistort);
            b["settings"]["knobs"] = knobs;
        }
        CHECK(b == a);
    }
}

TEST_CASE("Rainbow World: the live scan latches on a swell, on the synth stem or the mix, never re-firing within 12 s",
          "[examples][rainbow-world][adr1574][adr1575]") {
    const json live = load("rainbow-world.json");
    std::set<std::string> sources;
    int latched = 0;
    for (const json& r : live.at("routes")) {
        if (r.at("target") != "macros/scan") {
            continue;
        }
        sources.insert(r.at("source").get<std::string>());
        const json& c = r.at("chain");
        CHECK(c.at("envelopeRefractoryMs").get<double>() >= 12000.0);
        CHECK(c.at("trendSeconds").get<double>() > 0.0); // ADR-1575: a swell over the bed, not a level
        CHECK(c.contains("envelopeRearm"));
        CHECK_FALSE((r.contains("depthSource") && r.at("depthSource") == "stem.synth.level")); // the gate-4 tail glitch
        ++latched;
    }
    CHECK(latched == 2);
    CHECK(sources == std::set<std::string>{"stem.synth.rms", "response.sustain"});
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
    // Every firework and twirl effector is enabled, and the sparks and flashes drawn, only while macro.scan is up.
    for (const char* file : {"rainbow-world.json", "rainbow-world-offline.json"}) {
        INFO(file);
        const json p = load(file);
        int effectors = 0;
        int nodes = 0;
        for (const json& r : p.at("routes")) {
            const std::string target = r.at("target");
            if (r.at("source") != "macro.scan" || !(target.ends_with("/enabled") || target.ends_with("/visible"))) {
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
          "[examples][rainbow-world]") {
    // 29 section 9 ("the ground lights look completely off time"): the drum stem's low band peaked ~140 ms after the
    // kick; the phase lands within a frame of it.
    const json live = load("rainbow-world.json");
    int kick = 0;
    for (const json& r : live.at("routes")) {
        const std::string target = r.at("target");
        if (target.find("rw_grass") == std::string::npos || r.at("source") != "beat.phase") {
            continue;
        }
        // 29 section 10: a square flash on the beat -- on as the phase turns over, held an eighth of a beat, gone
        CHECK(r.at("chain").at("threshold") == "binary");
        CHECK(r.at("chain").at("attackMs").get<double>() == 0.0);
        CHECK(r.at("chain").at("decayMs").get<double>() < 100.0);
        ++kick;
    }
    CHECK(kick == 2); // with the drum stem (its level the depth) and without (the fallback)
}
