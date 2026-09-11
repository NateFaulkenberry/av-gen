#include "world/city.hpp"

#include "core/rng.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::world {
namespace {

// The street network is laid first and the blocks fill what is left, rather than the other way
// round. Roads that are carved out of a field of plots end up with plots hanging over them wherever
// the arithmetic is off by one; roads that are laid first cannot.
[[nodiscard]] bool isRoadColumn(int x, const CitySettings& s) {
    const int period = s.blockCells + s.roadCells;
    return (x % period) < s.roadCells;
}

} // namespace

const char* cellKindName(CellKind kind) {
    switch (kind) {
    case CellKind::Empty:    return "empty";
    case CellKind::Road:     return "road";
    case CellKind::Junction: return "junction";
    case CellKind::Crossing: return "crossing";
    case CellKind::Pavement: return "pavement";
    case CellKind::Plot:     return "plot";
    case CellKind::Courtyard: return "courtyard";
    case CellKind::Plaza:    return "plaza";
    }
    return "empty";
}

float quarterRadians(Quarter q) {
    return static_cast<float>(static_cast<int>(q)) * 1.57079633f;
}

Quarter quarterTowards(glm::ivec2 delta) {
    // Quarter turns are anticlockwise about +Y, and a piece faces +Z at rest, so the turns take that
    // front round +Z, +X, -Z, -X. Z is tested first so a diagonal delta -- which should not reach
    // here, but might -- resolves the same way every time rather than by argument order.
    if (delta.y > 0) return Quarter::Zero;
    if (delta.x > 0) return Quarter::One;
    if (delta.y < 0) return Quarter::Two;
    if (delta.x < 0) return Quarter::Three;
    return Quarter::Zero;
}

Result<void> CitySettings::validate() const {
    if (!(moduleSize > 0.0f) || !std::isfinite(moduleSize)) {
        return fail("city: moduleSize must be a finite value above zero");
    }
    if (blocksX < 1 || blocksZ < 1) {
        return fail("city: needs at least one block in each direction");
    }
    // Three, not one. A block is a ring of pavement round a core of plots, and a block two cells
    // across is *all* ring -- every cell touches a road, so there is nowhere a building can stand
    // that is not also the footway. That produced a city with roads, pavements and no buildings at
    // all, which counts as a plan and renders as an empty grid. Refused by name instead.
    // Three when the block spends a ring of cells on its footway -- a block two across is *all*
    // ring, so every cell is footway and there is nowhere a building can stand. That produced a city
    // with roads, pavements and no buildings at all, which counts as a plan and renders as an empty
    // grid, so it is refused by name instead.
    //
    // One is enough once the footway shares a cell with the building in front of it, because then no
    // cell is spent on pavement alone.
    const int smallest = footwayMetres > 0.0f ? 1 : 3;
    if (blockCells < smallest) {
        return fail("city: a block of {} cell(s) cannot hold both a footway and a building; it needs "
                    "at least {}, or a footway that shares its cell (footwayMetres above zero)",
                    blockCells, smallest);
    }
    if (roadCells < 1) {
        return fail("city: a road must be at least one cell wide");
    }
    if (!(tileUnits > 0.0f) || !std::isfinite(tileUnits)) {
        return fail("city: tileUnits must be a finite value above zero; it is the size the pack's "
                    "tiles were drawn at, in the pack's own units");
    }
    // A ceiling rather than a limit somebody discovers: the lattice is materialised as a vector of
    // cells, and the placer walks it once per asset. Ten thousand cells is a city; a million is a
    // mistake in a recipe that would otherwise show up as a hang.
    if (static_cast<long long>(width()) * depth() > 250000LL) {
        return fail("city: {} x {} cells is too large; reduce blocks or blockCells", width(), depth());
    }
    if (plazaFraction < 0.0f || plazaFraction > 1.0f) {
        return fail("city: plazaFraction must be between 0 and 1");
    }
    if (crossingFraction < 0.0f || crossingFraction > 1.0f) {
        return fail("city: crossingFraction must be between 0 and 1");
    }
    if (footwayMetres < 0.0f || !std::isfinite(footwayMetres)) {
        return fail("city: footwayMetres cannot be negative");
    }
    // A footway wider than half the module leaves less room for the building than for the pavement
    // in front of it, which is a footway with a shed on it rather than a street.
    if (footwayMetres > moduleSize * 0.5f) {
        return fail("city: a footway of {} m is more than half a {} m cell; there would be no room "
                    "left to build on", footwayMetres, moduleSize);
    }
    if (!(plotFill > 0.0f) || plotFill > 1.0f) {
        return fail("city: plotFill must be above 0 and at most 1; it is the fraction of its plot a "
                    "building fills");
    }
    return {};
}

int CitySettings::width() const { return blocksX * (blockCells + roadCells) + roadCells; }
int CitySettings::depth() const { return blocksZ * (blockCells + roadCells) + roadCells; }
float CitySettings::extentX() const { return static_cast<float>(width()) * moduleSize; }
float CitySettings::extentZ() const { return static_cast<float>(depth()) * moduleSize; }

CityCell CityPlan::at(glm::ivec2 c) const {
    if (!inBounds(c)) {
        return CityCell{};
    }
    return cells[static_cast<std::size_t>(c.y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(c.x)];
}

glm::vec3 CityPlan::centreOf(glm::ivec2 c) const {
    const float m = settings.moduleSize;
    const float halfX = static_cast<float>(width) * m * 0.5f;
    const float halfZ = static_cast<float>(depth) * m * 0.5f;
    return glm::vec3((static_cast<float>(c.x) + 0.5f) * m - halfX, 0.0f,
                     (static_cast<float>(c.y) + 0.5f) * m - halfZ);
}

bool CityPlan::isCarriageway(glm::ivec2 c) const {
    const CellKind k = kindAt(c);
    return k == CellKind::Road || k == CellKind::Junction || k == CellKind::Crossing;
}

std::size_t CityPlan::countOf(CellKind kind) const {
    return static_cast<std::size_t>(std::count_if(
        cells.begin(), cells.end(), [kind](const CityCell& c) { return c.kind == kind; }));
}

Result<CityPlan> planCity(const CitySettings& settings) {
    if (auto r = settings.validate(); !r) {
        return std::unexpected(r.error());
    }
    CityPlan plan;
    plan.settings = settings;
    plan.width = settings.width();
    plan.depth = settings.depth();
    plan.cells.assign(static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.depth),
                      CityCell{});

    const auto index = [&plan](int x, int z) {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(plan.width) +
               static_cast<std::size_t>(x);
    };
    const int period = settings.blockCells + settings.roadCells;

    // ---- the street network, first ----
    // A cell is carriageway when either axis is in a road band. Where both are, it is a junction,
    // which is what makes a crossing a crossing rather than two roads overlapping.
    for (int z = 0; z < plan.depth; ++z) {
        for (int x = 0; x < plan.width; ++x) {
            const bool roadX = isRoadColumn(x, settings);
            const bool roadZ = isRoadColumn(z, settings);
            CityCell& cell = plan.cells[index(x, z)];
            if (roadX && roadZ) {
                cell.kind = CellKind::Junction;
            } else if (roadX) {
                cell.kind = CellKind::Road;
                cell.rotation = Quarter::One; // runs along Z
            } else if (roadZ) {
                cell.kind = CellKind::Road;
                cell.rotation = Quarter::Zero; // runs along X
            }
        }
    }

    // ---- the blocks ----
    // Everything the network did not claim belongs to a block. The ring of cells touching a road
    // becomes pavement and the rest becomes plots, so a building is never flush against a
    // carriageway and there is always somewhere for a person to walk.
    Rng rng(settings.seed == 0 ? 0x9E3779B9u : settings.seed);
    for (int bz = 0; bz < settings.blocksZ; ++bz) {
        for (int bx = 0; bx < settings.blocksX; ++bx) {
            // A whole block left open. Decided per block rather than per cell: a plaza made of
            // scattered empty cells is not a plaza, it is a block with holes in it.
            const bool plaza = rng.nextFloat() < settings.plazaFraction;
            const int originX = bx * period + settings.roadCells;
            const int originZ = bz * period + settings.roadCells;
            for (int dz = 0; dz < settings.blockCells; ++dz) {
                for (int dx = 0; dx < settings.blockCells; ++dx) {
                    const int x = originX + dx;
                    const int z = originZ + dz;
                    if (x >= plan.width || z >= plan.depth) {
                        continue;
                    }
                    CityCell& cell = plan.cells[index(x, z)];
                    cell.block = glm::ivec2(bx, bz);
                    const bool edge = dx == 0 || dz == 0 || dx == settings.blockCells - 1 ||
                                      dz == settings.blockCells - 1;
                    (void)edge;
                    // Inside the pavement ring is the block's core, and inside *that* is the part
                    // of a block no street reaches. Buildings take a band round the core's edge and
                    // what is left becomes a courtyard -- the back of the block, where the bins and
                    // the parking are. Without this, a block five cells across has a plot in the
                    // middle with no frontage in any direction: a building whose front door opens
                    // onto the back of the building in front of it. A test found exactly that.
                    //
                    // How deep the band goes is `buildDepth`, clamped to what the core can hold. One
                    // cell is a perimeter block and is right up to about six cells across; past that
                    // the band stays one cell while the yard grows with the square of the block, and
                    // a nine-cell block becomes a ring of houses round a field.
                    // How many cells of the block are footway and nothing else. One, historically.
                    // None once the footway shares its cell with the building in front of it, which
                    // is what `footwayMetres` turns on -- the block's own edge becomes its frontage.
                    const int ring = settings.footwayMetres > 0.0f ? 0 : 1;
                    const int core = settings.blockCells - 2 * ring;
                    const int depth = std::clamp(settings.buildDepth, 1, (core + 1) / 2);
                    const int inX = std::min(dx - ring, core - 1 - (dx - ring));
                    const int inZ = std::min(dz - ring, core - 1 - (dz - ring));
                    const bool coreEdge = std::min(inX, inZ) < depth;
                    // Both axes at the core's own edge: the corner of the block, where two streets
                    // meet. Still the outermost ring however deep the band is -- a shop two rows
                    // back is not on the corner.
                    const bool coreCorner = inX == 0 && inZ == 0;
                    if (plaza) {
                        cell.kind = CellKind::Plaza;
                    } else if (ring > 0 && edge) {
                        cell.kind = CellKind::Pavement;
                    } else if (coreEdge) {
                        cell.kind = CellKind::Plot;
                        cell.corner = coreCorner;
                    } else {
                        cell.kind = CellKind::Courtyard;
                    }
                }
            }
        }
    }

    // ---- which way the buildings face ----
    // A building presents its front to a street. Done here rather than in the placer because it
    // needs only the lattice -- where the roads are -- and so can be checked exhaustively without an
    // asset, a mesh or a device, which is the whole reason planning is separate from placing.
    //
    // Not random: a row of buildings each facing a different way is the single clearest sign that a
    // city was scattered rather than laid out.
    for (int z = 0; z < plan.depth; ++z) {
        for (int x = 0; x < plan.width; ++x) {
            CityCell& cell = plan.cells[index(x, z)];
            if (cell.kind != CellKind::Plot) {
                continue;
            }
            // The four axis directions, nearest carriageway wins. A block is at most `blockCells`
            // across and roads bound every block, so the search cannot run past the grid.
            static constexpr glm::ivec2 kDirs[4] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
            int bestSteps = std::numeric_limits<int>::max();
            glm::ivec2 bestDir = kDirs[0];
            for (const glm::ivec2 dir : kDirs) {
                for (int step = 1; step <= settings.blockCells + 1; ++step) {
                    const glm::ivec2 probe{x + dir.x * step, z + dir.y * step};
                    if (!plan.inBounds(probe)) {
                        break;
                    }
                    if (plan.isCarriageway(probe)) {
                        // Strictly nearer, so the fixed direction order breaks every tie. A plot in
                        // the middle of a block is equidistant from all four streets and must still
                        // face one of them the same way on every run.
                        if (step < bestSteps) {
                            bestSteps = step;
                            bestDir = dir;
                        }
                        break;
                    }
                }
            }
            cell.rotation = quarterTowards(bestDir);
            // A plot whose cell touches a carriageway carries the footway as well as the building.
            // Only the immediate neighbour counts: a plot one row back has the frontage row between
            // it and the street, and sets back behind nothing.
            cell.frontage = bestSteps == 1;
        }
    }

    // ---- crossings ----
    // On a road cell that touches a junction, which is where a person would actually cross. Drawn
    // from the same stream as the plazas so the whole plan stays one pure function of the seed.
    if (settings.crossingFraction > 0.0f) {
        for (int z = 0; z < plan.depth; ++z) {
            for (int x = 0; x < plan.width; ++x) {
                CityCell& cell = plan.cells[index(x, z)];
                if (cell.kind != CellKind::Road) {
                    continue;
                }
                const bool nextToJunction = plan.kindAt({x - 1, z}) == CellKind::Junction ||
                                            plan.kindAt({x + 1, z}) == CellKind::Junction ||
                                            plan.kindAt({x, z - 1}) == CellKind::Junction ||
                                            plan.kindAt({x, z + 1}) == CellKind::Junction;
                if (nextToJunction && rng.nextFloat() < settings.crossingFraction) {
                    cell.kind = CellKind::Crossing;
                }
            }
        }
    }
    return plan;
}

} // namespace avgen::world
