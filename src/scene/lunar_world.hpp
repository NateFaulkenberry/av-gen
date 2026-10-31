#pragma once

// ADR-1882: the Moon -- a scene-level generator, like `attractorWorld`, for the dimensional portals' lunar leg.
//
// A height field built on the CPU, deterministically from the seed:
//   * rolling highland relief (fbm), then a crater population drawn from the small-crater size-frequency law
//     (cumulative N(>D) ~ D^-2), oldest first, each a bowl and rim calibrated on LROC NAC DTM data of Linne crater
//     (depth 0.21 D, rim 0.037 D, a bowl profile (1 - r^2)^1.9, ejecta falling as r^-4.5) and degraded with age
//     (shallower, softer, no bright ejecta);
//   * named hero craters overprinted by the small population (the portal's crater at Gate C);
//   * the Moon's curvature (y -= d^2 / 2R): the horizon is close;
//   * an albedo field in uv.x: dark regolith (about 0.12), bright ejecta and walls around fresh craters;
//   * a fine tile (`extent` square at `spacing`) and a coarse skirt (`skirtExtent` at `skirtSpacing`) sunk under it,
//     so no edge of the world is ever in view;
//   * boulders: rounded, irregular subdivided icospheres (lobes, split faces, lumps; smooth normals), a power law of
//     sizes (many small, few large), denser on fresh rims and ejecta, partly buried, the regolith's grey, merged per
//     chunk (the near ground's scale).
// The surface is drawn by the scene pass with the scene's material program (`program`, a lunar-Lambert regolith).
//
// File block: "lunarWorld": {"seed": 3, "extent": 4000, "spacing": 2.5, "skirtExtent": 32000, "skirtSpacing": 32,
//   "craters": 3.0, "rocks": 0.004, "program": "regolith",
//   "heroes": [{"x": 0, "z": -700, "diameter": 950, "fresh": 0.95}]}

#include "core/error.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <string>
#include <vector>

namespace avgen::scene {

struct LunarCrater {
    float x = 0.0f, z = 0.0f, diameter = 100.0f, fresh = 0.5f;
};

struct LunarGen {
    std::uint32_t seed = 3;
    float extent = 4000.0f;      // fine tile side (m)
    float spacing = 2.5f;        // fine tile grid (m)
    float skirtExtent = 32000.0f;
    float skirtSpacing = 32.0f;
    float craters = 3.0f;        // N(>100 m) per km^2
    float rocks = 0.004f;        // boulders per m^2 at full ejecta (0 = none)
    float relief = 60.0f;        // the rolling relief's amplitude (m)
    float curvature = 1737.4e3f; // the radius the ground falls away with (m): the Moon's own, or smaller to bring the
                                 // terminator (where a low sun meets the curve) within flying reach (Art Pass 2)
    std::vector<LunarCrater> heroes;
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t hash() const;
};

struct LunarChunk {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs; // x = albedo, y = 0 surface / 1 rock
    std::vector<std::uint32_t> indices;
};

struct LunarWorld {
    LunarGen gen;
    std::uint64_t hash = 0;
    int n = 0;                       // fine grid samples per side
    std::vector<float> height;       // n*n, z rows, x columns, curvature included
    std::vector<LunarChunk> chunks;  // fine tile, skirt, and rocks
    std::size_t triangles = 0;
    std::size_t rockCount = 0;
    double generateSeconds = 0.0;
    // The ground's height at (x, z) (bilinear on the fine tile; the skirt's function beyond it).
    [[nodiscard]] float sample(float x, float z) const;
};

[[nodiscard]] std::shared_ptr<const LunarWorld> generateLunarWorld(const LunarGen& gen);

struct LunarWorldBlock {
    bool enabled = false;
    LunarGen gen;
    std::string program = "regolith";
    std::shared_ptr<const LunarWorld> world;
};

[[nodiscard]] Result<LunarWorldBlock> lunarWorldFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json lunarWorldToJson(const LunarWorldBlock& b);

} // namespace avgen::scene
