#pragma once

// Keeping a camera out of the scenery (ADR-080).
//
// A directed camera is baked from shot geometry -- orbit points around a subject at a distance in
// radii -- and that geometry knows nothing about what is in the way. It will happily put the eye
// inside a hillside, inside a tree, or inside the hero it is looking at. On Glowmere the first
// directed pass flew through the canopy.
//
// What the world can cheaply say about obstruction:
//
//   * **Terrain** is exact and analytic. `WorldMap::height` is a closed-form evaluation, so asking
//     "how high is the ground here" costs a few noise samples and no memory.
//   * **Vegetation** is not enumerable -- there are a hundred thousand instances and they live on
//     the GPU -- but it is *predictable*. A scatter layer states its height and the biomes it grows
//     in, and the map states each biome's weight at a point. The tallest layer that grows here is
//     therefore known without touching a single instance. It is a statistical canopy rather than a
//     collision mesh: it says "trees about nine metres tall grow around here", not "there is a
//     trunk at exactly this spot". That is the right resolution for a camera, which wants to be
//     above the canopy or in a clearing, not threading between trunks.
//   * **Heroes** are a handful of spheres and are exact.
//   * **Clearances** (ADR-067) are where the composer decided nothing grows. A camera in one is in
//     a glade or a corridor and may fly low, which is the whole reason the corridor exists.
//
// The correction is minimal, and **which way it goes depends on what is in the way**.
//
// *Ground and canopy lift.* A camera that rises slightly to clear a hillside or a treeline reads as
// a camera choosing its altitude rather than as a camera being shoved, and there is nowhere sideways
// to go from inside a hill anyway.
//
// *Heroes push out sideways.* This is the opposite of what it was, and the reason is the Auto-director
// gaining a continuous-shot mode. Lifting worked for a cut-based film, where being inside a hero meant
// the camera had come too close on the way past. It is exactly wrong for a take that dollies toward a
// subject and orbits it: the camera is lifted **over** the thing it is supposed to be circling, and
// that is a worse failure than clipping because it looks deliberate. Pushing the eye out to the
// nearest point outside the hero's capsule keeps the orbit an orbit.
//
// Nothing here moves a camera that is already clear.

#include "world/ecology.hpp"
#include "world/hero.hpp"
#include "world/world_map.hpp"

#include <glm/glm.hpp>
#include <span>
#include <vector>

namespace avgen::world {

// What a camera has to stay out of at a given place.
struct ClearanceField {
    const WorldMap* map = nullptr;
    const Ecology* ecology = nullptr;
    std::span<const HeroPoint> heroes;

    // The camera's own personal space. A lens sitting exactly on a surface still shows it filling
    // the frame, and the near plane clips into it long before the eye point touches it.
    float cameraRadius = 1.2f;
    // Headroom above the ground when there is nothing growing.
    float groundClearance = 1.6f;
    // How much of the canopy a camera must clear to count as above it. Not 1.0: flying at exactly
    // the height of the tallest possible specimen means grazing every one of them, and the canopy
    // figure is the tallest thing that *could* grow here rather than what does.
    float canopyClearance = 1.12f;

    // The tallest vegetation that grows at `p`, in metres above the ground. Zero where nothing does.
    [[nodiscard]] float canopyHeight(glm::vec2 p) const;
    // The lowest world-space y a camera may occupy at `p`.
    [[nodiscard]] float minimumHeight(glm::vec2 p) const;
    // How far inside a hero `p` is, in metres. Zero when it is outside all of them.
    [[nodiscard]] float heroPenetration(glm::vec3 p) const;
    // The horizontal push that takes `p` out of the deepest hero it is inside: a planar vector whose
    // length is the penetration. Zero when it is outside all of them.
    //
    // A camera exactly on a hero's axis has no direction to be pushed in, and the fallback is +X --
    // deterministic rather than correct, because "correct" does not exist there and a random or
    // uninitialised direction would make a bake non-reproducible. It is vanishingly rare: the axis of
    // a hero is where the shot is looking *from* only if something has gone wrong already.
    [[nodiscard]] glm::vec2 heroPushOut(glm::vec3 p) const;
};

// One adjusted camera position, and why.
struct ClearanceAdjustment {
    glm::vec3 position{0.0f};
    float lifted = 0.0f;      // metres the point was raised (ground and canopy)
    float pushed = 0.0f;      // metres the point was moved sideways (heroes)
    bool insideHero = false;
};

// Clears `p`: sideways out of any hero it is inside, then up out of the ground and the canopy.
//
// Heroes first, because the push can land the camera somewhere lower and the floor has to be applied
// to where it ends up; then one more hero check, because a push out of one hero can land inside
// another. One extra iteration rather than a loop -- two heroes overlapping enough to trap a camera
// between them is a staging problem, and a solver that hides it is worse than a bake that shows it.
//
// Returns the point unchanged when it is already clear.
[[nodiscard]] ClearanceAdjustment clearPoint(const ClearanceField& field, glm::vec3 p);

// Applies `clearPoint` along a whole path, then smooths the corrections.
//
// Smoothed because correcting keys independently produces a kink at every corrected one: a camera
// that steps up for one key and back down for the next reads far worse than one that never dipped.
// The vertical smoothing only ever raises -- a pass that could lower a point would undo the clearance
// it was run to create.
//
// The lateral correction is smoothed too, and cannot use that trick: "only ever push further out" has
// no meaning when the push directions differ between neighbours. So the lateral pass smooths freely
// and then **re-asserts** clearance on the result, which is the only way to be sure the smoothing did
// not put a point back inside the hero it was moved out of.
//
// Returns how many points were moved, by either mechanism.
[[nodiscard]] std::size_t clearPath(const ClearanceField& field, std::vector<glm::vec3>& path,
                                    int smoothingPasses = 2);

} // namespace avgen::world
