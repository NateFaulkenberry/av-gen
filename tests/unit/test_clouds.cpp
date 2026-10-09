// ADR-1289: the cloud block's CPU half -- lobes from a file or inline, a mass filter, refusals that name the key, and a
// round trip that keeps the file reference.

#include "scene/clouds.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace avgen;
using json = nlohmann::json;
namespace fs = std::filesystem;

TEST_CASE("clouds read their lobes inline or from a file, filtered by mass", "[unit][clouds][adr1289]") {
    const json inline_ = {{"lobes", json::array({{{"c", {0, 1000, 5000}}, {"r", 300.0}},
                                                 {{"c", {200, 1100, 5100}}, {"r", 150.0}, {"scale", {1, 0.5, 1}},
                                                  {"density", 0.8}, {"soft", 0.4}}})},
                          {"lightDirection", {0, 2, 0}}};
    auto c = scene::Clouds::fromJson(inline_, fs::temp_directory_path());
    REQUIRE(c.has_value());
    CHECK(c->active());
    REQUIRE(c->lobes.size() == 2);
    CHECK(c->lobes[1].scale == glm::vec3(1.0f, 0.5f, 1.0f));
    CHECK(c->lobes[1].density == 0.8f);
    CHECK(c->lightDirection == glm::vec3(0.0f, 1.0f, 0.0f)); // normalised on load

    const fs::path dir = fs::temp_directory_path() / "avgen-clouds-test";
    fs::create_directories(dir);
    std::ofstream(dir / "sky.lobes.json") << json{{"masses",
                                                   {{"a", json::array({{{"c", {0, 0, 0}}, {"r", 10.0}}})},
                                                    {"b", json::array({{{"c", {1, 0, 0}}, {"r", 20.0}},
                                                                       {{"c", {2, 0, 0}}, {"r", 30.0}}})}}}}
                                                    .dump();
    auto all = scene::Clouds::fromJson({{"lobes", "sky.lobes.json"}}, dir);
    REQUIRE(all.has_value());
    CHECK(all->lobes.size() == 3);
    auto onlyB = scene::Clouds::fromJson({{"lobes", "sky.lobes.json"}, {"masses", {"b"}}}, dir);
    REQUIRE(onlyB.has_value());
    CHECK(onlyB->lobes.size() == 2);
    // the round trip keeps the reference, not a copy of the lobes
    const json out = onlyB->toJson();
    CHECK(out.at("lobes") == "sky.lobes.json");
    auto again = scene::Clouds::fromJson(out, dir);
    REQUIRE(again.has_value());
    CHECK(again->lobes.size() == 2);
}

TEST_CASE("clouds refuse nonsense and say what", "[unit][clouds][adr1289]") {
    const fs::path dir = fs::temp_directory_path();
    struct Bad {
        json j;
        const char* mentions;
    };
    const json lobe = {{"c", {0, 0, 0}}, {"r", 10.0}};
    const Bad bad[] = {
        {json::object(), "lobes"},
        {{{"lobes", json::array({{{"c", {0, 0, 0}}, {"r", -1.0}}})}}, "radius"},
        {{{"lobes", json::array({{{"c", {0, 0}}, {"r", 1.0}}})}}, "'c'"},
        {{{"lobes", "nowhere.lobes.json"}}, "cannot open"},
        {{{"lobes", json::array({lobe})}, {"steps", 2}}, "steps"},
        {{{"lobes", json::array({lobe})}, {"noiseAmount", 2.0}}, "noiseAmount"},
        {{{"lobes", json::array({lobe})}, {"lightDirection", {0, 0, 0}}}, "lightDirection"},
    };
    for (const Bad& b : bad) {
        INFO(b.j.dump());
        auto c = scene::Clouds::fromJson(b.j, dir);
        REQUIRE_FALSE(c.has_value());
        INFO(c.error().message);
        CHECK(c.error().message.find(b.mentions) != std::string::npos);
    }
}
