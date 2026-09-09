#include "world/terrain.hpp"

#include "scene/struct_hash.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::world {
namespace {

using scene::detail::StructHash;

int lodResolution(const TerrainSettings& settings, int lod) {
    return std::max(2, settings.resolution >> std::max(0, lod));
}

} // namespace

Result<void> TerrainSettings::validate() const {
    if (!(chunkSize > 0.0f) || chunkSize > 100000.0f) {
        return fail("terrain: chunkSize must be in (0, 100000]");
    }
    if (resolution < 2 || resolution > 256) {
        return fail("terrain: resolution must be in [2, 256]");
    }
    if (lodLevels < 1 || lodLevels > kMaxTerrainLods) {
        return fail("terrain: lodLevels must be in [1, {}]", kMaxTerrainLods);
    }
    if (!(lodDistance > 0.0f)) {
        return fail("terrain: lodDistance must be positive");
    }
    if (!(viewDistance > 0.0f)) {
        return fail("terrain: viewDistance must be positive");
    }
    if (skirtDepth < 0.0f || skirtDepth > 10000.0f) {
        return fail("terrain: skirtDepth must be in [0, 10000]");
    }
    return {};
}

std::uint64_t TerrainSettings::structuralHash() const {
    StructHash h;
    h.f32(chunkSize);
    h.i32(resolution);
    h.i32(lodLevels);
    h.f32(skirtDepth);
    // lodDistance and viewDistance choose meshes per frame; they change nothing that was built.
    return h.value();
}

std::vector<glm::ivec2> chunkGrid(const WorldMap& map, const TerrainSettings& settings) {
    const int nx = std::max(1, static_cast<int>(std::ceil(map.size.x / settings.chunkSize)));
    const int nz = std::max(1, static_cast<int>(std::ceil(map.size.y / settings.chunkSize)));
    std::vector<glm::ivec2> out;
    out.reserve(static_cast<std::size_t>(nx) * static_cast<std::size_t>(nz));
    for (int z = 0; z < nz; ++z) {
        for (int x = 0; x < nx; ++x) {
            out.emplace_back(x, z);
        }
    }
    return out;
}

glm::vec2 chunkOrigin(const WorldMap& map, const TerrainSettings& settings, glm::ivec2 coord) {
    return map.min() + glm::vec2(static_cast<float>(coord.x), static_cast<float>(coord.y)) * settings.chunkSize;
}

scene::MeshData buildChunkMesh(const WorldMap& map, const TerrainSettings& settings, glm::ivec2 coord, int lod) {
    const int res = lodResolution(settings, lod);
    const float step = settings.chunkSize / static_cast<float>(res);
    const glm::vec2 origin = chunkOrigin(map, settings, coord);
    // Normals are always sampled at the LOD 0 spacing. Sampling at the level's own spacing would
    // make a distant chunk's shading disagree with its neighbour's across the seam, which reads as
    // a visible tile grid -- the exact artefact chunking is supposed to be invisible about.
    const float epsilon = settings.chunkSize / static_cast<float>(settings.resolution) * 0.5f;

    scene::MeshData mesh;
    mesh.name = fmt::format("terrain_{}_{}_lod{}", coord.x, coord.y, lod);
    const int side = res + 1;
    mesh.vertices.reserve(static_cast<std::size_t>(side) * side + static_cast<std::size_t>(side) * 4);
    for (int j = 0; j <= res; ++j) {
        for (int i = 0; i <= res; ++i) {
            const glm::vec2 p = origin + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * step;
            const float h = map.height(p);
            scene::Vertex v;
            v.position = glm::vec3(p.x, h, p.y);
            v.normal = map.normal(p, epsilon);
            v.uv = glm::vec2(glm::clamp(1.0f - v.normal.y, 0.0f, 1.0f), map.altitude01(h));
            mesh.vertices.push_back(v);
        }
    }
    mesh.indices.reserve(static_cast<std::size_t>(res) * res * 6 + static_cast<std::size_t>(res) * 24);
    const auto index = [side](int i, int j) { return static_cast<std::uint32_t>(j * side + i); };
    for (int j = 0; j < res; ++j) {
        for (int i = 0; i < res; ++i) {
            const std::uint32_t a = index(i, j);
            const std::uint32_t b = index(i + 1, j);
            const std::uint32_t c = index(i, j + 1);
            const std::uint32_t d = index(i + 1, j + 1);
            // Counter-clockwise seen from +Y, matching every other generator in the engine.
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }

    if (settings.skirtDepth > 0.0f) {
        // The seam curtain: one quad strip hanging from each of the four borders. Its normal is the
        // border vertex's own normal rather than an outward-facing one, so where it is visible at
        // all it shades as a continuation of the ground instead of a dark band.
        const auto skirt = [&](int i0, int j0, int di, int dj) {
            std::uint32_t prevTop = 0;
            std::uint32_t prevBottom = 0;
            for (int k = 0; k <= res; ++k) {
                const int i = i0 + di * k;
                const int j = j0 + dj * k;
                const std::uint32_t top = index(i, j);
                scene::Vertex v = mesh.vertices[top];
                v.position.y -= settings.skirtDepth;
                const auto bottom = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(v);
                if (k > 0) {
                    mesh.indices.insert(mesh.indices.end(),
                                        {prevTop, prevBottom, top, top, prevBottom, bottom});
                }
                prevTop = top;
                prevBottom = bottom;
            }
        };
        // Wound so each strip faces outwards from the chunk: -Z and +X run one way, +Z and -X back.
        skirt(res, 0, -1, 0);   // north edge (j = 0), running -X
        skirt(0, res, 1, 0);    // south edge (j = res), running +X
        skirt(0, 0, 0, 1);      // west edge (i = 0), running +Z
        skirt(res, res, 0, -1); // east edge (i = res), running -Z
    }
    return mesh;
}

int chunkLod(const TerrainSettings& settings, float distance) {
    if (distance <= settings.lodDistance || settings.lodLevels <= 1) {
        return 0;
    }
    const int level = 1 + static_cast<int>(std::floor(std::log2(distance / settings.lodDistance)));
    return std::clamp(level, 0, settings.lodLevels - 1);
}

FrustumPlanes frustumPlanes(const glm::mat4& m) {
    const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    FrustumPlanes planes{{r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2}};
    for (glm::vec4& p : planes) {
        const float len = glm::length(glm::vec3(p));
        if (len > 0.0f) {
            p /= len;
        }
    }
    return planes;
}

bool aabbVisible(const FrustumPlanes& planes, const glm::vec3& min, const glm::vec3& max) {
    for (const glm::vec4& p : planes) {
        // The corner furthest along the plane normal: if even that is behind, the box is out.
        const glm::vec3 corner(p.x >= 0.0f ? max.x : min.x, p.y >= 0.0f ? max.y : min.y,
                               p.z >= 0.0f ? max.z : min.z);
        if (glm::dot(glm::vec3(p), corner) + p.w < 0.0f) {
            return false;
        }
    }
    return true;
}

std::vector<TerrainChunk> buildTerrain(
    const WorldMap& map, const TerrainSettings& settings,
    const std::function<scene::MeshId(std::size_t, int, scene::MeshData&&)>& emit) {
    const std::vector<glm::ivec2> coords = chunkGrid(map, settings);
    std::vector<TerrainChunk> chunks;
    chunks.reserve(coords.size());
    for (std::size_t c = 0; c < coords.size(); ++c) {
        TerrainChunk chunk;
        chunk.coord = coords[c];
        chunk.center = chunkOrigin(map, settings, coords[c]) + glm::vec2(settings.chunkSize * 0.5f);
        chunk.meshes.fill(scene::kInvalidMesh);
        for (int lod = 0; lod < settings.lodLevels; ++lod) {
            scene::MeshData mesh = buildChunkMesh(map, settings, coords[c], lod);
            if (lod == 0) {
                // LOD 0 bounds hold every level: coarser levels sample a subset of the same
                // surface, so they can only be flatter, and the skirt is already in the bounds.
                const auto [lo, hi] = mesh.bounds();
                chunk.boundsMin = lo;
                chunk.boundsMax = hi;
            }
            chunk.meshes[static_cast<std::size_t>(lod)] = emit(c, lod, std::move(mesh));
        }
        // A coarse level can overshoot LOD 0's extremes where it skips a peak, and the AABB is what
        // culling trusts, so give it a little slack rather than clipping a hill off the frustum.
        chunk.boundsMin.y -= settings.skirtDepth;
        chunk.boundsMax.y += settings.chunkSize * 0.1f;
        chunks.push_back(chunk);
    }
    return chunks;
}

} // namespace avgen::world
