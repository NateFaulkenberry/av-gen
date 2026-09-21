// TEMPORARY. Finds a patch of a scene's terrain with a wanted amount of relief across a
// character's own footprint, so a lab can be placed on measured ground rather than on a guess.
// Written to answer "why does the alien foot lab never clamp", and the answer turned out to be
// architectural (one ground plane per body) rather than a flat fixture -- see the design doc.
// Delete it when per-foot ground sampling lands and labs stop needing hand-placed slopes.
//
//   avgen_slope_probe <scene.json> <x> <z> [wantedRelief]


#include "assets/asset_registry.hpp"
#include "scene/composition.hpp"
#include "world/terrain_query.hpp"
#include <fmt/format.h>
#include <filesystem>
#include <cmath>
int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const fs::path file = argv[1];
    avgen::assets::AssetRegistry registry(file.parent_path());
    auto loaded = avgen::scene::Composition::loadFile(file, registry);
    if (!loaded) { fmt::print("load failed: {}\n", loaded.error().message); return 1; }
    avgen::params::ParameterSet params; avgen::params::Modulator mod;
    (*loaded)->attach(params, mod);
    avgen::FrameTime t; (*loaded)->update(t);
    const avgen::world::TerrainQuery terrain = (*loaded)->terrainQuery();
    const float cx = std::stof(argv[2]), cz = std::stof(argv[3]);
    // The worst height difference across a 0.7 m footprint, scanned over a grid.
    const float want = argc > 4 ? std::stof(argv[4]) : 0.30f;
    float bestErr = 1e9f; float worst = 0.0f; float bx = cx, bz = cz;
    for (float x = cx - 60.0f; x <= cx + 60.0f; x += 2.0f) {
        for (float z = cz - 60.0f; z <= cz + 60.0f; z += 2.0f) {
            const float r = 0.45f;
            float lo = 1e9f, hi = -1e9f;
            for (int i = 0; i < 4; ++i) {
                const float a = static_cast<float>(i) * 1.5708f;
                const auto p = terrain.at(glm::vec2(x + r * std::cos(a), z + r * std::sin(a)));
                lo = std::min(lo, p.height); hi = std::max(hi, p.height);
            }
            const auto c = terrain.at(glm::vec2(x, z));
            if (c.reject != avgen::world::TerrainReject::None) continue;
            const float relief = hi - lo;
            const float err = std::fabs(relief - want);
            if (err < bestErr) { bestErr = err; worst = relief; bx = x; bz = z; }
        }
    }
    fmt::print("relief nearest {:.2f} m: {:.4f} m at ({:.2f},{:.2f}) height {:.3f}\n",
               want, worst, bx, bz, terrain.at(glm::vec2(bx,bz)).height);
    return 0;
}
