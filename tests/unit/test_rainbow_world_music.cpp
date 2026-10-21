// Rainbow World 2 (docs/prototypes/rainbow-world/16-effect-mapping.md): the committed live project and the Feline
// Footwear showcase, checked as files -- the scene they name is generated and gitignored, so nothing here needs it.
//
//   * every stem route is optional, and every main-mix fallback stands aside while its stem is active (ADR-1570), so
//     the live scene plays on a stereo input and follows stems wherever they exist;
//   * the live project carries no audio file and no stems; the showcase carries the Feline mix and its three stems,
//     and otherwise the same routes;
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
    for (const char* file : {"rainbow-world.json", "rainbow-world-feline.json"}) {
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

TEST_CASE("Rainbow World: the live project has no audio; the showcase adds the mix, three stems and its baked scans",
          "[examples][rainbow-world]") {
    const json live = load("rainbow-world.json");
    const json show = load("rainbow-world-feline.json");
    CHECK_FALSE(live.at("assets").contains("audio"));
    CHECK_FALSE(live.at("assets").contains("stems"));
    CHECK(live.at("sonic").at("live") == true);
    REQUIRE(show.at("assets").contains("audio"));
    REQUIRE(show.at("assets").at("stems").size() == 3);
    std::set<std::string> keys;
    for (const json& s : show.at("assets").at("stems")) {
        keys.insert(s.at("key").get<std::string>());
        CHECK(s.at("file").get<std::string>().find("feline-footwear (") != std::string::npos);
        CHECK_FALSE(s.contains("offset")); // the stems are sample-aligned with the mix (rainbow-world/14 section 1)
    }
    CHECK(keys == std::set<std::string>{"drums", "bass", "synth"});
    CHECK(show.at("control") == live.at("control"));
    // The one difference (gate 5, 26-world-scan.md): the showcase's world scans are baked from the song -- two
    // timelines, `scan` and `scanFront` -- in place of the live trigger (the phrase pulse gated by the synth stem).
    // Everything else is the live project's.
    const auto isScan = [](const json& r) {
        const std::string target = r.at("target").get<std::string>();
        const std::string source = r.at("source").get<std::string>();
        return target == "macros/scan" ||
               ((source == "beat.phrasePulse" || source == "timeline.scanFront") &&
                (target.find("/remap/constant") != std::string::npos ||
                 (target.starts_with("field/scan") && target.ends_with("/radius"))));
    };
    json liveRest = json::array();
    json showRest = json::array();
    for (const json& r : live.at("routes")) {
        if (!isScan(r)) liveRest.push_back(r);
    }
    int baked = 0;
    for (const json& r : show.at("routes")) {
        if (!isScan(r)) {
            showRest.push_back(r);
        } else {
            CHECK(r.at("source").get<std::string>().starts_with("timeline.scan"));
            ++baked;
        }
    }
    CHECK(showRest == liveRest);
    CHECK(baked > 1);
    json showSources = json::array();
    std::set<std::string> scanTimelines;
    for (const json& src : show.at("sources")) {
        const std::string name = src.value("name", "");
        if (name == "scan" || name == "scanFront") {
            scanTimelines.insert(name);
        } else {
            showSources.push_back(src);
        }
    }
    CHECK(scanTimelines == std::set<std::string>{"scan", "scanFront"});
    CHECK(showSources == live.at("sources"));
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
