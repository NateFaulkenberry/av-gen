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
// Props are tagged by what they are, not by a cell kind, because a prop is not what a cell *is* --
// it is what stands on it, and several of them stand on one.
constexpr const char* kPropTag = "prop";
// Street furniture: what stands on a footway rather than in a yard. A separate vocabulary because
// pavements do not split by family -- a street is the same street whichever block it runs past.
constexpr const char* kStreetPropTag = "streetprop";
// An overlay declares the surface it belongs to: `overlays:road-straight`. Named explicitly rather
// than inferred from the `-barrier` suffix, because a naming convention is a coincidence the day
// somebody adds a piece that breaks it, and this one already has exceptions -- `road-slant-barrier`
// belongs to `road-slant`, but `road-straight-barrier-half` belongs to `road-straight` too.
constexpr const char* kOverlayPrefix = "overlays:";

// The family an asset declares, or empty. One definition, used by every role that splits by family.
[[nodiscard]] std::string familyOf(const assets::AssetDescriptor& asset) {
    for (const std::string& tag : asset.tags) {
        if (tag.starts_with(kFamilyPrefix)) {
            return tag.substr(std::string_view(kFamilyPrefix).size());
        }
    }
    return {};
}

// Groups a role's assets by the family each declares, dropping those that declare none -- they are
// already in the role's flat list, which is what `forFamily` falls back to.
void groupByFamily(const std::vector<std::string>& names, const assets::AssetLibrary& library,
                   CityLibrary::Families& into) {
    for (const std::string& name : names) {
        const assets::AssetDescriptor* asset = library.find(name);
        if (asset == nullptr) {
            continue;
        }
        const std::string family = familyOf(*asset);
        if (family.empty()) {
            continue;
        }
        auto it = std::ranges::find_if(into, [&family](const auto& e) { return e.first == family; });
        if (it == into.end()) {
            into.emplace_back(family, std::vector<std::string>{});
            it = into.end() - 1;
        }
        it->second.push_back(name);
    }
    std::ranges::sort(into, {}, &std::pair<std::string, std::vector<std::string>>::first);
    for (auto& [family, list] : into) {
        std::ranges::sort(list);
    }
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
    case CellKind::Courtyard: return courtyard;
    case CellKind::Plaza:    return plaza;
    case CellKind::Empty:    return none;
    }
    return none;
}

bool CityLibrary::empty() const {
    return road.empty() && junction.empty() && crossing.empty() && pavement.empty() &&
           plot.empty() && courtyard.empty() && plaza.empty() && prop.empty();
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

    // Props, which have no cell kind of their own.
    collect(kPropTag, out.prop);
    collect(kStreetPropTag, out.streetProp);

    // Overlays, grouped by the surface each names.
    for (const assets::AssetDescriptor& asset : library.assets()) {
        for (const std::string& tag : asset.tags) {
            if (!tag.starts_with(kOverlayPrefix)) {
                continue;
            }
            const std::string surface = tag.substr(std::string_view(kOverlayPrefix).size());
            auto it = std::ranges::find_if(out.overlays,
                                           [&surface](const auto& e) { return e.first == surface; });
            if (it == out.overlays.end()) {
                out.overlays.emplace_back(surface, std::vector<std::string>{});
                it = out.overlays.end() - 1;
            }
            it->second.push_back(asset.name);
        }
    }
    std::ranges::sort(out.overlays, {}, &std::pair<std::string, std::vector<std::string>>::first);
    for (auto& [surface, list] : out.overlays) {
        std::ranges::sort(list);
    }

    // Families, from a `family:<name>` tag. A prefix rather than a bare tag because the code has to
    // be able to tell a family from any other word an artist writes: every one of these buildings is
    // also tagged "city" and "building", and "a tag that only some of them carry" is a rule that
    // silently reclassifies the vocabulary the moment somebody tags one of them "damaged". The
    // prefix says which tag is meant to be a family, in the manifest, explicitly.
    groupByFamily(out.plot, library, out.plotFamilies);
    groupByFamily(out.courtyard, library, out.courtyardFamilies);
    groupByFamily(out.prop, library, out.propFamilies);
    return out;
}

std::string CityLibrary::familyFor(glm::ivec2 block, std::uint32_t seed) const {
    if (plotFamilies.empty()) {
        return {};
    }
    // From the block's coordinates, not from a running stream, for the reason `cellRng` exists: a
    // block's character must not change because a block somewhere else did.
    Rng rng = cellRng(seed ^ 0x51ED270Bu, block);
    return plotFamilies[rng.nextU32() % plotFamilies.size()].first;
}

const std::vector<std::string>& CityLibrary::forFamily(CellKind role,
                                                       const std::string& family) const {
    const Families* families = nullptr;
    switch (role) {
    case CellKind::Plot:      families = &plotFamilies; break;
    case CellKind::Courtyard: families = &courtyardFamilies; break;
    default:                  families = nullptr; break;
    }
    if (families != nullptr && !family.empty()) {
        const auto it = std::ranges::find_if(*families,
                                             [&family](const auto& e) { return e.first == family; });
        if (it != families->end() && !it->second.empty()) {
            return it->second;
        }
    }
    return forKind(role);
}

const std::vector<std::string>& CityLibrary::overlaysFor(const std::string& surface) const {
    static const std::vector<std::string> none;
    const auto it = std::ranges::find_if(overlays,
                                         [&surface](const auto& e) { return e.first == surface; });
    return it == overlays.end() ? none : it->second;
}

std::vector<std::string> CityLibrary::families() const {
    std::vector<std::string> out;
    out.reserve(plotFamilies.size());
    for (const auto& [family, names] : plotFamilies) {
        out.push_back(family);
    }
    return out;
}

const std::vector<std::string>& CityLibrary::propsFor(const std::string& family) const {
    if (!family.empty()) {
        const auto it = std::ranges::find_if(propFamilies,
                                             [&family](const auto& e) { return e.first == family; });
        if (it != propFamilies.end() && !it->second.empty()) {
            return it->second;
        }
    }
    return prop;
}

const std::vector<std::string>& CityLibrary::forBlock(glm::ivec2 block, std::uint32_t seed) const {
    return forFamily(CellKind::Plot, familyFor(block, seed));
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
        std::vector<glm::ivec2> cells;
        CellKind kind = CellKind::Empty;
        bool prop = false;
    };
    std::vector<std::pair<std::string, Gather>> gathered;
    const auto gatherFor = [&gathered](const std::string& asset, CellKind kind,
                                       bool prop) -> Gather& {
        for (auto& [name, g] : gathered) {
            if (name == asset) {
                return g;
            }
        }
        gathered.emplace_back(asset, Gather{});
        gathered.back().second.kind = kind;
        gathered.back().second.prop = prop;
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
                              const std::vector<std::string>& candidates, std::uint32_t salt,
                              const std::string& family) {
        if (candidates.empty()) {
            noteUndressed(role);
            return;
        }
        Rng rng = cellRng(plan.settings.seed ^ salt, coord);
        const std::string* chosen = &candidates[rng.nextU32() % candidates.size()];
        if (role == CellKind::Plot && candidates.size() > 1) {
            // Buildings step along a street rather than jumping. Drawing each plot independently
            // from the family gives a two-storey house between two six-storey ones and back again,
            // which is uniform in *character* -- the family did its job -- and random in height,
            // which no built street is.
            //
            // So a block gets a height *band* of its own, and each plot picks near it: the family's
            // pieces are sorted by height, the band names a place in that order, and a plot lands
            // within a piece or two of it. Neighbouring plots in the same block therefore differ by
            // a storey rather than by a tower, and two blocks of the same family still differ from
            // each other. The drift is deliberately small; the whole effect is lost if it is not.
            std::vector<const assets::AssetDescriptor*> byHeight;
            byHeight.reserve(candidates.size());
            for (const std::string& n : candidates) {
                if (const assets::AssetDescriptor* a = library.find(n); a != nullptr) {
                    byHeight.push_back(a);
                }
            }
            if (byHeight.size() > 1) {
                std::ranges::sort(byHeight, {}, [](const assets::AssetDescriptor* a) {
                    // By proportion, not by raw height: every building is scaled to the same plot,
                    // so what decides how tall one *stands* is its height over its footprint.
                    const float foot = std::max(a->naturalSize.x, a->naturalSize.z);
                    return foot > 0.0f ? a->naturalSize.y / foot : a->naturalSize.y;
                });
                Rng blockRng = cellRng(plan.settings.seed ^ 0x6C078965u, cell.block);
                const auto span = static_cast<std::uint32_t>(byHeight.size());
                const std::uint32_t band = blockRng.nextU32() % span;
                // +/- one piece either side of the band, clamped at the ends.
                const int drift = static_cast<int>(rng.nextU32() % 3u) - 1;
                const auto index = static_cast<std::uint32_t>(
                    std::clamp(static_cast<int>(band) + drift, 0, static_cast<int>(span) - 1));
                chosen = &byHeight[index]->name;
            }
        }
        const std::string& name = *chosen;
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
        (void)family;
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
            // A frontage building shares its cell with the footway in front of it, so it has the
            // module *less the footway* to stand in rather than the whole of it. Uniformly, like
            // every other building here: a house squashed along one axis to fit a pavement is a
            // worse answer than a slightly smaller house.
            const float room = cell.frontage
                                   ? std::min(plan.settings.moduleSize * plan.settings.plotFill,
                                              plan.settings.moduleSize - plan.settings.footwayMetres)
                                   : plan.settings.moduleSize * plan.settings.plotFill;
            if (footprint > 0.0f && std::isfinite(footprint)) {
                scale = room / footprint;
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
            if (cell.frontage && plan.settings.footwayMetres > 0.0f) {
                // Back from the kerb by half the footway, which centres the building in what is left
                // of the cell. The building already faces the street, so the direction it faces is
                // the direction to retreat from.
                static constexpr glm::ivec2 kFacing[4] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
                const glm::ivec2 towardStreet = kFacing[static_cast<int>(cell.rotation)];
                position.x -= static_cast<float>(towardStreet.x) * plan.settings.footwayMetres * 0.5f;
                position.z -= static_cast<float>(towardStreet.y) * plan.settings.footwayMetres * 0.5f;
            }
        }
        Gather& g = gatherFor(name, role, false);
        g.positions.push_back(position);
        g.rotations.emplace_back(rotation.x, rotation.y, rotation.z, rotation.w);
        g.scales.emplace_back(scale);
        g.cells.push_back(coord);

        // The overlay that belongs to this piece, if it drew one. Exactly the surface's transform:
        // an overlay is drawn to sit on one specific tile, so anything else -- a jitter, a turn of
        // its own -- puts a guard rail through the middle of the road it is meant to edge.
        const std::vector<std::string>& over = roles.overlaysFor(name);
        if (over.empty() || plan.settings.overlayChance <= 0.0f || building) {
            return;
        }
        // Per run, not per cell. A rail on one cell in the middle of an open road is not a rail, it
        // is litter -- so neighbouring cells along the same band share one decision. The run index
        // is quantised on both axes, so a run reads the same whichever way its road happens to go.
        const int runLen = std::max(1, plan.settings.overlayRun);
        const glm::ivec2 run{coord.x / runLen, coord.y / runLen};
        Rng runRng = cellRng(plan.settings.seed ^ 0x85EBCA77u, run);
        if (runRng.nextFloat() >= plan.settings.overlayChance) {
            return;
        }
        const std::string& overlay = over[runRng.nextU32() % over.size()];
        if (library.find(overlay) == nullptr) {
            return;
        }
        Gather& og = gatherFor(overlay, role, true);
        og.positions.push_back(position);
        og.rotations.emplace_back(rotation.x, rotation.y, rotation.z, rotation.w);
        og.scales.emplace_back(scale);
        og.cells.push_back(coord);
    };

    // Props: several on one cell, jittered, and free to face any way. A prop is not a tile -- it
    // adjoins nothing -- so the quarter-turn rule that keeps roads meeting does not apply to it, and
    // a row of trees all facing the same way is the giveaway that they were placed by a program.
    const auto scatterProps = [&](glm::ivec2 coord, const std::vector<std::string>& candidates) {
        if (candidates.empty() || plan.settings.propsPerCell <= 0) {
            return;
        }
        Rng rng = cellRng(plan.settings.seed ^ 0x27D4EB2Fu, coord);
        // How many, not always the maximum: a yard with exactly three things in it, on every yard,
        // is a pattern rather than a yard.
        const int count = static_cast<int>(rng.nextU32() % (static_cast<std::uint32_t>(
                                                                plan.settings.propsPerCell) + 1u));
        const glm::vec3 centre = plan.centreOf(coord);
        const float spread = plan.settings.moduleSize * plan.settings.propSpread;
        for (int i = 0; i < count; ++i) {
            const std::string& name = candidates[rng.nextU32() % candidates.size()];
            const assets::AssetDescriptor* asset = library.find(name);
            if (asset == nullptr) {
                continue;
            }
            glm::vec3 position = centre;
            position.x += (rng.nextFloat() * 2.0f - 1.0f) * spread;
            position.z += (rng.nextFloat() * 2.0f - 1.0f) * spread;
            if (terrain != nullptr) {
                position.y = terrain->heightAt(glm::vec2(position.x, position.z));
            }
            const glm::quat rotation = glm::angleAxis(rng.nextFloat() * 6.2831853f,
                                                      glm::vec3(0.0f, 1.0f, 0.0f));
            // Pack scale, like the ground: a prop is sized against the kit it was drawn with, and
            // Kenney's trees, planters and tanks are already in proportion to its buildings. Not the
            // plot rule -- a tree has no plot to fill, and stretching one to an 8 m footprint is how
            // a garden ends up with a single enormous shrub in it.
            const float scale = plan.settings.moduleSize / plan.settings.tileUnits;
            Gather& g = gatherFor(name, CellKind::Courtyard, true);
            g.positions.push_back(position);
            g.rotations.emplace_back(rotation.x, rotation.y, rotation.z, rotation.w);
            g.scales.emplace_back(scale);
            g.cells.push_back(coord);
        }
    };

    // Street furniture stands at the kerb and faces the road. Not jittered across the cell like a
    // yard prop: a lamp post in the middle of a footway is in the way, and one that has wandered to
    // the back of it is in somebody's garden. The road is what it belongs to, so the road is what
    // positions it.
    const auto placeStreetProp = [&](glm::ivec2 coord) {
        if (roles.streetProp.empty() || plan.settings.streetPropChance <= 0.0f) {
            return;
        }
        Rng rng = cellRng(plan.settings.seed ^ 0x1B873593u, coord);
        if (rng.nextFloat() >= plan.settings.streetPropChance) {
            return;
        }
        // Which way the road is. A pavement cell with no carriageway beside it is inside a block,
        // and furnishing it would put a traffic light in a back yard.
        static constexpr glm::ivec2 kDirs[4] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
        const glm::ivec2* toward = nullptr;
        for (const glm::ivec2& dir : kDirs) {
            if (plan.isCarriageway({coord.x + dir.x, coord.y + dir.y})) {
                toward = &dir;
                break;
            }
        }
        if (toward == nullptr) {
            return;
        }
        const std::string& name = roles.streetProp[rng.nextU32() % roles.streetProp.size()];
        const assets::AssetDescriptor* asset = library.find(name);
        if (asset == nullptr) {
            noteUndressed(CellKind::Pavement);
            return;
        }
        const float m = plan.settings.moduleSize;
        glm::vec3 position = plan.centreOf(coord);
        // Out towards the kerb, and along it by a little so a run of lamps is not a ruled line.
        position.x += static_cast<float>(toward->x) * m * 0.34f +
                      static_cast<float>(toward->y) * (rng.nextFloat() - 0.5f) * m * 0.4f;
        position.z += static_cast<float>(toward->y) * m * 0.34f +
                      static_cast<float>(toward->x) * (rng.nextFloat() - 0.5f) * m * 0.4f;
        if (terrain != nullptr) {
            position.y = terrain->heightAt(glm::vec2(position.x, position.z));
        }
        // Facing the carriageway, which is the whole point of a signal and the right way round for
        // a lamp's arm. A quarter turn, because the street is on an axis.
        const glm::quat rotation =
            glm::angleAxis(quarterRadians(quarterTowards(*toward)), glm::vec3(0.0f, 1.0f, 0.0f));
        const float scale = plan.settings.moduleSize / plan.settings.tileUnits;
        Gather& g = gatherFor(name, CellKind::Pavement, true);
        g.positions.push_back(position);
        g.rotations.emplace_back(rotation.x, rotation.y, rotation.z, rotation.w);
        g.scales.emplace_back(scale);
        g.cells.push_back(coord);
    };

    for (int z = 0; z < plan.depth; ++z) {
        for (int x = 0; x < plan.width; ++x) {
            const glm::ivec2 coord{x, z};
            const CityCell cell = plan.at(coord);
            if (cell.kind == CellKind::Empty) {
                continue;
            }
            // One family per block, chosen once. Its buildings, the ground under them and the things
            // standing in its yards all come from it, which is what makes a block read as one place.
            std::string family = cell.block.x >= 0
                                     ? roles.familyFor(cell.block, plan.settings.seed)
                                     : std::string{};
            if (cell.kind == CellKind::Plot && cell.corner && !family.empty() &&
                plan.settings.cornerMix > 0.0f) {
                // The corner shop. A block of one family throughout is a suburb with nothing in it
                // but houses; the corner is where the exception belongs, because that is where two
                // streets meet and where a shop would actually stand. Which other family it draws
                // from comes from the seed, so no family is named in code -- "commercial" is a word
                // in a manifest, not a concept this file knows.
                const std::vector<std::string> all = roles.families();
                if (all.size() > 1) {
                    Rng rng = cellRng(plan.settings.seed ^ 0xCC9E2D51u, coord);
                    if (rng.nextFloat() < plan.settings.cornerMix) {
                        std::vector<std::string> others;
                        for (const std::string& f : all) {
                            if (f != family) {
                                others.push_back(f);
                            }
                        }
                        family = others[rng.nextU32() % others.size()];
                    }
                }
            }
            if (cell.kind == CellKind::Plot) {
                // The ground first, then the building on it. Salted so the floor's choice is its own
                // and not the building's -- without that, every plot that drew building number two
                // would also draw ground number two, and the two would move together for no reason
                // anybody could see.
                // A frontage cell's ground is the footway it shares; anywhere else it is the
                // family's own forecourt.
                const CellKind groundRole =
                    cell.frontage ? CellKind::Pavement : CellKind::Courtyard;
                placeOne(coord, cell, groundRole,
                         cell.frontage ? roles.forKind(CellKind::Pavement)
                                       : roles.forFamily(CellKind::Courtyard, family),
                         0x5BD1E995u, family);
                placeOne(coord, cell, CellKind::Plot, roles.forFamily(CellKind::Plot, family), 0u,
                         family);
                // A frontage cell carries the footway, so it carries what stands on a footway. When
                // the block still has a pavement ring the lamps go there instead; either way they
                // follow the pavement rather than a cell kind.
                if (cell.frontage) {
                    placeStreetProp(coord);
                }
                continue;
            }
            if (cell.kind == CellKind::Courtyard || cell.kind == CellKind::Plaza) {
                placeOne(coord, cell, cell.kind, roles.forFamily(cell.kind, family), 0u, family);
                scatterProps(coord, roles.propsFor(family));
                continue;
            }
            placeOne(coord, cell, cell.kind, roles.forKind(cell.kind), 0u, family);
            if (cell.kind == CellKind::Pavement) {
                placeStreetProp(coord);
            }
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
        placement.prop = g.prop;
        placement.cells = g.cells;
        placement.cloud = std::move(cloud);
        out.placements.push_back(std::move(placement));
    }
    return out;
}

} // namespace avgen::world
