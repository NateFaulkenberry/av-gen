// The Glowmere scale invariants (ADR-330).
//
// ## Why every arm here is a ratio
//
// A scale is never a number on its own. "The cow is at 1.0" passes just as happily at 3.6 as at
// 1.0, provided whoever changed the scene also changed the number the test expected -- which is
// the whole of how ADR-213's 3.6x reached four scenes, two projects and a tractor beam without
// anything failing. A probe that cannot fail proves nothing (ADR-182), and the corollary this file
// is built on is that **an absolute size cannot fail in the way that matters**: what a viewer sees
// is the cow against the mushroom, the alien against the plant, the animal against the beam.
//
// So every arm measures one thing against another thing in the same frame, and every arm carries a
// control -- the same computation over `kBeforeCast` and `kBeforeFungi`, the scales the four
// Glowmere valley scenes carried at bc79a51 -- which must **fail**. An arm that passes on both
// worlds is not measuring the difference between them.
//
// ## Where each number comes from, and why none of them is authored here
//
//   creature height   `assets/farm.manifest.json`'s measured `naturalSize.y` for the farm, the
//                     GLB's own vertex bounds for the aliens, times the scene node's scale.
//   fungus height     the generated mesh, rebuilt from the `index` the scene node carries, times
//                     `procedural.sourceTransform.scale`. `mushroomAnchors` reads the gill line off
//                     the vertices rather than off the parameters, for the reason mushroom.hpp
//                     gives: a parameter-space check agrees with itself.
//   scatter height    the valley's own scatter layers, whose `height` is the metres one instance of
//                     that species occupies and whose `maxScale` is the largest it will place.
//
// The three numbers this file *does* write down -- 1.25, 4.0, 8.0 -- are the art direction, and
// they are argued for in ADR-330 rather than tuned until a screenshot passed.
//
// GPU-free: the generator, the anchors and the JSON are all CPU.

#include "assets/gltf_loader.hpp"
#include "organism/mushroom.hpp"
#include "scene/scene.hpp"
#include "search/candidate_search.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// ---- the art direction, and the only authored numbers in this file -------------------------

// A canopy you have to duck under is a canopy you cannot be filmed standing under: the gill line
// clears the tallest body in the cast by a quarter of that body again.
constexpr float kCanopyHeadroom = 1.25f;

// The signature organism is monumental, and it stops there. Below four bodies it is a big plant
// and the world has no landmark; above eight it stops being something a figure has a relationship
// with and becomes terrain, and a creature at its foot is a scale marker rather than a character.
constexpr float kSignatureMin = 4.0f;
constexpr float kSignatureMax = 8.0f;

// ---- the control: the world as it was ------------------------------------------------------

constexpr float kBeforeCast = 3.6f; // every farm animal; the four named aliens were 3.344 to 3.61
const std::map<std::string, float>& beforeFungi() {
    static const std::map<std::string, float> kBefore{
        {"elder-2", 16.0f}, {"lantern", 6.5f}, {"spire", 4.2f}, {"bloom", 9.0f}, {"veil", 3.4f},
        {"umbra", 5.5f},    {"cairn", 7.5f},   {"ridge", 5.0f}, {"scree", 6.2f}, {"ember", 4.0f}};
    return kBefore;
}

fs::path sourceDir() { return fs::path(AVGEN_SOURCE_DIR); }
fs::path worldDir() { return sourceDir() / "examples" / "world"; }

constexpr std::array<const char*, 4> kScenes{{"glowmere-valley-2", "glowmere-valley-2-multicam",
                                              "glowmere-valley-2-song", "glowmere-atmospherics"}};

// The ten hero organisms, in the order `tools/make_glowmere_valley_2.py` lists them.
constexpr std::array<const char*, 10> kHeroes{{"elder-2", "lantern", "spire", "bloom", "veil",
                                               "umbra", "cairn", "ridge", "scree", "ember"}};

json readJson(const fs::path& path) {
    std::ifstream in(path);
    REQUIRE(in.good());
    json doc;
    in >> doc;
    return doc;
}

const json* findNode(const json& doc, std::string_view name) {
    for (const json& n : doc.at("nodes")) {
        if (n.value("name", std::string()) == name) {
            return &n;
        }
    }
    return nullptr;
}

bool assetsPresent() {
    return fs::exists(sourceDir() / "assets" / "farm" / "cow.glb") &&
           fs::exists(sourceDir() / "assets" / "aliens" / "alien-scout.glb");
}

float nativeHeight(const fs::path& glb) {
    scene::Scene s;
    const auto summary = assets::loadGltf(glb, s, {});
    REQUIRE(summary.has_value());
    return summary->boundsMax.y - summary->boundsMin.y;
}

// species -> metres, for the nine farm animals, out of the manifest that measured them.
std::map<std::string, float> farmNaturalHeights() {
    const json manifest = readJson(sourceDir() / "assets" / "farm.manifest.json");
    std::map<std::string, float> out;
    for (const auto& [category, entries] : manifest.at("categories").items()) {
        (void)category;
        for (const json& e : entries) {
            out[e.at("name").get<std::string>()] = e.at("naturalSize")[1].get<float>();
        }
    }
    REQUIRE(out.size() == 9);
    return out;
}

std::map<std::string, float> alienNaturalHeights() {
    std::map<std::string, float> out;
    for (const char* file : {"alien-scout", "alien-diver", "alien-elder", "alien-ranger",
                             "alien-pilot", "alien-trooper"}) {
        const fs::path glb = sourceDir() / "assets" / "aliens" / (std::string(file) + ".glb");
        if (fs::exists(glb)) {
            out[file] = nativeHeight(glb);
        }
    }
    return out;
}

struct Body {
    std::string node;
    float height = 0.0f; // world metres: the asset's own size times the node's scale
    float scale = 0.0f;
};

// Every animal and alien the scene places, with the metres it actually stands. The species is the
// GLB's stem, which is the one place the asset's identity is written down -- the node name is an
// instance ("cow-3") and the entity's tag list is a second copy.
std::vector<Body> castOf(const json& doc, const std::map<std::string, float>& farm,
                         const std::map<std::string, float>& aliens) {
    std::vector<Body> out;
    for (const json& n : doc.at("nodes")) {
        if (n.value("kind", std::string()) != "gltf" || !n.contains("scale")) {
            continue;
        }
        const std::string stem = fs::path(n.value("asset", std::string())).stem().string();
        const float scale = n.at("scale")[0].get<float>();
        if (const auto it = farm.find(stem); it != farm.end()) {
            out.push_back({n.value("name", std::string()), it->second * scale, scale});
        } else if (const auto a = aliens.find(stem); a != aliens.end()) {
            out.push_back({n.value("name", std::string()), a->second * scale, scale});
        }
    }
    return out;
}

const Body& tallestOf(const std::vector<Body>& cast) {
    return *std::max_element(cast.begin(), cast.end(),
                             [](const Body& a, const Body& b) { return a.height < b.height; });
}

struct Fungus {
    std::string name;
    float scale = 0.0f;    // where a hero organism's size actually lives
    float height = 0.0f;   // world metres, crown to stem foot
    float gillLine = 0.0f; // world metres: the underside of the cap, off the vertices
};

std::vector<Fungus> fungiOf(const json& doc) {
    const organism::MushroomGenerator generator;
    std::vector<Fungus> out;
    for (const char* hero : kHeroes) {
        const json* cap = findNode(doc, fmt::format("{}-cap", hero));
        REQUIRE(cap != nullptr);
        const json& gen = cap->at("procedural").at("source").at("generated");
        auto subject =
            generator.build(search::Parameters{gen.at("values").get<std::vector<float>>()});
        REQUIRE(subject.has_value());
        const organism::MushroomAnchors anchors = organism::mushroomAnchors(*subject);
        REQUIRE(anchors.valid);
        const float scale = cap->at("procedural").at("sourceTransform").at("scale")[0].get<float>();
        // Cap crown (part 0) down to stem foot (part 2): the generator's own `unitH`, and what a
        // hero's authored height means.
        const float unitH = subject->parts[0].mesh.bounds().second.y -
                            subject->parts[2].mesh.bounds().first.y;
        out.push_back({hero, scale, unitH * scale, anchors.gillLow.y * scale});
    }
    return out;
}

struct Layer {
    float height = 0.0f;
    float maxScale = 1.0f;
};

std::map<std::string, Layer> scatterOf(const json& doc) {
    const json* valley = findNode(doc, "valley");
    REQUIRE(valley != nullptr);
    std::map<std::string, Layer> out;
    for (const json& l : valley->at("scatter")) {
        out[l.value("name", std::string())] = {l.value("height", 0.0f), l.value("maxScale", 1.0f)};
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// 1. The undergrowth is undergrowth.
//
// `fan-plants` is the tallest thing the terrain scatters that is not a tree -- 3.2 m of frond, up
// to 1.6x -- and it is everywhere, which a tree is not. A body taller than the largest instance
// the layer can place is not walking through the undergrowth; it is walking over it. That is the
// most legible single symptom of the inflated cast, and it is visible in any frame with ground in
// it rather than only in a frame that happens to contain a hero organism.
TEST_CASE("Glowmere's cast stands in its undergrowth", "[glowmere][scale]") {
    if (!assetsPresent()) {
        SKIP("assets/farm or assets/aliens is not present");
    }
    const auto farm = farmNaturalHeights();
    const auto aliens = alienNaturalHeights();
    int scenesChecked = 0;

    for (const char* name : kScenes) {
        const json doc = readJson(worldDir() / fmt::format("{}.scene.json", name));
        const auto layers = scatterOf(doc);
        const auto plants = layers.find("fan-plants");
        const auto cast = castOf(doc, farm, aliens);
        if (plants == layers.end() || cast.empty()) {
            continue;
        }
        ++scenesChecked;
        const float tallestPlant = plants->second.height * plants->second.maxScale;
        const Body& tallest = tallestOf(cast);
        INFO(name << ": " << cast.size() << " bodies; the tallest is " << tallest.node << " at "
                  << tallest.height << " m (node scale " << tallest.scale << "), against a "
                  << tallestPlant << " m fan-plant");
        CHECK(tallest.height < tallestPlant);

        // The control. The same body at the scale ADR-213 put the farm at is half as tall again as
        // the largest plant the layer can grow, so this arm is measuring the change rather than
        // agreeing with itself.
        const float before = tallest.height / tallest.scale * kBeforeCast;
        INFO("control: the same body at " << kBeforeCast << "x stands " << before << " m");
        CHECK(before > tallestPlant);
    }
    CHECK(scenesChecked == 4);
}

// ---------------------------------------------------------------------------------------------
// 2. Every hero fungus is a canopy.
//
// The gill line -- the underside of the cap, measured off the vertices -- is what a body walks
// under, and it is about three quarters of an organism's height for every one of the ten. If it
// sits below the cast's own height then the smaller hero organisms are hats rather than landmarks.
// Four of the ten were, which is why the cast looked wrong beside them and not only beside the big
// ones.
TEST_CASE("every Glowmere hero fungus is a canopy the cast walks under", "[glowmere][scale]") {
    if (!assetsPresent()) {
        SKIP("assets/farm or assets/aliens is not present");
    }
    const auto farm = farmNaturalHeights();
    const auto aliens = alienNaturalHeights();

    for (const char* name : kScenes) {
        const json doc = readJson(worldDir() / fmt::format("{}.scene.json", name));
        const auto cast = castOf(doc, farm, aliens);
        if (cast.empty()) {
            continue;
        }
        const Body& tallest = tallestOf(cast);
        const float beforeCast = tallest.height / tallest.scale * kBeforeCast;
        int failedBefore = 0;

        for (const Fungus& f : fungiOf(doc)) {
            INFO(name << " / " << f.name << ": gills at " << f.gillLine << " m of a " << f.height
                      << " m organism, cast tallest " << tallest.height << " m");
            CHECK(f.gillLine > tallest.height * kCanopyHeadroom);

            // The gill line is a fixed fraction of an organism's height, so the same organism at
            // the height it used to be has its gills at that fraction of the old height.
            const auto it = beforeFungi().find(f.name);
            REQUIRE(it != beforeFungi().end());
            if (f.gillLine / f.height * it->second <= beforeCast * kCanopyHeadroom) {
                ++failedBefore;
            }
        }
        // The control, counted rather than asserted per organism, because the claim is about the
        // *set*: on the world as it was, a substantial part of the hero cast was below the cast's
        // own head height. Four of the ten, at bc79a51.
        INFO(name << ": " << failedBefore
                  << " of the ten heroes were below head height on the old world");
        CHECK(failedBefore >= 4);
    }
}

// ---------------------------------------------------------------------------------------------
// 3. The signature organism is monumental, and it stops there.
//
// This is the arm with two controls, and it is the one that says what "flora comes down" meant.
// The old world fails it low -- a 16 m elder against a 6.4 m bull is 2.5 bodies, a mushroom the
// cast could climb -- and a world where the cast came down and the flora did not fails it high: 16
// m against a 1.77 m bull is 9 bodies, at which point the organism is terrain and the figure at
// its foot is a scale marker rather than a character. Neither control is hypothetical; the second
// is the arm this change was rendered against before the flora was touched at all.
TEST_CASE("Glowmere's signature organism is monumental and not terrain", "[glowmere][scale]") {
    if (!assetsPresent()) {
        SKIP("assets/farm or assets/aliens is not present");
    }
    const auto farm = farmNaturalHeights();
    const auto aliens = alienNaturalHeights();

    for (const char* name : kScenes) {
        const json doc = readJson(worldDir() / fmt::format("{}.scene.json", name));
        const auto cast = castOf(doc, farm, aliens);
        if (cast.empty()) {
            continue;
        }
        const Body& tallest = tallestOf(cast);
        const auto fungi = fungiOf(doc);
        const float signature =
            std::max_element(fungi.begin(), fungi.end(), [](const Fungus& a, const Fungus& b) {
                return a.height < b.height;
            })->height;
        const float bodies = signature / tallest.height;
        INFO(name << ": the signature organism is " << signature << " m, the tallest body "
                  << tallest.height << " m -- " << bodies << " bodies");
        CHECK(bodies >= kSignatureMin);
        CHECK(bodies <= kSignatureMax);

        const float native = tallest.height / tallest.scale;
        const float beforeBoth = signature / (native * kBeforeCast);
        INFO("control (the old world): " << beforeBoth << " bodies");
        CHECK(beforeBoth < kSignatureMin);

        const float castOnly = beforeFungi().at("elder-2") / native;
        INFO("control (the cast comes down and the flora does not): " << castOnly << " bodies");
        CHECK(castOnly > kSignatureMax);
    }
}

// ---------------------------------------------------------------------------------------------
// 4. A project does not undo its scene.
//
// A project's `parameters` block is applied *over* the values a scene registers, so a scene edited
// alone is a scene whose sizes never reach a render -- and that is not hypothetical either: it is
// exactly what `sync_project` in tools/make_abduction_scenario.py exists to stop, after a fix was
// reported, made and shipped three times with the project quietly putting the old numbers back.
// Every Glowmere project carries an absolute copy of all forty hero source scales, and one of them
// carries a whole hero table as well.
//
// This arm has no "before" control because it is not a claim about scale: it is the claim that
// whatever the scale is, one file cannot silently disagree with the other. Its control is that it
// fails on any half-applied edit, which is the failure it was written for.
TEST_CASE("the Glowmere projects do not undo their scenes", "[glowmere][scale]") {
    for (const char* name : kScenes) {
        const json scene = readJson(worldDir() / fmt::format("{}.scene.json", name));
        const fs::path projectPath = worldDir() / fmt::format("{}.json", name);
        if (!fs::exists(projectPath)) {
            continue;
        }
        const json project = readJson(projectPath);
        const json& params = project.at("parameters");

        for (const char* hero : kHeroes) {
            for (const char* part : {"cap", "under", "stem", "gills"}) {
                const std::string node = fmt::format("{}-{}", hero, part);
                const json* n = findNode(scene, node);
                REQUIRE(n != nullptr);
                const float authored =
                    n->at("procedural").at("sourceTransform").at("scale")[0].get<float>();
                const std::string key = fmt::format("procedural/{}/source/scale", node);
                if (!params.contains(key)) {
                    continue;
                }
                INFO(name << ": " << key << " overrides the scene");
                CHECK(params.at(key)[0].get<float>() == Catch::Approx(authored).epsilon(1e-4));
            }
        }

        for (const json& n : scene.at("nodes")) {
            if (n.value("kind", std::string()) != "gltf" || !n.contains("scale")) {
                continue;
            }
            const std::string key = fmt::format("nodes/{}/scale", n.value("name", std::string()));
            if (!params.contains(key)) {
                continue;
            }
            INFO(name << ": " << key << " overrides the scene");
            CHECK(params.at(key)[0].get<float>() ==
                  Catch::Approx(n.at("scale")[0].get<float>()).epsilon(1e-4));
        }

        // And the hero table, when the project carries one: a hero's `height` and `radius` are how
        // far the camera stands off (camera_director.cpp), so a stale copy reframes every shot.
        if (!project.contains("heroes") || project.at("heroes").is_null()) {
            continue;
        }
        for (const json& h : project.at("heroes")) {
            const std::string heroName = h.at("name").get<std::string>();
            for (const json& s : scene.at("heroes")) {
                if (s.at("name").get<std::string>() != heroName) {
                    continue;
                }
                INFO(name << ": the project's hero '" << heroName << "' overrides the scene's");
                CHECK(h.at("height").get<float>() ==
                      Catch::Approx(s.at("height").get<float>()).epsilon(1e-3));
                CHECK(h.at("radius").get<float>() ==
                      Catch::Approx(s.at("radius").get<float>()).epsilon(1e-3));
                CHECK(h.at("preferredCameraDistance").get<float>() ==
                      Catch::Approx(s.at("preferredCameraDistance").get<float>()).epsilon(1e-3));
            }
        }
    }
}
