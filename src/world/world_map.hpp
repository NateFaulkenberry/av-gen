#pragma once

// The world map (ADR-046): the single authored description of a place, from which terrain, biomes
// and ecology are all derived. It is data, sampled as a pure function of (x, z) and a seed --
// nothing here knows about meshes, chunks, the GPU or the camera.
//
// Why it is not just noise. Undirected fBm makes *terrain*; it does not make *geography*. A
// landscape reads as a place when it has features you could name and navigate by: this ridge, that
// valley, the river that runs between them, the clearing where the light lands. So a world map is
// a noise base plus an ordered list of authored `Feature` stamps -- ridges, valleys, rivers and
// flats drawn as polylines in world space. An artist moves a river by moving three numbers.
//
// Evaluation order at a point p (the order matters, and each stage exists for a reason):
//   1. feature weights   w_i = falloff(1 - d_i / width_i), d_i = distance from p to feature i
//   2. base noise        n = octaves(p), damped by the smallest `roughness` any feature asks for,
//                        so a river bed is smooth and a clearing is level without a special case
//   3. raise / lower     h = baseHeight + n + sum(+-amplitude_i * w_i)
//   4. flatten           h = mix(h, level_i, w_i * flatten_i), levels interpolated along the path,
//                        so a river descends along its course instead of being a canal
//
// A feature's `path` carries its level in y: (x, level, z). One point is a radial feature (a
// mound, a crater, a clearing); two or more is a corridor (a ridge line, a valley floor, a river).

#include "core/error.hpp"
#include "world/biome.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace avgen::world {

// One octave of the terrain base. `ridged` morphs the octave continuously from ordinary fBm
// (0: rolling hills) to ridged multifractal (1: crests and gullies), because the two read as
// completely different rock and every real landscape is somewhere between them.
struct NoiseLayer {
    float frequency = 0.01f;   // cycles per metre
    float amplitude = 20.0f;   // metres, peak to trough
    float ridged = 0.0f;       // 0 = fBm, 1 = ridged
    float warp = 0.0f;         // metres of domain warp applied to this octave's lookup
};

enum class FeatureKind : std::uint8_t {
    Ridge,   // raise by amplitude * weight
    Valley,  // lower by amplitude * weight
    River,   // lower, and flatten the bed toward the path's own descending level
    Flat,    // blend toward the path's level: a plateau, a lake bed, a clearing, a terrace
};
[[nodiscard]] const char* featureKindName(FeatureKind kind);
[[nodiscard]] std::optional<FeatureKind> featureKindFromName(std::string_view name);

struct Feature {
    std::string name;
    FeatureKind kind = FeatureKind::Ridge;
    // (x, level, z) in world metres. The level is what `flatten` pulls toward, interpolated along
    // the polyline, so a river authored as a descending sequence of points carves a descending bed.
    std::vector<glm::vec3> path;
    float width = 24.0f;       // metres from the path at which the influence reaches zero
    float amplitude = 12.0f;   // metres raised (Ridge) or cut (Valley/River)
    float falloff = 1.0f;      // shoulder shape: < 1 broad and soft, > 1 a sharp lip
    float flatten = 0.0f;      // 0..1 blend toward the path level (River and Flat mainly)
    float roughness = 1.0f;    // multiplies the base noise inside the feature; 0 = glassy
    bool water = false;        // the feature holds water up to `waterDepth` above its bed
    float waterDepth = 0.0f;   // metres of water above the path level
    // Corner-cutting iterations applied to the path before it is sampled. A polyline river has
    // visible straight reaches and mitred bends; three iterations of Chaikin turn the same authored
    // points into a curve without moving them far or ever overshooting them, which matters because
    // an overshooting river climbs. 0 keeps the polyline exactly.
    int smoothing = 3;
    // Runtime: `path` after `smoothing`, filled by WorldMap::prepare(). Never serialised, and part
    // of no hash except through `path` and `smoothing` themselves.
    std::vector<glm::vec3> curve;
    // Runtime: the XZ box outside which this feature's weight is exactly zero -- the path's bounds
    // grown by `width`. Sampling a height means asking every feature how far away it is, and a
    // smoothed river is a hundred segments; without this the cost of a world is the product of
    // every sample and every segment of every feature, most of which are nowhere near the point.
    glm::vec2 boundsMin{0.0f};
    glm::vec2 boundsMax{0.0f};
    [[nodiscard]] bool reaches(glm::vec2 p) const {
        return p.x >= boundsMin.x && p.x <= boundsMax.x && p.y >= boundsMin.y && p.y <= boundsMax.y;
    }

    [[nodiscard]] Result<void> validate() const;
    // The points actually sampled: `curve` when prepared, `path` otherwise.
    [[nodiscard]] const std::vector<glm::vec3>& samplePath() const { return curve.empty() ? path : curve; }
};

// An optional painted heightmap. Greyscale 0..1 over the whole world extent, blended with the
// procedural base by `imageBlend`. This is the escape hatch that makes the world format an artist
// tool: repaint the PNG, get a different continent, touch no C++. Loaded by the caller (the
// Composition, through the AssetRegistry) so this header stays free of image decoding.
struct HeightImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> samples;      // row-major, top-left, 0..1
    [[nodiscard]] float bilinear(glm::vec2 uv) const; // uv 0..1, clamped at the border
};

// What a point of the world is: everything a terrain mesh, a biome lookup or a scatter pass needs
// from one sample, computed together because they share the same four height evaluations.
struct Sample {
    float height = 0.0f;      // metres
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float slope = 0.0f;       // 0 flat .. 1 vertical (1 - normal.y)
    float waterSurface = 0.0f;// metres; the surface above this point, or -inf where there is none
    bool submerged = false;   // height < waterSurface
    float moisture = 0.0f;    // 0 dry .. 1 at the water's edge
    float altitude = 0.0f;    // height as 0..1 over the map's measured range
};

struct WorldMap {
    std::string name = "world";
    std::uint32_t seed = 1;
    glm::vec2 size{512.0f, 512.0f};  // metres, centred on the origin
    float baseHeight = 0.0f;         // metres added to every sample
    // Multifractal weighting, 0..1. At 0 the octaves are summed, which gives rolling dunes: every
    // hillside carries the same amount of every frequency, and the result reads as smooth at any
    // distance. At 1 each octave's amplitude is scaled by how high the octaves above it already
    // are, so detail collects on crests and drains out of hollows -- the signature of eroded ground,
    // where ridges are sharp and valley floors are smooth, and the single most effective thing that
    // can be done to a height function without simulating erosion.
    float erosion = 0.0f;
    float seaLevel = -1000.0f;       // global water plane; below the world by default (no sea)
    std::vector<NoiseLayer> layers;
    std::vector<Feature> features;
    BiomeSet biomes;                 // ADR-047; empty means the world has no biomes yet
    // How far from open water the ground stays damp. Moisture is the third axis a biome is defined
    // on and the only one that is not already a property of a single point, so it is defined here,
    // once, rather than being re-derived by everything that needs it.
    float moistureReach = 90.0f;     // metres
    float lowlandMoisture = 0.35f;   // how wet the bottom of the map is before any water is near
    std::string heightImage;         // path as written; resolved by the caller
    float imageHeight = 60.0f;       // metres the image's 0..1 spans
    float imageBlend = 1.0f;         // 0 = ignore the image, 1 = the image replaces the base noise
    std::shared_ptr<const HeightImage> image; // runtime; never serialised
    // Runtime, from prepare(): the height range over a coarse sample of the whole map. Terrain uses
    // it to normalise altitude into a 0..1 the material can blend against, which is why it lives
    // here rather than being recomputed per chunk -- every chunk must agree on where "high" is.
    float sampledMinHeight = 0.0f;
    float sampledMaxHeight = 1.0f;

    [[nodiscard]] float height(glm::vec2 p) const;
    // Central differences at +-`epsilon` metres. `epsilon` should be about half the spacing of the
    // mesh being built: sampling finer than the mesh produces normals the silhouette contradicts.
    [[nodiscard]] glm::vec3 normal(glm::vec2 p, float epsilon = 0.5f) const;
    [[nodiscard]] Sample sample(glm::vec2 p, float epsilon = 0.5f) const;
    // The water surface above p (sea level or the nearest water feature), or -infinity if dry.
    [[nodiscard]] float waterSurface(glm::vec2 p) const;
    // 0 dry .. 1 at the water's edge: the larger of a falloff from the nearest water feature and a
    // term that makes the bottom of the map damper than the top.
    [[nodiscard]] float moisture(glm::vec2 p, float altitude01) const;
    [[nodiscard]] glm::vec2 min() const { return -size * 0.5f; }
    [[nodiscard]] glm::vec2 max() const { return size * 0.5f; }

    [[nodiscard]] Result<void> validate() const;
    // Expands every feature's smoothed curve and measures the map's height range. Idempotent, and
    // required before sampling: the parsers and defaultWorld() call it, so a map built by hand is
    // the only one that has to.
    void prepare();
    // Altitude as 0..1 over the measured range, which is what a terrain material blends against.
    [[nodiscard]] float altitude01(float h) const {
        return glm::clamp((h - sampledMinHeight) / std::max(sampledMaxHeight - sampledMinHeight, 1e-3f), 0.0f, 1.0f);
    }
    // Changes whenever a sample of the map could change, so a terrain rebuilds only then.
    [[nodiscard]] std::uint64_t structuralHash() const;
};

// A default world is shipped rather than left to every scene, because "make a landscape" is a
// design job and a blank noise field is the flat plane by another name.
[[nodiscard]] WorldMap defaultWorld();

[[nodiscard]] Result<WorldMap> worldMapFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json worldMapToJson(const WorldMap& map);

} // namespace avgen::world
