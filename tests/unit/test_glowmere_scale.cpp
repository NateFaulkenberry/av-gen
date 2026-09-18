// The Glowmere scale invariants (ADR-334, ADR-335).
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
// The numbers this file *does* write down -- 1.25, 3, 4.0, 1.2 -- are the art direction, and they
// are argued for in ADR-334 and ADR-335 rather than tuned until a screenshot passed.
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
#include <cstdio>
#include <cstdlib>
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
// clears the tallest body in the cast by a quarter of that body again. This is the *definition* of
// a canopy and it has not moved. What moved is the claim built on it -- see arm 2.
constexpr float kCanopyHeadroom = 1.25f;

// And of the ten signature organisms, at least three are canopies and at least three are not.
//
// ADR-334's arm said all ten were canopies. That was true of the world it shipped and it is the
// arm ADR-335 had to rewrite, because the owner looked at that world and said the cast was too
// small. Replacing the claim rather than widening the constant is the same move ADR-334 itself
// made on its third arm, and for the same reason: the number was not wrong, the sentence was.
//
// The sentence that is true of the world the owner asked for is that **the cast stands inside its
// own ladder**. The ten hero fungi are a ladder produced by a search, and a ladder every rung of
// which is over your head is a ceiling: the elder reads as monumental because a veil in the same
// world is at the cast's own height. So the claim is two-sided and the count is the instrument.
//
//   3.6x (ADR-213)   1 canopy, 9 grounded -- the cast was the top of its own ladder
//   1.0x (ADR-334)  10 canopies, 0 grounded -- nothing in the signature set was creature-sized
//   1.94x (ADR-335)  5 canopies, 5 grounded
//
// Both bounds bite. Holding the flora still, they pin the cast between 1.749x (below which umbra
// becomes a canopy and there are eight) and 2.449x (above which cairn stops being one and there
// are two) -- a window 1.4x wide that 1.94 sits near the middle of. An arm that passes on any
// world is worthless; this one refuses both worlds Glowmere has actually shipped.
constexpr int kCanopiesMin = 3;
constexpr int kGroundedMin = 3;

// The signature organism is monumental: at least four of the cast's tallest body, or it is a big
// plant and the world has no landmark.
constexpr float kSignatureMin = 4.0f;

// And it stands above the tree line, by a fifth again of the tallest instance any tree layer will
// place. This number replaces an upper bound on bodies -- "no more than eight, or it stops being
// something a figure has a relationship with" -- which was written first, was contradicted by the
// renders, and is recorded as contradicted in ADR-334 rather than quietly widened. What the frames
// showed is that a 16 m elder at 8.9 alien-heights does not read as terrain at all; what it reads
// as is a mushroom shorter than the trees around it, in a world whose whole subject is mushrooms.
// The failure was never in the ratio to the cast. It was that Glowmere's signature organisms were
// not the tallest things in Glowmere.
constexpr float kAboveTreeLine = 1.2f;

// ---- the control: the world as it was ------------------------------------------------------

constexpr float kBeforeCast = 3.6f; // every farm animal; the four named aliens were 3.344 to 3.61
// And the second control, which is a world this repository shipped for one afternoon: the cast at
// the metres its GLBs occupy at rest, with the flora where ADR-334 left it. The owner rejected it
// in those words -- "they are too small now that the world scale is corrected" -- so it is not a
// hypothetical either.
constexpr float kCastAt334 = 1.0f;
const std::map<std::string, float>& beforeFungi() {
    static const std::map<std::string, float> kBefore{
        {"elder-2", 16.0f}, {"lantern", 6.5f}, {"spire", 4.2f}, {"bloom", 9.0f}, {"veil", 3.4f},
        {"umbra", 5.5f},    {"cairn", 7.5f},   {"ridge", 5.0f}, {"scree", 6.2f}, {"ember", 4.0f}};
    return kBefore;
}

fs::path sourceDir() { return fs::path(AVGEN_SOURCE_DIR); }
fs::path worldDir() { return sourceDir() / "examples" / "world"; }

// The four valley-2-family scenes plus valley 3 (ADR-344). Valley 3 has no farm in it, so its
// `cast` is the five aliens and the tallest of them is the same `alien-ranger` at the same 1.94x
// -- which is the point of adding it: a new Glowmere is exactly where a scale ladder goes wrong,
// and it is where ADR-213's 3.6x reached four files without anything failing.
constexpr std::array<const char*, 5> kScenes{{"glowmere-valley-2", "glowmere-valley-2-multicam",
                                              "glowmere-valley-2-song", "glowmere-atmospherics",
                                              "glowmere-valley-3"}};

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
    CHECK(scenesChecked == 5);
}

// ---------------------------------------------------------------------------------------------
// 2. The cast stands inside the fungal ladder: some of the ten are canopies and some are not.
//
// The gill line -- the underside of the cap, measured off the vertices -- is what a body walks
// under, and it is about three quarters of an organism's height for every one of the ten. An
// organism whose gill line clears the tallest body by a quarter of that body again is a **canopy**:
// a figure can be filmed standing under it. One whose gill line does not is **grounded**: a figure
// stands beside it, at something like its own height.
//
// ADR-334's arm here was `CHECK(gillLine > tallest * 1.25)` for all ten, and it was written to
// catch a cast that had grown until it was the top of its own ladder. It is not the arm this
// world wants, and the replacement is ADR-335's whole argument:
//
//   * At ADR-213's 3.6x exactly one of the ten -- the elder -- was a canopy. Nine were hats.
//   * At ADR-334's 1.0x all ten were canopies, and the owner looked at that and said the cast was
//     too small for the world. What they were looking at is a world with nothing creature-sized in
//     its signature set: the elder's 16 m had no small mushroom to be sixteen metres *against*.
//   * At 1.94x the ladder brackets the cast. Five canopies, five grounded.
//
// So the assertion is a band on the count, not a floor on every organism, and the two halves fail
// on the two different worlds. This is the same shape ADR-334 gave its own third arm after three
// renders contradicted the sentence it was written with.
TEST_CASE("Glowmere's cast stands inside the fungal ladder", "[glowmere][scale]") {
    if (!assetsPresent()) {
        SKIP("assets/farm or assets/aliens is not present");
    }
    const auto farm = farmNaturalHeights();
    const auto aliens = alienNaturalHeights();
    int scenesChecked = 0;

    for (const char* name : kScenes) {
        const json doc = readJson(worldDir() / fmt::format("{}.scene.json", name));
        const auto cast = castOf(doc, farm, aliens);
        if (cast.empty()) {
            continue;
        }
        ++scenesChecked;
        const Body& tallest = tallestOf(cast);
        const float native = tallest.height / tallest.scale;

        // A closure rather than three copies: the same count over the same gill lines, against a
        // cast of whatever size, is exactly what makes the controls controls.
        const auto fungi = fungiOf(doc);
        const auto canopiesAgainst = [&fungi](float body) {
            int n = 0;
            for (const Fungus& f : fungi) {
                n += f.gillLine > body * kCanopyHeadroom ? 1 : 0;
            }
            return n;
        };

        const int canopies = canopiesAgainst(tallest.height);
        const int grounded = static_cast<int>(fungi.size()) - canopies;
        std::string ladder;
        for (const Fungus& f : fungi) {
            ladder += fmt::format("\n    {:<9} gills {:6.3f} m  {}", f.name, f.gillLine,
                                  f.gillLine > tallest.height * kCanopyHeadroom ? "canopy"
                                                                                : "grounded");
        }
        INFO(name << ": the tallest body is " << tallest.node << " at " << tallest.height
                  << " m (node scale " << tallest.scale << "), so a canopy needs its gills above "
                  << tallest.height * kCanopyHeadroom << " m." << ladder);
        // Enough of the ten can be filmed with a figure under them that "walk under a mushroom" is
        // a shot this world has, rather than one shot it has once.
        CHECK(canopies >= kCanopiesMin);
        // And enough of them are at the cast's own height that the big ones have something in
        // their own species to be big against.
        CHECK(grounded >= kGroundedMin);

        // Control one: ADR-213's 3.6x. One canopy, so the canopy floor fails.
        const int canopiesBefore = canopiesAgainst(native * kBeforeCast);
        INFO("control (" << kBeforeCast << "x, ADR-213): " << canopiesBefore << " canopies, "
                         << static_cast<int>(fungi.size()) - canopiesBefore << " grounded");
        CHECK(canopiesBefore < kCanopiesMin);

        // Control two: ADR-334's 1.0x, the world the owner rejected. Ten canopies, so the grounded
        // floor fails -- and it fails on the *other* half of the band, which is the point. An arm
        // whose two controls fail the same way is one bound wearing two hats.
        const int canopiesAt334 = canopiesAgainst(native * kCastAt334);
        const int groundedAt334 = static_cast<int>(fungi.size()) - canopiesAt334;
        INFO("control (" << kCastAt334 << "x, ADR-334): " << canopiesAt334 << " canopies, "
                         << groundedAt334 << " grounded");
        CHECK(groundedAt334 < kGroundedMin);
    }
    CHECK(scenesChecked == 5);
}

// ---------------------------------------------------------------------------------------------
// 3. The signature organism is monumental, and it is the tallest thing in the world.
//
// This is the arm with two controls, and it is the one that says what "flora comes down" meant.
// It fails on the old world low -- a 16 m elder against a 6.38 m bull is 2.5 bodies, a mushroom the
// cast could climb -- and it fails on the world where the cast came down and nothing else did,
// because there the elder is still shorter than the trees: 16 m against a `pines` instance the
// layer will place at up to 21 m.
//
// Neither control is hypothetical. Both were rendered, and the second is what changed this arm:
// the elder at 8.9 alien-heights does not read as terrain, it reads as a mushroom standing in a
// forest that is taller than it is. See ADR-334.
TEST_CASE("Glowmere's signature organism is monumental and stands above the tree line",
          "[glowmere][scale]") {
    if (!assetsPresent()) {
        SKIP("assets/farm or assets/aliens is not present");
    }
    const auto farm = farmNaturalHeights();
    const auto aliens = alienNaturalHeights();
    // **A tree is a layer that places something at least five metres tall.** Structural, and not a
    // list of three names, because a name list is a floor that outlives what it counted: valley 3
    // has eight tree layers of which exactly one is still called `canopy`, and a `kTreeLayers` of
    // {"pines", "canopy", "deadwood"} would have computed that world's tree line off one layer and
    // thrown on the two names it no longer has.
    constexpr float kTreeMetres = 5.0f;
    // What those layers carried at bc79a51, for the control. 15.0 -- `pines`, the tallest -- is
    // the stand-in for a layer that did not exist then, because what the control asks is "would
    // this arm have passed in the world where Glowmere's trees were as tall as Glowmere ever made
    // them", and 15 m is that number.
    const std::map<std::string, float> kBeforeTrees{
        {"pines", 15.0f}, {"canopy", 14.0f}, {"deadwood", 12.0f}};
    constexpr float kTallestTreeBefore = 15.0f;

    for (const char* name : kScenes) {
        const json doc = readJson(worldDir() / fmt::format("{}.scene.json", name));
        const auto cast = castOf(doc, farm, aliens);
        const auto layers = scatterOf(doc);
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

        float treeLine = 0.0f;
        float beforeTreeLine = 0.0f;
        int treeLayers = 0;
        for (const auto& [layerName, l] : layers) {
            if (l.height < kTreeMetres) {
                continue;
            }
            ++treeLayers;
            treeLine = std::max(treeLine, l.height * l.maxScale);
            const auto before = kBeforeTrees.find(layerName);
            beforeTreeLine = std::max(
                beforeTreeLine,
                (before != kBeforeTrees.end() ? before->second : kTallestTreeBefore) * l.maxScale);
        }
        INFO(name << " has " << treeLayers << " tree layer(s)");
        // A liveness check on the derivation above: a world with no tree layers has a tree line of
        // zero and would clear the arm below without measuring anything.
        CHECK(treeLayers >= 3);
        INFO(name << ": the tree line is " << treeLine
                  << " m (the tallest instance the tree layers will place)");
        CHECK(signature >= treeLine * kAboveTreeLine);

        // Control one: the old world, where the cast was 3.6x and the signature was 2.5 bodies.
        const float native = tallest.height / tallest.scale;
        INFO("control (the old world): " << signature / (native * kBeforeCast) << " bodies");
        CHECK(signature / (native * kBeforeCast) < kSignatureMin);

        // Control two: the cast comes down and the flora does not. The elder is 16 m and the
        // `pines` layer places instances to 21 m, so the mushroom world's mushroom is four fifths
        // of a tree. This is the arm the renders moved.
        INFO("control (the cast comes down and the flora does not): a "
             << beforeFungi().at("elder-2") << " m elder against a " << beforeTreeLine
             << " m tree line");
        CHECK(beforeFungi().at("elder-2") < beforeTreeLine * kAboveTreeLine);
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
//
// **It had a hole and the hole was used.** Written against `nodes/*/scale` and the hero table, it
// said nothing about `entity/<name>/wander/speed`, and every Glowmere project carries one of those
// per animal plus a `runSpeed` beside it -- forty-two per project. ADR-334 moved those speeds in
// the scene and not in the project, so between that merge and ADR-335 a 1.77 m bull walked at the
// 3.585 m/s authored for a 6.38 m one, in every render anybody made, and this arm passed. The
// speeds are in it now, and so is the alien `explore` spelling of the same two keys.
TEST_CASE("the Glowmere projects do not undo their scenes", "[glowmere][scale]") {
    // Counted across the scenes rather than within each, because the liveness check and the
    // agreement check are different questions and a per-scene floor conflated them. Valley 3's
    // project copies **no** behaviour speed at all -- four parameters, none of them naming an
    // entity -- and a project that carries no copy cannot disagree with its scene, which is the
    // strictly safer state and not the vacuous one. What would be vacuous is the whole arm finding
    // nothing anywhere, and that is what the total below asserts.
    int speedsCheckedTotal = 0;
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

        // Every behaviour speed the project carries a copy of. A `wander`'s `speed` is metres per
        // second and scales with the body exactly as `gait.walkSpeed` does, so a project copy left
        // behind is a body walking at another cast's speed -- which is what happened.
        int speedsChecked = 0;
        for (const json& e : scene.at("entities")) {
            const std::string entityName = e.value("name", std::string());
            if (!e.contains("behaviors")) {
                continue;
            }
            for (const json& b : e.at("behaviors")) {
                const std::string kind = b.value("kind", std::string());
                if (kind != "wander" && kind != "explore") {
                    continue;
                }
                for (const char* field : {"speed", "runSpeed"}) {
                    if (!b.contains(field)) {
                        continue;
                    }
                    const std::string key =
                        fmt::format("entity/{}/{}/{}", entityName, kind, field);
                    if (!params.contains(key)) {
                        continue;
                    }
                    ++speedsChecked;
                    INFO(name << ": " << key << " overrides the scene's "
                              << b.at(field).get<float>());
                    CHECK(params.at(key).get<float>() ==
                          Catch::Approx(b.at(field).get<float>()).epsilon(1e-4));
                }
            }
        }
        INFO(name << ": " << speedsChecked << " behaviour speeds copied into the project");
        speedsCheckedTotal += speedsChecked;

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
    // Three of the four valley-2-family projects carry all forty-two speeds and the atmospherics
    // demonstration carries eight, so a total under eight means the walk above stopped walking.
    INFO(speedsCheckedTotal << " behaviour speeds checked across " << kScenes.size() << " scenes");
    CHECK(speedsCheckedTotal >= 8);
}

// ---------------------------------------------------------------------------------------------
// A probe, not a test: the whole of Glowmere's scale ladder in one column of metres, so that the
// question "what stands next to what" is answered by a measurement rather than by four files.
//
//   ./build/release/tests/avgen_tests "[.probe][glowmere-scale]"
TEST_CASE("probe: the Glowmere scale ladder", "[.probe][glowmere-scale]") {
    if (!assetsPresent()) {
        SKIP("assets/farm or assets/aliens is not present");
    }
    const char* which = std::getenv("AVGEN_LADDER_SCENE");
    const json doc = readJson(worldDir() / fmt::format("{}.scene.json",
                                                       which != nullptr ? which
                                                                        : "glowmere-valley-2-multicam"));
    const auto cast = castOf(doc, farmNaturalHeights(), alienNaturalHeights());
    const Body& tallest = tallestOf(cast);

    std::vector<std::pair<std::string, float>> ladder;
    for (const auto& [name, layer] : scatterOf(doc)) {
        ladder.emplace_back(fmt::format("{} (scatter, max)", name), layer.height * layer.maxScale);
    }
    for (const Fungus& f : fungiOf(doc)) {
        ladder.emplace_back(fmt::format("{} (hero fungus)", f.name), f.height);
        ladder.emplace_back(fmt::format("{} gill line", f.name), f.gillLine);
    }
    for (const Body& b : cast) {
        ladder.emplace_back(fmt::format("{} (body)", b.node), b.height);
    }
    std::sort(ladder.begin(), ladder.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::printf("\n===== Glowmere's scale ladder, metres =====\n");
    std::printf("  the tallest body is %s at %.3f m (node scale %.4g)\n\n", tallest.node.c_str(),
                static_cast<double>(tallest.height), static_cast<double>(tallest.scale));
    for (const auto& [name, metres] : ladder) {
        std::printf("  %7.3f  %-28s  %6.2f bodies\n", static_cast<double>(metres), name.c_str(),
                    static_cast<double>(metres / tallest.height));
    }
    std::fflush(stdout);
}
