#pragma once

// What terrain hands water (§12 / §31, ADR-090).
//
// §31 forbids drawing a water polygon over random terrain: the channel has to exist in the ground
// before anything is drawn in it. That makes the terrain generator, not the water renderer, the
// authority on where water is and which way it runs -- and it makes the boundary between them worth
// writing down, because the two are built by different hands at the same time.
//
// **The seam has two halves, and both are needed.**
//
//   * *Continuous.* `TerrainQuery::waterDepthAt(p)` is metres of water over the bed at any point,
//     and `WorldMap::waterSurface(p)` is the surface's world y. These are analytic, exact at any
//     resolution, and already what `buildChunkWater` meshes the surface from -- so a renderer that
//     wants depth colouration, a shoreline fade or a wet-sand band reads them directly and needs
//     nothing from this file. Terrain does not hand over a baked depth texture, because the field
//     is a closed-form function and a baked copy of it would be a second truth to keep in step.
//
//   * *Described.* A depth field cannot say which way a river runs, where it begins, or that these
//     two wet regions are one body and that one is another. Those are topological facts the
//     generator knows when it traces a course and which cannot be recovered from the field
//     afterwards without a flow simulation. `WaterCourse` below carries them: a downstream-ordered
//     centreline with the surface level at every node, a half-width, a depth, and the descent the
//     course makes over its length.
//
// A `WaterCourse` is terrain's statement of fact. It is not §12's `WaterBody` and does not try to
// be: a `WaterBody` is a renderable, simulable thing with a material, a flow strength and
// turbulence, and it is water's to define. The intended direction is one way -- a `WaterBody` is
// *built from* a `WaterCourse`, taking its type from `kind`, its `flowDirection` from `flowAt`, and
// a starting `flowSpeed` from `flowSpeed()` -- so that moving a river in the terrain moves the
// water, and nothing has to be re-authored when it does.
//
// This reads `WorldMap::features`, so it works on an authored world exactly as it does on a
// generated one. Glowmere's hand-written river produces a course here without being regenerated.

#include "world/world_map.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::world {

// What kind of body this is. The distinction that matters to a renderer is whether it flows: a
// river has a direction everywhere, a pond and a lake do not and want wind-driven or circular
// motion instead (§12). Lake and Pond differ only in size and in having a shape rather than a
// centre, which is enough to justify separate words because they are lit and inhabited differently.
enum class WaterKind : std::uint8_t { River, Pond, Lake, Sea };
[[nodiscard]] const char* waterKindName(WaterKind kind);

struct WaterCourse {
    std::string name;
    WaterKind kind = WaterKind::River;
    // (x, surface y, z) in world metres, ordered **downstream**: node 0 is the head and the last
    // node is the mouth. For still water this is the shape of the body rather than a direction of
    // travel, and for a pond it is a single node at the centre. The y values are the water surface,
    // not the bed -- the bed is `depth` below them, and the real bed at any point is
    // `WorldMap::height`, which is what the shoreline is actually drawn against.
    std::vector<glm::vec3> centreline;
    float halfWidth = 8.0f;   // metres from the centreline to the bank
    float depth = 1.5f;       // metres from the surface to the bed at the centre
    float descent = 0.0f;     // metres the surface falls from head to mouth; 0 for still water
    float length = 0.0f;      // metres along the centreline
    int feature = -1;         // index into WorldMap::features, or -1 for the sea

    // Unit downstream direction at p, or (0, 0) where the body does not flow. Taken from the
    // centreline segment nearest p rather than from the height gradient: the gradient of a river
    // bed points at the near bank as often as it points downstream, because a channel is a trough
    // and a trough's steepest descent is across it.
    [[nodiscard]] glm::vec2 flowAt(glm::vec2 p) const;
    // The water surface above p, interpolated along the centreline. Equal to
    // `WorldMap::waterSurface` inside the banks; outside them it is the level this course *would*
    // hold, which is what a caller placing something at the water's edge wants.
    [[nodiscard]] float surfaceAt(glm::vec2 p) const;
    // Metres per second, suggested from the course's own gradient: a steep short river runs, a long
    // flat one drifts. A starting value for §12's `flowSpeed`, not a simulation -- water owns what
    // it does with it.
    [[nodiscard]] float flowSpeed() const;
    // Is p between the banks of this course? A cheap planar test against the centreline, which is
    // what a caller scattering lily pads or asking "which body is this" wants; whether there is
    // actually water over the bed at p is `TerrainQuery::waterDepthAt`.
    [[nodiscard]] bool contains(glm::vec2 p) const;
    // Where p is along the course, 0 at the head and 1 at the mouth. Undefined for a Sea.
    [[nodiscard]] float alongAt(glm::vec2 p) const;
};

// Every water body in the map, in feature order, with the sea last when the map has one. Pure
// function of the map; call it whenever the map changes rather than caching it, because it is a
// walk over a handful of features and the cached copy is the one that goes stale.
[[nodiscard]] std::vector<WaterCourse> waterCourses(const WorldMap& map);

// The course nearest p, or null when the map holds none. Ties go to the earlier course, so the
// answer is stable.
[[nodiscard]] const WaterCourse* nearestCourse(const std::vector<WaterCourse>& courses, glm::vec2 p);

} // namespace avgen::world
