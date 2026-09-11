#pragma once

// The city as a lattice (ADR-100).
//
// This engine places things in two ways: an author puts a node somewhere, or the composer scatters
// by density. A forest is a density; a street is not. This is the third producer -- a grid of cells,
// each with a kind and a quarter turn, from which road tiles, pavements and building plots fall out
// in a way that *adjoins*.
//
// ## Why planning is separate from placing
//
// Nothing in this header knows what an asset is. A plan is a grid of enum values, so the properties
// that actually matter -- that a road runs unbroken from one edge to the other, that no plot sits in
// a carriageway, that a junction really has roads on the sides it claims -- can be checked
// exhaustively without a library, a device, a mesh or a terrain. Placement is the separate step
// where assets, scales and the ground enter, and it can then be wrong in ways that are visible
// rather than structural.
//
// ## Determinism
//
// `planCity` is a pure function of `CitySettings` (ADR-091). Same settings, same city, every time --
// for the same reason `world::composeWorld` is: a world that is not reproducible cannot be rendered
// offline.

#include "core/error.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::world {

// What a cell is. The kinds are the vocabulary a placer turns into meshes, and they are deliberately
// few: everything a city needs to *look* like a city is a road, the pavement beside it, the plot
// behind that, and the open space a plaza makes by leaving plots out.
enum class CellKind : std::uint8_t {
    Empty,      // outside the city, or a gap left on purpose
    Road,       // carriageway, running along one axis
    Junction,   // where two roads cross
    Crossing,   // a pedestrian crossing laid over a road cell
    Pavement,   // the footway beside a road; where people walk
    Plot,       // a building stands here
    Plaza,      // deliberately open: the space a stage or a crowd needs
};
[[nodiscard]] const char* cellKindName(CellKind kind);

// Which way a piece faces, in quarter turns anticlockwise about +Y. A road tile is modelled along
// one axis and a junction is symmetric, so this is all the orientation a lattice needs.
enum class Quarter : std::uint8_t { Zero, One, Two, Three };
[[nodiscard]] float quarterRadians(Quarter q);

struct CityCell {
    CellKind kind = CellKind::Empty;
    Quarter rotation = Quarter::Zero;
    // Which block this cell belongs to, or (-1,-1) for the street network between them. Carried so a
    // placer can vary a block's character without re-deriving which cells are in it.
    glm::ivec2 block{-1, -1};
};

struct CitySettings {
    // Metres per cell. The lattice pitch, and the single number every asset pack is scaled against
    // (ADR-100): Kenney's kit is 1x1 *units* with a 0.79-unit person, which is not metres.
    float moduleSize = 8.0f;
    // Blocks in each direction, and how many plot cells a block is across. The street network runs
    // between blocks, so a 3x3 city of 4-cell blocks is 3*4 + 4 = 16 cells across: a road on each
    // side of every block, sharing the roads between neighbours.
    int blocksX = 3;
    int blocksZ = 3;
    int blockCells = 4;
    // How wide a road is, in cells. Two is a carriageway each way and is what the Kenney road pieces
    // are modelled for; one reads as an alley.
    int roadCells = 1;
    std::uint32_t seed = 0;
    // Blocks left open instead of built on, as a fraction. A city with no plazas has nowhere to put
    // a stage, a crowd or a shot that needs air.
    float plazaFraction = 0.12f;
    // Crossings are placed on road cells adjacent to a junction. 0 leaves them out.
    float crossingFraction = 0.5f;

    [[nodiscard]] Result<void> validate() const;
    // Cells across the whole plan, in each direction.
    [[nodiscard]] int width() const;
    [[nodiscard]] int depth() const;
    // The world extent the plan covers, in metres.
    [[nodiscard]] float extentX() const;
    [[nodiscard]] float extentZ() const;
};

// A planned city. Row-major, `width * depth` cells, indexed from the -X/-Z corner.
struct CityPlan {
    CitySettings settings;
    int width = 0;
    int depth = 0;
    std::vector<CityCell> cells;

    [[nodiscard]] bool inBounds(glm::ivec2 c) const {
        return c.x >= 0 && c.y >= 0 && c.x < width && c.y < depth;
    }
    // The cell at a coordinate; an Empty cell for anything outside, so callers may look at a
    // neighbour without checking the edge first. That is the whole reason it returns by value.
    [[nodiscard]] CityCell at(glm::ivec2 c) const;
    [[nodiscard]] CellKind kindAt(glm::ivec2 c) const { return at(c).kind; }
    // Where a cell's centre is in the world, with the city centred on the origin.
    [[nodiscard]] glm::vec3 centreOf(glm::ivec2 c) const;
    // True when this cell carries traffic: a road, a junction or a crossing.
    [[nodiscard]] bool isCarriageway(glm::ivec2 c) const;
    [[nodiscard]] std::size_t countOf(CellKind kind) const;
};

// The plan. Pure, and a pure function of `settings`.
[[nodiscard]] Result<CityPlan> planCity(const CitySettings& settings);

} // namespace avgen::world
