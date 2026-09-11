#include "world/terrain_gen.hpp"

#include "core/noise.hpp"
#include "core/rng.hpp"
#include "scene/struct_hash.hpp"
#include "world/world_map.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace avgen::world {
namespace {

using json = nlohmann::json;
using scene::detail::StructHash;

constexpr float kTwoPi = 6.2831853071795864769f;

// Independent random streams, one per stage. Numbered rather than sequential so that adding a stage
// -- or changing how many draws an earlier one makes -- does not shift every later stage's stream
// and silently change every existing seed's world.
constexpr std::uint64_t kStreamLandform = 11;
constexpr std::uint64_t kStreamDrainage [[maybe_unused]] = 23;
constexpr std::uint64_t kStreamStanding = 41;

[[nodiscard]] float clamp01(float v) { return glm::clamp(v, 0.0f, 1.0f); }

// ---- 1. shape: the octave stack --------------------------------------------------------------
//
// One `NoiseLayer` is one octave, and none of these numbers is exposed: §30 is explicit that the
// artist gets a style and a roughness, not a lacunarity. What roughness actually moves is three
// things at once -- how many octaves there are, how much energy the fine ones carry, and how much
// the multifractal weighting collects detail on crests -- because moving any one of them alone
// gives "the same terrain with more fuzz" rather than "rougher country".
struct Shape {
    std::vector<NoiseLayer> layers;
    float erosion = 0.4f;
};

Shape shapeFor(const TerrainParams& p) {
    Shape out;
    const float r = clamp01(p.roughness);
    const float extent = std::max(p.extent, 1.0f);

    int octaves = 4;
    float persistence = glm::mix(0.40f, 0.56f, r);   // energy handed to each finer octave
    float lacunarity = 2.07f;                        // deliberately not 2: integer ratios align
    float ridgedBase = 0.0f;                         // how teeth-like the mid octaves are
    float ridgedRamp = 0.0f;                         // how much more so the finer ones are
    float warp = extent * glm::mix(0.020f, 0.055f, r);
    // The broadest octave's wavelength as a fraction of the map. Under about half a map per cycle a
    // "landform" octave is really a second detail octave and the world loses its large shape.
    float broad = 0.72f;

    switch (p.style) {
    case TerrainStyle::RollingHills:
        octaves = 4 + static_cast<int>(std::lround(r * 2.0f));
        ridgedBase = 0.05f;
        ridgedRamp = 0.15f;
        out.erosion = 0.30f + r * 0.25f;
        break;
    case TerrainStyle::Valley:
        // The valley itself is a stamped feature; the noise's job is to give its sides spurs and
        // drainage texture rather than to compete with it for the large shape.
        octaves = 4 + static_cast<int>(std::lround(r * 2.0f));
        broad = 0.58f;
        ridgedBase = 0.18f;
        ridgedRamp = 0.35f;
        out.erosion = 0.40f + r * 0.25f;
        break;
    case TerrainStyle::Basin:
        octaves = 4 + static_cast<int>(std::lround(r * 2.0f));
        broad = 0.62f;
        ridgedBase = 0.10f;
        ridgedRamp = 0.25f;
        out.erosion = 0.35f + r * 0.25f;
        break;
    case TerrainStyle::Mountainous:
        // Ridged multifractal is what makes rock read as rock: crests sharp, gullies smooth. The
        // erosion weighting matters more here than anywhere else, because it is the thing that
        // keeps the detail on the arêtes and out of the cirques.
        //
        // The ramp is *negative* here alone. Ridging a fine octave does not make a mountain sharper,
        // it crinkles every surface at the scale of a few metres -- the first cut of this ramped
        // upward and produced 400 m of relief textured like a fingerprint, with no landform legible
        // at any distance. Big ridges sharp, small detail smooth, which is also what erosion does.
        octaves = 5 + static_cast<int>(std::lround(r * 1.0f));
        persistence = glm::mix(0.44f, 0.56f, r);
        broad = 0.88f;
        ridgedBase = glm::mix(0.55f, 0.90f, clamp01(p.ridgeStrength));
        ridgedRamp = -0.28f;
        out.erosion = 0.62f + r * 0.25f;
        warp = extent * glm::mix(0.030f, 0.070f, r);
        break;
    case TerrainStyle::Plateau:
        // Broad and quiet. A plateau is made by the mesas stamped on top of it, and noise loud
        // enough to be interesting on its own is noise that stops a mesa reading as a table.
        octaves = 3 + static_cast<int>(std::lround(r * 2.0f));
        persistence = glm::mix(0.32f, 0.48f, r);
        broad = 0.85f;
        ridgedBase = 0.0f;
        ridgedRamp = 0.10f;
        out.erosion = 0.18f + r * 0.22f;
        break;
    }

    const float f0 = 1.0f / (extent * broad);
    float amplitude = 1.0f;
    float frequency = f0;
    for (int i = 0; i < octaves; ++i) {
        NoiseLayer l;
        l.frequency = frequency;
        l.amplitude = amplitude;
        const float t = octaves > 1 ? static_cast<float>(i) / static_cast<float>(octaves - 1) : 0.0f;
        l.ridged = clamp01(ridgedBase + ridgedRamp * t);
        // Only the coarse octaves are warped. Warping a fine octave costs an fbm3Vec per sample and
        // buys detail nobody can see; warping the coarse ones is what stops the landform sitting on
        // a visible grid.
        l.warp = i < 2 ? warp / (1.0f + static_cast<float>(i)) : 0.0f;
        out.layers.push_back(l);
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return out;
}

// ---- 2. fit: make elevation min/max mean metres -------------------------------------------------
//
// `octaveSum` is linear in every layer's amplitude (the multifractal weight is computed from the
// unit-amplitude octave value, not from the amplitude), so a single measurement of the stack is
// enough to rescale it exactly onto the range the artist asked for. That is the whole reason
// Elevation min/max can be a metre figure rather than a hint.
// Every vertical quantity in a `WorldMap`, scaled and shifted together. The height function is
// affine in exactly these terms -- `h = baseHeight + noise + raise`, then a mix toward a path level,
// then a min against a path level minus an amplitude -- so `h -> k*h + shift` is produced exactly by
// scaling the amplitudes, scaling and shifting the path levels, and doing the same to `baseHeight`.
// Widths, falloffs, flattens and roughnesses are horizontal or unitless and must not move.
void rescale(WorldMap& map, float k, float shift) {
    for (NoiseLayer& l : map.layers) {
        l.amplitude *= k;
    }
    map.baseHeight = map.baseHeight * k + shift;
    for (Feature& f : map.features) {
        f.amplitude *= k;
        f.waterDepth *= k;
        for (glm::vec3& q : f.path) {
            q.y = q.y * k + shift;
        }
    }
}

// Put the map's measured height range exactly on [lo, hi]. Run once on the bare octave stack and
// again once the landform features are stamped -- the second is the one that makes Elevation
// min/max mean metres, because a style that stacks five ridges on top of the noise otherwise
// overshoots the range it was given by more than the range itself.
void fitToRange(WorldMap& map, float lo, float hi) {
    map.prepare();
    const float measured = map.sampledMaxHeight - map.sampledMinHeight;
    const float want = std::max(hi - lo, 0.0f);
    const float k = measured > 1e-4f ? want / measured : 1.0f;
    rescale(map, k, lo - map.sampledMinHeight * k);
    map.prepare();
}

// ---- path helpers ------------------------------------------------------------------------------

// A polyline crossing the whole map at `angle`, offset `offset` metres to one side of centre, with
// `points` control points wobbled laterally. Used for ridges and valleys: a perfectly straight ridge
// is the loudest possible tell that terrain was generated, and it costs one noise call to avoid.
// `headLevel` and `mouthLevel` are the path's y values at the two ends, interpolated along it.
// They only matter for a feature with a non-zero `flatten`, and when they do they are the whole
// difference between a trough and a valley: a corridor of constant depth has no downhill along its
// length, so a river that reaches its floor has nowhere to go and stops there. Every generated
// valley did exactly that until these were plumbed through.
std::vector<glm::vec3> crossingPath(float extent, float angle, float offset, int points,
                                    float wobble, Rng& rng, float headLevel = 0.0f,
                                    float mouthLevel = 0.0f) {
    const glm::vec2 dir(std::cos(angle), std::sin(angle));
    const glm::vec2 perp(-dir.y, dir.x);
    const glm::vec2 centre = perp * offset;
    const float half = extent * 0.62f;   // runs past the edge, so the feature does not stop mid-map
    std::vector<glm::vec3> path;
    path.reserve(static_cast<std::size_t>(points));
    for (int i = 0; i < points; ++i) {
        const float t = points > 1 ? static_cast<float>(i) / static_cast<float>(points - 1) : 0.5f;
        const glm::vec2 p = centre + dir * glm::mix(-half, half, t) + perp * rng.range(-wobble, wobble);
        path.emplace_back(p.x, glm::mix(headLevel, mouthLevel, t), p.y);
    }
    return path;
}

// A closed ring. `closestOnPath` measures distance to the polyline, so a ring of Ridge is a crater
// rim or a basin wall -- the one landform the feature vocabulary has no word for and the one a
// basin needs most.
std::vector<glm::vec3> ringPath(glm::vec2 centre, float radius, int points, float wobble, Rng& rng) {
    std::vector<glm::vec3> path;
    path.reserve(static_cast<std::size_t>(points) + 1);
    for (int i = 0; i <= points; ++i) {
        const float a = kTwoPi * static_cast<float>(i % points) / static_cast<float>(points);
        const float r = radius + rng.range(-wobble, wobble);
        path.emplace_back(centre.x + std::cos(a) * r, 0.0f, centre.y + std::sin(a) * r);
    }
    return path;
}

Feature makeFeature(std::string name, FeatureKind kind, std::vector<glm::vec3> path, float width,
                    float amplitude, float falloff, float flatten, float roughness, int smoothing) {
    Feature f;
    f.name = std::move(name);
    f.kind = kind;
    f.path = std::move(path);
    f.width = width;
    f.amplitude = amplitude;
    f.falloff = falloff;
    f.flatten = flatten;
    f.roughness = roughness;
    f.smoothing = smoothing;
    return f;
}

// ---- 3. landform: what a style actually is ------------------------------------------------------

// The features that do not need to know how high the ground is: ridges and valleys raise and lower
// relative to whatever is there. They go on first so that the level-dependent ones (a basin floor, a
// mesa top) can be measured against a map that already has them.
std::vector<Feature> landformFeatures(const TerrainParams& p, float span, Rng& rng) {
    const float E = std::max(p.extent, 1.0f);
    const float valley = clamp01(p.valleyStrength);
    const float ridge = clamp01(p.ridgeStrength);
    const float floorHi = p.elevationMin + span * 0.50f;   // a corridor's head
    const float floorLo = p.elevationMin + span * 0.06f;   // and its mouth
    // Which way a corridor runs downhill. Drawn from the seed rather than fixed, because a
    // generator whose valleys always descend the same way is a generator with a signature.
    const bool flip = rng.nextFloat() < 0.5f;
    const float headLevel = flip ? floorLo : floorHi;
    const float mouthLevel = flip ? floorHi : floorLo;
    std::vector<Feature> out;

    switch (p.style) {
    case TerrainStyle::RollingHills: {
        const int ridges = 1 + static_cast<int>(std::lround(ridge * 2.0f));
        for (int i = 0; i < ridges; ++i) {
            out.push_back(makeFeature("swell-" + std::to_string(i), FeatureKind::Ridge,
                                      crossingPath(E, rng.range(0.0f, kTwoPi),
                                                   rng.range(-E * 0.30f, E * 0.30f), 5, E * 0.06f, rng),
                                      E * rng.range(0.14f, 0.22f), span * 0.13f * (0.4f + ridge),
                                      0.75f, 0.0f, 1.0f, 3));
        }
        const int hollows = 1 + static_cast<int>(std::lround(valley * 1.0f));
        for (int i = 0; i < hollows; ++i) {
            out.push_back(makeFeature("hollow-" + std::to_string(i), FeatureKind::Valley,
                                      crossingPath(E, rng.range(0.0f, kTwoPi),
                                                   rng.range(-E * 0.28f, E * 0.28f), 5, E * 0.07f, rng,
                                                   headLevel, mouthLevel),
                                      E * rng.range(0.18f, 0.26f), span * 0.16f * (0.4f + valley),
                                      0.70f, 0.22f, 0.9f, 3));
        }
        break;
    }
    case TerrainStyle::Valley: {
        // One corridor through the middle with a ridge wall on each side. The corridor is the
        // subject; the walls are what make it a valley rather than a dent.
        const float angle = rng.range(0.0f, kTwoPi);
        out.push_back(makeFeature("valley-floor", FeatureKind::Valley,
                                  crossingPath(E, angle, rng.range(-E * 0.06f, E * 0.06f), 6, E * 0.075f, rng,
                                               headLevel, mouthLevel),
                                  E * 0.27f, span * 0.42f * (0.45f + valley), 0.75f, 0.38f, 0.55f, 3));
        for (int side = 0; side < 2; ++side) {
            const float offset = (side == 0 ? 1.0f : -1.0f) * E * rng.range(0.28f, 0.36f);
            out.push_back(makeFeature("wall-" + std::to_string(side), FeatureKind::Ridge,
                                      crossingPath(E, angle + rng.range(-0.12f, 0.12f), offset, 5,
                                                   E * 0.05f, rng),
                                      E * rng.range(0.13f, 0.18f), span * 0.34f * (0.4f + ridge),
                                      1.05f, 0.0f, 1.0f, 3));
        }
        break;
    }
    case TerrainStyle::Basin: {
        const glm::vec2 centre(rng.range(-E * 0.07f, E * 0.07f), rng.range(-E * 0.07f, E * 0.07f));
        // A single-point Valley is a radial depression: the bowl.
        out.push_back(makeFeature("bowl", FeatureKind::Valley,
                                  {glm::vec3(centre.x, 0.0f, centre.y)}, E * 0.42f,
                                  span * 0.48f * (0.4f + valley), 0.70f, 0.0f, 0.8f, 0));
        // And a ring of Ridge around it is the wall. Without it a bowl is a dimple; with it the
        // horizon closes and the place has an inside and an outside.
        out.push_back(makeFeature("basin-wall", FeatureKind::Ridge,
                                  ringPath(centre, E * 0.44f, 11, E * 0.05f, rng), E * 0.13f,
                                  span * 0.30f * (0.4f + ridge), 0.95f, 0.0f, 1.0f, 3));
        break;
    }
    case TerrainStyle::Mountainous: {
        const int ranges = 2 + static_cast<int>(std::lround(ridge * 3.0f));
        for (int i = 0; i < ranges; ++i) {
            out.push_back(makeFeature("range-" + std::to_string(i), FeatureKind::Ridge,
                                      crossingPath(E, rng.range(0.0f, kTwoPi),
                                                   rng.range(-E * 0.36f, E * 0.36f), 6, E * 0.055f, rng),
                                      E * rng.range(0.075f, 0.145f),
                                      span * (0.22f + 0.30f * ridge) * rng.range(0.7f, 1.3f),
                                      rng.range(1.15f, 1.6f), 0.0f, 1.0f, 3));
        }
        const int glens = 1 + static_cast<int>(std::lround(valley * 2.0f));
        for (int i = 0; i < glens; ++i) {
            out.push_back(makeFeature("glen-" + std::to_string(i), FeatureKind::Valley,
                                      crossingPath(E, rng.range(0.0f, kTwoPi),
                                                   rng.range(-E * 0.32f, E * 0.32f), 6, E * 0.06f, rng,
                                                   headLevel, mouthLevel),
                                      E * rng.range(0.09f, 0.15f), span * 0.26f * (0.4f + valley),
                                      0.80f, 0.30f, 0.75f, 3));
        }
        break;
    }
    case TerrainStyle::Plateau: {
        // One escarpment, so the tableland has an edge to be seen from. The mesas themselves are
        // level-dependent and are stamped in the next pass.
        out.push_back(makeFeature("escarpment", FeatureKind::Ridge,
                                  crossingPath(E, rng.range(0.0f, kTwoPi), rng.range(-E * 0.24f, E * 0.24f),
                                               5, E * 0.04f, rng),
                                  E * 0.16f, span * 0.22f * (0.4f + ridge), 1.4f, 0.0f, 0.9f, 3));
        if (valley > 0.25f) {
            out.push_back(makeFeature("gorge", FeatureKind::Valley,
                                      crossingPath(E, rng.range(0.0f, kTwoPi),
                                                   rng.range(-E * 0.26f, E * 0.26f), 6, E * 0.05f, rng,
                                                   headLevel, mouthLevel),
                                      E * 0.075f, span * 0.40f * valley, 1.3f, 0.35f, 0.6f, 3));
        }
        break;
    }
    }
    return out;
}

// The features that have to be measured against the ground: a basin floor levelled at the height the
// bowl actually reached, a mesa whose top is a stated elevation. Stamped after `landformFeatures`
// and after a `prepare()`, which is why they are a separate pass rather than a separate `switch`.
std::vector<Feature> terraceFeatures(const TerrainParams& p, const WorldMap& land, float span, Rng& rng) {
    const float E = std::max(p.extent, 1.0f);
    std::vector<Feature> out;
    switch (p.style) {
    case TerrainStyle::Basin: {
        // Find the bowl again by asking the ground rather than by remembering where it was put:
        // the noise moves the true low point off the geometric centre, and a floor flattened at the
        // wrong height is a shelf halfway up the side.
        glm::vec2 lowest(0.0f);
        float lowestH = std::numeric_limits<float>::max();
        constexpr int kProbe = 33;
        for (int j = 0; j < kProbe; ++j) {
            for (int i = 0; i < kProbe; ++i) {
                const glm::vec2 uv((static_cast<float>(i) + 0.5f) / kProbe,
                                   (static_cast<float>(j) + 0.5f) / kProbe);
                const glm::vec2 q = land.min() + land.size * uv;
                const float h = land.height(q);
                if (h < lowestH) {
                    lowestH = h;
                    lowest = q;
                }
            }
        }
        out.push_back(makeFeature("basin-floor", FeatureKind::Flat,
                                  {glm::vec3(lowest.x, lowestH + span * 0.02f, lowest.y)},
                                  E * rng.range(0.18f, 0.26f), 0.0f, 0.85f, 0.55f, 0.7f, 0));
        break;
    }
    case TerrainStyle::Plateau: {
        const int mesas = 2 + static_cast<int>(std::lround(clamp01(p.ridgeStrength) * 2.0f));
        for (int i = 0; i < mesas; ++i) {
            const glm::vec2 c(rng.range(-E * 0.34f, E * 0.34f), rng.range(-E * 0.34f, E * 0.34f));
            // A stated top, not a measured one: that is what a mesa is. `falloff` above 2 turns the
            // smoothstep shoulder into a lip, which is the difference between a table and a dome.
            const float top = p.elevationMin + span * rng.range(0.52f, 0.94f);
            std::vector<glm::vec3> shape{glm::vec3(c.x, top, c.y)};
            if (rng.nextFloat() > 0.45f) {
                // Some mesas are lobed rather than round. Two nodes at the same level is the whole
                // trick, and it is what stops four circles reading as four circles.
                const float a = rng.range(0.0f, kTwoPi);
                const float reach = E * rng.range(0.05f, 0.12f);
                shape.emplace_back(c.x + std::cos(a) * reach, top, c.y + std::sin(a) * reach);
            }
            out.push_back(makeFeature("mesa-" + std::to_string(i), FeatureKind::Flat, std::move(shape),
                                      E * rng.range(0.11f, 0.21f), 0.0f, rng.range(2.0f, 2.9f),
                                      0.92f, 0.22f, 2));
        }
        break;
    }
    case TerrainStyle::RollingHills:
    case TerrainStyle::Valley:
    case TerrainStyle::Mountainous:
        break;
    }
    return out;
}

// ---- 4. drainage: rivers that run downhill -------------------------------------------------------

// The downhill direction at p. Taken from the surface normal rather than from two height
// differences because `WorldMap::normal` already does exactly that and has already decided what
// epsilon means.
glm::vec2 downhill(const WorldMap& map, glm::vec2 p, float epsilon) {
    const glm::vec3 n = map.normal(p, epsilon);
    const glm::vec2 d(n.x, n.z);
    const float len = glm::length(d);
    return len > 1e-5f ? d / len : glm::vec2(0.0f);
}

struct Course {
    // Why the trace stopped, for AVGEN_TRACE_DEBUG below. A string rather than an enum because its
    // only consumer is a diagnostic, and a reason nobody can read is a reason nobody acts on.
    const char* why = "budget";
    std::vector<glm::vec2> path;
    bool exits = false;        // it left the map, rather than ending in a hollow
    float length = 0.0f;
};

// Distance from p to the nearest node of any already-traced course, and which one.
float nearestCourseDistance(const std::vector<Course>& courses, glm::vec2 p, std::size_t skip,
                            glm::vec2& hit) {
    float best = std::numeric_limits<float>::max();
    for (std::size_t c = 0; c < courses.size(); ++c) {
        if (c == skip) {
            continue;
        }
        for (const glm::vec2& q : courses[c].path) {
            const float d = glm::distance(p, q);
            if (d < best) {
                best = d;
                hit = q;
            }
        }
    }
    return best;
}

// Walk downhill from `start`, meandering, until the map's edge, a hollow, or a course already
// traced. This is the whole of §31's "connected flow": a river's course is not drawn, it is found,
// which is why the channel ends up in the low ground instead of across it.
//
// The step is a search over a fan of directions rather than a step along the gradient. The
// difference is not cosmetic. A gradient step walks into the first bump it meets on a rough
// hillside and stops -- the first cut did exactly that, and every river on the mountainous style
// was a fifty-metre stub ending in a lake perched on a slope. A fan looks at where nine candidate
// steps would actually land and takes the lowest, so a course rounds an obstruction the way water
// does instead of butting into it. The preference for going straight on is what stops the same fan
// from rattling from side to side down the fall line.
Course traceCourse(const WorldMap& land, glm::vec2 start, float step, float boundary,
                   std::uint32_t meanderSeed, float meanderAmount, const std::vector<Course>& joinTo,
                   float joinRadius, float climbTolerance, float selfClearance) {
    constexpr int kFanHalf = 4;            // 9 candidates
    constexpr float kFanSpread = 1.30f;    // radians either side: about 75 degrees
    Course out;
    out.path.push_back(start);
    const glm::vec2 lo = land.min() + boundary;
    const glm::vec2 hi = land.max() - boundary;
    glm::vec2 p = start;
    glm::vec2 dir = downhill(land, p, step * 0.5f);
    if (glm::length(dir) < 1e-5f) {
        const float a = kTwoPi * noise::hashIndex(meanderSeed, 0u, 3u);
        dir = glm::vec2(std::cos(a), std::sin(a));
    }
    float runningMin = land.height(p);
    float arc = 0.0f;
    int stalled = 0;
    int climbing = 0;
    // A course may not be longer than a couple of crossings of the map; a meandering trace that
    // finds a flat can otherwise circle in it until the step budget runs out.
    const int maxSteps = static_cast<int>(std::max(land.size.x, land.size.y) * 2.2f / step);
    for (int i = 0; i < maxSteps; ++i) {
        // A smooth lateral preference along the course, so the channel wanders on a scale of tens of
        // metres. Per-step randomness would give it the jitter of a bad hand-drawn line; this is a
        // low frequency of the arc length, which is a meander.
        const float lateral =
            noise::valueNoise(glm::vec3(arc * 0.011f, 0.0f, 0.0f), meanderSeed) * 2.0f - 1.0f;
        // A course may not run back within `selfClearance` of itself, which is its own channel
        // width and change. Without this the fan will happily circle on nearly level ground -- the
        // first basin it was run on produced a river coiled into a flat spiral with a lake at the
        // centre -- and a polyline that doubles back becomes a barb once it is smoothed, because
        // corner-cutting a hairpin leaves a spike. A step's worth of clearance is not enough: a
        // twelve-metre channel spiralling with six metres between its arms is still one river
        // flowing through itself.
        const auto crosses = [&](glm::vec2 q) {
            const std::size_t n = out.path.size();
            if (n < 10) {
                return false;
            }
            for (std::size_t k = 0; k + 8 < n; ++k) {
                if (glm::distance(out.path[k], q) < selfClearance) {
                    return true;
                }
            }
            return false;
        };
        glm::vec2 bestDir = dir;
        glm::vec2 bestPoint = p + dir * step;
        float bestHeight = std::numeric_limits<float>::max();
        float bestScore = std::numeric_limits<float>::max();
        bool found = false;
        for (int k = -kFanHalf; k <= kFanHalf; ++k) {
            const float theta = kFanSpread * static_cast<float>(k) / static_cast<float>(kFanHalf);
            const float c = std::cos(theta);
            const float sn = std::sin(theta);
            const glm::vec2 d(dir.x * c - dir.y * sn, dir.x * sn + dir.y * c);
            const glm::vec2 q = p + d * step;
            if (crosses(q)) {
                continue;
            }
            const float h = land.height(q);
            // The score is a height in metres plus two penalties in metres: turning at all, and
            // turning against the meander. Both are small next to real relief, so they only decide
            // between candidates the ground does not.
            const float score = h + std::fabs(theta) * step * 0.18f - theta * lateral * meanderAmount * step;
            if (score < bestScore) {
                bestScore = score;
                bestHeight = h;
                bestDir = d;
                bestPoint = q;
                found = true;
            }
        }
        if (!found) {
            out.why = "loop";
            break;
        }
        dir = bestDir;
        p = bestPoint;
        arc += step;
        if (p.x < lo.x || p.x > hi.x || p.y < lo.y || p.y > hi.y) {
            out.exits = true;
            out.why = "exit";
            break;
        }
        glm::vec2 junction(0.0f);
        if (!joinTo.empty() && nearestCourseDistance(joinTo, p, joinTo.size(), junction) < joinRadius) {
            // A tributary ends *on* the trunk, not near it. Without the snap the two channels run
            // parallel a few metres apart and the confluence reads as a fork that never meets.
            out.path.push_back(junction);
            out.exits = true;
            out.why = "join";
            break;
        }
        // Uphill by more than the local relief tolerates, with nine directions tried and none of
        // them better, means the course may be in a hollow -- but one such step means only that it
        // has crossed a ripple, and a course that gives up on the first of those stops at the toe
        // of every slope it descends. It takes six in a row to be a hillside.
        if (bestHeight > runningMin + climbTolerance) {
            if (++climbing > 12) {
                out.why = "climb";
                break;
            }
        } else {
            climbing = 0;
        }
        // Progress means a new low, with a dead band so that the metre of noise on a valley floor
        // does not read as failure to descend.
        if (bestHeight < runningMin - 0.05f) {
            runningMin = bestHeight;
            stalled = 0;
        } else if (++stalled > 64) {
            out.why = "stall";
            break;   // a genuine sink: the course ends here, and a lake goes in it
        }
        out.path.push_back(p);
        out.length = arc;
    }
    return out;
}

// The control points a `Feature` gets: a trace is hundreds of steps and every one of them is paid
// for at every height sample inside the feature's bounds. Sixteen points plus two rounds of Chaikin
// is a curve that follows the trace to within a metre at a twentieth of the cost.
// The indices a `Feature`'s control points are taken from. A trace is hundreds of steps and every
// one of them is paid for at every height sample inside the feature's bounds; sixteen points plus
// three rounds of Chaikin is a curve that follows the trace to within a metre at a twentieth of the
// cost.
//
// Indices rather than points, because the trough a river runs in and the channel cut into it must be
// the *same curve*. Thinning them separately -- the trough from the whole trace, the channel from
// the part of it the level pass kept -- lands their control points a few metres apart, and a
// twelve-metre channel offset a few metres inside its own trough has a bank on one side and none on
// the other. Measured at two and a half channel widths out, only a third of a course had dry ground
// on both sides.
std::vector<std::size_t> thinIndices(std::size_t count, int want) {
    std::vector<std::size_t> out;
    if (count == 0) {
        return out;
    }
    if (static_cast<int>(count) <= want || want < 2) {
        out.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = i;
        }
        return out;
    }
    out.reserve(static_cast<std::size_t>(want));
    for (int i = 0; i < want; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(want - 1);
        const auto idx = static_cast<std::size_t>(std::lround(t * static_cast<float>(count - 1)));
        if (out.empty() || out.back() != idx) {
            out.push_back(idx);
        }
    }
    return out;
}

// ---- 5. standing water --------------------------------------------------------------------------

struct Sink {
    glm::vec2 position{0.0f};
    float level = 0.0f;
    float rise = 0.0f;   // how far the ground climbs away from it, in metres
};

// Points the ground rises away from on every side. A sink found this way will hold water without
// any flood fill, because the rise is measured before anything is placed in it.
std::vector<Sink> findSinks(const WorldMap& map, float radius, float margin) {
    constexpr int kGrid = 56;
    constexpr int kRing = 12;
    std::vector<Sink> out;
    const glm::vec2 lo = map.min() + margin;
    const glm::vec2 span = map.size - margin * 2.0f;
    if (span.x <= 0.0f || span.y <= 0.0f) {
        return out;
    }
    for (int j = 0; j < kGrid; ++j) {
        for (int i = 0; i < kGrid; ++i) {
            const glm::vec2 uv((static_cast<float>(i) + 0.5f) / kGrid,
                               (static_cast<float>(j) + 0.5f) / kGrid);
            const glm::vec2 p = lo + span * uv;
            const float h = map.height(p);
            float minRise = std::numeric_limits<float>::max();
            for (int k = 0; k < kRing; ++k) {
                const float a = kTwoPi * static_cast<float>(k) / static_cast<float>(kRing);
                const glm::vec2 q = p + glm::vec2(std::cos(a), std::sin(a)) * radius;
                minRise = std::min(minRise, map.height(q) - h);
            }
            if (minRise > 0.35f) {
                out.push_back({p, h, minRise});
            }
        }
    }
    // Deepest first, ties by position so the order does not depend on the grid walk. The sort has to
    // be total for §32: a comparator that leaves equal elements in an arbitrary order is a
    // comparator whose output depends on the standard library's sort implementation.
    std::sort(out.begin(), out.end(), [](const Sink& a, const Sink& b) {
        if (a.rise != b.rise) return a.rise > b.rise;
        if (a.position.x != b.position.x) return a.position.x < b.position.x;
        return a.position.y < b.position.y;
    });
    return out;
}

// ---- JSON --------------------------------------------------------------------------------------

Result<float> readFloat(const json& j, const char* key, float fallback) {
    if (!j.contains(key)) {
        return fallback;
    }
    if (!j.at(key).is_number()) {
        return fail("terrain '{}' must be a number", key);
    }
    return j.at(key).get<float>();
}

} // namespace

// ---- the public surface ------------------------------------------------------------------------

const char* terrainStyleName(TerrainStyle style) {
    switch (style) {
    case TerrainStyle::RollingHills: return "rolling_hills";
    case TerrainStyle::Valley: return "valley";
    case TerrainStyle::Basin: return "basin";
    case TerrainStyle::Mountainous: return "mountainous";
    case TerrainStyle::Plateau: return "plateau";
    }
    return "rolling_hills";
}

std::optional<TerrainStyle> terrainStyleFromName(std::string_view name) {
    // Both spellings of the two-word one, because an artist writing JSON by hand will type the
    // hyphen as often as the underscore and a silent fallback to rolling hills is a bug report.
    if (name == "rolling_hills" || name == "rolling-hills" || name == "hills") {
        return TerrainStyle::RollingHills;
    }
    if (name == "valley") return TerrainStyle::Valley;
    if (name == "basin") return TerrainStyle::Basin;
    if (name == "mountainous" || name == "mountains") return TerrainStyle::Mountainous;
    if (name == "plateau") return TerrainStyle::Plateau;
    return std::nullopt;
}

std::vector<TerrainStyle> terrainStyles() {
    return {TerrainStyle::RollingHills, TerrainStyle::Valley, TerrainStyle::Basin,
            TerrainStyle::Mountainous, TerrainStyle::Plateau};
}

Result<void> TerrainParams::validate() const {
    if (!(extent > 0.0f) || extent > 100000.0f) {
        return fail("terrain '{}': extent must be in (0, 100000] metres", name);
    }
    if (!std::isfinite(elevationMin) || !std::isfinite(elevationMax)) {
        return fail("terrain '{}': elevation must be finite", name);
    }
    if (elevationMax < elevationMin) {
        return fail("terrain '{}': elevationMax ({}) is below elevationMin ({})", name, elevationMax,
                    elevationMin);
    }
    if (elevationMax - elevationMin > 20000.0f) {
        return fail("terrain '{}': elevation range must be <= 20000 metres", name);
    }
    const auto unit = [&](const char* label, float v) -> Result<void> {
        if (!(v >= 0.0f) || !(v <= 1.0f)) {
            return fail("terrain '{}': {} must be in [0, 1]", name, label);
        }
        return {};
    };
    if (auto r = unit("roughness", roughness); !r) return r;
    if (auto r = unit("valleyStrength", valleyStrength); !r) return r;
    if (auto r = unit("ridgeStrength", ridgeStrength); !r) return r;
    if (auto r = unit("waterAmount", waterAmount); !r) return r;
    if (auto r = unit("riverFrequency", riverFrequency); !r) return r;
    if (auto r = unit("pondFrequency", pondFrequency); !r) return r;
    return {};
}

std::uint64_t TerrainParams::structuralHash() const {
    StructHash h;
    h.str(name);
    h.u32(static_cast<std::uint32_t>(style));
    h.u32(seed);
    h.f32(extent);
    h.f32(elevationMin);
    h.f32(elevationMax);
    h.f32(roughness);
    h.f32(valleyStrength);
    h.f32(ridgeStrength);
    h.f32(waterAmount);
    h.f32(riverFrequency);
    h.f32(pondFrequency);
    return h.value();
}

json TerrainParams::toJson() const {
    json j;
    j["name"] = name;
    j["style"] = terrainStyleName(style);
    j["seed"] = seed;
    j["extent"] = extent;
    j["elevationMin"] = elevationMin;
    j["elevationMax"] = elevationMax;
    j["roughness"] = roughness;
    j["valleyStrength"] = valleyStrength;
    j["ridgeStrength"] = ridgeStrength;
    j["waterAmount"] = waterAmount;
    j["riverFrequency"] = riverFrequency;
    j["pondFrequency"] = pondFrequency;
    return j;
}

Result<TerrainParams> TerrainParams::fromJson(const json& j, float extentHint) {
    if (!j.is_object()) {
        return fail("terrain block must be a JSON object");
    }
    // The style is read first and the preset for it is the starting point, so `{"style": "basin"}`
    // gives a basin that looks like a basin rather than a basin shaped by rolling-hills defaults.
    TerrainStyle style = TerrainStyle::RollingHills;
    if (j.contains("style")) {
        if (!j.at("style").is_string()) {
            return fail("terrain 'style' must be a string");
        }
        const auto parsed = terrainStyleFromName(j.at("style").get<std::string>());
        if (!parsed) {
            return fail("terrain: unknown style '{}'", j.at("style").get<std::string>());
        }
        style = *parsed;
    }
    // The block's own extent wins over the hint, and is applied before the preset so the preset's
    // elevation range is scaled to the right world.
    float extent = extentHint;
    if (j.contains("extent") && j.at("extent").is_number()) {
        extent = j.at("extent").get<float>();
    }
    TerrainParams p = terrainPreset(style, extent);
    if (j.contains("name")) {
        if (!j.at("name").is_string()) {
            return fail("terrain 'name' must be a string");
        }
        p.name = j.at("name").get<std::string>();
    }
    if (j.contains("seed")) {
        if (!j.at("seed").is_number_unsigned()) {
            return fail("terrain 'seed' must be an unsigned integer");
        }
        p.seed = j.at("seed").get<std::uint32_t>();
    }
    struct Field {
        const char* key;
        float* target;
    };
    const Field fields[] = {
        {"extent", &p.extent},           {"elevationMin", &p.elevationMin},
        {"elevationMax", &p.elevationMax}, {"roughness", &p.roughness},
        {"valleyStrength", &p.valleyStrength}, {"ridgeStrength", &p.ridgeStrength},
        {"waterAmount", &p.waterAmount},  {"riverFrequency", &p.riverFrequency},
        {"pondFrequency", &p.pondFrequency},
    };
    for (const Field& f : fields) {
        auto v = readFloat(j, f.key, *f.target);
        if (!v) {
            return fail("{}", v.error().message);
        }
        *f.target = *v;
    }
    if (auto v = p.validate(); !v) {
        return fail("{}", v.error().message);
    }
    return p;
}

TerrainParams terrainPreset(TerrainStyle style, float extent) {
    TerrainParams p;
    p.style = style;
    p.extent = extent;
    switch (style) {
    case TerrainStyle::RollingHills:
        p.elevationMin = -2.0f;
        p.elevationMax = 34.0f;
        p.roughness = 0.38f;
        p.valleyStrength = 0.45f;
        p.ridgeStrength = 0.40f;
        p.waterAmount = 0.40f;
        p.riverFrequency = 0.35f;
        p.pondFrequency = 0.45f;
        break;
    case TerrainStyle::Valley:
        p.elevationMin = -6.0f;
        p.elevationMax = 82.0f;
        p.roughness = 0.45f;
        p.valleyStrength = 0.80f;
        p.ridgeStrength = 0.65f;
        p.waterAmount = 0.55f;
        p.riverFrequency = 0.55f;
        p.pondFrequency = 0.30f;
        break;
    case TerrainStyle::Basin:
        p.elevationMin = -10.0f;
        p.elevationMax = 76.0f;
        p.roughness = 0.42f;
        p.valleyStrength = 0.75f;
        p.ridgeStrength = 0.60f;
        p.waterAmount = 0.65f;
        p.riverFrequency = 0.45f;
        p.pondFrequency = 0.55f;
        break;
    case TerrainStyle::Mountainous:
        p.elevationMin = 0.0f;
        p.elevationMax = 175.0f;
        p.roughness = 0.68f;
        p.valleyStrength = 0.60f;
        p.ridgeStrength = 0.85f;
        p.waterAmount = 0.35f;
        p.riverFrequency = 0.70f;
        p.pondFrequency = 0.35f;
        break;
    case TerrainStyle::Plateau:
        p.elevationMin = 0.0f;
        p.elevationMax = 96.0f;
        p.roughness = 0.30f;
        p.valleyStrength = 0.35f;
        p.ridgeStrength = 0.55f;
        p.waterAmount = 0.30f;
        p.riverFrequency = 0.40f;
        p.pondFrequency = 0.25f;
        break;
    }
    const float relief = glm::clamp(extent / 400.0f, 0.2f, 5.0f);
    p.elevationMin *= relief;
    p.elevationMax *= relief;
    return p;
}

WorldMap generateTerrain(const TerrainParams& params) {
    TerrainParams p = params;
    // Clamped rather than rejected: this returns a map, and a caller that passed 1.4 for roughness
    // wants the roughest terrain there is, not a default world with no explanation.
    p.extent = glm::clamp(p.extent, 16.0f, 100000.0f);
    p.roughness = clamp01(p.roughness);
    p.valleyStrength = clamp01(p.valleyStrength);
    p.ridgeStrength = clamp01(p.ridgeStrength);
    p.waterAmount = clamp01(p.waterAmount);
    p.riverFrequency = clamp01(p.riverFrequency);
    p.pondFrequency = clamp01(p.pondFrequency);
    if (p.elevationMax < p.elevationMin) {
        std::swap(p.elevationMin, p.elevationMax);
    }

    const float E = p.extent;
    const float span = std::max(p.elevationMax - p.elevationMin, 0.5f);

    WorldMap map;
    map.name = p.name;
    map.seed = p.seed;
    map.size = glm::vec2(E, E);
    const Shape shape = shapeFor(p);
    map.layers = shape.layers;
    map.erosion = shape.erosion;

    // 2. fit the bare octave stack, so the landform below can be written in metres
    map.baseHeight = 0.0f;
    fitToRange(map, p.elevationMin, p.elevationMax);

    // 3. landform, then fit again. The second fit is what keeps a style that stacks five ridge lines
    // from tripling the elevation range the artist asked for.
    Rng landRng(p.seed, kStreamLandform);
    for (Feature& f : landformFeatures(p, span, landRng)) {
        map.features.push_back(std::move(f));
    }
    fitToRange(map, p.elevationMin, p.elevationMax);
    for (Feature& f : terraceFeatures(p, map, span, landRng)) {
        map.features.push_back(std::move(f));
    }
    map.prepare();

    // 4. drainage. No Rng: a course's shape comes from the ground it runs over and a meander taken
    // from a low frequency of its own arc length, which is smooth where a per-step draw would be
    // jittery. The stream number below is left reserved so that a later stage that does need one
    // cannot shift the streams either side of it.
    const bool wet = p.waterAmount > 0.02f;
    const int riverCount = (wet && p.riverFrequency > 0.02f)
                               ? 1 + static_cast<int>(std::lround(p.riverFrequency * 3.0f))
                               : 0;
    std::vector<Course> courses;
    std::vector<float> courseWidth;
    std::vector<float> courseDepth;
    if (riverCount > 0) {
        const float step = std::max(E * 0.010f, 1.5f);
        const float margin = E * 0.012f;
        // Headwaters: the highest well-separated points on the landform. Starting a river anywhere
        // else is what produces a watercourse that begins halfway down a hillside for no reason.
        struct Candidate {
            glm::vec2 p;
            float h;
        };
        std::vector<Candidate> candidates;
        constexpr int kGrid = 30;
        // Headwaters are kept well inside the map. A spring five metres from the edge produces a
        // river that leaves after twenty metres, and because the trunk is whichever course runs
        // longest, one such head at the top of the height ranking was enough to make the whole
        // world's drainage a pair of stubs in a corner.
        const float headMargin = E * 0.17f;
        for (int j = 0; j < kGrid; ++j) {
            for (int i = 0; i < kGrid; ++i) {
                const glm::vec2 uv((static_cast<float>(i) + 0.5f) / kGrid,
                               (static_cast<float>(j) + 0.5f) / kGrid);
                const glm::vec2 q = map.min() + headMargin + (map.size - headMargin * 2.0f) * uv;
                candidates.push_back({q, map.height(q)});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.h != b.h) return a.h > b.h;
            if (a.p.x != b.p.x) return a.p.x < b.p.x;
            return a.p.y < b.p.y;
        });
        std::vector<glm::vec2> heads;
        // Three times as many springs as rivers are wanted, because which of them yields a long
        // course is a fact about the terrain and cannot be read off the height ranking: the highest
        // point on a map is as often a knoll above a short drop as it is the head of a valley.
        const int pool = std::max(6, riverCount * 3);
        const float separation = E * 0.16f;
        for (const Candidate& c : candidates) {
            if (static_cast<int>(heads.size()) >= pool) {
                break;
            }
            bool clear = true;
            for (const glm::vec2& h : heads) {
                if (glm::distance(h, c.p) < separation) {
                    clear = false;
                    break;
                }
            }
            if (clear) {
                heads.push_back(c.p);
            }
        }

        // Enough to beat the preference for going straight on. It has to be: on ground smooth
        // enough that no candidate step is meaningfully lower than any other -- which is most of a
        // rolling-hills world -- the height term decides nothing and the straightness term decides
        // everything, and the result is a river drawn with a ruler.
        const float meander = 0.28f + p.roughness * 0.16f;
        // How far a course may climb before it is considered to have run into the far side of a
        // hollow. Relief-relative, because the same two metres is a wall on a heath and nothing at
        // all on a mountainside.
        const float climbTolerance = std::max(4.0f, span * 0.06f);
        const float trunkWidth = std::max(E * (0.012f + p.waterAmount * 0.026f), 3.0f);
        const float selfClearance = std::max(trunkWidth * 2.5f, step * 2.0f);
        // Probe every spring, then trace the best of them again in length order so the trunk is the
        // longest course rather than the highest spring, and the rest join it.
        std::vector<std::pair<float, std::size_t>> byLength;
        for (std::size_t i = 0; i < heads.size(); ++i) {
            const Course probe = traceCourse(map, heads[i], step, margin,
                                             p.seed + static_cast<std::uint32_t>(i) * 7717u, meander, {},
                                             0.0f, climbTolerance, selfClearance);
            byLength.emplace_back(probe.length, i);
        }
        std::sort(byLength.begin(), byLength.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        });
        if (static_cast<int>(byLength.size()) > riverCount) {
            byLength.resize(static_cast<std::size_t>(riverCount));
        }

        for (std::size_t rank = 0; rank < byLength.size(); ++rank) {
            const std::size_t head = byLength[rank].second;
            const float width = trunkWidth * std::pow(0.74f, static_cast<float>(rank));
            const Course c = traceCourse(map, heads[head], step, margin,
                                         p.seed + static_cast<std::uint32_t>(head) * 7717u, meander,
                                         courses, std::max(width * 2.6f, step * 2.5f), climbTolerance,
                                         std::max(width * 2.5f, step * 2.0f));
            // A course that went nowhere is not a river. An eighth of the map is the shortest thing
            // that reads as a watercourse rather than as a scar on a hillside, and a generator that
            // emits the scars fills a world with channels that begin and end in the open.
            if (c.length < E * 0.125f) {
                continue;
            }
            // Why a course stopped is the single most useful fact about a generated drainage and
            // the hardest to recover from a picture: "stub on a hillside" has four different causes
            // and they want four different fixes. Behind an environment variable rather than the log
            // because it is a handful of lines per world and only ever wanted deliberately.
            if (std::getenv("AVGEN_TRACE_DEBUG") != nullptr) {
                std::fprintf(stderr, "  trace %zu: %.0f m, %zu steps, ended by %s\n", rank, c.length,
                             c.path.size(), c.why);
            }
            courses.push_back(c);
            courseWidth.push_back(width);
            courseDepth.push_back(1.0f + p.waterAmount * 3.2f);
        }
    }

    // The trough first, the channel second. This is the whole of "the river occupies a depression":
    // a channel cut straight into a hillside is a slot with a water surface in it, and it is what
    // the first pass produced. The trough is what gives it banks and a floodplain to sit in.
    constexpr int kRiverControlPoints = 16;
    constexpr int kRiverSmoothing = 3;
    for (std::size_t i = 0; i < courses.size(); ++i) {
        std::vector<glm::vec3> path;
        for (const std::size_t k : thinIndices(courses[i].path.size(), kRiverControlPoints)) {
            path.emplace_back(courses[i].path[k].x, 0.0f, courses[i].path[k].y);
        }
        map.features.push_back(makeFeature("valley-" + std::to_string(i), FeatureKind::Valley, path,
                                           courseWidth[i] * 4.2f, 1.8f + p.waterAmount * 4.6f, 0.72f,
                                           0.0f, 0.70f, kRiverSmoothing));
    }
    if (!courses.empty()) {
        map.prepare();
    }

    // Now the levels, measured on the ground the troughs made and forced to descend.
    //
    // `maxCut` is how deep a cut the level pass will accept before it ends the course. It has to be
    // at least what the trace was willing to climb, or the two disagree and the level pass truncates
    // every course at the first ripple the trace crossed -- which is how a river ends up as a stub
    // at the toe of the slope it came down.
    const float maxCut = std::max(6.0f, span * 0.06f) + 2.0f;
    const float lakeDepth = 0.8f + p.waterAmount * 2.2f;
    int lakes = 0;
    for (std::size_t i = 0; i < courses.size(); ++i) {
        const std::vector<glm::vec2>& full = courses[i].path;
        // How far the water line sits below the floor of its own trough. It is the height of the
        // bank, and it has to be more than the noise left inside the trough or the "bank" is a damp
        // margin the same height as the water: measured at two and a half channel widths out, only
        // a third of the course had dry ground on both sides before this was raised.
        const float freeboard = 0.45f + p.waterAmount * 1.05f;
        const float minGrade = 0.004f;   // metres of fall per metre of run: enough that it never ponds
        // The ground the water line has to stay under is not the ground *on* the centreline: a water
        // surface is flat across its width and stops where the ground rises through it, so a level
        // taken from the centreline alone renders as a wall of water standing over whichever bank is
        // lower. Measured on the first generated valley: at the middle of one course the ground three
        // half-widths to either side was 2.9 m and 3.5 m *below* the water line. So the level is
        // taken from the lowest ground across the channel, not from the line down the middle of it.
        const float reach = courseWidth[i] * 2.0f;
        const auto channelFloor = [&](std::size_t k) {
            const glm::vec2 here = full[k];
            const glm::vec2 ahead = full[std::min(k + 1, full.size() - 1)];
            const glm::vec2 behind = full[k > 0 ? k - 1 : 0];
            const glm::vec2 along = ahead - behind;
            const float len = glm::length(along);
            const glm::vec2 across = len > 1e-5f ? glm::vec2(-along.y, along.x) / len : glm::vec2(1.0f, 0.0f);
            float lowest = map.height(here);
            for (const float m : {-2.0f, -1.5f, -1.0f, -0.5f, 0.5f, 1.0f, 1.5f, 2.0f}) {
                lowest = std::min(lowest, map.height(here + across * (reach * m * 0.5f)));
            }
            return lowest;
        };
        std::vector<glm::vec3> nodes;
        nodes.reserve(full.size());
        float level = channelFloor(0) - freeboard;
        nodes.emplace_back(full.front().x, level, full.front().y);
        for (std::size_t k = 1; k < full.size(); ++k) {
            const float run = glm::distance(full[k - 1], full[k]);
            const float ground = channelFloor(k) - freeboard;
            const float next = std::min(level - run * minGrade, ground);
            // A course that would have to cut more than this to keep descending has run into rising
            // ground; ending it there leaves a river that stops at a lake instead of a river in a
            // trench. A couple of channel depths is a water gap you can believe; more is a canal.
            if (ground - next > maxCut) {
                break;
            }
            level = next;
            nodes.emplace_back(full[k].x, level, full[k].y);
        }
        if (nodes.size() < 6) {
            continue;
        }

        // The mouth. A course that left the map needs nothing; one that stopped inside it needs a
        // lake, or it is a river that simply ends in the open -- the clearest possible sign that
        // nothing understood the terrain.
        //
        // The lake is sized to the hollow rather than to the river: the widest ring around the mouth
        // whose ground still stands above the water it would hold. A fixed radius either floats a
        // disc of water on an open hillside (too big) or leaves a river ending in a puddle in the
        // middle of a basin floor (too small), and both had to be seen before this was written.
        const glm::vec3 mouth = nodes.back();
        float lakeRadius = 0.0f;
        if (!courses[i].exits) {
            const glm::vec2 at(mouth.x, mouth.z);
            constexpr int kRing = 16;
            for (const float multiple : {5.0f, 3.6f, 2.6f, 1.8f, 1.2f}) {
                const float r = courseWidth[i] * multiple;
                float rise = std::numeric_limits<float>::max();
                for (int k = 0; k < kRing; ++k) {
                    const float a = kTwoPi * static_cast<float>(k) / static_cast<float>(kRing);
                    rise = std::min(rise, map.height(at + glm::vec2(std::cos(a), std::sin(a)) * r) - mouth.y);
                }
                if (rise >= lakeDepth * 0.6f) {
                    lakeRadius = r;
                    break;
                }
            }
        }
        if (lakeRadius > 0.0f) {
            // The channel stops at the shore. Leaving it running to the middle of its own lake puts
            // a trench under the water and, because a course that ends in a basin tends to curl as
            // it slows, leaves the curl visible through it.
            const glm::vec2 at(mouth.x, mouth.z);
            const auto insideLake = [&] {
                return glm::distance(glm::vec2(nodes.back().x, nodes.back().z), at) < lakeRadius * 0.8f;
            };
            while (nodes.size() > 5 && insideLake()) {
                nodes.pop_back();
            }
        }

        // Exactly the control points the trough was built from, so the two curves coincide.
        std::vector<glm::vec3> path;
        for (const std::size_t k : thinIndices(full.size(), kRiverControlPoints)) {
            if (k < nodes.size()) {
                path.push_back(nodes[k]);
            }
        }
        if (path.size() < 4) {
            continue;
        }
        Feature river = makeFeature("river-" + std::to_string(i), FeatureKind::River, path,
                                    courseWidth[i], courseDepth[i], 0.85f, 0.55f, 0.25f,
                                    kRiverSmoothing);
        river.water = true;
        river.waterDepth = 0.0f;   // a River's path level *is* its water surface
        map.features.push_back(std::move(river));
        if (lakeRadius > 0.0f) {
            Feature lake = makeFeature("lake-" + std::to_string(lakes), FeatureKind::Flat,
                                       {glm::vec3(mouth.x, mouth.y - lakeDepth * 0.5f, mouth.z)},
                                       lakeRadius, 0.0f, 0.9f, 0.92f, 0.15f, 0);
            lake.water = true;
            lake.waterDepth = lakeDepth;
            map.features.push_back(std::move(lake));
            ++lakes;
        }
    }
    map.prepare();

    // 5. standing water
    Rng pondRng(p.seed, kStreamStanding);
    const int pondCount = (wet && p.pondFrequency > 0.02f)
                              ? 1 + static_cast<int>(std::lround(p.pondFrequency * 4.0f))
                              : 0;
    if (pondCount > 0) {
        const float radius = std::max(E * (0.016f + p.waterAmount * 0.030f), 4.0f);
        const std::vector<Sink> sinks = findSinks(map, radius * 1.5f, E * 0.05f);
        int placed = 0;
        std::vector<glm::vec2> taken;
        for (const Sink& s : sinks) {
            if (placed >= pondCount) {
                break;
            }
            const float depth = 0.5f + p.waterAmount * 1.9f;
            // The rim has to stand above the water the pond would hold, or it is not a pond, it is
            // a puddle draining over a lip. This is the test `findSinks` measures `rise` for.
            if (s.rise < depth * 1.25f) {
                continue;
            }
            if (std::isfinite(map.waterSurface(s.position))) {
                continue;   // already wet: a river runs through here
            }
            bool clear = true;
            for (const glm::vec2& q : taken) {
                if (glm::distance(q, s.position) < radius * 3.0f) {
                    clear = false;
                    break;
                }
            }
            if (!clear) {
                continue;
            }
            taken.push_back(s.position);
            const float r = radius * pondRng.range(0.75f, 1.35f);
            Feature pond = makeFeature("pond-" + std::to_string(placed), FeatureKind::Flat,
                                       {glm::vec3(s.position.x, s.level - depth * 0.5f, s.position.y)},
                                       r, 0.0f, 0.9f, 0.90f, 0.18f, 0);
            pond.water = true;
            pond.waterDepth = depth;
            map.features.push_back(std::move(pond));
            ++placed;
        }
    }

    // Moisture: the third biome axis, and the one that makes §31's "vegetation responds to water
    // proximity" true rather than aspirational. It only means anything now that there is water for
    // it to be measured from -- before this every generated world's moisture was the lowland term
    // and nothing else.
    map.moistureReach = glm::clamp(E * 0.11f, 30.0f, 120.0f);
    // Before there was any water in a generated world this term was the *only* source of moisture,
    // so it had to be cranked to 0.78 to make a marsh biome exist at all -- which made the whole
    // low half of every map read as marsh whether or not there was water within four hundred
    // metres. Now that rivers and ponds supply the wet end properly, this goes back to meaning what
    // it says: low ground is a little damp, and the water's edge is wet.
    map.lowlandMoisture = 0.22f + p.waterAmount * 0.30f;
    map.prepare();
    return map;
}

} // namespace avgen::world
