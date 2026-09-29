// avgen_water_probe: where the drawn water and the drawn ground disagree with the world they were
// built from (the GV3 art pass, ADR-980).
//
//   avgen_water_probe <scene.json> <out-prefix> [--region x0 z0 x1 z1] [--step metres] [--lods a,b,...]
//
// A water surface has one silhouette -- where it meets the bank -- and nothing in the engine measured
// it. `avgen_world_preview --seams` finds a step inside the ground or inside the water surface; it
// cannot see a sheet standing a metre proud of its bank, or a coarse ground poking up through a
// river, because neither is a step in either function. Both are about the two MESHES, and which of
// them is in front where.
//
// So this builds, for every chunk the region touches, exactly the meshes a scene load builds -- the
// chunk's water surface (`buildChunkWater`, from the LOD 0 field) and its ground at every level
// (`buildChunkMesh`) -- and compares them on a fine grid, sample by sample, with the world
// (`WorldMap::height`, `WorldMap::waterSurface`). For each ground level it reports:
//
//   * FALSE WATER: the water sheet drawn above the drawn ground where the world is dry. A sheet
//     standing proud of the bank is what a low camera sees as a slab, or on a steep course as a
//     stepped wall: the height it stands above the drawn ground is the height of that wall.
//   * FALSE DRY: the drawn ground above the drawn water where the world holds water: the bank or the
//     bed showing through the river.
//   * SURFACE: how far the water mesh departs from `waterSurface` where both call it wet.
//   * STEEP: how much of the drawn water surface is steeper than 30 and 40 degrees.
//
// and writes a top-down map per level: ground shaded, true water blue, false water red (brighter the
// taller it stands), false dry yellow. It is CPU only: everything it reads is the core library's.

#include "assets/image.hpp"
#include "world/terrain.hpp"
#include "world/world_map.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;

namespace {

// One chunk's grids, as the meshes carry them: the first (n + 1)^2 vertices of a ground mesh are its
// grid (the skirt follows), and a water mesh keeps the LOD 0 grid whole with only the wet quads indexed.
struct ChunkGrids {
    glm::vec2 origin{0.0f};
    int res0 = 0;
    std::vector<float> water;     // (res0 + 1)^2 heights; meaningless where no quad is drawn
    std::vector<char> waterQuad;  // res0^2: is this quad drawn
    bool anyWater = false;
    std::vector<int> groundRes;   // per level
    std::vector<std::vector<float>> ground; // per level: (res + 1)^2 heights
};

// The height of a regular grid mesh at a point, through the same two triangles per quad the engine
// indexes: (a, c, b) and (b, c, d), a = (i, j), b = (i + 1, j), c = (i, j + 1), d = (i + 1, j + 1).
float gridHeight(const std::vector<float>& h, int n, float cell, glm::vec2 local, int* quad = nullptr) {
    const float fx = local.x / cell;
    const float fz = local.y / cell;
    const int i = std::clamp(static_cast<int>(std::floor(fx)), 0, n - 1);
    const int j = std::clamp(static_cast<int>(std::floor(fz)), 0, n - 1);
    const float u = fx - static_cast<float>(i);
    const float v = fz - static_cast<float>(j);
    const int side = n + 1;
    const float a = h[static_cast<std::size_t>(j) * side + i];
    const float b = h[static_cast<std::size_t>(j) * side + i + 1];
    const float c = h[static_cast<std::size_t>(j + 1) * side + i];
    const float d = h[static_cast<std::size_t>(j + 1) * side + i + 1];
    if (quad != nullptr) {
        *quad = j * n + i;
    }
    if (u + v <= 1.0f) {
        return a + (u * (b - a)) + (v * (c - a));
    }
    return d + ((1.0f - u) * (c - d)) + ((1.0f - v) * (b - d));
}

float percentile(std::vector<float> v, float p) {
    if (v.empty()) {
        return 0.0f;
    }
    std::sort(v.begin(), v.end());
    const auto k = static_cast<std::size_t>(std::clamp(p, 0.0f, 1.0f) * static_cast<float>(v.size() - 1));
    return v[k];
}

struct LevelStats {
    std::size_t trueWet = 0;
    std::size_t drawnWet = 0;
    std::size_t falseWater = 0;
    std::size_t falseDry = 0;
    std::vector<float> proud;     // metres the sheet stands above the drawn ground, on false water
    std::vector<float> dryDepth;  // metres of world water where the drawn ground shows through
    glm::vec2 worstProudAt{0.0f};
    float worstProud = 0.0f;
    glm::vec2 worstDryAt{0.0f};
    float worstDry = 0.0f;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: avgen_water_probe <scene.json> <out-prefix> [--region x0 z0 x1 z1] "
                             "[--step m] [--lods 0,1,2,3]\n");
        return 2;
    }
    const std::string scenePath = argv[1];
    const std::string prefix = argv[2];
    float x0 = -1e9f;
    float z0 = -1e9f;
    float x1 = 1e9f;
    float z1 = 1e9f;
    float step = 0.25f;
    std::vector<int> lods = {0, 1, 2, 3};
    for (int a = 3; a < argc; ++a) {
        const std::string flag = argv[a];
        if (flag == "--region" && a + 4 < argc) {
            x0 = std::strtof(argv[a + 1], nullptr);
            z0 = std::strtof(argv[a + 2], nullptr);
            x1 = std::strtof(argv[a + 3], nullptr);
            z1 = std::strtof(argv[a + 4], nullptr);
            a += 4;
        } else if (flag == "--step" && a + 1 < argc) {
            step = std::max(0.02f, std::strtof(argv[++a], nullptr));
        } else if (flag == "--lods" && a + 1 < argc) {
            lods.clear();
            std::stringstream ss(argv[++a]);
            std::string item;
            while (std::getline(ss, item, ',')) {
                lods.push_back(std::atoi(item.c_str()));
            }
        } else {
            std::fprintf(stderr, "unknown argument '%s'\n", flag.c_str());
            return 2;
        }
    }

    std::ifstream in(scenePath);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", scenePath.c_str());
        return 2;
    }
    nlohmann::json scene;
    in >> scene;
    const nlohmann::json* node = nullptr;
    for (const auto& n : scene.at("nodes")) {
        if (n.value("kind", "") == "terrain" && n.contains("world")) {
            node = &n;
            break;
        }
    }
    if (node == nullptr) {
        std::fprintf(stderr, "%s has no terrain node with a world\n", scenePath.c_str());
        return 2;
    }
    auto parsed = world::worldMapFromJson(node->at("world"));
    if (!parsed) {
        std::fprintf(stderr, "world: %s\n", parsed.error().message.c_str());
        return 2;
    }
    const world::WorldMap map = std::move(*parsed);
    world::TerrainSettings settings;
    const nlohmann::json& t = node->at("terrain");
    settings.chunkSize = t.value("chunkSize", settings.chunkSize);
    settings.resolution = t.value("resolution", settings.resolution);
    settings.lodLevels = t.value("lodLevels", settings.lodLevels);
    settings.skirtDepth = t.value("skirtDepth", settings.skirtDepth);
    settings.water.enabled = t.contains("water") ? t.at("water").value("enabled", true) : false;
    if (!settings.water.enabled) {
        std::fprintf(stderr, "the terrain draws no water\n");
        return 2;
    }

    // The region, clipped to the map.
    x0 = std::max(x0, map.min().x);
    z0 = std::max(z0, map.min().y);
    x1 = std::min(x1, map.max().x);
    z1 = std::min(z1, map.max().y);
    const int nx = std::max(1, static_cast<int>(std::floor((x1 - x0) / step)));
    const int nz = std::max(1, static_cast<int>(std::floor((z1 - z0) / step)));

    // The chunks the region touches, built as the scene builds them.
    const float cs = settings.chunkSize;
    const auto chunkOf = [&](glm::vec2 p) {
        return glm::ivec2(static_cast<int>(std::floor((p.x - map.min().x) / cs)),
                          static_cast<int>(std::floor((p.y - map.min().y) / cs)));
    };
    std::map<std::pair<int, int>, ChunkGrids> chunks;
    const glm::ivec2 c0 = chunkOf(glm::vec2(x0, z0));
    const glm::ivec2 c1 = chunkOf(glm::vec2(x1, z1));
    for (int cz = c0.y; cz <= c1.y; ++cz) {
        for (int cx = c0.x; cx <= c1.x; ++cx) {
            const glm::ivec2 coord(cx, cz);
            ChunkGrids g;
            g.origin = world::chunkOrigin(map, settings, coord);
            g.res0 = settings.resolution;
            const world::ChunkField field = world::sampleChunkField(map, settings, coord);
            const scene::MeshData water = world::buildChunkWater(map, settings, coord, &field, nullptr);
            const int side0 = g.res0 + 1;
            g.water.assign(static_cast<std::size_t>(side0) * side0, 0.0f);
            g.waterQuad.assign(static_cast<std::size_t>(g.res0) * g.res0, 0);
            if (!water.vertices.empty()) {
                g.anyWater = true;
                for (std::size_t k = 0; k < g.water.size() && k < water.vertices.size(); ++k) {
                    g.water[k] = water.vertices[k].position.y;
                }
                for (std::size_t k = 0; k + 5 < water.indices.size(); k += 6) {
                    const std::uint32_t a = water.indices[k];
                    const int i = static_cast<int>(a % static_cast<std::uint32_t>(side0));
                    const int j = static_cast<int>(a / static_cast<std::uint32_t>(side0));
                    if (i < g.res0 && j < g.res0) {
                        g.waterQuad[static_cast<std::size_t>(j) * g.res0 + i] = 1;
                    }
                }
            }
            for (int lod = 0; lod < settings.lodLevels; ++lod) {
                const scene::MeshData ground = world::buildChunkMesh(map, settings, coord, lod);
                const int res = std::max(2, settings.resolution >> lod);
                const int side = res + 1;
                std::vector<float> h(static_cast<std::size_t>(side) * side, 0.0f);
                for (std::size_t k = 0; k < h.size() && k < ground.vertices.size(); ++k) {
                    h[k] = ground.vertices[k].position.y;
                }
                g.groundRes.push_back(res);
                g.ground.push_back(std::move(h));
            }
            chunks.emplace(std::make_pair(cx, cz), std::move(g));
        }
    }

    const float cell0 = cs / static_cast<float>(settings.resolution);
    std::vector<LevelStats> stats(lods.size());
    std::vector<std::vector<std::uint8_t>> images(lods.size(),
                                                  std::vector<std::uint8_t>(static_cast<std::size_t>(nx) * nz * 4, 0));
    std::vector<float> surfaceDeparture; // |drawn - world| where both are wet
    float worstDeparture = 0.0f;
    glm::vec2 worstDepartureAt{0.0f};
    std::size_t steep30 = 0;
    std::size_t steep40 = 0;
    float steepest = 0.0f;
    glm::vec2 steepestAt{0.0f};

    for (int iz = 0; iz < nz; ++iz) {
        for (int ix = 0; ix < nx; ++ix) {
            const glm::vec2 p(x0 + (static_cast<float>(ix) + 0.5f) * step, z0 + (static_cast<float>(iz) + 0.5f) * step);
            const glm::ivec2 cc = chunkOf(p);
            const auto found = chunks.find({cc.x, cc.y});
            if (found == chunks.end()) {
                continue;
            }
            const ChunkGrids& g = found->second;
            const glm::vec2 local = p - g.origin;
            const float groundTrue = map.height(p);
            const float surfaceTrue = map.waterSurface(p);
            const bool worldWet = surfaceTrue > -999.0f && groundTrue < surfaceTrue;
            int quad = -1;
            const float waterY = gridHeight(g.water, g.res0, cell0, local, &quad);
            const bool quadDrawn = g.anyWater && quad >= 0 && g.waterQuad[static_cast<std::size_t>(quad)] != 0;
            if (quadDrawn && worldWet) {
                const float dep = std::fabs(waterY - surfaceTrue);
                surfaceDeparture.push_back(dep);
                if (dep > worstDeparture) {
                    worstDeparture = dep;
                    worstDepartureAt = p;
                }
            }
            if (quadDrawn && (ix % 2 == 0) && (iz % 2 == 0)) {
                // The slope of the drawn surface: a finite difference over half a cell.
                const float e = cell0 * 0.5f;
                const float hx = gridHeight(g.water, g.res0, cell0, local + glm::vec2(e, 0.0f)) - waterY;
                const float hz = gridHeight(g.water, g.res0, cell0, local + glm::vec2(0.0f, e)) - waterY;
                const float slope = std::atan(std::sqrt((hx * hx) + (hz * hz)) / e) * 57.2957795f;
                steep30 += slope > 30.0f ? 1 : 0;
                steep40 += slope > 40.0f ? 1 : 0;
                if (slope > steepest) {
                    steepest = slope;
                    steepestAt = p;
                }
            }
            // A plain hillshade of the world for the map's ground.
            const float hx = map.height(p + glm::vec2(step, 0.0f)) - groundTrue;
            const float hz = map.height(p + glm::vec2(0.0f, step)) - groundTrue;
            const glm::vec3 n = glm::normalize(glm::vec3(-hx, step, -hz));
            const float shade = std::clamp(glm::dot(n, glm::normalize(glm::vec3(-0.5f, 0.8f, -0.4f))), 0.0f, 1.0f);
            for (std::size_t li = 0; li < lods.size(); ++li) {
                const int lod = std::clamp(lods[li], 0, static_cast<int>(g.ground.size()) - 1);
                const int res = g.groundRes[static_cast<std::size_t>(lod)];
                const float groundY = gridHeight(g.ground[static_cast<std::size_t>(lod)], res, cs / static_cast<float>(res), local);
                const bool drawnWet = quadDrawn && waterY > groundY + 0.005f;
                LevelStats& s = stats[li];
                s.trueWet += worldWet ? 1 : 0;
                s.drawnWet += drawnWet ? 1 : 0;
                glm::vec3 colour = glm::vec3(0.16f, 0.18f, 0.15f) * (0.35f + 0.65f * shade);
                if (drawnWet && worldWet) {
                    const float depth = surfaceTrue - groundTrue;
                    colour = glm::mix(glm::vec3(0.25f, 0.55f, 0.85f), glm::vec3(0.05f, 0.15f, 0.45f),
                                      std::clamp(depth / 4.0f, 0.0f, 1.0f));
                } else if (drawnWet) {
                    const float proud = waterY - groundY;
                    ++s.falseWater;
                    s.proud.push_back(proud);
                    if (proud > s.worstProud) {
                        s.worstProud = proud;
                        s.worstProudAt = p;
                    }
                    colour = glm::vec3(0.45f + (0.55f * std::clamp(proud, 0.0f, 1.0f)), 0.05f, 0.05f);
                } else if (worldWet && (surfaceTrue - groundTrue) > 0.15f) {
                    const float depth = surfaceTrue - groundTrue;
                    ++s.falseDry;
                    s.dryDepth.push_back(depth);
                    if (depth > s.worstDry) {
                        s.worstDry = depth;
                        s.worstDryAt = p;
                    }
                    colour = glm::vec3(0.95f, 0.85f, 0.10f);
                }
                std::uint8_t* px = &images[li][(static_cast<std::size_t>(iz) * nx + ix) * 4];
                px[0] = static_cast<std::uint8_t>(std::clamp(colour.r, 0.0f, 1.0f) * 255.0f);
                px[1] = static_cast<std::uint8_t>(std::clamp(colour.g, 0.0f, 1.0f) * 255.0f);
                px[2] = static_cast<std::uint8_t>(std::clamp(colour.b, 0.0f, 1.0f) * 255.0f);
                px[3] = 255;
            }
        }
    }

    const float area = step * step;
    nlohmann::json out;
    out["scene"] = scenePath;
    out["region"] = {x0, z0, x1, z1};
    out["step"] = step;
    out["surface"] = {{"samples", surfaceDeparture.size()},
                      {"p95", percentile(surfaceDeparture, 0.95f)},
                      {"worst", worstDeparture},
                      {"worstAt", {worstDepartureAt.x, worstDepartureAt.y}}};
    out["steep"] = {{"over30degM2", static_cast<float>(steep30) * area * 4.0f},
                    {"over40degM2", static_cast<float>(steep40) * area * 4.0f},
                    {"steepestDeg", steepest},
                    {"steepestAt", {steepestAt.x, steepestAt.y}}};
    nlohmann::json levels = nlohmann::json::array();
    for (std::size_t li = 0; li < lods.size(); ++li) {
        const LevelStats& s = stats[li];
        std::size_t proudOver10 = 0;
        std::size_t proudOver30 = 0;
        for (const float v : s.proud) {
            proudOver10 += v > 0.10f ? 1 : 0;
            proudOver30 += v > 0.30f ? 1 : 0;
        }
        levels.push_back({{"lod", lods[li]},
                          {"worldWetM2", static_cast<float>(s.trueWet) * area},
                          {"drawnWetM2", static_cast<float>(s.drawnWet) * area},
                          {"falseWaterM2", static_cast<float>(s.falseWater) * area},
                          {"falseWaterOver10cmM2", static_cast<float>(proudOver10) * area},
                          {"falseWaterOver30cmM2", static_cast<float>(proudOver30) * area},
                          {"proudP95", percentile(s.proud, 0.95f)},
                          {"proudWorst", s.worstProud},
                          {"proudWorstAt", {s.worstProudAt.x, s.worstProudAt.y}},
                          {"falseDryM2", static_cast<float>(s.falseDry) * area},
                          {"falseDryP95", percentile(s.dryDepth, 0.95f)},
                          {"falseDryWorst", s.worstDry},
                          {"falseDryWorstAt", {s.worstDryAt.x, s.worstDryAt.y}}});
        const std::string png = prefix + "-lod" + std::to_string(lods[li]) + ".png";
        if (!assets::writePng(png, static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(nz), images[li])) {
            std::fprintf(stderr, "could not write %s\n", png.c_str());
            return 1;
        }
    }
    out["levels"] = std::move(levels);
    std::printf("%s\n", out.dump(1).c_str());
    return 0;
}
