// The city lattice (ADR-100). Planning knows nothing about assets, which is the whole point of
// separating it: the properties that make a lattice a *street* rather than a pattern can be checked
// exhaustively here, with no library, no device and no mesh.

#include "world/city.hpp"
#include "core/time.hpp"
#include <unistd.h>
#include <fstream>
#include "scene/composition.hpp"
#include <cmath>
#include <filesystem>
#include <algorithm>
#include "assets/asset_library.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <map>
#include <set>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

world::CityPlan planOrFail(const world::CitySettings& settings) {
    auto plan = world::planCity(settings);
    INFO((plan ? std::string{} : plan.error().message));
    REQUIRE(plan.has_value());
    return *plan;
}

} // namespace

TEST_CASE("A plan covers the extent its settings describe", "[world][city]") {
    world::CitySettings s;
    s.blocksX = 3;
    s.blocksZ = 2;
    s.blockCells = 4;
    s.roadCells = 1;
    s.moduleSize = 8.0f;
    const world::CityPlan plan = planOrFail(s);

    // A road on each side of every block, shared between neighbours: 3 blocks of 4 with 4 roads.
    CHECK(plan.width == 3 * 5 + 1);
    CHECK(plan.depth == 2 * 5 + 1);
    CHECK(plan.cells.size() == static_cast<std::size_t>(plan.width) * plan.depth);
    CHECK_THAT(s.extentX(), WithinAbs(static_cast<float>(plan.width) * 8.0f, 1e-3f));

    // The city is centred on the origin, so a shot framed at 0,0 is framed at the city.
    const glm::vec3 first = plan.centreOf({0, 0});
    const glm::vec3 last = plan.centreOf({plan.width - 1, plan.depth - 1});
    CHECK_THAT(first.x + last.x, WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(first.z + last.z, WithinAbs(0.0f, 1e-3f));
    // Adjacent cells are exactly one module apart, which is what makes tiles adjoin rather than
    // overlap or leave a seam.
    CHECK_THAT(plan.centreOf({1, 0}).x - first.x, WithinAbs(8.0f, 1e-3f));
}

TEST_CASE("Every road runs unbroken from one edge of the city to the other", "[world][city]") {
    // The property that makes this a street network and not a texture. A road band that stopped
    // halfway would look plausible from above and be impossible to drive or walk along, which is
    // exactly the kind of wrong that survives until somebody follows it.
    world::CitySettings s;
    s.blocksX = 4;
    s.blocksZ = 3;
    s.blockCells = 3;
    const world::CityPlan plan = planOrFail(s);

    // Which rows and columns are roads has to be read from a line that crosses the *blocks*, not
    // from the edge: row 0 is itself a road, so every cell in it is carriageway and inferring road
    // columns from it would call every column a road. (That was this test's own first bug.)
    const int insideBlockZ = s.roadCells + 1;
    const int insideBlockX = s.roadCells + 1;
    std::size_t columns = 0;
    for (int x = 0; x < plan.width; ++x) {
        if (!plan.isCarriageway({x, insideBlockZ})) {
            continue;
        }
        ++columns;
        for (int z = 0; z < plan.depth; ++z) {
            INFO("column " << x << " breaks at z=" << z << " (" << world::cellKindName(plan.kindAt({x, z})) << ")");
            CHECK(plan.isCarriageway({x, z}));
        }
    }
    std::size_t rows = 0;
    for (int z = 0; z < plan.depth; ++z) {
        if (!plan.isCarriageway({insideBlockX, z})) {
            continue;
        }
        ++rows;
        for (int x = 0; x < plan.width; ++x) {
            INFO("row " << z << " breaks at x=" << x);
            CHECK(plan.isCarriageway({x, z}));
        }
    }
    // One road per block edge, shared, plus the far side.
    CHECK(columns == static_cast<std::size_t>(s.blocksX + 1));
    CHECK(rows == static_cast<std::size_t>(s.blocksZ + 1));
}

TEST_CASE("A building never stands in the road, and never flush against one", "[world][city]") {
    // Two separate claims, and the second is the one that makes a street readable: a plot touching a
    // carriageway is a building with its front door in the traffic.
    world::CitySettings s;
    s.blocksX = 3;
    s.blocksZ = 3;
    s.blockCells = 4;
    s.plazaFraction = 0.0f; // every block built on, so the check has the most to catch
    const world::CityPlan plan = planOrFail(s);

    std::size_t plots = 0;
    for (int z = 0; z < plan.depth; ++z) {
        for (int x = 0; x < plan.width; ++x) {
            if (plan.kindAt({x, z}) != world::CellKind::Plot) {
                continue;
            }
            ++plots;
            INFO("plot at " << x << "," << z);
            CHECK_FALSE(plan.isCarriageway({x, z}));
            CHECK_FALSE(plan.isCarriageway({x - 1, z}));
            CHECK_FALSE(plan.isCarriageway({x + 1, z}));
            CHECK_FALSE(plan.isCarriageway({x, z - 1}));
            CHECK_FALSE(plan.isCarriageway({x, z + 1}));
        }
    }
    CHECK(plots > 0); // a city with no buildings would pass every check above
}

TEST_CASE("Every carriageway cell has a pavement beside it somewhere", "[world][city]") {
    // Where people walk. A road with no footway anywhere along it is a road nobody can use, and the
    // navigation work downstream has nothing to prefer.
    world::CitySettings s;
    s.plazaFraction = 0.0f;
    const world::CityPlan plan = planOrFail(s);
    CHECK(plan.countOf(world::CellKind::Pavement) > 0);

    // Every block that was built on has a ring of pavement round it.
    for (int z = 1; z < plan.depth - 1; ++z) {
        for (int x = 1; x < plan.width - 1; ++x) {
            if (plan.kindAt({x, z}) != world::CellKind::Plot) {
                continue;
            }
            // Walking outward from a plot in any direction must meet pavement before carriageway.
            for (const glm::ivec2 step : {glm::ivec2{1, 0}, glm::ivec2{-1, 0}, glm::ivec2{0, 1},
                                          glm::ivec2{0, -1}}) {
                glm::ivec2 c{x, z};
                bool sawPavement = false;
                for (int i = 0; i < plan.width + plan.depth; ++i) {
                    c += step;
                    const world::CellKind kind = plan.kindAt(c);
                    if (kind == world::CellKind::Pavement) {
                        sawPavement = true;
                        break;
                    }
                    if (plan.isCarriageway(c) || kind == world::CellKind::Empty) {
                        break;
                    }
                }
                INFO("from plot " << x << "," << z << " stepping " << step.x << "," << step.y);
                CHECK(sawPavement);
            }
        }
    }
}

TEST_CASE("A junction is where two roads actually cross", "[world][city]") {
    world::CitySettings s;
    const world::CityPlan plan = planOrFail(s);
    std::size_t junctions = 0;
    for (int z = 0; z < plan.depth; ++z) {
        for (int x = 0; x < plan.width; ++x) {
            if (plan.kindAt({x, z}) != world::CellKind::Junction) {
                continue;
            }
            ++junctions;
            // Carriageway on both axes: otherwise it is a road that has been mislabelled, and a
            // placer would put a crossroads mesh in the middle of a straight.
            INFO("junction at " << x << "," << z);
            const bool alongX = plan.isCarriageway({x - 1, z}) || plan.isCarriageway({x + 1, z});
            const bool alongZ = plan.isCarriageway({x, z - 1}) || plan.isCarriageway({x, z + 1});
            CHECK(alongX);
            CHECK(alongZ);
        }
    }
    CHECK(junctions == static_cast<std::size_t>((s.blocksX + 1) * (s.blocksZ + 1)));
}

TEST_CASE("A crossing only appears on a road, next to a junction", "[world][city]") {
    world::CitySettings s;
    s.crossingFraction = 1.0f; // every eligible cell, so the rule has the most chance to be broken
    const world::CityPlan plan = planOrFail(s);
    std::size_t crossings = 0;
    for (int z = 0; z < plan.depth; ++z) {
        for (int x = 0; x < plan.width; ++x) {
            if (plan.kindAt({x, z}) != world::CellKind::Crossing) {
                continue;
            }
            ++crossings;
            INFO("crossing at " << x << "," << z);
            const bool nextToJunction = plan.kindAt({x - 1, z}) == world::CellKind::Junction ||
                                        plan.kindAt({x + 1, z}) == world::CellKind::Junction ||
                                        plan.kindAt({x, z - 1}) == world::CellKind::Junction ||
                                        plan.kindAt({x, z + 1}) == world::CellKind::Junction;
            CHECK(nextToJunction);
        }
    }
    CHECK(crossings > 0);

    world::CitySettings none = s;
    none.crossingFraction = 0.0f;
    CHECK(planOrFail(none).countOf(world::CellKind::Crossing) == 0);
}

TEST_CASE("The same settings plan the same city", "[world][city]") {
    // ADR-091. A world that is not reproducible cannot be rendered offline, and a city that shifted
    // between the preview and the render would be discovered at the worst possible moment.
    world::CitySettings s;
    s.seed = 4321;
    const world::CityPlan a = planOrFail(s);
    const world::CityPlan b = planOrFail(s);
    REQUIRE(a.cells.size() == b.cells.size());
    for (std::size_t i = 0; i < a.cells.size(); ++i) {
        INFO("cell " << i);
        CHECK(a.cells[i].kind == b.cells[i].kind);
        CHECK(a.cells[i].rotation == b.cells[i].rotation);
    }

    // ...and a different seed plans a different one, or the seed is decorative.
    world::CitySettings other = s;
    other.seed = 99;
    const world::CityPlan c = planOrFail(other);
    bool differs = false;
    for (std::size_t i = 0; i < a.cells.size() && !differs; ++i) {
        differs = a.cells[i].kind != c.cells[i].kind;
    }
    CHECK(differs);
}

TEST_CASE("A plaza is a whole block, not a block with holes in it", "[world][city]") {
    world::CitySettings s;
    s.plazaFraction = 1.0f; // every block open
    s.blocksX = 2;
    s.blocksZ = 2;
    const world::CityPlan plan = planOrFail(s);
    CHECK(plan.countOf(world::CellKind::Plot) == 0);
    CHECK(plan.countOf(world::CellKind::Plaza) > 0);

    // Every plaza cell of a block is a plaza: scattered open cells would be a block with holes,
    // which is not somewhere a stage or a crowd can go.
    std::set<std::pair<int, int>> plazaBlocks;
    for (const world::CityCell& cell : plan.cells) {
        if (cell.kind == world::CellKind::Plaza) {
            plazaBlocks.emplace(cell.block.x, cell.block.y);
        }
    }
    for (const world::CityCell& cell : plan.cells) {
        if (cell.block.x < 0) {
            continue;
        }
        if (plazaBlocks.count({cell.block.x, cell.block.y}) != 0) {
            INFO("block " << cell.block.x << "," << cell.block.y);
            CHECK(cell.kind == world::CellKind::Plaza);
        }
    }
}

TEST_CASE("A block too small to hold a building is refused", "[world][city]") {
    // A block is a ring of pavement round a core of plots. Two cells across is all ring: every cell
    // touches a road, so there is nowhere a building can stand that is not also the footway. The
    // first version of this planner accepted it and produced a city of roads and pavements with no
    // buildings in it -- a plan that passes every count and renders as an empty grid.
    for (int cells : {1, 2}) {
        world::CitySettings s;
        s.blockCells = cells;
        const auto r = world::planCity(s);
        INFO("blockCells " << cells);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().message.find("footway") != std::string::npos);
    }
    // Three is the smallest that works, and it must actually have both.
    world::CitySettings smallest;
    smallest.blockCells = 3;
    smallest.plazaFraction = 0.0f;
    const world::CityPlan plan = planOrFail(smallest);
    CHECK(plan.countOf(world::CellKind::Plot) > 0);
    CHECK(plan.countOf(world::CellKind::Pavement) > 0);
}

TEST_CASE("Settings that cannot make a city are refused by name", "[world][city]") {
    const auto refused = [](world::CitySettings s) {
        const auto r = world::planCity(s);
        CHECK_FALSE(r.has_value());
        return r.has_value() ? std::string{} : r.error().message;
    };
    world::CitySettings zeroBlocks;
    zeroBlocks.blocksX = 0;
    CHECK(refused(zeroBlocks).find("block") != std::string::npos);

    world::CitySettings zeroModule;
    zeroModule.moduleSize = 0.0f;
    CHECK(refused(zeroModule).find("moduleSize") != std::string::npos);

    world::CitySettings huge;
    huge.blocksX = 400;
    huge.blocksZ = 400;
    huge.blockCells = 8;
    CHECK(refused(huge).find("too large") != std::string::npos);

    world::CitySettings badFraction;
    badFraction.plazaFraction = 2.0f;
    CHECK(refused(badFraction).find("plazaFraction") != std::string::npos);
}

// ---- placing (ADR-100, second half) -------------------------------------------------------------

namespace {

std::filesystem::path cityPiecesManifest() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "city-pieces.manifest.json";
}

} // namespace

TEST_CASE("The placer dresses every cell the library has a piece for", "[world][city][place]") {
    if (!std::filesystem::exists(cityPiecesManifest())) {
        SKIP("assets/city-pieces.manifest.json is not present");
    }
    auto library = assets::AssetLibrary::loadFile(cityPiecesManifest());
    INFO((library ? std::string{} : library.error().message));
    REQUIRE(library.has_value());
    const world::CityLibrary roles = world::CityLibrary::fromTags(*library);
    CHECK_FALSE(roles.road.empty());
    CHECK_FALSE(roles.junction.empty());

    world::CitySettings s;
    s.blocksX = 2;
    s.blocksZ = 2;
    s.blockCells = 3;
    s.plazaFraction = 0.0f;
    const world::CityPlan plan = planOrFail(s);

    auto placed = world::placeCity(plan, roles, *library);
    INFO((placed ? std::string{} : placed.error().message));
    REQUIRE(placed.has_value());
    CHECK(placed->instanceCount() > 0);

    // One instance per dressable cell, no more and no fewer. A tile placed twice is z-fighting and a
    // tile missed is a hole in the road, and both read as "the city looks a bit wrong".
    // A plot counts twice: a building, and the ground it stands on. Every other cell is its own
    // floor -- a road tile *is* the road -- but a building is a solid thing on top of something, and
    // a plot given only a building has the void around its feet.
    const std::size_t dressable =
        plan.countOf(world::CellKind::Road) + plan.countOf(world::CellKind::Junction) +
        plan.countOf(world::CellKind::Crossing) + plan.countOf(world::CellKind::Pavement) +
        plan.countOf(world::CellKind::Courtyard) + plan.countOf(world::CellKind::Plot) * 2;
    CHECK(placed->instanceCount() == dressable);

    // Including the buildings, which is what distinguishes a city from a road layout. A plot left
    // undressed is not an error and still renders -- as an empty block -- so the report is the only
    // thing that would say why.
    CHECK(plan.countOf(world::CellKind::Plot) > 0);
    CHECK(std::ranges::find(placed->undressed, std::string("plot")) == placed->undressed.end());

    // The negative control. Without it the check above passes just as well against a placer that
    // never reports anything at all. The gap is made here rather than borrowed from the manifest:
    // an earlier version of this control relied on plazas having no piece, and silently stopped
    // testing anything the moment one was tagged.
    world::CityLibrary stripped = roles;
    stripped.plaza.clear();
    world::CitySettings openSettings = s;
    openSettings.plazaFraction = 1.0f;
    const world::CityPlan open = planOrFail(openSettings);
    REQUIRE(open.countOf(world::CellKind::Plaza) > 0);
    auto openPlaced = world::placeCity(open, stripped, *library);
    REQUIRE(openPlaced.has_value());
    CHECK(std::ranges::find(openPlaced->undressed, std::string("plaza")) !=
          openPlaced->undressed.end());
}

TEST_CASE("A placed tile is one module across and square to the lattice", "[world][city][place]") {
    // The two properties that make tiles *meet*. A piece scaled by its height instead of its
    // footprint leaves a seam; a piece given a few degrees of yaw "for variation" no longer adjoins
    // the one beside it, which is the most visible way a tiled city goes wrong.
    if (!std::filesystem::exists(cityPiecesManifest())) {
        SKIP("assets/city-pieces.manifest.json is not present");
    }
    auto library = assets::AssetLibrary::loadFile(cityPiecesManifest());
    REQUIRE(library.has_value());
    const world::CityLibrary roles = world::CityLibrary::fromTags(*library);

    world::CitySettings s;
    s.moduleSize = 8.0f;
    s.blocksX = 1;
    s.blocksZ = 1;
    s.blockCells = 3;
    const world::CityPlan plan = planOrFail(s);
    auto placed = world::placeCity(plan, roles, *library);
    REQUIRE(placed.has_value());

    std::set<std::pair<int, int>> occupied;
    for (const world::CityPlacement& p : placed->placements) {
        // Ground pieces only. A building is not a tile: it has no neighbour to meet across its
        // edges, and it is scaled to its plot rather than to the pack (see "A building fits its
        // plot whatever size it was drawn").
        if (p.kind == world::CellKind::Plot) {
            continue;
        }
        const assets::AssetDescriptor* asset = library->find(p.asset);
        REQUIRE(asset != nullptr);
        REQUIRE(p.cloud != nullptr);
        const auto scales = p.cloud->scales();
        const auto rotations = p.cloud->rotations();
        const auto positions = p.cloud->positions();
        for (std::size_t i = 0; i < p.cloud->count(); ++i) {
            // The *tile* is one module across -- not the bounding box, which includes decoration
            // the artist meant to overhang. `road-side` bounds 1.0 x 1.31 because of its kerb, and
            // an earlier version of the placer scaled by that box and shrank the tile to 6.1 m of
            // an 8 m cell, leaving a gap beside every one of them.
            const float tileAfter = s.tileUnits * scales[i].x;
            INFO(p.asset << " instance " << i << " (bounds " << asset->naturalSize.x << " x "
                         << asset->naturalSize.z << ")");
            CHECK_THAT(tileAfter, WithinAbs(s.moduleSize, 1e-3f));
            // Yaw is a quarter turn and nothing else: the quaternion's x and z stay zero.
            CHECK_THAT(rotations[i].x, WithinAbs(0.0f, 1e-5f));
            CHECK_THAT(rotations[i].z, WithinAbs(0.0f, 1e-5f));
            // Every instance sits on a distinct cell centre.
            const int cx = static_cast<int>(std::lround(positions[i].x / s.moduleSize));
            const int cz = static_cast<int>(std::lround(positions[i].z / s.moduleSize));
            CHECK(occupied.insert({cx, cz}).second);
        }
    }
}

TEST_CASE("The same plan places the same city", "[world][city][place]") {
    if (!std::filesystem::exists(cityPiecesManifest())) {
        SKIP("assets/city-pieces.manifest.json is not present");
    }
    auto library = assets::AssetLibrary::loadFile(cityPiecesManifest());
    REQUIRE(library.has_value());
    const world::CityLibrary roles = world::CityLibrary::fromTags(*library);
    world::CitySettings s;
    s.seed = 77;
    const world::CityPlan plan = planOrFail(s);

    auto a = world::placeCity(plan, roles, *library);
    auto b = world::placeCity(plan, roles, *library);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->placements.size() == b->placements.size());
    for (std::size_t i = 0; i < a->placements.size(); ++i) {
        INFO("placement " << i);
        CHECK(a->placements[i].asset == b->placements[i].asset);
        REQUIRE(a->placements[i].cloud->count() == b->placements[i].cloud->count());
        const auto pa = a->placements[i].cloud->positions();
        const auto pb = b->placements[i].cloud->positions();
        for (std::size_t k = 0; k < pa.size(); ++k) {
            CHECK_THAT(pa[k].x, WithinAbs(pb[k].x, 1e-5f));
            CHECK_THAT(pa[k].z, WithinAbs(pb[k].z, 1e-5f));
        }
    }
}

TEST_CASE("Which piece a cell gets does not depend on the cells before it", "[world][city][place]") {
    // Drawn per cell from its own coordinates rather than from one running stream, so growing the
    // city by a block does not reshuffle the part that was already there. A placer that used a
    // single stream would repaint the whole city every time a setting changed.
    if (!std::filesystem::exists(cityPiecesManifest())) {
        SKIP("assets/city-pieces.manifest.json is not present");
    }
    auto library = assets::AssetLibrary::loadFile(cityPiecesManifest());
    REQUIRE(library.has_value());
    const world::CityLibrary roles = world::CityLibrary::fromTags(*library);

    world::CitySettings small;
    small.blocksX = 1;
    small.blocksZ = 1;
    small.blockCells = 3;
    small.seed = 5;
    world::CitySettings big = small;
    big.blocksX = 2;

    const auto placedSmall = world::placeCity(planOrFail(small), roles, *library);
    const auto placedBig = world::placeCity(planOrFail(big), roles, *library);
    REQUIRE(placedSmall.has_value());
    REQUIRE(placedBig.has_value());

    // Both cities are centred on the origin, so compare by cell *kind* at matching plan coordinates
    // instead: the piece chosen for a given coordinate must be the same in both.
    const world::CityPlan planSmall = planOrFail(small);
    const world::CityPlan planBig = planOrFail(big);
    const auto assetAt = [&](const world::PlacedCity& city, const world::CityPlan& plan,
                             glm::ivec2 c) -> std::string {
        const glm::vec3 want = plan.centreOf(c);
        for (const world::CityPlacement& p : city.placements) {
            const auto positions = p.cloud->positions();
            for (const glm::vec3& pos : positions) {
                if (std::abs(pos.x - want.x) < 1e-3f && std::abs(pos.z - want.z) < 1e-3f) {
                    return p.asset;
                }
            }
        }
        return {};
    };
    int compared = 0;
    for (int z = 0; z < planSmall.depth; ++z) {
        for (int x = 0; x < planSmall.width; ++x) {
            if (planSmall.kindAt({x, z}) != planBig.kindAt({x, z})) {
                continue; // the bigger plan is a different shape here; nothing to compare
            }
            const std::string sa = assetAt(*placedSmall, planSmall, {x, z});
            const std::string ba = assetAt(*placedBig, planBig, {x, z});
            if (sa.empty() || ba.empty()) {
                continue;
            }
            INFO("cell " << x << "," << z);
            CHECK(sa == ba);
            ++compared;
        }
    }
    CHECK(compared > 0);
}

TEST_CASE("A library with nothing in it is refused rather than placing nothing", "[world][city][place]") {
    auto library = assets::AssetLibrary::loadFile(cityPiecesManifest());
    if (!library) {
        SKIP("assets/city-pieces.manifest.json is not present");
    }
    const world::CityPlan plan = planOrFail(world::CitySettings{});
    const world::CityLibrary empty;
    const auto r = world::placeCity(plan, empty, *library);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find("tagged") != std::string::npos);
}

// ---- the node that installs one ------------------------------------------------------------------

TEST_CASE("A city node turns into instanced geometry", "[world][city][node]") {
    // The seam to the engine. A `city` node carries settings only -- a scatter cloud is runtime
    // state and is never serialised -- so the plan and the placements are made again on every
    // rebuild, exactly as a terrain's ecology scatter is. This is what makes a city survive a save.
    const std::filesystem::path scene =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "city" / "first-block.scene.json";
    if (!std::filesystem::exists(scene) || !std::filesystem::exists(cityPiecesManifest())) {
        SKIP("the first-block example is not present in this checkout");
    }
    assets::AssetRegistry registry;
    // The base the scene's relative paths resolve against, which `Engine::loadComposition` sets
    // before loading. Without it the city's tiling manifest is looked for in the wrong place, the
    // rebuild warns, and the scene comes back with no city in it -- which looks exactly like the
    // city not working.
    registry.setBaseDirectory(scene.parent_path());
    auto composition = scene::Composition::loadFile(scene, registry);
    INFO((composition ? std::string{} : composition.error().message));
    REQUIRE(composition.has_value());

    // A composition is built lazily: loading parses the file and `update` is what flattens the
    // nodes into a scene. Reading `procedurals` before one would find an empty list and say nothing
    // about whether the city works.
    (*composition)->update(FrameTime{});

    const scene::CompositionNode* node = (*composition)->findNode("city");
    REQUIRE(node != nullptr);
    CHECK(node->kind == scene::NodeKind::City);
    // Against the file, not against a number typed here: the example is art direction and its
    // settings get retuned. A test that hardcodes one of them fails every time somebody makes the
    // city look better, which teaches people to edit the test rather than read it.
    const nlohmann::json raw = nlohmann::json::parse(std::ifstream(scene));
    CHECK(node->city.blockCells == raw["nodes"][0]["city"]["blockCells"].get<int>());
    CHECK(node->city.moduleSize == raw["nodes"][0]["city"]["moduleSize"].get<float>());
    CHECK(node->cityCells > 0); // the rebuild planned it

    // One instanced object per distinct piece, each carrying a cloud rather than a single transform.
    const auto& procedurals = (*composition)->scene().procedurals;
    std::size_t cityObjects = 0;
    std::size_t instances = 0;
    for (const scene::ProceduralGeometry& pg : procedurals) {
        if (pg.name.find("city_") == std::string::npos) {
            continue;
        }
        ++cityObjects;
        REQUIRE(pg.distribution.kind == scene::DistributionKind::Scatter);
        REQUIRE(pg.distribution.scatterCloud != nullptr);
        instances += pg.distribution.scatterCloud->count();
        // The mesh is resolved, not merely named. Setting `source.asset` alone generates nothing --
        // the first version of this did exactly that and rendered an empty frame while reporting
        // 152 instances placed.
        CHECK(pg.source.kind == scene::PrimitiveKind::Mesh);
        CHECK(pg.source.assetMesh != nullptr);
        CHECK(pg.lod.cull);
    }
    CHECK(cityObjects > 0);
    CHECK(instances > 0);
}

TEST_CASE("A city node round-trips through a scene file", "[world][city][node]") {
    const std::filesystem::path scene =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "city" / "first-block.scene.json";
    if (!std::filesystem::exists(scene)) {
        SKIP("the first-block example is not present in this checkout");
    }
    assets::AssetRegistry registry;
    registry.setBaseDirectory(scene.parent_path());
    auto first = scene::Composition::loadFile(scene, registry);
    REQUIRE(first.has_value());
    (*first)->update(FrameTime{});
    const nlohmann::json doc = (*first)->toJson();

    auto second = scene::Composition::fromJson(doc, registry);
    INFO((second ? std::string{} : second.error().message));
    REQUIRE(second.has_value());
    (*second)->update(FrameTime{});
    const scene::CompositionNode* a = (*first)->findNode("city");
    const scene::CompositionNode* b = (*second)->findNode("city");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(b->kind == scene::NodeKind::City);
    CHECK(b->city.seed == a->city.seed);
    CHECK(b->city.blocksX == a->city.blocksX);
    CHECK(b->city.blockCells == a->city.blockCells);
    CHECK_THAT(b->city.moduleSize, WithinAbs(a->city.moduleSize, 1e-5f));
    CHECK_THAT(b->city.tileUnits, WithinAbs(a->city.tileUnits, 1e-5f));
    // And it planned the same city on the way back in, which is the point of carrying settings
    // rather than placements.
    CHECK(b->cityCells == a->cityCells);
}

TEST_CASE("A city whose settings cannot be planned is refused at load", "[world][city][node]") {
    // Refused when the file is read, not when the frame is drawn: a scene that cannot make a city
    // is a file somebody has to fix, and a warning during rebuild is a thing nobody sees.
    const auto dir = std::filesystem::temp_directory_path() /
                     ("avgen_city_bad_" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::create_directories(dir);
    const auto file = dir / "bad.scene.json";
    std::ofstream(file) << R"({"format":"avgen-scene","version":1,"name":"bad","nodes":[
        {"name":"city","kind":"city","city":{"blockCells":1}}]})";
    assets::AssetRegistry registry;
    const auto loaded = scene::Composition::loadFile(file, registry);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("footway") != std::string::npos);
    std::filesystem::remove_all(dir);
}

// ---- buildings ----------------------------------------------------------------------------------

TEST_CASE("Every building faces a street", "[world][city]") {
    // A plan property, so it needs no library: which way a building faces is decided by where the
    // roads are, and a row of buildings each facing a different way is the clearest sign that a city
    // was scattered rather than laid out.
    world::CitySettings s;
    s.blocksX = 3;
    s.blocksZ = 3;
    s.blockCells = 5;
    s.plazaFraction = 0.0f;
    s.seed = 1234u;
    const world::CityPlan plan = planOrFail(s);
    REQUIRE(plan.countOf(world::CellKind::Plot) > 0);

    std::size_t checked = 0;
    for (int z = 0; z < plan.depth; ++z) {
        for (int x = 0; x < plan.width; ++x) {
            const world::CityCell cell = plan.at({x, z});
            if (cell.kind != world::CellKind::Plot) {
                continue;
            }
            // The direction the quarter turn points, as a unit step. A piece faces +Z at rest.
            static constexpr glm::ivec2 kFacing[4] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
            const glm::ivec2 dir = kFacing[static_cast<int>(cell.rotation)];

            // Walk that way. A carriageway must be reachable without passing through another plot's
            // building: a house whose front door opens onto the back of the next house is exactly
            // the failure this rule exists to prevent.
            bool reachedRoad = false;
            for (int step = 1; step <= s.blockCells + 2; ++step) {
                const glm::ivec2 probe{x + dir.x * step, z + dir.y * step};
                if (!plan.inBounds(probe)) {
                    break;
                }
                if (plan.isCarriageway(probe)) {
                    reachedRoad = true;
                    break;
                }
                if (plan.kindAt(probe) == world::CellKind::Plot) {
                    break;
                }
            }
            INFO("plot at " << x << "," << z << " faces quarter "
                            << static_cast<int>(cell.rotation));
            CHECK(reachedRoad);
            ++checked;
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("A building faces the nearest street, not just any street", "[world][city]") {
    // The negative control for the test above, which a plan that faced every building the same way
    // would also pass on a small block. An edge plot has one street closer than the rest, and that
    // is the one it must face.
    world::CitySettings s;
    s.blocksX = 1;
    s.blocksZ = 1;
    s.blockCells = 7;
    s.plazaFraction = 0.0f;
    const world::CityPlan plan = planOrFail(s);

    std::set<int> facings;
    for (int z = 0; z < plan.depth; ++z) {
        for (int x = 0; x < plan.width; ++x) {
            const world::CityCell cell = plan.at({x, z});
            if (cell.kind != world::CellKind::Plot) {
                continue;
            }
            facings.insert(static_cast<int>(cell.rotation));
            // Distance to the street it faces, against the distance to the nearest street at all.
            static constexpr glm::ivec2 kDirs[4] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
            const auto stepsTo = [&plan, &s](glm::ivec2 from, glm::ivec2 dir) {
                for (int step = 1; step <= s.blockCells + 2; ++step) {
                    const glm::ivec2 probe{from.x + dir.x * step, from.y + dir.y * step};
                    if (!plan.inBounds(probe)) {
                        return 9999;
                    }
                    if (plan.isCarriageway(probe)) {
                        return step;
                    }
                }
                return 9999;
            };
            int nearest = 9999;
            for (const glm::ivec2 d : kDirs) {
                nearest = std::min(nearest, stepsTo({x, z}, d));
            }
            const int faced = stepsTo({x, z}, kDirs[static_cast<int>(cell.rotation)]);
            INFO("plot at " << x << "," << z);
            CHECK(faced == nearest);
        }
    }
    // And they do not all face the same way, which is what a block of buildings looks like when the
    // rule is written but never actually applied.
    CHECK(facings.size() > 1);
}

TEST_CASE("A block builds from one family", "[world][city][place]") {
    if (!std::filesystem::exists(cityPiecesManifest())) {
        SKIP("assets/city-pieces.manifest.json is not present");
    }
    auto library = assets::AssetLibrary::loadFile(cityPiecesManifest());
    REQUIRE(library.has_value());
    const world::CityLibrary roles = world::CityLibrary::fromTags(*library);
    // More than one family, or the property below holds for reasons that have nothing to do with the
    // code under test.
    REQUIRE(roles.plotFamilies.size() > 1);

    world::CitySettings s;
    s.blocksX = 4;
    s.blocksZ = 4;
    s.blockCells = 5;
    s.plazaFraction = 0.0f;
    s.seed = 99u;
    const world::CityPlan plan = planOrFail(s);

    // Which family each asset belongs to.
    std::map<std::string, std::string> familyOf;
    for (const auto& [family, names] : roles.plotFamilies) {
        for (const std::string& n : names) {
            familyOf[n] = family;
        }
    }

    auto placed = world::placeCity(plan, roles, *library);
    REQUIRE(placed.has_value());

    // Every building instance, back to the block it stands on, by position.
    std::map<std::pair<int, int>, std::set<std::string>> familiesPerBlock;
    for (const world::CityPlacement& p : placed->placements) {
        if (p.kind != world::CellKind::Plot) {
            continue;
        }
        const auto it = familyOf.find(p.asset);
        REQUIRE(it != familyOf.end());
        for (const glm::vec3& pos : p.cloud->positions()) {
            // Back to cell coordinates, then to the block that cell belongs to.
            const float m = s.moduleSize;
            const int cx = static_cast<int>(std::floor(
                (pos.x + static_cast<float>(plan.width) * m * 0.5f) / m));
            const int cz = static_cast<int>(std::floor(
                (pos.z + static_cast<float>(plan.depth) * m * 0.5f) / m));
            const glm::ivec2 block = plan.at({cx, cz}).block;
            REQUIRE(block.x >= 0);
            familiesPerBlock[{block.x, block.y}].insert(it->second);
        }
    }
    REQUIRE(familiesPerBlock.size() > 1);
    std::set<std::string> distinct;
    for (const auto& [block, families] : familiesPerBlock) {
        INFO("block " << block.first << "," << block.second << " has " << families.size()
                      << " families");
        CHECK(families.size() == 1);
        distinct.insert(*families.begin());
    }
    // And the whole city is not one family either, which is what "every block picks one family"
    // degenerates to if the choice ignores the block.
    CHECK(distinct.size() > 1);
}

TEST_CASE("A building fits its plot whatever size it was drawn", "[world][city][place]") {
    if (!std::filesystem::exists(cityPiecesManifest())) {
        SKIP("assets/city-pieces.manifest.json is not present");
    }
    auto library = assets::AssetLibrary::loadFile(cityPiecesManifest());
    REQUIRE(library.has_value());
    const world::CityLibrary roles = world::CityLibrary::fromTags(*library);

    world::CitySettings s;
    s.blocksX = 3;
    s.blocksZ = 3;
    s.blockCells = 4;
    s.plazaFraction = 0.0f;
    const world::CityPlan plan = planOrFail(s);
    auto placed = world::placeCity(plan, roles, *library);
    REQUIRE(placed.has_value());

    // The footprints really do differ, or the property below is about nothing.
    std::set<int> footprints;
    bool sawBuilding = false;
    for (const world::CityPlacement& p : placed->placements) {
        if (p.kind != world::CellKind::Plot) {
            continue;
        }
        const assets::AssetDescriptor* asset = library->find(p.asset);
        REQUIRE(asset != nullptr);
        const float footprint = std::max(asset->naturalSize.x, asset->naturalSize.z);
        footprints.insert(static_cast<int>(footprint * 100.0f));
        sawBuilding = true;

        for (const glm::vec3& scale : p.cloud->scales()) {
            // Uniform, so a building is never squashed to a square. A tower stays a tower.
            CHECK_THAT(scale.x, WithinAbs(scale.y, 1e-5f));
            CHECK_THAT(scale.y, WithinAbs(scale.z, 1e-5f));
            // And it covers its plot without overhanging it. This is the property the pack-wide tile
            // scale gets wrong: applied to a 1.3-unit building it gives a 10.4 m footprint on an 8 m
            // plot, which stands in the pavement.
            const float width = footprint * scale.x;
            INFO(p.asset << " is " << width << " m across an " << s.moduleSize << " m plot");
            CHECK(width <= s.moduleSize);
            CHECK(width > s.moduleSize * 0.5f);
        }
    }
    CHECK(sawBuilding);
    CHECK(footprints.size() > 1);
}

TEST_CASE("A yard has several things in it, inside the yard", "[world][city][place]") {
    if (!std::filesystem::exists(cityPiecesManifest())) {
        SKIP("assets/city-pieces.manifest.json is not present");
    }
    auto library = assets::AssetLibrary::loadFile(cityPiecesManifest());
    REQUIRE(library.has_value());
    const world::CityLibrary roles = world::CityLibrary::fromTags(*library);
    REQUIRE_FALSE(roles.prop.empty());

    world::CitySettings s;
    s.blocksX = 3;
    s.blocksZ = 3;
    s.blockCells = 5; // a core of 3x3, so the middle cell is a courtyard
    s.plazaFraction = 0.0f;
    s.seed = 31u;
    const world::CityPlan plan = planOrFail(s);
    const std::size_t courtyards = plan.countOf(world::CellKind::Courtyard);
    REQUIRE(courtyards > 0);

    auto placed = world::placeCity(plan, roles, *library);
    REQUIRE(placed.has_value());

    // Which assets are props, so prop instances can be told from the ground under them.
    std::set<std::string> propNames(roles.prop.begin(), roles.prop.end());

    std::size_t propInstances = 0;
    for (const world::CityPlacement& p : placed->placements) {
        if (!propNames.contains(p.asset)) {
            continue;
        }
        for (const glm::vec3& pos : p.cloud->positions()) {
            ++propInstances;
            // Inside the cell it belongs to. A prop that strays into the next cell is a tree in the
            // road, which is the whole reason `propSpread` is below half a module.
            const float m = s.moduleSize;
            const int cx = static_cast<int>(
                std::floor((pos.x + static_cast<float>(plan.width) * m * 0.5f) / m));
            const int cz = static_cast<int>(
                std::floor((pos.z + static_cast<float>(plan.depth) * m * 0.5f) / m));
            const world::CellKind kind = plan.kindAt({cx, cz});
            INFO(p.asset << " at " << pos.x << "," << pos.z << " landed on "
                         << world::cellKindName(kind));
            CHECK((kind == world::CellKind::Courtyard || kind == world::CellKind::Plaza));

            const glm::vec3 centre = plan.centreOf({cx, cz});
            CHECK(std::abs(pos.x - centre.x) <= m * s.propSpread + 1e-3f);
            CHECK(std::abs(pos.z - centre.z) <= m * s.propSpread + 1e-3f);
        }
    }
    // Several per yard on average, not one: a courtyard with a single tree dead in its middle reads
    // as a placed object rather than as a yard.
    INFO(propInstances << " props over " << courtyards << " courtyards");
    CHECK(propInstances > courtyards);

    // And not always the same number, or it is a pattern rather than a yard. Counted per cell.
    std::map<std::pair<int, int>, int> perCell;
    for (const world::CityPlacement& p : placed->placements) {
        if (!propNames.contains(p.asset)) {
            continue;
        }
        for (const glm::vec3& pos : p.cloud->positions()) {
            const float m = s.moduleSize;
            perCell[{static_cast<int>(
                         std::floor((pos.x + static_cast<float>(plan.width) * m * 0.5f) / m)),
                     static_cast<int>(
                         std::floor((pos.z + static_cast<float>(plan.depth) * m * 0.5f) / m))}]++;
        }
    }
    std::set<int> counts;
    for (const auto& [cell, n] : perCell) {
        counts.insert(n);
    }
    CHECK(counts.size() > 1);
}

TEST_CASE("Props are turned every which way, and tiles are not", "[world][city][place]") {
    if (!std::filesystem::exists(cityPiecesManifest())) {
        SKIP("assets/city-pieces.manifest.json is not present");
    }
    auto library = assets::AssetLibrary::loadFile(cityPiecesManifest());
    REQUIRE(library.has_value());
    const world::CityLibrary roles = world::CityLibrary::fromTags(*library);

    world::CitySettings s;
    s.blocksX = 3;
    s.blocksZ = 3;
    s.blockCells = 5;
    s.seed = 8u;
    const world::CityPlan plan = planOrFail(s);
    auto placed = world::placeCity(plan, roles, *library);
    REQUIRE(placed.has_value());

    std::set<std::string> propNames(roles.prop.begin(), roles.prop.end());
    bool sawOffAxisProp = false;
    for (const world::CityPlacement& p : placed->placements) {
        const bool isProp = propNames.contains(p.asset);
        for (const glm::vec4& r : p.cloud->rotations()) {
            // Yaw only, whatever the piece: nothing here should ever tilt.
            CHECK_THAT(r.x, WithinAbs(0.0f, 1e-5f));
            CHECK_THAT(r.z, WithinAbs(0.0f, 1e-5f));
            // A quarter turn has y in {0, +/-sin45, +/-1}. A prop is free of that, and at least one
            // must actually land off it -- a rule that is written but never fires is not a rule.
            const float y = std::abs(r.y);
            const bool quarter = y < 1e-4f || std::abs(y - 0.70710678f) < 1e-4f ||
                                 std::abs(y - 1.0f) < 1e-4f;
            if (isProp) {
                sawOffAxisProp = sawOffAxisProp || !quarter;
            } else {
                INFO(p.asset << " is a tile and must be square to the lattice");
                CHECK(quarter);
            }
        }
    }
    CHECK(sawOffAxisProp);
}

TEST_CASE("A block's ground belongs to its family", "[world][city][place]") {
    if (!std::filesystem::exists(cityPiecesManifest())) {
        SKIP("assets/city-pieces.manifest.json is not present");
    }
    auto library = assets::AssetLibrary::loadFile(cityPiecesManifest());
    REQUIRE(library.has_value());
    const world::CityLibrary roles = world::CityLibrary::fromTags(*library);
    // More than one family declares ground, or the property below holds for free.
    REQUIRE(roles.courtyardFamilies.size() > 1);

    world::CitySettings s;
    s.blocksX = 4;
    s.blocksZ = 4;
    s.blockCells = 5;
    s.plazaFraction = 0.0f;
    s.seed = 4242u;
    const world::CityPlan plan = planOrFail(s);

    // Ground assets, and which family each belongs to.
    std::map<std::string, std::string> familyOfGround;
    for (const auto& [family, names] : roles.courtyardFamilies) {
        for (const std::string& n : names) {
            familyOfGround[n] = family;
        }
    }

    auto placed = world::placeCity(plan, roles, *library);
    REQUIRE(placed.has_value());

    std::size_t checked = 0;
    for (const world::CityPlacement& p : placed->placements) {
        const auto it = familyOfGround.find(p.asset);
        if (it == familyOfGround.end()) {
            continue; // a prop, a building, or the unfamilied fallback ground
        }
        for (const glm::vec3& pos : p.cloud->positions()) {
            const float m = s.moduleSize;
            const int cx = static_cast<int>(
                std::floor((pos.x + static_cast<float>(plan.width) * m * 0.5f) / m));
            const int cz = static_cast<int>(
                std::floor((pos.z + static_cast<float>(plan.depth) * m * 0.5f) / m));
            const glm::ivec2 block = plan.at({cx, cz}).block;
            REQUIRE(block.x >= 0);
            INFO(p.asset << " is " << it->second << " ground on block " << block.x << ","
                         << block.y);
            CHECK(it->second == roles.familyFor(block, s.seed));
            ++checked;
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("A street steps rather than jumps", "[world][city][place]") {
    if (!std::filesystem::exists(cityPiecesManifest())) {
        SKIP("assets/city-pieces.manifest.json is not present");
    }
    auto library = assets::AssetLibrary::loadFile(cityPiecesManifest());
    REQUIRE(library.has_value());
    const world::CityLibrary roles = world::CityLibrary::fromTags(*library);

    world::CitySettings s;
    s.blocksX = 4;
    s.blocksZ = 4;
    s.blockCells = 6; // a 4x4 core: enough plots in a block to have a spread at all
    s.plazaFraction = 0.0f;
    s.seed = 77u;
    const world::CityPlan plan = planOrFail(s);
    auto placed = world::placeCity(plan, roles, *library);
    REQUIRE(placed.has_value());

    // How tall a building stands, as height over footprint: every building is scaled to the same
    // plot, so proportion is what decides it.
    const auto aspectOf = [&library](const std::string& name) -> float {
        const assets::AssetDescriptor* a = library->find(name);
        if (a == nullptr) {
            return 0.0f;
        }
        const float foot = std::max(a->naturalSize.x, a->naturalSize.z);
        return foot > 0.0f ? a->naturalSize.y / foot : a->naturalSize.y;
    };

    std::set<std::string> buildings(roles.plot.begin(), roles.plot.end());
    std::map<std::pair<int, int>, std::vector<float>> perBlock;
    std::map<std::pair<int, int>, std::string> familyOfBlock;
    for (const world::CityPlacement& p : placed->placements) {
        if (!buildings.contains(p.asset)) {
            continue;
        }
        const float aspect = aspectOf(p.asset);
        for (const glm::vec3& pos : p.cloud->positions()) {
            const float m = s.moduleSize;
            const int cx = static_cast<int>(
                std::floor((pos.x + static_cast<float>(plan.width) * m * 0.5f) / m));
            const int cz = static_cast<int>(
                std::floor((pos.z + static_cast<float>(plan.depth) * m * 0.5f) / m));
            const glm::ivec2 block = plan.at({cx, cz}).block;
            REQUIRE(block.x >= 0);
            perBlock[{block.x, block.y}].push_back(aspect);
            familyOfBlock[{block.x, block.y}] = roles.familyFor(block, s.seed);
        }
    }
    REQUIRE(perBlock.size() > 4);

    const auto spread = [](std::vector<float> v) {
        const auto [lo, hi] = std::ranges::minmax_element(v);
        return *hi - *lo;
    };

    // Against the block's *own family*, which is the comparison that isolates what is being tested.
    //
    // The first version of this test compared a block's spread against the whole city's and passed
    // against a deliberately broken placer, because the city spans three families (0.57 to 3.15) and
    // any one block draws from one of them -- so the ratio held on the strength of the family rule
    // alone, which was already working. It measured the wrong thing and would have gone on passing
    // if the height band were deleted.
    std::map<std::string, std::vector<float>> familyAspects;
    for (const auto& [family, names] : roles.plotFamilies) {
        for (const std::string& n : names) {
            familyAspects[family].push_back(aspectOf(n));
        }
    }

    float ratioTotal = 0.0f;
    std::size_t counted = 0;
    for (const auto& [block, aspects] : perBlock) {
        const std::string& family = familyOfBlock[block];
        const auto it = familyAspects.find(family);
        if (it == familyAspects.end() || aspects.size() < 4) {
            continue;
        }
        const float familySpread = spread(it->second);
        if (familySpread <= 0.0f) {
            continue; // a family whose pieces are all the same height says nothing either way
        }
        ratioTotal += spread(aspects) / familySpread;
        ++counted;
    }
    REQUIRE(counted > 3);
    const float ratio = ratioTotal / static_cast<float>(counted);

    // A block occupies a band of its family's range, not the whole of it. Drawing each plot
    // independently puts this near 1 on a block with a dozen plots and ten pieces to choose from.
    INFO(counted << " blocks, mean spread " << ratio << " of the family's own range");
    CHECK(ratio < 0.6f);
}
