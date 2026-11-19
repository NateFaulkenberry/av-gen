// ADR-1289: the cloud block's CPU half -- lobes from a file or inline, a mass filter, refusals that name the key, and a
// round trip that keeps the file reference.

#include "params/parameter_set.hpp"
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

// ADR-2100: the sea, the glows and the v2 settings read, refuse and round-trip; a sea alone is a cloud block.
TEST_CASE("clouds v2: a sea and glows read, round-trip, and a sea needs no lobes", "[unit][clouds][adr2100]") {
    const json j = {{"sea", {{"base", -400.0}, {"top", -50.0}, {"coverage", 0.7}, {"coverageScale", 0.0004}}},
                    {"glows", json::array({{{"position", {0, 10, -100}}, {"color", {1, 0, 0.5}}, {"radius", 150.0},
                                            {"intensity", 2.0}, {"name", "city"}}})},
                    {"drift", {3, 0, -1}}, {"resolution", 0.5}, {"temporal", 0.8}, {"swell", 0.2}};
    auto c = scene::Clouds::fromJson(j, fs::temp_directory_path());
    REQUIRE(c.has_value());
    CHECK(c->active()); // no lobes, but a sea
    CHECK(c->lobes.empty());
    CHECK(c->sea.enabled);
    CHECK(c->sea.coverage == 0.7f);
    REQUIRE(c->glows.size() == 1);
    CHECK(c->glows[0].radius == 150.0f);
    CHECK(c->glows[0].name == "city");
    CHECK(c->drift == glm::vec3(3.0f, 0.0f, -1.0f));
    auto again = scene::Clouds::fromJson(c->toJson(), fs::temp_directory_path());
    REQUIRE(again.has_value());
    CHECK(again->sea.top == -50.0f);
    CHECK(again->glows.size() == 1);
    CHECK(again->glows[0].color == glm::vec3(1.0f, 0.0f, 0.5f));
    CHECK(again->temporal == 0.8f);
    CHECK(again->swell == 0.2f);

    const json lobe = {{"c", {0, 0, 0}}, {"r", 10.0}};
    struct Bad {
        json j;
        const char* mentions;
    };
    json many = json::array();
    for (int i = 0; i < 17; ++i) many.push_back({{"position", {0, 0, i}}});
    const Bad bad[] = {
        {{{"sea", {{"base", 0.0}, {"top", -10.0}}}}, "sea.top"},
        {{{"sea", {{"coverage", 1.5}}}}, "coverage"},
        {{{"lobes", json::array({lobe})}, {"glows", many}}, "glows"},
        {{{"lobes", json::array({lobe})}, {"glows", json::array({{{"position", {0, 0, 0}}, {"radius", 0.0}}})}},
         "radius"},
        {{{"lobes", json::array({lobe})}, {"resolution", 0.1}}, "resolution"},
        {{{"lobes", json::array({lobe})}, {"temporal", 0.99}}, "temporal"},
        {{{"sea", {{"enabled", false}}}}, "lobes"},
    };
    for (const Bad& b : bad) {
        INFO(b.j.dump());
        auto r = scene::Clouds::fromJson(b.j, fs::temp_directory_path());
        REQUIRE_FALSE(r.has_value());
        INFO(r.error().message);
        CHECK(r.error().message.find(b.mentions) != std::string::npos);
    }
}

TEST_CASE("clouds v2: the registered knobs start at the block's values and reach the live block",
          "[unit][clouds][adr2100]") {
    scene::Clouds rest;
    rest.sea.enabled = true;
    rest.sea.coverage = 0.55f;
    rest.swell = 0.1f;
    rest.glows = {scene::CloudGlow{"a", {0, 0, 0}, {1, 0, 1}, 100.0f, 1.5f},
                  scene::CloudGlow{"b", {50, 0, 0}, {0, 0, 1}, 80.0f, 0.5f}};
    params::ParameterSet ps;
    const scene::CloudParameters p = scene::registerCloudParameters(ps, rest, "clouds/");
    REQUIRE(ps.findAs<float>("clouds/swell") != nullptr);
    REQUIRE(ps.findAs<float>("clouds/sea/coverage") != nullptr);
    REQUIRE(ps.findAs<float>("clouds/glow/1/intensity") != nullptr);
    REQUIRE(ps.findAs<glm::vec3>("clouds/glow/0/color") != nullptr);
    REQUIRE(ps.findAs<glm::vec3>("clouds/drift") != nullptr);
    CHECK(ps.findAs<float>("clouds/glow/1/intensity")->value() == 0.5f);

    // untouched, the live block is the authored one
    scene::Clouds live;
    scene::applyCloudParameters(p, rest, live);
    CHECK(live.swell == 0.1f);
    CHECK(live.sea.coverage == 0.55f);
    CHECK(live.glows[1].intensity == 0.5f);

    // a route's final lands on the live block, not on the authored one
    ps.findAs<float>("clouds/swell")->setFinalComponent(0, 0.8f);
    ps.findAs<float>("clouds/sea/coverage")->setFinalComponent(0, 0.9f);
    ps.findAs<float>("clouds/glow/1/intensity")->setFinalComponent(0, 4.0f);
    ps.findAs<glm::vec3>("clouds/glow/0/color")->setFinalComponent(1, 0.25f);
    ps.findAs<float>("clouds/lightIntensity")->setFinalComponent(0, 0.2f);
    scene::applyCloudParameters(p, rest, live);
    CHECK(live.swell == 0.8f);
    CHECK(live.sea.coverage == 0.9f);
    CHECK(live.glows[1].intensity == 4.0f);
    CHECK(live.glows[0].color.y == 0.25f);
    CHECK(live.lightIntensity == 0.2f);
    CHECK(rest.swell == 0.1f);
}
