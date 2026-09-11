#include "world/city.hpp"

#include "core/rng.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace avgen::world {
namespace {

// The tag an artist writes on a manifest entry to say what it dresses. The vocabulary lives in the
// manifest rather than in this file, so adding a kind of street furniture is a data change.
[[nodiscard]] const char* roleTag(CellKind kind) {
    switch (kind) {
    case CellKind::Road:     return "road";
    case CellKind::Junction: return "junction";
    case CellKind::Crossing: return "crossing";
    case CellKind::Pavement: return "pavement";
    case CellKind::Plot:     return "building";
    case CellKind::Plaza:    return "plaza";
    case CellKind::Empty:    return "";
    }
    return "";
}

// A stable per-cell stream. Not the plan's seed alone: a placer that drew from one running stream
// would produce a different city the moment a cell was added anywhere before it, so every cell's
// choices come from its own coordinates instead.
[[nodiscard]] Rng cellRng(std::uint32_t seed, glm::ivec2 c) {
    std::uint32_t h = seed ^ 0x9E3779B9u;
    h ^= static_cast<std::uint32_t>(c.x) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<std::uint32_t>(c.y) * 0xC2B2AE35u;
    return Rng(h == 0 ? 1u : h);
}

} // namespace

const std::vector<std::string>& CityLibrary::forKind(CellKind kind) const {
    static const std::vector<std::string> none;
    switch (kind) {
    case CellKind::Road:     return road;
    case CellKind::Junction: return junction;
    case CellKind::Crossing: return crossing;
    case CellKind::Pavement: return pavement;
    case CellKind::Plot:     return plot;
    case CellKind::Plaza:    return plaza;
    case CellKind::Empty:    return none;
    }
    return none;
}

bool CityLibrary::empty() const {
    return road.empty() && junction.empty() && crossing.empty() && pavement.empty() &&
           plot.empty() && plaza.empty();
}

CityLibrary CityLibrary::fromTags(const assets::AssetLibrary& library) {
    CityLibrary out;
    const auto collect = [&library](const char* tag, std::vector<std::string>& into) {
        for (const assets::AssetDescriptor& asset : library.assets()) {
            if (std::ranges::find(asset.tags, std::string(tag)) != asset.tags.end()) {
                into.push_back(asset.name);
            }
        }
        // Sorted, so the library a placer sees does not depend on the order a manifest happened to
        // list its entries in. The per-cell choice is then a function of the seed alone.
        std::ranges::sort(into);
    };
    // Through `roleTag`, so the tag vocabulary has one definition rather than two that can drift.
    collect(roleTag(CellKind::Road), out.road);
    collect(roleTag(CellKind::Junction), out.junction);
    collect(roleTag(CellKind::Crossing), out.crossing);
    collect(roleTag(CellKind::Pavement), out.pavement);
    collect(roleTag(CellKind::Plot), out.plot);
    collect(roleTag(CellKind::Plaza), out.plaza);
    return out;
}

std::size_t PlacedCity::instanceCount() const {
    std::size_t total = 0;
    for (const CityPlacement& p : placements) {
        total += p.cloud ? p.cloud->count() : 0;
    }
    return total;
}

Result<PlacedCity> placeCity(const CityPlan& plan, const CityLibrary& roles,
                             const assets::AssetLibrary& library, const TerrainQuery* terrain) {
    if (plan.cells.empty()) {
        return fail("city: nothing to place; the plan is empty");
    }
    if (roles.empty()) {
        return fail("city: the asset library has nothing tagged road, junction, crossing, "
                    "pavement, building or plaza");
    }

    // Gathered per asset, because a placement is one cloud of one asset and the cells that want a
    // given asset are scattered all over the plan.
    struct Gather {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec4> rotations;
        std::vector<glm::vec3> scales;
        CellKind kind = CellKind::Empty;
    };
    std::vector<std::pair<std::string, Gather>> gathered;
    const auto gatherFor = [&gathered](const std::string& asset, CellKind kind) -> Gather& {
        for (auto& [name, g] : gathered) {
            if (name == asset) {
                return g;
            }
        }
        gathered.emplace_back(asset, Gather{});
        gathered.back().second.kind = kind;
        return gathered.back().second;
    };

    std::vector<std::string> undressed;
    const auto noteUndressed = [&undressed](CellKind kind) {
        const std::string name = cellKindName(kind);
        if (std::ranges::find(undressed, name) == undressed.end()) {
            undressed.push_back(name);
        }
    };

    for (int z = 0; z < plan.depth; ++z) {
        for (int x = 0; x < plan.width; ++x) {
            const glm::ivec2 coord{x, z};
            const CityCell cell = plan.at(coord);
            if (cell.kind == CellKind::Empty) {
                continue;
            }
            const std::vector<std::string>& candidates = roles.forKind(cell.kind);
            if (candidates.empty()) {
                noteUndressed(cell.kind);
                continue;
            }
            Rng rng = cellRng(plan.settings.seed, coord);
            const std::string& name = candidates[rng.nextU32() % candidates.size()];
            const assets::AssetDescriptor* asset = library.find(name);
            if (asset == nullptr) {
                noteUndressed(cell.kind);
                continue;
            }
            glm::vec3 position = plan.centreOf(coord);
            if (terrain != nullptr) {
                position.y = terrain->heightAt(glm::vec2(position.x, position.z));
            }
            // The plan's quarter turn, and nothing random on top of it. A road tile rotated by a
            // few degrees "for variation" is a road that no longer meets the one beside it, which is
            // the single most visible way a tiled city goes wrong.
            const float yaw = quarterRadians(cell.rotation);
            const glm::quat rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            // One scale for the whole pack, from the tile it was drawn on to the module the city
            // uses (ADR-100). Uniform on purpose: scaling each piece by its own bounding box looks
            // like it would make everything fit and does the opposite, because a piece whose kerb
            // or awning overhangs the tile then has its *tile* shrunk to fit the overhang, and it
            // no longer meets its neighbour. The overhang is the artist's intent and is preserved.
            const float scale = plan.settings.moduleSize / plan.settings.tileUnits;
            Gather& g = gatherFor(name, cell.kind);
            g.positions.push_back(position);
            g.rotations.emplace_back(rotation.x, rotation.y, rotation.z, rotation.w);
            g.scales.emplace_back(scale);
        }
    }

    PlacedCity out;
    out.undressed = std::move(undressed);
    out.placements.reserve(gathered.size());
    for (auto& [name, g] : gathered) {
        if (g.positions.empty()) {
            continue;
        }
        auto cloud = std::make_shared<spatial::PointCloud>();
        cloud->resize(g.positions.size());
        std::copy(g.positions.begin(), g.positions.end(), cloud->positions().begin());
        std::copy(g.rotations.begin(), g.rotations.end(), cloud->rotations().begin());
        std::copy(g.scales.begin(), g.scales.end(), cloud->scales().begin());
        cloud->renumberIndices();
        cloud->reseed(plan.settings.seed);
        CityPlacement placement;
        placement.name = "city-" + name;
        placement.asset = name;
        placement.kind = g.kind;
        placement.cloud = std::move(cloud);
        out.placements.push_back(std::move(placement));
    }
    return out;
}

} // namespace avgen::world
