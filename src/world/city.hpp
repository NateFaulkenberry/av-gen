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

#include "assets/asset_library.hpp"
#include "core/error.hpp"
#include "spatial/point_cloud.hpp"
#include "world/terrain_query.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
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
    Courtyard,  // inside a block, behind the buildings: yards, bins, parking, gardens
    Plaza,      // deliberately open: the space a stage or a crowd needs
};
[[nodiscard]] const char* cellKindName(CellKind kind);

// Which way a piece faces, in quarter turns anticlockwise about +Y. A road tile is modelled along
// one axis and a junction is symmetric, so this is all the orientation a lattice needs.
enum class Quarter : std::uint8_t { Zero, One, Two, Three };
[[nodiscard]] float quarterRadians(Quarter q);
// The quarter turn that points a piece along `delta`, which must be one axis step. Pieces are
// modelled facing +Z -- a declared convention, like `tileUnits`, and for the same reason: it is a
// property of how the pack was authored and cannot be recovered from a mesh's bounds.
[[nodiscard]] Quarter quarterTowards(glm::ivec2 delta);

struct CityCell {
    CellKind kind = CellKind::Empty;
    // For a road, the axis it runs along. For a plot, the way the building faces: towards the
    // nearest carriageway, so a building presents its front to a street rather than its back.
    Quarter rotation = Quarter::Zero;
    // Which block this cell belongs to, or (-1,-1) for the street network between them. Carried so a
    // placer can vary a block's character without re-deriving which cells are in it.
    glm::ivec2 block{-1, -1};
};

struct CitySettings {
    // Metres per cell. The lattice pitch, and the single number every asset pack is scaled against
    // (ADR-100): Kenney's kit is 1x1 *units* with a 0.79-unit person, which is not metres.
    float moduleSize = 8.0f;
    // The tile size the pack was authored on, in its own units. Kenney's road pieces are drawn on a
    // 1x1 tile, so this is 1.
    //
    // It is stated rather than measured, and that is the point. `road-side` *bounds* 1.0 x 1.31
    // because its kerb is meant to overhang the tile; scaling that piece by its bounding box shrinks
    // the tile it is supposed to fill to 6.1 m of an 8 m cell and leaves a gap between it and its
    // neighbour. Decoration that reaches past the tile is the artist's intent, so the tile has to be
    // declared by whoever curated the pack -- it cannot be recovered from the mesh.
    float tileUnits = 1.0f;
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
    // How much of its cell a building's footprint fills. Buildings are not tiles: a road piece is
    // drawn on a 1x1 tile and is scaled by the pack (`tileUnits`) so that it meets its neighbour,
    // but a building's footprint is its own -- these vary from 0.9 to 1.3 units in the same kit --
    // and scaling them all by the pack's tile factor would leave some overhanging the pavement and
    // others floating in the middle of their plot. So a building is scaled to fit its plot instead,
    // uniformly, keeping the proportions it was drawn with. Below 1 so neighbouring buildings on
    // adjacent plots do not touch.
    float plotFill = 0.9f;

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

// ---- placing ------------------------------------------------------------------------------------
//
// The second half of ADR-100, and where assets, scale and the ground enter. A plan is a grid of enum
// values; a placement is a cloud of instances of one asset. One cloud per asset, because that is what
// `ProceduralGeometry::distribution.scatterCloud` takes and therefore what gets GPU culling, the LOD
// ladder and instanced draws without any of it being written again.

// Which assets dress which cell kind. Names are `assets::AssetLibrary` entry names, not file paths:
// the library already carries the file, the measured bounds and `preferredScale`, which is where the
// Kenney-units-to-metres reconciliation lives (ADR-100). A role with no assets leaves those cells
// undressed, and `placeCity` says so rather than placing nothing quietly.
struct CityLibrary {
    std::vector<std::string> road;
    std::vector<std::string> junction;
    std::vector<std::string> crossing;
    std::vector<std::string> pavement;
    std::vector<std::string> plot;     // buildings, all of them, whatever family
    std::vector<std::string> courtyard; // what fills the inside of a block
    std::vector<std::string> plaza;    // ground for an open block

    // Buildings grouped by the family they belong to, from a `family:<name>` tag. A block picks one
    // family and every plot in it draws from that family alone, which is what makes a street read as
    // a street: houses together, warehouses together, offices together. Drawing each plot from the
    // whole library independently gives a bungalow between two towers on every block, which is the
    // characteristic look of a city nobody planned.
    //
    // Sorted by family name, and every family's assets sorted, for the same reason the flat lists
    // are: the choice must be a function of the seed, not of manifest order.
    std::vector<std::pair<std::string, std::vector<std::string>>> plotFamilies;

    [[nodiscard]] const std::vector<std::string>& forKind(CellKind kind) const;
    [[nodiscard]] bool empty() const;
    // The assets a given block should build from: one family, chosen from `seed` and the block's
    // coordinates. Falls back to the whole `plot` list when nothing declares a family, so a manifest
    // that never adopted the convention still builds a city.
    [[nodiscard]] const std::vector<std::string>& forBlock(glm::ivec2 block,
                                                           std::uint32_t seed) const;
    // Builds a library by reading the tags of an asset library: an entry tagged "road" dresses road
    // cells, and so on. Keeps the role vocabulary in the manifest, where an artist can change it,
    // rather than in this header.
    [[nodiscard]] static CityLibrary fromTags(const assets::AssetLibrary& library);
};

// One asset, and every instance of it the plan asked for.
struct CityPlacement {
    std::string name;    // layer name, unique within a city: "city-road-straight"
    std::string asset;   // the library entry this came from
    CellKind kind = CellKind::Empty;
    std::shared_ptr<spatial::PointCloud> cloud;
};

struct PlacedCity {
    std::vector<CityPlacement> placements;
    // Cells the plan asked to dress and the library had nothing for. Reported rather than logged,
    // because a city missing its roads still renders -- as an empty grid -- and the count is the
    // only thing that says why.
    std::vector<std::string> undressed;
    [[nodiscard]] std::size_t instanceCount() const;
};

// Turns a plan into placements. `terrain` may be null, in which case every piece sits at y = 0;
// given one, each piece is dropped onto the ground under itself. Pure apart from that query, and a
// pure function of (plan, library, terrain) -- same inputs, same city (ADR-091).
[[nodiscard]] Result<PlacedCity> placeCity(const CityPlan& plan, const CityLibrary& roles,
                                           const assets::AssetLibrary& library,
                                           const TerrainQuery* terrain = nullptr);

} // namespace avgen::world
