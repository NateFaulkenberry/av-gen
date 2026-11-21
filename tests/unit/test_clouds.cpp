// ADR-1289: the cloud block's CPU half -- lobes from a file or inline, a mass filter, refusals that name the key, and a
// round trip that keeps the file reference.

#include "params/parameter_set.hpp"
#include "scene/clouds.hpp"

#include <catch2/catch_approx.hpp>
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
    for (int i = 0; i <= static_cast<int>(scene::kMaxCloudGlows); ++i) many.push_back({{"position", {0, 0, i}}}); // ADR-2104: 24
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

// ADR-2101: the relief sea's keys read, validate and round-trip; the default sea is the ADR-2100 band.
TEST_CASE("the relief sea and its breaks read, validate and round-trip", "[clouds][adr2101]") {
    const nlohmann::json j = {{"sea", {{"base", -150.0}, {"top", -38.0}, {"relief", 70.0}, {"reliefScale", 2000.0},
                                       {"billow", 26.0}, {"farFade", 0.35},
                                       {"breaks", {{{"centre", {1640.0, -60.0}}, {"radius", 270.0},
                                                    {"elongation", 2.0}, {"yaw", -0.05}}}}}}};
    auto c = scene::Clouds::fromJson(j, fs::temp_directory_path());
    REQUIRE(c);
    CHECK(c->sea.relief == 70.0f);
    CHECK(c->sea.billow == 26.0f);
    REQUIRE(c->sea.breaks.size() == 1);
    CHECK(c->sea.breaks[0].centre.x == 1640.0f);
    CHECK(c->sea.breaks[0].elongation == 2.0f);
    auto again = scene::Clouds::fromJson(c->toJson(), fs::temp_directory_path());
    REQUIRE(again);
    CHECK(again->sea.relief == 70.0f);
    CHECK(again->sea.farFade == Catch::Approx(0.35f));
    REQUIRE(again->sea.breaks.size() == 1);
    CHECK(again->sea.breaks[0].yaw == Catch::Approx(-0.05f));

    auto band = scene::Clouds::fromJson({{"sea", {{"base", -100.0}, {"top", 0.0}}}}, fs::temp_directory_path());
    REQUIRE(band);
    CHECK(band->sea.relief == 0.0f);
    CHECK_FALSE(band->toJson().at("sea").contains("relief"));

    nlohmann::json three = j;
    for (int i = 0; i < 2; ++i) three["sea"]["breaks"].push_back(three["sea"]["breaks"][0]);
    CHECK_FALSE(scene::Clouds::fromJson(three, fs::temp_directory_path()));
    nlohmann::json negative = j;
    negative["sea"]["relief"] = -1.0;
    CHECK_FALSE(scene::Clouds::fromJson(negative, fs::temp_directory_path()));
}

// ADR-2102: lighthouse beams and the moon's halo on the cloud block -- read, refused, round-tripped, turned by the
// timeline second, and routable.
TEST_CASE("cloud beams and the halo read, validate, round-trip and turn with the timeline", "[unit][clouds][adr2102]") {
    const json j = {{"sea", {{"base", -100.0}, {"top", 0.0}}},
                    {"beams", {{{"name", "harbour"}, {"origin", {10.0, 50.0, -20.0}}, {"count", 2}, {"yaw", 90.0},
                                {"elevation", 0.0}, {"rpm", 15.0}, {"length", 800.0}, {"halfAngle", 2.0},
                                {"radius", 1.5}, {"color", {0.5, 0.6, 1.0}}, {"intensity", 2.0}}}},
                    {"beamAir", 0.3},
                    {"beamCloud", 1.5},
                    {"halo", {{"direction", {0.0, 0.0, -2.0}}, {"color", {0.3, 0.3, 0.4}}, {"radius", 3.0},
                              {"intensity", 0.5}}}};
    auto c = scene::Clouds::fromJson(j, fs::temp_directory_path());
    REQUIRE(c);
    REQUIRE(c->beams.size() == 1);
    CHECK(c->beams[0].count == 2);
    CHECK(c->beams[0].rpm == 15.0f);
    CHECK(c->beams[0].color.z == 1.0f);
    CHECK(c->beamAir == Catch::Approx(0.3f));
    CHECK(c->beamCloud == Catch::Approx(1.5f));
    CHECK(c->halo.enabled);
    CHECK(c->halo.direction.z == Catch::Approx(-1.0f)); // normalised on load

    // yaw 90 at t = 0 points along -z (counter-clockwise from +x seen from above); the second arm is opposed; at 15
    // rpm a second later the arm has turned a quarter turn further, to -x
    const glm::vec3 d0 = scene::cloudBeamDirection(c->beams[0], 0, 0.0);
    CHECK(d0.z == Catch::Approx(-1.0f).margin(1e-5));
    const glm::vec3 d1 = scene::cloudBeamDirection(c->beams[0], 1, 0.0);
    CHECK(d1.z == Catch::Approx(1.0f).margin(1e-5));
    const glm::vec3 later = scene::cloudBeamDirection(c->beams[0], 0, 1.0);
    CHECK(later.x == Catch::Approx(-1.0f).margin(1e-5));
    // a seek lands where play would: a whole number of turns later the arm is where it started
    const glm::vec3 turned = scene::cloudBeamDirection(c->beams[0], 0, 4.0 * 1000.0);
    CHECK(turned.z == Catch::Approx(-1.0f).margin(1e-4));

    auto again = scene::Clouds::fromJson(c->toJson(), fs::temp_directory_path());
    REQUIRE(again);
    REQUIRE(again->beams.size() == 1);
    CHECK(again->beams[0].name == "harbour");
    CHECK(again->beams[0].halfAngle == 2.0f);
    CHECK(again->beams[0].intensity == 2.0f);
    CHECK(again->beamCloud == Catch::Approx(1.5f));
    CHECK(again->halo.enabled);
    CHECK(again->halo.radius == 3.0f);

    // none authored: nothing written
    auto plain = scene::Clouds::fromJson({{"sea", {{"base", -100.0}, {"top", 0.0}}}}, fs::temp_directory_path());
    REQUIRE(plain);
    CHECK_FALSE(plain->toJson().contains("beams"));
    CHECK_FALSE(plain->toJson().contains("halo"));

    json typo = j;
    typo["beams"][0]["speed"] = 3.0;
    CHECK_FALSE(scene::Clouds::fromJson(typo, fs::temp_directory_path()));
    json many = j;
    many["beams"][0]["count"] = 9;
    CHECK_FALSE(scene::Clouds::fromJson(many, fs::temp_directory_path()));
    json wide = j;
    wide["beams"][0]["halfAngle"] = 50.0;
    CHECK_FALSE(scene::Clouds::fromJson(wide, fs::temp_directory_path()));
    json arms = j; // five beams of two arms: ten arms, more than the renderer holds
    for (int i = 0; i < 4; ++i) arms["beams"].push_back(j["beams"][0]);
    CHECK_FALSE(scene::Clouds::fromJson(arms, fs::temp_directory_path()));

    // the knobs: each beam's intensity, the air and cloud gains and the halo
    params::ParameterSet ps;
    const scene::CloudParameters p = scene::registerCloudParameters(ps, *c, "clouds/");
    REQUIRE(ps.findAs<float>("clouds/beam/0/intensity") != nullptr);
    REQUIRE(ps.findAs<float>("clouds/beamAir") != nullptr);
    REQUIRE(ps.findAs<float>("clouds/halo/intensity") != nullptr);
    CHECK(ps.findAs<float>("clouds/beam/0/intensity")->value() == 2.0f);
    ps.findAs<float>("clouds/beam/0/intensity")->setFinalComponent(0, 7.0f);
    ps.findAs<float>("clouds/beamCloud")->setFinalComponent(0, 0.25f);
    ps.findAs<float>("clouds/halo/intensity")->setFinalComponent(0, 2.0f);
    scene::Clouds live;
    scene::applyCloudParameters(p, *c, live);
    CHECK(live.beams[0].intensity == 7.0f);
    CHECK(live.beamCloud == 0.25f);
    CHECK(live.halo.intensity == 2.0f);
    CHECK(c->beams[0].intensity == 2.0f);
}

// ADR-2104: a far lobe set on its own grid, and the glows shaped by the cloud's structure: read, validate, round-trip.
TEST_CASE("clouds: far lobes and the glow shape read, validate and round-trip", "[unit][clouds][adr2104]") {
    const json j = {{"lobes", json::array({{{"c", {0, 100, 0}}, {"r", 50.0}}})},
                    {"farLobes", json::array({{{"c", {5000, 300, 0}}, {"r", 400.0}}, {{"c", {6000, 300, 0}}, {"r", 300.0}}})},
                    {"glowBreakup", 0.7}, {"glowShadow", 1.2}, {"glowBreakupScale", 0.02}, {"glowShadowDistance", 20.0},
                    {"glows", json::array({{{"position", {0, 100, 0}}, {"color", {1, 0, 1}}, {"radius", 50.0}}})}};
    auto c = scene::Clouds::fromJson(j, fs::temp_directory_path());
    REQUIRE(c.has_value());
    CHECK(c->lobes.size() == 1);
    REQUIRE(c->farLobes.size() == 2);
    CHECK(c->farLobes[1].radius == 300.0f);
    CHECK(c->glowBreakup == 0.7f);
    CHECK(c->glowShadow == 1.2f);
    const json back = c->toJson();
    CHECK(back.at("farLobes").size() == 2);
    CHECK(back.at("glowBreakup").get<float>() == 0.7f);
    auto again = scene::Clouds::fromJson(back, fs::temp_directory_path());
    REQUIRE(again.has_value());
    CHECK(again->farLobes.size() == 2);
    CHECK(again->glowShadowDistance == 20.0f);

    // off by default and not written
    auto plain = scene::Clouds::fromJson({{"lobes", json::array({{{"c", {0, 0, 0}}, {"r", 5.0}}})}}, fs::temp_directory_path());
    REQUIRE(plain.has_value());
    CHECK(plain->glowBreakup == 0.0f);
    CHECK_FALSE(plain->toJson().contains("farLobes"));
    CHECK_FALSE(plain->toJson().contains("glowBreakup"));

    // refused out of range
    json bad = j;
    bad["glowBreakup"] = 1.5;
    CHECK_FALSE(scene::Clouds::fromJson(bad, fs::temp_directory_path()).has_value());

    // the breakup and shadow are live knobs
    params::ParameterSet ps;
    const scene::CloudParameters p = scene::registerCloudParameters(ps, *c, "clouds/");
    REQUIRE(ps.findAs<float>("clouds/glowBreakup") != nullptr);
    ps.findAs<float>("clouds/glowShadow")->setFinalComponent(0, 2.0f);
    scene::Clouds live;
    scene::applyCloudParameters(p, *c, live);
    CHECK(live.glowShadow == 2.0f);
    CHECK(live.farLobes.size() == 2);
}

// ADR-2106: detail erosion and the key's light taps: read, validate, round-trip, live knobs.
TEST_CASE("clouds: the erosion and the light taps read, validate and round-trip", "[unit][clouds][adr2106]") {
    const json j = {{"lobes", json::array({{{"c", {0, 100, 0}}, {"r", 50.0}}})},
                    {"erosion", 0.8}, {"erosionScale", 60.0}, {"erosionDetail", 1.2}, {"erosionCurl", 8.0},
                    {"erosionFlatten", 0.3}, {"erosionOctaves", 2}, {"lightSpacing", 2}, {"multiScatter", 0.6}};
    auto c = scene::Clouds::fromJson(j, fs::temp_directory_path());
    REQUIRE(c.has_value());
    CHECK(c->erosion == 0.8f);
    CHECK(c->erosionScale == 60.0f);
    CHECK(c->erosionOctaves == 2);
    CHECK(c->lightSpacing == 2);
    CHECK(c->multiScatter == 0.6f);
    auto again = scene::Clouds::fromJson(c->toJson(), fs::temp_directory_path());
    REQUIRE(again.has_value());
    CHECK(again->erosionCurl == 8.0f);
    CHECK(again->erosionFlatten == 0.3f);
    CHECK(again->erosionDetail == 1.2f);
    CHECK(again->lightSpacing == 2);

    // off by default and not written
    auto plain = scene::Clouds::fromJson({{"lobes", json::array({{{"c", {0, 0, 0}}, {"r", 5.0}}})}}, fs::temp_directory_path());
    REQUIRE(plain.has_value());
    CHECK(plain->erosion == 0.0f);
    CHECK(plain->lightSpacing == 1);
    CHECK_FALSE(plain->toJson().contains("erosion"));
    CHECK_FALSE(plain->toJson().contains("lightSpacing"));

    for (const auto& [key, value] : {std::pair{"erosion", json(1.5)}, std::pair{"erosionOctaves", json(4)},
                                     std::pair{"lightSpacing", json(3)}, std::pair{"erosionScale", json(0.0)}}) {
        json bad = j;
        bad[key] = value;
        INFO(key);
        CHECK_FALSE(scene::Clouds::fromJson(bad, fs::temp_directory_path()).has_value());
    }

    // strength, scale, detail, curl and multiple scattering are live knobs
    params::ParameterSet ps;
    const scene::CloudParameters p = scene::registerCloudParameters(ps, *c, "clouds/");
    for (const char* k : {"clouds/erosion", "clouds/erosionScale", "clouds/erosionDetail", "clouds/erosionCurl",
                          "clouds/multiScatter"}) {
        INFO(k);
        REQUIRE(ps.findAs<float>(k) != nullptr);
    }
    ps.findAs<float>("clouds/erosion")->setFinalComponent(0, 0.25f);
    scene::Clouds live;
    scene::applyCloudParameters(p, *c, live);
    CHECK(live.erosion == 0.25f);
    CHECK(live.erosionScale == 60.0f);
}

// ADR-2107: a lobe's own share of the erosion, and 32 glows.
TEST_CASE("clouds: a lobe's erode share reads, validates and round-trips; 32 glows fit", "[unit][clouds][adr2107]") {
    const json j = {{"lobes", json::array({{{"c", {0, 100, 0}}, {"r", 50.0}, {"erode", 0.25}},
                                           {{"c", {0, 200, 0}}, {"r", 50.0}}})},
                    {"erosion", 1.0}};
    auto c = scene::Clouds::fromJson(j, fs::temp_directory_path());
    REQUIRE(c.has_value());
    CHECK(c->lobes[0].erode == 0.25f);
    CHECK(c->lobes[1].erode == 1.0f);
    const json back = c->toJson();
    CHECK(back.at("lobes")[0].at("erode").get<float>() == 0.25f);
    CHECK_FALSE(back.at("lobes")[1].contains("erode"));
    json bad = j;
    bad["lobes"][0]["erode"] = 1.5;
    CHECK_FALSE(scene::Clouds::fromJson(bad, fs::temp_directory_path()).has_value());

    // a glow's own share of the shadow
    const json gl = {{"lobes", json::array({{{"c", {0, 0, 0}}, {"r", 5.0}}})},
                     {"glows", json::array({{{"position", {0, 0, 0}}, {"shadow", 0.2}}, {{"position", {0, 1, 0}}}})}};
    auto withGlows = scene::Clouds::fromJson(gl, fs::temp_directory_path());
    REQUIRE(withGlows.has_value());
    CHECK(withGlows->glows[0].shadow == 0.2f);
    CHECK(withGlows->glows[1].shadow == 1.0f);
    CHECK(withGlows->toJson().at("glows")[0].at("shadow").get<float>() == 0.2f);
    CHECK_FALSE(withGlows->toJson().at("glows")[1].contains("shadow"));
    json badGlow = gl;
    badGlow["glows"][0]["shadow"] = -1.0;
    CHECK_FALSE(scene::Clouds::fromJson(badGlow, fs::temp_directory_path()).has_value());

    CHECK(scene::kMaxCloudGlows == 32);
    json glows = json::array();
    for (int i = 0; i < 32; ++i) glows.push_back({{"position", {0, 0, i}}});
    CHECK(scene::Clouds::fromJson({{"lobes", json::array({{{"c", {0, 0, 0}}, {"r", 5.0}}})}, {"glows", glows}},
                                  fs::temp_directory_path())
              .has_value());
}

TEST_CASE("clouds: a block written for ADR-1289's march keeps its even steps", "[unit][clouds][adr2109]") {
    const json lobe = json::array({{{"c", {0, 0, 0}}, {"r", 5.0}}});
    // the biovalley shots: none of the v2 march's keys
    auto legacy = scene::Clouds::fromJson({{"lobes", lobe}, {"steps", 128}}, fs::temp_directory_path());
    REQUIRE(legacy);
    CHECK(legacy->evenSteps);
    CHECK(legacy->resolution == 1.0f);
    // it survives a save (toJson writes resolution and the step keys, so the flag itself is written)
    auto again = scene::Clouds::fromJson(legacy->toJson(), fs::temp_directory_path());
    REQUIRE(again);
    CHECK(again->evenSteps);
    CHECK(again->resolution == 1.0f);
    // any v2 march key opts into the v2 march, with its defaults
    auto v2 = scene::Clouds::fromJson({{"lobes", lobe}, {"stepGrowth", 0.02}}, fs::temp_directory_path());
    REQUIRE(v2);
    CHECK_FALSE(v2->evenSteps);
    CHECK(v2->resolution == 0.5f);
    CHECK_FALSE(v2->toJson().contains("evenSteps"));
    // and the flag can be asked for with a resolution of its own
    auto explicitEven = scene::Clouds::fromJson({{"lobes", lobe}, {"evenSteps", true}, {"resolution", 0.5}},
                                                fs::temp_directory_path());
    REQUIRE(explicitEven);
    CHECK(explicitEven->evenSteps);
    CHECK(explicitEven->resolution == 0.5f);
}

// ADR-2119: churn, edge air and mist sheets; ADR-2120: the vortex.
TEST_CASE("clouds: churn, edge air, mist sheets and the vortex read, validate, round-trip and route",
          "[unit][clouds][adr2119][adr2120]") {
    const json j = {
        {"sea", {{"enabled", true}, {"base", -260.0}, {"top", -38.0}, {"relief", 60.0}}},
        {"churn", {{"amount", 0.8}, {"period", 6.0}, {"boil", 1.5}, {"writhe", 0.5}}},
        {"edgeAir", 0.3},
        {"wisps", {{"enabled", true},
                   {"layers", json::array({{{"name", "low"}, {"centre", {0, 10}}, {"radius", 500.0}, {"base", 40.0},
                                            {"top", 90.0}, {"density", 0.6}, {"scale", 50.0}, {"coverage", 0.4},
                                            {"stretch", 3.0}, {"drift", {2, 0, 1}}},
                                           {{"base", 0.0}, {"top", 20.0}}})}}},
        {"vortex", {{"centre", {2090, -20}}, {"radius", 620.0}, {"eye", 150.0}, {"elongation", 1.6}, {"depth", 175.0},
                    {"spin", 0.12}, {"wind", 1.8}, {"shelves", 5}, {"arms", 3}, {"glow", 2.0}, {"glowColor", {0.5, 0.1, 1}},
                    {"column", 260.0}, {"columnDensity", 0.8}, {"columnPitch", 180.0}}}};
    auto c = scene::Clouds::fromJson(j, fs::temp_directory_path());
    REQUIRE(c.has_value());
    CHECK(c->churn.amount == 0.8f);
    CHECK(c->churn.period == 6.0f);
    CHECK(c->edgeAir == 0.3f);
    REQUIRE(c->wisps.layers.size() == 2);
    CHECK(c->wisps.layers[0].radius == 500.0f);
    CHECK(c->wisps.layers[0].drift == glm::vec3(2.0f, 0.0f, 1.0f));
    CHECK(c->wisps.layers[1].radius == 0.0f); // unbounded by default
    REQUIRE(c->vortex.enabled);
    CHECK(c->vortex.shelves == 5);
    CHECK(c->vortex.wind == 1.8f);
    CHECK(c->vortex.column == 260.0f);

    auto again = scene::Clouds::fromJson(c->toJson(), fs::temp_directory_path());
    REQUIRE(again.has_value());
    CHECK(again->churn.writhe == 0.5f);
    CHECK(again->edgeAir == 0.3f);
    REQUIRE(again->wisps.layers.size() == 2);
    CHECK(again->wisps.layers[0].name == "low");
    CHECK(again->vortex.eye == 150.0f);
    CHECK(again->vortex.glowColor == glm::vec3(0.5f, 0.1f, 1.0f));
    CHECK(again->vortex.columnPitch == 180.0f);

    // off by default and not written
    auto plain = scene::Clouds::fromJson({{"lobes", json::array({{{"c", {0, 0, 0}}, {"r", 5.0}}})}}, fs::temp_directory_path());
    REQUIRE(plain.has_value());
    CHECK(plain->churn.amount == 0.0f);
    CHECK_FALSE(plain->vortex.enabled);
    CHECK_FALSE(plain->toJson().contains("churn"));
    CHECK_FALSE(plain->toJson().contains("vortex"));
    CHECK_FALSE(plain->toJson().contains("edgeAir"));

    // refusals
    for (const auto& [path, value] : {std::pair{"/churn/amount", json(1.5)}, std::pair{"/edgeAir", json(-0.1)},
                                      std::pair{"/vortex/eye", json(700.0)}, std::pair{"/vortex/shelves", json(13)},
                                      std::pair{"/wisps/layers/1/top", json(-5.0)}}) {
        json bad = j;
        bad[json::json_pointer(path)] = value;
        INFO(path);
        CHECK_FALSE(scene::Clouds::fromJson(bad, fs::temp_directory_path()).has_value());
    }

    // live knobs: the churn, the edge air, each sheet's density, the vortex's glow, pulse and spin
    params::ParameterSet ps;
    const scene::CloudParameters p = scene::registerCloudParameters(ps, *c, "clouds/");
    for (const char* k : {"clouds/churn", "clouds/edgeAir", "clouds/wisps/layer/0/density",
                          "clouds/wisps/layer/1/density", "clouds/vortex/glow", "clouds/vortex/pulse",
                          "clouds/vortex/spin"}) {
        INFO(k);
        REQUIRE(ps.findAs<float>(k) != nullptr);
    }
    ps.findAs<float>("clouds/vortex/pulse")->setFinalComponent(0, 0.5f);
    ps.findAs<float>("clouds/wisps/layer/1/density")->setFinalComponent(0, 1.25f);
    ps.findAs<float>("clouds/churn")->setFinalComponent(0, 0.2f);
    scene::Clouds live;
    scene::applyCloudParameters(p, *c, live);
    CHECK(live.vortex.pulse == 0.5f);
    CHECK(live.wisps.layers[1].density == 1.25f);
    CHECK(live.churn.amount == 0.2f);
    CHECK(live.vortex.glow == 2.0f);
}
