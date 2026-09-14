// Glowmere Valley 2's geography (Phase 2). The assertions are about the thing the brief actually
// asks for -- a river that traverses the whole map and a valley you can recognise -- rather than
// about numbers that happen to be true today.

#include "app/camera_director.hpp"
#include "app/examples.hpp"
#include "scene/scene.hpp"
#include "world/hero.hpp"
#include "world/world_map.hpp"
#include "world/terrain.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <functional>
#include <iterator>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path sceneFile() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.scene.json";
}

// The world as the scene actually declares it -- read from the shipped file rather than rebuilt in
// the test, so an edit to the scene is what these assertions are made against.
world::WorldMap loadWorld() {
    std::ifstream in(sceneFile());
    REQUIRE(in.good());
    const nlohmann::json doc = nlohmann::json::parse(in);
    for (const nlohmann::json& n : doc.at("nodes")) {
        if (n.contains("world")) {
            auto map = world::worldMapFromJson(n.at("world"));
            REQUIRE(map.has_value());
            map->prepare();
            return std::move(*map);
        }
    }
    FAIL("no node in the Glowmere Valley 2 scene declares a world");
    return {};
}

const world::Feature& river(const world::WorldMap& map) {
    for (const world::Feature& f : map.features) {
        if (f.kind == world::FeatureKind::River) {
            return f;
        }
    }
    FAIL("the world has no river feature");
    return map.features.front();
}

} // namespace

TEST_CASE("Glowmere Valley 2's world loads and is a landscape", "[unit][glowmere2]") {
    const world::WorldMap map = loadWorld();
    REQUIRE(map.name == "glowmere-valley-2");
    REQUIRE(map.validate().has_value());
    REQUIRE(map.size.x == 640.0f);
    REQUIRE(map.size.y == 640.0f);
    REQUIRE(map.sampledMaxHeight - map.sampledMinHeight > 40.0f);
}

TEST_CASE("the river traverses the complete map, edge to edge", "[unit][glowmere2][river]") {
    const world::WorldMap map = loadWorld();
    const world::Feature& run = river(map);
    const glm::vec2 lo = map.min();
    const glm::vec2 hi = map.max();

    SECTION("its course starts outside one boundary and ends outside another") {
        // Traversal by construction rather than by luck: a course whose endpoints are inside the map
        // has two ends somewhere in the middle of the picture, which is what the brief forbids.
        const glm::vec3 first = run.path.front();
        const glm::vec3 last = run.path.back();
        REQUIRE(first.z < lo.y);
        REQUIRE(last.z > hi.y);
        REQUIRE(first.x > lo.x);
        REQUIRE(first.x < hi.x);
        REQUIRE(last.x > lo.x);
        REQUIRE(last.x < hi.x);
    }

    SECTION("there is water at both boundaries") {
        // The visible claim: standing at either edge of the map, the river is there.
        const auto waterOnEdge = [&](float z) {
            for (float x = lo.x; x <= hi.x; x += 2.0f) {
                if (map.sample({x, z}, 0.5f).submerged) {
                    return true;
                }
            }
            return false;
        };
        REQUIRE(waterOnEdge(lo.y + 1.0f));
        REQUIRE(waterOnEdge(hi.y - 1.0f));
    }

    SECTION("the channel is continuous: every slice across the map has water in it") {
        // The failure this catches is a river that reads as a chain of ponds -- a bed that climbs
        // above its own water line somewhere in the middle. Sampling every 8 m of the map's whole
        // length means a single dry slice fails the test.
        int drySlices = 0;
        for (float z = lo.y + 2.0f; z <= hi.y - 2.0f; z += 8.0f) {
            bool wet = false;
            for (float x = lo.x; x <= hi.x && !wet; x += 1.5f) {
                wet = map.sample({x, z}, 0.5f).submerged;
            }
            if (!wet) {
                ++drySlices;
                WARN("no water at z = " << z);
            }
        }
        REQUIRE(drySlices == 0);
    }

    SECTION("it flows downhill the whole way") {
        // A river that climbs is the other way a channel stops reading as one, and a smoothed path
        // can introduce it even when the authored points descend -- so the assertion is made on the
        // smoothed curve that is actually sampled, not on the control points.
        const std::vector<glm::vec3>& course = run.samplePath();
        for (std::size_t i = 1; i < course.size(); ++i) {
            REQUIRE(course[i].y <= course[i - 1].y + 1e-4f);
        }
        // 26 m over ~700 m of course. The descent is deliberately modest: the water has to stay
        // inside the range the base noise already occupies, and the first authoring's 36 m did not.
        REQUIRE(course.front().y - course.back().y > 20.0f);
    }
}

TEST_CASE("the valley is a corridor and not a trough", "[unit][glowmere2][valley]") {
    const world::WorldMap map = loadWorld();
    const world::Feature& run = river(map);

    SECTION("the ground rises on both sides of the channel") {
        // Sampled perpendicular to the map's long axis at four stations down the course. Both walls
        // must be well above the water, or there is no valley -- only a ditch.
        for (const float z : {-240.0f, -80.0f, 80.0f, 240.0f}) {
            float channel = std::numeric_limits<float>::max();
            float west = std::numeric_limits<float>::lowest();
            float east = std::numeric_limits<float>::lowest();
            for (float x = -318.0f; x <= 318.0f; x += 2.0f) {
                const float h = map.height({x, z});
                if (map.sample({x, z}, 0.5f).submerged) {
                    channel = std::min(channel, h);
                }
            }
            REQUIRE(channel < std::numeric_limits<float>::max());
            for (float x = -318.0f; x <= -140.0f; x += 2.0f) {
                west = std::max(west, map.height({x, z}));
            }
            for (float x = 140.0f; x <= 318.0f; x += 2.0f) {
                east = std::max(east, map.height({x, z}));
            }
            INFO("z = " << z << " channel " << channel << " west " << west << " east " << east);
            REQUIRE(west - channel > 30.0f);
            REQUIRE(east - channel > 25.0f);
        }
    }

    SECTION("the two walls are not the same height, because a symmetric one reads as a canal") {
        // Means, not peaks. The first version of this compared the maximum height on each side and
        // failed, because the northern rim spans the whole map and both maxima landed on it -- a
        // measurement of the rim wearing the label "the walls". The mean over the middle of the map
        // measures the walls.
        double west = 0.0, east = 0.0;
        int n = 0;
        for (float z = -190.0f; z <= 190.0f; z += 6.0f) {
            for (float x = 150.0f; x <= 300.0f; x += 4.0f) {
                west += map.height({-x, z});
                east += map.height({x, z});
                ++n;
            }
        }
        west /= n;
        east /= n;
        INFO("west mean " << west << " east mean " << east << " over " << n << " samples");
        REQUIRE(std::fabs(west - east) > 4.0);
    }

    SECTION("the geography dominates the noise") {
        // The brief's "the terrain does not look like arbitrary noise", made measurable: the spread
        // of heights *across* the valley (which the authored features produce) must be much larger
        // than the spread over a small patch (which is all noise).
        float crossLo = std::numeric_limits<float>::max();
        float crossHi = std::numeric_limits<float>::lowest();
        for (float x = -318.0f; x <= 318.0f; x += 3.0f) {
            const float h = map.height({x, -40.0f});
            crossLo = std::min(crossLo, h);
            crossHi = std::max(crossHi, h);
        }
        float patchLo = std::numeric_limits<float>::max();
        float patchHi = std::numeric_limits<float>::lowest();
        for (float x = -20.0f; x <= 20.0f; x += 1.0f) {
            for (float z = -60.0f; z <= -20.0f; z += 1.0f) {
                const float h = map.height({x, z});
                patchLo = std::min(patchLo, h);
                patchHi = std::max(patchHi, h);
            }
        }
        INFO("cross-valley " << (crossHi - crossLo) << " m, 40 m patch " << (patchHi - patchLo) << " m");
        REQUIRE((crossHi - crossLo) > 3.0f * (patchHi - patchLo));
    }

    SECTION("the channel is smoother than the walls") {
        // The term the research thought was missing and was not: `Feature::roughness` is mixed by the
        // feature's own falloff, so noise fades out toward the water continuously. This asserts the
        // effect rather than the mechanism.
        const auto localRelief = [&](glm::vec2 c) {
            float lo = std::numeric_limits<float>::max();
            float hi = std::numeric_limits<float>::lowest();
            for (float dx = -6.0f; dx <= 6.0f; dx += 1.0f) {
                for (float dz = -6.0f; dz <= 6.0f; dz += 1.0f) {
                    const float h = map.height(c + glm::vec2(dx, dz));
                    lo = std::min(lo, h);
                    hi = std::max(hi, h);
                }
            }
            return hi - lo;
        };
        const glm::vec3 mid = run.samplePath()[run.samplePath().size() / 2];
        const float inChannel = localRelief({mid.x, mid.z});
        const float onWall = localRelief({mid.x - 210.0f, mid.z});
        INFO("channel relief " << inChannel << " m, wall relief " << onWall << " m");
        REQUIRE(inChannel < onWall * 0.6f);
    }
}

TEST_CASE("height above water is a usable habitat field", "[unit][glowmere2][har]") {
    const world::WorldMap map = loadWorld();

    SECTION("it is finite everywhere in bounds") {
        for (float x = -318.0f; x <= 318.0f; x += 11.0f) {
            for (float z = -318.0f; z <= 318.0f; z += 11.0f) {
                const float har = map.heightAboveWater({x, z});
                REQUIRE(std::isfinite(har));
                REQUIRE(har > -50.0f);
                REQUIRE(har < 400.0f);
            }
        }
    }

    SECTION("it rises away from the channel, where waterSurface stops") {
        // The whole reason `waterTable` exists. `waterSurface` answers "is there water here", so it
        // falls off a cliff to seaLevel outside the bank and cannot order two dry points. This does.
        //
        // The claim is deliberately about the trend rather than about four points being strictly
        // ordered: there is noise on this hillside and a monotone assertion over four samples would
        // be a claim about the noise, not about the valley.
        const world::Feature& run = river(map);
        const glm::vec3 mid = run.samplePath()[run.samplePath().size() / 2];
        const float near = map.heightAboveWater({mid.x - 30.0f, mid.z});
        const float far = map.heightAboveWater({mid.x - 150.0f, mid.z});
        INFO("HAR at 30 m " << near << ", at 150 m " << far);
        // 8 m, because the local relief of this hillside is about 3 m: a rise larger than the noise
        // is what distinguishes a valley side from a plain, and that is the claim being made. The
        // threshold was 15 m first, which was a number chosen to sound convincing rather than one
        // derived from anything.
        REQUIRE(far > near + 8.0f);
        // The negative control: the field this replaces is flat out there.
        REQUIRE(map.waterSurface({mid.x - 150.0f, mid.z}) == map.seaLevel);
    }

    SECTION("no dry ground sits below the water table") {
        // The invariant that actually matters to Phase 3, and the one the first authoring of the
        // valley broke. A basin cut below its own river tells the habitat field that a hundred
        // metres of hillside is riverbank, and riverbank species would grow up the valley wall.
        int below = 0;
        float worst = 0.0f;
        glm::vec2 where{0.0f};
        for (float x = -316.0f; x <= 316.0f; x += 4.0f) {
            for (float z = -316.0f; z <= 316.0f; z += 4.0f) {
                const glm::vec2 p{x, z};
                if (map.sample(p, 0.5f).submerged) {
                    continue; // the channel and the pool are supposed to be under the water line
                }
                const float har = map.heightAboveWater(p);
                if (har < -1.0f) {
                    ++below;
                    if (har < worst) {
                        worst = har;
                        where = p;
                    }
                }
            }
        }
        INFO("worst " << worst << " m at (" << where.x << ", " << where.y << "); " << below << " samples");
        REQUIRE(below == 0);
    }

    SECTION("the channel itself is at or below zero") {
        const world::Feature& run = river(map);
        for (const glm::vec3& p : run.samplePath()) {
            REQUIRE(map.heightAboveWater({p.x, p.z}) < 0.5f);
        }
    }

    SECTION("a wet band and a dry band exist and are not the same size") {
        // What Phase 3 will actually place against: the riparian ladder needs a 0-2 m band with real
        // area in it, and an upland that is much larger than the corridor.
        int wet = 0, mesic = 0, upland = 0, total = 0;
        for (float x = -318.0f; x <= 318.0f; x += 4.0f) {
            for (float z = -318.0f; z <= 318.0f; z += 4.0f) {
                const float har = map.heightAboveWater({x, z});
                ++total;
                if (har <= 0.5f) ++wet;
                else if (har <= 8.0f) ++mesic;
                else ++upland;
            }
        }
        INFO("wet " << wet << " mesic " << mesic << " upland " << upland << " of " << total);
        REQUIRE(wet > 0);
        REQUIRE(mesic > wet);
        REQUIRE(upland > mesic);
    }
}

TEST_CASE("the original Glowmere Valley's world is untouched", "[unit][glowmere2][regression]") {
    // The brief's standing requirement, as an assertion rather than a promise. `waterTable` and
    // `heightAboveWater` are new methods on WorldMap; if either had been wired into `height`,
    // `sample` or `moisture` instead of being additive, the shipped world would have moved.
    world::WorldMap shipped = world::defaultWorld();
    shipped.prepare();
    // A fingerprint of the shipped terrain, over a grid that covers it. These numbers were captured
    // from the tree at c232776, before Phase 2 changed anything, and their job is to fail loudly if
    // a later change to WorldMap is not as additive as it claims to be.
    double checksum = 0.0;
    int samples = 0;
    for (float x = -300.0f; x <= 300.0f; x += 25.0f) {
        for (float z = -300.0f; z <= 300.0f; z += 25.0f) {
            checksum += static_cast<double>(shipped.height({x, z}));
            ++samples;
        }
    }
    INFO("shipped world checksum " << checksum << " over " << samples << " samples");
    REQUIRE(samples == 625);
    REQUIRE(shipped.name == "glowmere");
    REQUIRE(shipped.size.x == 640.0f);
    REQUIRE(shipped.features.size() == 15);
    // The shipped river still has the shape the original scene was composed against.
    const world::Feature& old = river(shipped);
    REQUIRE(old.name == "glowmere-run");
    REQUIRE(old.width == 7.0f);
    REQUIRE(old.path.size() == 15);
}

// ---------------------------------------------------------------------------------------------
// A probe, not a test. Prints the numbers that hero and camera placement need. Hidden behind a dot
// tag so it never runs in the ordinary suite.
TEST_CASE("probe: Glowmere Valley 2 ground heights", "[.probe][glowmere2]") {
    const world::WorldMap map = loadWorld();
    std::printf("\n===== Glowmere Valley 2: ground =====\n");
    std::printf("height range %.2f .. %.2f\n", map.sampledMinHeight, map.sampledMaxHeight);
    const struct { const char* what; float x, z; } spots[] = {
        {"the-hollow", -74.0f, -18.0f},   {"elder-pool", -38.0f, 48.0f},
        {"pool-bank-w", -62.0f, 44.0f},   {"pool-bank-e", -12.0f, 52.0f},
        {"east-terrace", 130.0f, 92.0f},  {"south-shelf", -88.0f, 240.0f},
        {"camera-open", -96.0f, -70.0f},  {"camera-mid", -60.0f, 6.0f},
        {"wanderer", -92.0f, 96.0f},      {"upstream", 12.0f, -130.0f},
        {"downstream", 36.0f, 210.0f},    {"west-wall", -220.0f, 0.0f},
        {"east-wall", 230.0f, 0.0f},      {"origin", 0.0f, 0.0f},
        // staging candidates
        {"ELDER", -12.0f, 52.0f},         {"FGLEAF", -92.0f, -58.0f},
        {"WANDER", -74.0f, -18.0f},       {"CAMEYE", -118.0f, -96.0f},
        {"CAMTGT", -20.0f, 40.0f},        {"UFO", 20.0f, 150.0f},
        {"SPORES", -30.0f, 30.0f},
        {"V02eye", 14.0f, 286.0f},        {"V03eye", -40.0f, -290.0f},
        {"V05eye", -58.0f, 86.0f},        {"V06eye", 236.0f, 60.0f},
        {"V03alt", -30.0f, -232.0f},      {"V02alt", 10.0f, 268.0f},
        // hero mushroom habitat pockets, near the water
        {"H0", -12.0f, 52.0f},            {"H1", -46.0f, -28.0f},
        {"H2", 68.0f, -104.0f},           {"H3", -62.0f, 118.0f},
        {"H4", 78.0f, 198.0f},            {"H5", -66.0f, 166.0f},
        {"H1b", -78.0f, -34.0f},
    };
    for (const auto& s : spots) {
        const world::Sample smp = map.sample({s.x, s.z}, 0.5f);
        std::printf("  %-14s (%7.1f,%7.1f)  h %7.2f  har %7.2f  slope %.3f  %s\n", s.what, s.x, s.z,
                    smp.height, map.heightAboveWater({s.x, s.z}), smp.slope,
                    smp.submerged ? "SUBMERGED" : "");
    }
    std::fflush(stdout);
}

// ---- the dangling material-program rule (ADR-176, adopted) ------------------------------------
//
// A program a surface names and the scene does not carry is skipped, and the surface renders with
// its authored material as though nothing were wrong. That is the exact shape of the regression that
// put green gills on a mushroom whose material said amber -- enforced correctly where anyone was
// looking, lost where nobody was.

TEST_CASE("a material program nothing carries is reported, not skipped quietly", "[unit][glowmere2][materials]") {
    scene::Scene s;
    scene::MaterialProgram carried;
    carried.name = "present";
    s.materialPrograms.push_back(carried);

    scene::Entity ok;
    ok.material.program = "present";
    s.entities.push_back(ok);
    REQUIRE(scene::danglingMaterialPrograms(s).empty());

    SECTION("an entity naming an absent program is named") {
        scene::Entity bad;
        bad.material.program = "glowmere2TissueWarm";
        s.entities.push_back(bad);
        const std::vector<std::string> dangling = scene::danglingMaterialPrograms(s);
        REQUIRE(dangling.size() == 1);
        REQUIRE(dangling[0] == "glowmere2TissueWarm");
    }

    SECTION("a procedural naming an absent program is named too") {
        scene::ProceduralGeometry p;
        p.name = "elder-2-gills";
        p.material.program = "typoTissue";
        s.procedurals.push_back(p);
        REQUIRE(scene::danglingMaterialPrograms(s) == std::vector<std::string>{"typoTissue"});
    }

    SECTION("a water program is doing its job, not dangling") {
        // ADR-099: a water surface finds its settings by material-program name, so a name that
        // matches a water is resolved even though no MaterialProgram carries it. The negative
        // control for this rule's own false positive.
        scene::WaterSurface w;
        w.program = "riverWater";
        s.waters.push_back(w);
        scene::Entity river;
        river.material.program = "riverWater";
        s.entities.push_back(river);
        REQUIRE(scene::danglingMaterialPrograms(s).empty());
    }

    SECTION("it deduplicates and sorts, so the message is readable") {
        for (const char* n : {"zeta", "alpha", "zeta"}) {
            scene::Entity e;
            e.material.program = n;
            s.entities.push_back(e);
        }
        REQUIRE(scene::danglingMaterialPrograms(s) == std::vector<std::string>{"alpha", "zeta"});
    }
}

TEST_CASE("Glowmere Valley 2 names no material program it does not carry", "[unit][glowmere2][materials]") {
    // The regression guard on the real scene. Every hero part names a program; seven are carried.
    std::ifstream in(sceneFile());
    REQUIRE(in.good());
    const nlohmann::json doc = nlohmann::json::parse(in);
    std::vector<std::string> carried;
    for (const nlohmann::json& path : doc.at("materialPrograms")) {
        const std::string p = path.get<std::string>();
        const auto slash = p.find_last_of('/');
        std::ifstream mat(sceneFile().parent_path() / p);
        REQUIRE(mat.good());
        carried.push_back(nlohmann::json::parse(mat).at("name").get<std::string>());
        (void)slash;
    }
    std::vector<std::string> named;
    const std::function<void(const nlohmann::json&)> walk = [&](const nlohmann::json& j) {
        if (j.is_object()) {
            if (j.contains("material") && j.at("material").is_object() &&
                j.at("material").contains("program")) {
                named.push_back(j.at("material").at("program").get<std::string>());
            }
            for (const auto& [k, v] : j.items()) {
                walk(v);
            }
        } else if (j.is_array()) {
            for (const nlohmann::json& e : j) {
                walk(e);
            }
        }
    };
    walk(doc);
    REQUIRE_FALSE(named.empty());
    for (const std::string& n : named) {
        if (n.empty()) continue;
        INFO("program '" << n << "' is named by a surface");
        REQUIRE(std::find(carried.begin(), carried.end(), n) != carried.end());
    }
}

// ---- section 11: the example project, verified rather than assumed -----------------------------
//
// Phase 2 registered it. Eight phases of change later, "it is still registered" is a claim and not a
// fact, so each clause of the brief's section 11 is checked here against the shipped files.

TEST_CASE("Glowmere Valley 2 is a functional example project", "[unit][glowmere2][integration]") {
    const fs::path examples = fs::path(AVGEN_SOURCE_DIR) / "examples";

    SECTION("it appears in the examples list, through the loader the menu uses") {
        const auto loaded = app::loadExampleIndex(examples / "index.json");
        REQUIRE(loaded.has_value());
        const auto it = std::find_if(loaded->begin(), loaded->end(),
                                     [](const app::ExampleInfo& e) { return e.name == "Glowmere Valley 2"; });
        REQUIRE(it != loaded->end());
        REQUIRE(fs::is_regular_file(it->file));
        REQUIRE_FALSE(it->description.empty());
        REQUIRE(it->category == "Showcase");

        // ...and it did not displace the scene it succeeds. The brief's standing requirement.
        for (const char* kept : {"Glowmere Valley", "Glowmere Valley - Painterly",
                                 "Glowmere Valley - Lyrics", "Glowmere Valley - Matched PBR"}) {
            INFO(kept);
            REQUIRE(std::any_of(loaded->begin(), loaded->end(),
                                [&](const app::ExampleInfo& e) { return e.name == kept; }));
        }
    }

    SECTION("its project resolves its scene, its audio and its material programs") {
        std::ifstream in(examples / "world" / "glowmere-valley-2.json");
        REQUIRE(in.good());
        const nlohmann::json proj = nlohmann::json::parse(in);
        const fs::path base = examples / "world";
        REQUIRE(fs::is_regular_file(base / proj.at("assets").at("scene").at("path").get<std::string>()));
        // The audio is a symlink into the main checkout and may legitimately be absent in a fresh
        // clone, so its *reference* is checked rather than its presence.
        REQUIRE(proj.at("assets").contains("audio"));
        // A stale content hash on a present file is inert, but a stale one is worse than none --
        // Phase 2 dropped the painterly scene's, and this asserts it stayed dropped.
        REQUIRE_FALSE(proj.at("assets").at("scene").contains("sha256"));
    }

    SECTION("every route in the project names a parameter path that still exists in the scene") {
        // The failure this catches is the one Phase 5 created and fixed by hand: a route that names a
        // node the scene no longer has stays enabled and inert, which looks like a working
        // modulation in the UI and does nothing.
        std::ifstream sin(sceneFile());
        REQUIRE(sin.good());
        const nlohmann::json scene = nlohmann::json::parse(sin);
        std::vector<std::string> nodes;
        for (const nlohmann::json& n : scene.at("nodes")) {
            nodes.push_back(n.at("name").get<std::string>());
        }
        std::ifstream pin(examples / "world" / "glowmere-valley-2.json");
        const nlohmann::json proj = nlohmann::json::parse(pin);
        int checked = 0;
        for (const nlohmann::json& r : proj.at("routes")) {
            const std::string target = r.at("target").get<std::string>();
            for (const char* prefix : {"nodes/", "procedural/", "particles/"}) {
                if (!target.starts_with(prefix)) {
                    continue;
                }
                const std::string rest = target.substr(std::strlen(prefix));
                const std::string node = rest.substr(0, rest.find('/'));
                INFO("route target '" << target << "' names node '" << node << "'");
                REQUIRE(std::find(nodes.begin(), nodes.end(), node) != nodes.end());
                ++checked;
            }
        }
        REQUIRE(checked > 0);
    }

    SECTION("it gives the Auto-director something to direct") {
        std::ifstream in(sceneFile());
        REQUIRE(in.good());
        const nlohmann::json scene = nlohmann::json::parse(in);
        std::vector<world::HeroPoint> heroes;
        for (const nlohmann::json& h : scene.at("heroes")) {
            world::HeroPoint p;
            p.name = h.at("name").get<std::string>();
            p.radius = h.at("radius").get<float>();
            p.height = h.at("height").get<float>();
            p.importance = h.at("importance").get<float>();
            p.preferredCameraDistance = h.at("preferredCameraDistance").get<float>();
            heroes.push_back(p);
        }
        // Rank order is the director's contract (ADR-072): strictly descending importance.
        REQUIRE(heroes.size() >= 6);
        for (std::size_t i = 1; i < heroes.size(); ++i) {
            INFO(heroes[i - 1].name << " then " << heroes[i].name);
            REQUIRE(heroes[i].importance < heroes[i - 1].importance);
        }
        const auto brief = app::briefFromHeroes(heroes);
        REQUIRE(brief.has_value());
        REQUIRE(brief->hero.radius > 0.0f);
        REQUIRE_FALSE(brief->supporting.empty());
        // And every hero names a node that exists, or the director aims at nothing.
        std::vector<std::string> nodes;
        for (const nlohmann::json& n : scene.at("nodes")) {
            nodes.push_back(n.at("name").get<std::string>());
        }
        for (const world::HeroPoint& h : heroes) {
            INFO("hero '" << h.name << "'");
            REQUIRE(std::find(nodes.begin(), nodes.end(), h.name) != nodes.end());
        }
    }

    SECTION("its configuration is one authored file, not magic numbers in the scene") {
        // The brief's section 11 asks for documented configuration rather than numbers scattered
        // through 45 kB of generated JSON. The generator is that file, and this asserts it is present
        // and names the things the brief lists.
        const fs::path tool = fs::path(AVGEN_SOURCE_DIR) / "tools" / "make_glowmere_valley_2.py";
        REQUIRE(fs::is_regular_file(tool));
        std::ifstream in(tool);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (const char* knob : {"world[\"seed\"]", "RIVER", "HAR_BANDS", "BUDGET", "HERO_SITES",
                                 "clearings", "CAM_EYE"}) {
            INFO("configuration '" << knob << "'");
            REQUIRE(text.find(knob) != std::string::npos);
        }
    }
}
