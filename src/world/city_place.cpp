#include "world/city.hpp"

#include "core/rng.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

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
    case CellKind::Courtyard: return "courtyard";
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

// The tag prefix that declares which family a building belongs to.
constexpr const char* kFamilyPrefix = "family:";

} // namespace

const std::vector<std::string>& CityLibrary::forKind(CellKind kind) const {
    static const std::vector<std::string> none;
    switch (kind) {
    case CellKind::Road:     return road;
    case CellKind::Junction: return junction;
    case CellKind::Crossing: return crossing;
    case CellKind::Pavement: return pavement;
    case CellKind::Plot:     return plot;
    case CellKind::Courtyard: return courtyard;
    case CellKind::Plaza:    return plaza;
    case CellKind::Empty:    return none;
    }
    return none;
}

bool CityLibrary::empty() const {
    return road.empty() && junction.empty() && crossing.empty() && pavement.empty() &&
           plot.empty() && courtyard.empty() && plaza.empty();
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
    collect(roleTag(CellKind::Courtyard), out.courtyard);
    collect(roleTag(CellKind::Plaza), out.plaza);

    // Families, from a `family:<name>` tag on a building. A prefix rather than a bare tag because
    // the code has to be able to tell a family from any other word an artist writes: every one of
    // these buildings is also tagged "city" and "building", and "a tag that only some of them carry"
    // is a rule that silently reclassifies the vocabulary the moment somebody tags one of them
    // "damaged". The prefix says which tag is meant to be a family, in the manifest, explicitly.
    for (const std::string& name : out.plot) {
        const assets::AssetDescriptor* asset = library.find(name);
        if (asset == nullptr) {
            continue;
        }
        for (const std::string& tag : asset->tags) {
            if (!tag.starts_with(kFamilyPrefix)) {
                continue;
            }
            const std::string family = tag.substr(std::string_view(kFamilyPrefix).size());
            auto it = std::ranges::find_if(out.plotFamilies,
                                           [&family](const auto& e) { return e.first == family; });
            if (it == out.plotFamilies.end()) {
                out.plotFamilies.emplace_back(family, std::vector<std::string>{});
                it = out.plotFamilies.end() - 1;
            }
            it->second.push_back(name);
        }
    }
    std::ranges::sort(out.plotFamilies, {}, &std::pair<std::string, std::vector<std::string>>::first);
    for (auto& [family, names] : out.plotFamilies) {
        std::ranges::sort(names);
    }
    return out;
}

const std::vector<std::string>& CityLibrary::forBlock(glm::ivec2 block, std::uint32_t seed) const {
    if (plotFamilies.empty()) {
        return plot;
    }
    // From the block's coordinates, not from a running stream, for the reason `cellRng` exists: a
    // block's character must not change because a block somewhere else did.
    Rng rng = cellRng(seed ^ 0x51ED270Bu, block);
    return plotFamilies[rng.nextU32() % plotFamilies.size()].second;
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

    // One piece, on one cell. Pulled out of the loop because a plot needs two of them: a building
    // standing on a piece of ground. Every other cell is its own floor -- a road tile *is* the road
    // -- but a building is a solid thing on top of something, and a plot given only a building has
    // nothing under it but the void, which is exactly what it looks like.
    const auto placeOne = [&](glm::ivec2 coord, const CityCell& cell, CellKind role,
                              const std::vector<std::string>& candidates, std::uint32_t salt) {
        if (candidates.empty()) {
            noteUndressed(role);
            return;
        }
        Rng rng = cellRng(plan.settings.seed ^ salt, coord);
        const std::string& name = candidates[rng.nextU32() % candidates.size()];
        const assets::AssetDescriptor* asset = library.find(name);
        if (asset == nullptr) {
            noteUndressed(role);
            return;
        }
        glm::vec3 position = plan.centreOf(coord);
        if (terrain != nullptr) {
            position.y = terrain->heightAt(glm::vec2(position.x, position.z));
        }
        // The plan's quarter turn, and nothing random on top of it. A road tile rotated by a few
        // degrees "for variation" is a road that no longer meets the one beside it, which is the
        // single most visible way a tiled city goes wrong. A plot's ground is laid square, since the
        // quarter turn on a plot cell is the *building's* facing and means nothing to the floor.
        const bool building = role == CellKind::Plot;
        const float yaw = building ? quarterRadians(cell.rotation) : 0.0f;
        const glm::quat rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        // Two scale rules, because a city has two kinds of thing in it.
        //
        // Ground pieces get one scale for the whole pack, from the tile they were drawn on to the
        // module the city uses (ADR-100). Uniform on purpose: scaling each piece by its own bounding
        // box looks like it would make everything fit and does the opposite, because a piece whose
        // kerb or awning overhangs the tile then has its *tile* shrunk to fit the overhang, and it
        // no longer meets its neighbour. The overhang is the artist's intent.
        //
        // A building is scaled to its plot instead. It has no tile to meet a neighbour across -- a
        // plot is bounded by pavement on every side -- and its footprint is its own: the same Kenney
        // kit ranges from 0.9 to 1.3 units across, so the pack scale would leave some overhanging
        // the footway and others adrift in the middle of the plot. Uniform again, so a building
        // keeps the proportions it was drawn with, which is why a 3.15-aspect tower stays a tower.
        float scale = plan.settings.moduleSize / plan.settings.tileUnits;
        if (building) {
            const float footprint = std::max(asset->naturalSize.x, asset->naturalSize.z);
            if (footprint > 0.0f && std::isfinite(footprint)) {
                scale = plan.settings.moduleSize * plan.settings.plotFill / footprint;
            }
            // Onto the middle of the plot, not onto its own origin. Six of the buildings in this
            // pack are modelled about a corner rather than their middle -- `industrial-building-h`
            // spans -0.58 units to one side -- and at plot scale that is 4.6 m, which puts half the
            // building in the road. Rotated with the piece, so it still lands on the plot after the
            // building has been turned to face its street.
            //
            // Ground pieces are deliberately left alone: `road-side` is off-centre too, but that is
            // its kerb overhanging the tile on purpose, and recentring it would pull the kerb into
            // the carriageway. Same measurement, opposite meaning -- which is why this is decided by
            // what is being placed rather than by the number.
            const glm::vec3 offset = rotation * glm::vec3(asset->naturalCentre.x * scale, 0.0f,
                                                          asset->naturalCentre.z * scale);
            position.x -= offset.x;
            position.z -= offset.z;
        }
        Gather& g = gatherFor(name, role);
        g.positions.push_back(position);
        g.rotations.emplace_back(rotation.x, rotation.y, rotation.z, rotation.w);
        g.scales.emplace_back(scale);
    };

    for (int z = 0; z < plan.depth; ++z) {
        for (int x = 0; x < plan.width; ++x) {
            const glm::ivec2 coord{x, z};
            const CityCell cell = plan.at(coord);
            if (cell.kind == CellKind::Empty) {
                continue;
            }
            if (cell.kind == CellKind::Plot) {
                // The ground first, then the building on it. Salted so the floor's choice is its own
                // and not the building's -- without that, every plot that drew building number two
                // would also draw ground number two, and the two would move together for no reason
                // anybody could see.
                placeOne(coord, cell, CellKind::Courtyard, roles.forKind(CellKind::Courtyard),
                         0x5BD1E995u);
                placeOne(coord, cell, CellKind::Plot,
                         roles.forBlock(cell.block, plan.settings.seed), 0u);
                continue;
            }
            placeOne(coord, cell, cell.kind, roles.forKind(cell.kind), 0u);
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
