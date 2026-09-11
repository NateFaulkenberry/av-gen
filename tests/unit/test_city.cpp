// The city lattice (ADR-100). Planning knows nothing about assets, which is the whole point of
// separating it: the properties that make a lattice a *street* rather than a pattern can be checked
// exhaustively here, with no library, no device and no mesh.

#include "world/city.hpp"
#include <cmath>
#include <filesystem>
#include <algorithm>
#include "assets/asset_library.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
    const std::size_t dressable =
        plan.countOf(world::CellKind::Road) + plan.countOf(world::CellKind::Junction) +
        plan.countOf(world::CellKind::Crossing) + plan.countOf(world::CellKind::Pavement);
    CHECK(placed->instanceCount() == dressable);

    // Buildings have no piece in this manifest yet, so the plots are reported as undressed rather
    // than quietly skipped -- a city missing its buildings still renders, as an empty grid.
    CHECK(std::ranges::find(placed->undressed, std::string("plot")) != placed->undressed.end());
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
