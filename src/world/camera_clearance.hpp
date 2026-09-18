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
#include <string>
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
    // The same answer, from a sample of the map the caller has already taken at `p`.
    //
    // The canopy is a function of the biome and the biome is a function of altitude, slope and
    // moisture -- three numbers that come out of `WorldMap::sample` and nowhere else. The one-argument
    // form above takes that sample itself, which is correct and is why a walkability query used to
    // evaluate the world **twice at the same point**: `TerrainQuery::at` sampled the map, then called
    // the canopy, which sampled the map again. Measured on Glowmere: `WorldMap::sample` 7.810 us,
    // `canopyHeight` 8.136 us, `TerrainQuery::at` 15.923 us -- the sum, because the second sample is
    // the first one done over. Handing the sample across is the same arithmetic on the same point.
    [[nodiscard]] float canopyHeight(glm::vec2 p, const Sample& sample) const;
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

// ---- line of sight ------------------------------------------------------------------------------
//
// Everything above keeps the camera out of the scenery. **Nothing above asks whether the scenery is
// between the camera and the thing the camera is pointed at**, and that is a different failure with
// a different look: a camera perfectly clear of the ground, at the authored distance and elevation,
// framing a sixteen-metre elder that happens to be on the far side of a ridge. Every check this
// repository had passes on that shot. The owner asked for the missing half by name -- "ensure the
// hero is not obstructed by an object when it's got focus".
//
// **Obstruction is not binary, and the model's honesty about it decides the design.** The world
// offers three kinds of thing that can stand in the way and it knows them to three different
// resolutions:
//
//   * **Terrain** is exact and analytic, and it is opaque. A sightline that dips under the ground
//     is blocked, full stop.
//   * **Other heroes** are exact capsules and are opaque over the capsule.
//   * **The canopy is statistical.** It says "trees about nine metres tall grow around here", never
//     "there is a trunk at this spot" (see the note at the top of this file). A ray running under
//     the canopy top may be passing a trunk or may be passing between two of them, and *no amount
//     of arithmetic on a height field can tell those apart*.
//
// So the canopy is **measured and reported, never corrected for**. Inventing an extinction
// coefficient over a field that carries no density would be a magic number in the sense §28
// prohibits, and it would be actively wrong for the shot vocabulary: `ShotKind::Discovery` exists to
// come in "from the side and from above, so the subject slides out from behind whatever is in front
// of it", and `ShotKind::Drift` is defined as being "about what passes between the camera and the
// subject". Both are composed *around* foreground vegetation. A pass that lifted the camera over
// every canopy between it and its hero would delete two of the fourteen moves.
//
// The two exact obstructions are corrected, and the correction is derived rather than chosen -- see
// `clearSightlines`.

// The thing being framed, as a capsule. Deliberately not a `HeroPoint`: a shot's `FocalTarget` is
// not a hero (it carries a radius and a name and nothing else this needs), and a sightline query
// should be answerable about anything with a size.
struct SubjectCapsule {
    glm::vec3 position{0.0f};  // the base, as `HeroPoint::position` is
    float radius = 1.0f;
    float height = 1.0f;
    // Which hero this is, so the subject is not counted among the things blocking the view of it.
    // Matched by name because that is the only identity a `FocalTarget` and a `HeroPoint` share.
    std::string name;
};

// What an eye can see of a subject, and what is in the way.
struct Sightline {
    // Of the rays cast across the subject's silhouette, the fraction that reach it past the
    // obstructions the world knows **exactly** -- the ground and other heroes. 1 is a clear view.
    float visible = 1.0f;
    // Of those same rays, the fraction that pass through vegetation at all, and the longest run
    // inside it on any of them. Reported; never acted on. See above.
    float throughCanopy = 0.0f;
    float canopyMetres = 0.0f;

    // The minimal eye motion that would restore a clear view, in metres, derived and not chosen.
    //
    // A ray from eye E to a point A on the subject passes through P = E + s(A - E). A is fixed --
    // it is on the subject -- so raising E by d raises P by exactly (1 - s)d. The lift that clears
    // an obstruction standing h above P is therefore h / (1 - s), and the lift that clears them all
    // is the largest of those. The same lever gives the lateral push: an eye that must clear a
    // blocking hero's radius by g at parameter s moves g / (1 - s) sideways.
    //
    // Both are reported unbounded. Whether a correction that large is still the shot somebody
    // composed is the caller's judgement, and `clearSightlines` is where it is made.
    float requiredLift = 0.0f;
    glm::vec2 requiredPush{0.0f};
    // The hero standing in the way, when one is. Empty when the ground is the obstruction, or when
    // nothing is.
    std::string blocker;

    [[nodiscard]] bool clear() const { return visible >= 1.0f; }
};

// What the camera can see of `subject` from `eye`.
//
// Nine rays: three heights up the capsule and three across it. The lateral pair sit at r/sqrt(2),
// which is the radius that halves a disc's area -- so the three samples stand for roughly equal
// thirds of the silhouette rather than for three arbitrary points on it, and "two of nine blocked"
// means about what it sounds like. The heights skip the very bottom and the very top: a ray
// arriving at the subject's feet grazes the ground the subject is standing on by construction, and
// a capsule's top is a cap rather than a cylinder.
//
// Marched at a fixed step in metres, not a fixed sample count, so a forty-metre sightline and a
// four-hundred-metre one are examined at the same resolution. A probe whose resolution scales with
// the thing it measures cannot compare two shots.
//
// The last `subject.radius` of every ray is excluded. The ground the subject stands on, and the
// subject's own body, are not obstructions between the camera and the subject; without this every
// ray to a low sample point reports the hero as blocking itself, and the (1 - s) lever runs away as
// s approaches 1.
[[nodiscard]] Sightline heroSightline(const ClearanceField& field, glm::vec3 eye,
                                      const SubjectCapsule& subject, float stepMetres = 1.0f);

// What one key of a baked path is looking at. `holds` false means the shot covering this key is not
// holding a subject here -- it is aiming down its own move, or it is halfway through a handoff's
// swing -- and the key is examined by nothing and moved by nothing.
struct SightlineTarget {
    SubjectCapsule subject;
    bool holds = false;
};

struct SightlineResult {
    std::size_t examined = 0;    // keys whose shot was holding a subject
    std::size_t obstructed = 0;  // ...of which something exactly-known stood in the way
    std::size_t corrected = 0;   // ...of which the correction was inside the bound and was applied
    std::size_t refused = 0;     // ...of which it was not, and the key was left exactly where it was
    float worstRefused = 0.0f;   // the largest correction refused, in metres
    float largestApplied = 0.0f; // the largest correction applied, in metres
};

// Clears the line of sight along a whole baked path, one subject per key, after `clearPath` has run.
//
// **Bounded, and the bound is the point.** "Move until nothing is in the way" produces a worse shot
// than a slightly-occluded one as soon as the thing in the way is a hillside: the lift that sees
// over a ridge halfway to the subject is twice the ridge's height above the sightline, and a
// hundred-metre lift is not the shot anybody composed. So a correction is applied only while it
// stays inside `maxCorrectionRatio` of the eye's distance to the subject, and is otherwise refused
// and counted.
//
// The default ratio is 0.25 and it is read off the director's own table rather than chosen.
// `Shot::startElevation` is a height as a *multiple of the orbit radius* -- exactly this ratio --
// and every elevation `defaultsFor` composes an ordinary shot at is below it: Establish 0.22/0.20,
// Approach 0.18/0.20, Orbit 0.18/0.24, Track 0.12, Transition 0.18. A correction larger than 0.25
// therefore moves the eye further than the entire range of elevations the vocabulary works in,
// which is the definition of "this is no longer a shot the director would have produced".
//
// Smoothed the same way `clearPath` smooths, and for the same reason: a key lifted three metres
// between two that were not reads as a bump. The vertical pass only ever raises, so it cannot undo
// the sightline it was run to create -- and because raising the eye raises every point on the ray
// monotonically, a smoothing pass that only raises cannot re-block a sightline it cleared either.
[[nodiscard]] SightlineResult clearSightlines(const ClearanceField& field,
                                              std::vector<glm::vec3>& path,
                                              std::span<const SightlineTarget> targets,
                                              float maxCorrectionRatio = 0.25f,
                                              int smoothingPasses = 2);

} // namespace avgen::world
