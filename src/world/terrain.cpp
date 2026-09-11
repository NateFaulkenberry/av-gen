#include "world/terrain.hpp"

#include "scene/struct_hash.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace avgen::world {
namespace {

using scene::detail::StructHash;

int lodResolution(const TerrainSettings& settings, int lod) {
    return std::max(2, settings.resolution >> std::max(0, lod));
}

} // namespace

// WaterSettings::validate / structuralHash moved to scene/water_surface.cpp with the struct.

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
    if (shadowDistance < 0.0f) {
        return fail("terrain: shadowDistance must be >= 0 (0 = the view distance)");
    }
    if (skirtDepth < 0.0f || skirtDepth > 10000.0f) {
        return fail("terrain: skirtDepth must be in [0, 10000]");
    }
    if (auto r = water.validate(); !r) {
        return r;
    }
    return {};
}

std::uint64_t TerrainSettings::structuralHash() const {
    StructHash h;
    h.f32(chunkSize);
    h.i32(resolution);
    h.i32(lodLevels);
    h.f32(skirtDepth);
    h.u64(water.structuralHash());
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

float ChunkField::at(int i, int j) const {
    const int x = std::clamp(i + 1, 0, side - 1);
    const int y = std::clamp(j + 1, 0, side - 1);
    return heights[static_cast<std::size_t>(y) * side + x];
}

glm::vec3 ChunkField::normalAt(int i, int j, int spread) const {
    // Central differences on the LOD 0 grid: the same expression the analytic normal used, reading
    // samples that already exist. The border cell is what lets the chunk's edge use a centred
    // difference too, so two chunks agree exactly along their shared edge.
    const int d = std::max(spread, 1);
    const float hx = at(i + d, j) - at(i - d, j);
    const float hz = at(i, j + d) - at(i, j - d);
    return glm::normalize(glm::vec3(-hx, 2.0f * step * static_cast<float>(d), -hz));
}

ChunkField sampleChunkField(const WorldMap& map, const TerrainSettings& settings, glm::ivec2 coord) {
    ChunkField field;
    field.side = settings.resolution + 3;
    field.step = settings.chunkSize / static_cast<float>(settings.resolution);
    field.origin = chunkOrigin(map, settings, coord) - glm::vec2(field.step);
    field.heights.resize(static_cast<std::size_t>(field.side) * field.side);
    for (int y = 0; y < field.side; ++y) {
        for (int x = 0; x < field.side; ++x) {
            const glm::vec2 p = field.origin + glm::vec2(static_cast<float>(x), static_cast<float>(y)) * field.step;
            field.heights[static_cast<std::size_t>(y) * field.side + x] = map.height(p);
        }
    }
    return field;
}

namespace {

// The stride through the LOD 0 grid that gives `res` quads across the chunk, or 0 when the level's
// resolution does not divide the base one and the grid cannot be reused.
int strideFor(const TerrainSettings& settings, int res) {
    return res > 0 && settings.resolution % res == 0 ? settings.resolution / res : 0;
}

scene::MeshData buildChunkMeshFrom(const WorldMap& map, const TerrainSettings& settings, glm::ivec2 coord, int lod,
                                   const ChunkField* field) {
    const int res = lodResolution(settings, lod);
    const float step = settings.chunkSize / static_cast<float>(res);
    const glm::vec2 origin = chunkOrigin(map, settings, coord);
    // Normals are always sampled at the LOD 0 spacing. Sampling at the level's own spacing would
    // make a distant chunk's shading disagree with its neighbour's across the seam, which reads as
    // a visible tile grid -- the exact artefact chunking is supposed to be invisible about.
    const float epsilon = settings.chunkSize / static_cast<float>(settings.resolution) * 0.5f;
    const int stride = field != nullptr ? strideFor(settings, res) : 0;
    // About eight metres of hillside, whatever the chunk's resolution: the scale a biome boundary
    // should be drawn at rather than the scale the mesh happens to be tessellated at.
    const int biomeSpread =
        std::max(1, static_cast<int>(std::lround(8.0f / (settings.chunkSize / static_cast<float>(settings.resolution)))));

    scene::MeshData mesh;
    mesh.name = fmt::format("terrain_{}_{}_lod{}", coord.x, coord.y, lod);
    const int side = res + 1;
    mesh.vertices.reserve(static_cast<std::size_t>(side) * side + static_cast<std::size_t>(side) * 4);
    for (int j = 0; j <= res; ++j) {
        for (int i = 0; i <= res; ++i) {
            const glm::vec2 p = origin + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * step;
            scene::Vertex v;
            if (stride > 0) {
                v.position = glm::vec3(p.x, field->at(i * stride, j * stride), p.y);
                v.normal = field->normalAt(i * stride, j * stride);
            } else {
                v.position = glm::vec3(p.x, map.height(p), p.y);
                v.normal = map.normal(p, epsilon);
            }
            const float slope = glm::clamp(1.0f - v.normal.y, 0.0f, 1.0f);
            const float altitude = map.altitude01(v.position.y);
            float axis = altitude;
            if (!map.biomes.empty()) {
                // The biome reads the hillside's slope, not this vertex's. Beyond the chunk's own
                // grid the coarse difference has nothing to read, so it falls back to the fine one
                // rather than clamping into the border and reporting a hillside that is flat.
                const bool coarseAvailable = stride > 0 && biomeSpread > 0;
                const float biomeSlope =
                    coarseAvailable
                        ? glm::clamp(1.0f - field->normalAt(i * stride, j * stride, biomeSpread).y, 0.0f, 1.0f)
                        : slope;
                axis = map.biomes.at(altitude, biomeSlope, map.moisture(p, altitude), p).axis();
            }
            v.uv = glm::vec2(axis, slope);
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

} // namespace

scene::MeshData buildChunkMesh(const WorldMap& map, const TerrainSettings& settings, glm::ivec2 coord, int lod) {
    const ChunkField field = sampleChunkField(map, settings, coord);
    return buildChunkMeshFrom(map, settings, coord, lod, &field);
}

scene::MeshData buildChunkWater(const WorldMap& map, const TerrainSettings& settings, glm::ivec2 coord,
                                const ChunkField* field, const WaterBodySet* bodies) {
    scene::MeshData mesh;
    if (!settings.water.enabled) {
        return mesh;
    }
    // The ground's own resolution. A water surface is flat, so its own shape needs no detail -- but
    // its *edge* is where it crosses the terrain, and a coarser grid puts that crossing on a coarser
    // staircase. The shoreline is the only silhouette water has.
    const int res = settings.resolution;
    const float step = settings.chunkSize / static_cast<float>(res);
    const glm::vec2 origin = chunkOrigin(map, settings, coord);
    const int side = res + 1;
    const int stride = field != nullptr ? strideFor(settings, res) : 0;

    struct Point {
        float surface = 0.0f;
        float bed = 0.0f;
        bool wet = false;
    };
    std::vector<Point> points(static_cast<std::size_t>(side) * side);
    bool any = false;
    for (int j = 0; j <= res; ++j) {
        for (int i = 0; i <= res; ++i) {
            const glm::vec2 p = origin + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * step;
            Point& q = points[static_cast<std::size_t>(j) * side + i];
            q.surface = map.waterSurface(p);
            q.bed = stride > 0 ? field->at(i * stride, j * stride) : map.height(p);
            q.wet = std::isfinite(q.surface) && q.bed < q.surface;
            any = any || q.wet;
        }
    }
    if (!any) {
        return mesh; // a dry chunk costs one grid of samples and no geometry at all
    }

    mesh.name = fmt::format("water_{}_{}", coord.x, coord.y);
    mesh.vertices.resize(static_cast<std::size_t>(side) * side);
    // Speed is baked as a fraction of the fastest body in the world rather than in metres per
    // second, so the lane stays in [0, 1] and a world of slow water does not come out still.
    float maxSpeed = 0.0f;
    if (bodies != nullptr) {
        for (const WaterBody& body : bodies->bodies) {
            maxSpeed = std::max(maxSpeed, body.speed);
        }
    }
    for (int j = 0; j <= res; ++j) {
        for (int i = 0; i <= res; ++i) {
            const std::size_t k = static_cast<std::size_t>(j) * side + i;
            const Point& q = points[k];
            const glm::vec2 p = origin + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * step;
            // A dry corner still needs a position: it is a corner of a quad whose other corners are
            // wet, and it sits at the neighbouring water level so the surface reaches the bank
            // rather than folding down to meet the ground.
            // A dry corner takes a wet neighbour's level, whatever `waterSurface` said about it.
            // Testing for a non-finite surface was not enough: a world with no sea still reports
            // its sea level, and that sentinel is a finite -1000, so dry corners were placed a
            // kilometre underground and their quads came out as vertical fins hanging off the
            // underside of the sheet. That was the artefact; everything else about the shoreline
            // was a symptom of it.
            float surface = q.surface;
            if (!q.wet) {
                // Take a wet neighbour's level, and never the bed. Falling back to the bed was what
                // made the water climb the bank: a corner whose ground is above the waterline would
                // lift its vertex with it, and the surface came out as a wall of vertical quads
                // standing over the shore instead of a flat sheet the terrain cuts off.
                surface = -std::numeric_limits<float>::infinity();
                // Diagonals included: every corner of a quad touches every other, so a dry corner
                // whose only wet neighbour is across the diagonal is exactly the case that would
                // otherwise fall back to the bed and put a step in the surface.
                for (const glm::ivec2 d : {glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1),
                                           glm::ivec2(1, 1), glm::ivec2(1, -1), glm::ivec2(-1, 1),
                                           glm::ivec2(-1, -1)}) {
                    const int ni = i + d.x;
                    const int nj = j + d.y;
                    if (ni < 0 || nj < 0 || ni > res || nj > res) {
                        continue;
                    }
                    const Point& n = points[static_cast<std::size_t>(nj) * side + ni];
                    if (n.wet) {
                        surface = std::max(surface, n.surface);
                    }
                }
                if (!std::isfinite(surface)) {
                    surface = q.bed; // no wet neighbour: this corner is in no emitted quad
                }
            }
            const float depth = std::max(surface - q.bed, 0.0f);
            scene::Vertex v;
            v.position = glm::vec3(p.x, surface, p.y);
            // ADR-091: the normal slot carries the flow, because a flat sheet's normal is the one
            // thing already known. xz is the downstream direction, y is the speed as a fraction of
            // the fastest body in the world, so the water surface shader can scroll its layers
            // along the real course without the GPU ever hearing of a river. A world with no
            // bodies derived leaves it at +Y, which is exactly the vertex the sheet used to carry.
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            if (bodies != nullptr && !bodies->empty()) {
                const FlowSample flow = bodies->flowAt(p);
                const float scale = maxSpeed > 1e-4f ? flow.speed / maxSpeed : 0.0f;
                v.normal = glm::vec3(flow.direction.x, glm::clamp(scale, 0.0f, 1.0f), flow.direction.y);
            }
            // uv.x is the bed depth in *metres* -- the shader curves it itself, and the shoreline it
            // draws is sub-quad because it comes from the scene depth rather than from this. uv.y is
            // the old normalised shore term, kept because it is the one cue a fragment has when
            // there is no depth prepass to read.
            v.uv = glm::vec2(depth, glm::clamp(depth / std::max(settings.water.shoreFade, 1e-3f), 0.0f, 1.0f));
            mesh.vertices[k] = v;
        }
    }
    const auto index = [side](int i, int j) { return static_cast<std::uint32_t>(j * side + i); };
    for (int j = 0; j < res; ++j) {
        for (int i = 0; i < res; ++i) {
            bool wet = false;
            for (const glm::ivec2 d : {glm::ivec2(0, 0), glm::ivec2(1, 0), glm::ivec2(0, 1), glm::ivec2(1, 1)}) {
                wet = wet || points[static_cast<std::size_t>(j + d.y) * side + i + d.x].wet;
            }
            if (!wet) {
                continue;
            }
            const std::uint32_t a = index(i, j);
            const std::uint32_t b = index(i + 1, j);
            const std::uint32_t c = index(i, j + 1);
            const std::uint32_t d = index(i + 1, j + 1);
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    return mesh.indices.empty() ? scene::MeshData{} : mesh;
}

float lodProjectionScale(float fovYRadians, float viewportHeight) {
    const float halfFov = std::clamp(fovYRadians, 1e-3f, 3.0f) * 0.5f;
    return 0.5f * std::max(viewportHeight, 1.0f) / std::max(std::tan(halfFov), 1e-4f);
}

int chunkLod(const TerrainSettings& settings, float distance, float projScale) {
    if (settings.lodLevels <= 1) {
        return 0;
    }
    // The reference lens `lodDistance` was authored against: a 900-pixel-tall viewport at 50
    // degrees. A scene keeps the number it tuned and gets the same result at that lens, and a
    // different result -- the right one -- at any other.
    constexpr float kReferenceProjScale = 0.5f * 900.0f / 0.466307658f; // tan(25 degrees)
    const float scale = projScale > 0.0f ? projScale : kReferenceProjScale;
    // How far away this chunk would have to be, at the reference lens, to look the size it looks
    // now. Wider lens or taller viewport pushes the switch further out; longer lens pulls it in.
    const float effective = distance * kReferenceProjScale / scale;
    if (effective <= settings.lodDistance) {
        return 0;
    }
    const int level = 1 + static_cast<int>(std::floor(std::log2(effective / settings.lodDistance)));
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
    const std::function<scene::MeshId(std::size_t, int, scene::MeshData&&)>& emit,
    const WaterBodySet* bodies) {
    const std::vector<glm::ivec2> coords = chunkGrid(map, settings);
    const std::size_t levels = static_cast<std::size_t>(std::max(settings.lodLevels, 1));
    std::vector<scene::MeshData> built(coords.size() * levels);
    std::vector<scene::MeshData> water(coords.size());
    std::vector<TerrainChunk> chunks(coords.size());

    // One chunk is a few thousand independent height evaluations and a few hundred chunks is a
    // second or more of a cold start, so the meshes are built across the machine's cores. Each
    // thread owns whole chunks and writes only into its own slots, which is why there is no lock:
    // the shared state is the map, and sampling a map is a pure function.
    const auto buildRange = [&](std::size_t first, std::size_t last) {
        for (std::size_t c = first; c < last; ++c) {
            const ChunkField field = sampleChunkField(map, settings, coords[c]);
            TerrainChunk& chunk = chunks[c];
            chunk.coord = coords[c];
            chunk.center = chunkOrigin(map, settings, coords[c]) + glm::vec2(settings.chunkSize * 0.5f);
            chunk.meshes.fill(scene::kInvalidMesh);
            chunk.water = scene::kInvalidMesh;
            water[c] = buildChunkWater(map, settings, coords[c], &field, bodies);
            for (std::size_t lod = 0; lod < levels; ++lod) {
                scene::MeshData mesh = buildChunkMeshFrom(map, settings, coords[c], static_cast<int>(lod), &field);
                if (lod == 0) {
                    // LOD 0 bounds hold every level: coarser levels sample a subset of the same
                    // surface, so they can only be flatter, and the skirt is already in the bounds.
                    const auto [lo, hi] = mesh.bounds();
                    chunk.boundsMin = lo;
                    chunk.boundsMax = hi;
                }
                built[c * levels + lod] = std::move(mesh);
            }
            // A coarse level can overshoot LOD 0's extremes where it skips a peak, and the AABB is
            // what culling trusts, so give it slack rather than clipping a hill off the frustum.
            chunk.boundsMin.y -= settings.skirtDepth;
            chunk.boundsMax.y += settings.chunkSize * 0.1f;
        }
    };

    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const std::size_t workers = std::min<std::size_t>(hardware, std::max<std::size_t>(coords.size() / 4, 1));
    if (workers <= 1) {
        buildRange(0, coords.size());
    } else {
        std::vector<std::thread> pool;
        pool.reserve(workers - 1);
        const std::size_t span = (coords.size() + workers - 1) / workers;
        for (std::size_t w = 1; w < workers; ++w) {
            const std::size_t first = std::min(w * span, coords.size());
            const std::size_t last = std::min(first + span, coords.size());
            if (first < last) {
                pool.emplace_back(buildRange, first, last);
            }
        }
        buildRange(0, std::min(span, coords.size()));
        for (std::thread& t : pool) {
            t.join();
        }
    }

    // Emission is serial and in order: it hands meshes to the Scene, which is not thread safe and
    // has no reason to be.
    for (std::size_t c = 0; c < coords.size(); ++c) {
        for (std::size_t lod = 0; lod < levels; ++lod) {
            chunks[c].meshes[lod] = emit(c, static_cast<int>(lod), std::move(built[c * levels + lod]));
        }
        // A dry chunk emits no water mesh at all rather than an empty one, so the caller can tell
        // "there is no water here" from "here is water with nothing in it".
        if (water[c].valid()) {
            chunks[c].water = emit(c, kWaterLevel, std::move(water[c]));
        }
    }
    return chunks;
}

// ---- the generated ground material ---------------------------------------------------------------

namespace {

scene::MaterialOp materialOp(scene::MaterialOpKind kind, int dst, int srcA = 0, int srcB = 0, int srcC = 0) {
    scene::MaterialOp op;
    op.kind = kind;
    op.dst = dst;
    op.srcA = srcA;
    op.srcB = srcB;
    op.srcC = srcC;
    return op;
}

glm::vec3 ground(const BiomeSet& set, std::size_t i) {
    return set.biomes[std::min(i, set.biomes.size() - 1)].groundColor;
}
glm::vec3 rock(const BiomeSet& set, std::size_t i) {
    return set.biomes[std::min(i, set.biomes.size() - 1)].rockColor;
}

} // namespace

scene::MaterialProgram terrainMaterialProgram(const BiomeSet& biomes, std::string name, bool mottle,
                                             float glow, float glowScale, float glowCoverage,
                                             glm::vec3 glowColor) {
    using Kind = scene::MaterialOpKind;
    scene::MaterialProgram program;
    program.name = std::move(name);
    if (biomes.empty()) {
        return program;
    }
    const std::size_t n = biomes.biomes.size();
    const std::size_t mid = n / 2;

    // Op count is the cost of this program, though less of it than it once was. The first version
    // ran to twenty ops and cost 32 ms of an 87 ms frame -- a third of the frame, spent painting the
    // ground. That was blamed on a per-pixel read of the program's own op records; ADR-050 shows it
    // was the interpreter's loop body instead, and fixing that took the per-op cost from 0.78 ms to
    // 0.26 over a full-screen surface. Fewer ops is still cheaper, and a fullscreen material is
    // still the place where that matters most.
    //
    // So the palette is one three-stop ramp rather than two crossed over. A set ordered as a
    // gradient loses its second and fourth entries as distinct stops and keeps them as the
    // interpolations between the ones that remain, which is what an ordered set means. Eight ops.
    std::vector<scene::MaterialOp>& ops = program.ops;
    scene::MaterialOp uv = materialOp(Kind::Input, 0);
    uv.input = scene::MaterialInput::Uv;
    ops.push_back(uv);
    scene::MaterialOp axis = materialOp(Kind::Swizzle, 1, 0);
    axis.constant = {0.0f, 0.0f, 0.0f, 0.0f}; // uv.x is the biome axis; every mask op reads x
    ops.push_back(axis);
    scene::MaterialOp slope = materialOp(Kind::Swizzle, 2, 0);
    slope.constant = {1.0f, 1.0f, 1.0f, 1.0f}; // uv.y is the slope
    ops.push_back(slope);

    const auto ramp = [&](int dst, glm::vec3 (*pick)(const BiomeSet&, std::size_t)) {
        scene::MaterialOp r = materialOp(Kind::Ramp, dst, 1); // the axis already spans 0..1
        r.constant = glm::vec4(pick(biomes, 0), 1.0f);
        r.constant2 = glm::vec4(pick(biomes, mid), 1.0f);
        r.constant3 = glm::vec4(pick(biomes, n - 1), 1.0f);
        ops.push_back(r);
    };
    ramp(3, ground);
    ramp(4, rock);

    // Where the ground gives way to bare rock. Slope is already one of the three things a biome
    // rule is written in, so a broad mask here double-counts it: this fires only on a genuine
    // cliff -- the 99th percentile of this terrain's slope is 0.31 -- which is the part a biome
    // cannot express, because a cliff inside a marsh is still a cliff.
    scene::MaterialOp rockMask = materialOp(Kind::Smoothstep, 5, 2);
    rockMask.constant = {0.28f, 0.50f, 0.0f, 0.0f};
    ops.push_back(rockMask);
    ops.push_back(materialOp(Kind::MixBy, 6, 3, 4, 5));

    if (mottle) {
        // World-space, so it does not swim with the camera and does not tile with a chunk. Four
        // more ops and an fbm: measured at 7.7 ms of that same frame, so it is a knob, not a given.
        scene::MaterialOp world = materialOp(Kind::Input, 4);
        world.input = scene::MaterialInput::WorldPosition;
        ops.push_back(world);
        scene::MaterialOp mottleNoise = materialOp(Kind::Noise, 7, 4);
        mottleNoise.value = 0.045f;
        mottleNoise.seed = 41;
        ops.push_back(mottleNoise);
        scene::MaterialOp mottleRange = materialOp(Kind::Remap, 7, 7);
        mottleRange.value = 1.0f;
        mottleRange.constant = {0.0f, 1.0f, 0.80f, 1.20f};
        ops.push_back(mottleRange);
        ops.push_back(materialOp(Kind::Multiply, 6, 6, 7));
    }

    // ADR-056: patches of the ground itself are alive. Four ops, and unlike a scatter layer the
    // cost does not grow with how far away it has to reach -- which is the only way the far
    // hillside gets any bioluminescence at all without quadrupling the instance count.
    if (glow > 0.0f) {
        if (!mottle) { // `mottle` already left world position in register 4
            scene::MaterialOp world = materialOp(Kind::Input, 4);
            world.input = scene::MaterialInput::WorldPosition;
            ops.push_back(world);
        }
        scene::MaterialOp patches = materialOp(Kind::Voronoi, 3, 4);
        patches.value = std::max(glowScale, 1e-4f);
        patches.seed = 907;
        ops.push_back(patches);
        // Voronoi F1 is small at a cell's centre, so the mask runs from the coverage edge inwards.
        scene::MaterialOp mask = materialOp(Kind::Smoothstep, 3, 3);
        mask.constant = {std::clamp(glowCoverage, 0.0f, 1.0f) * 0.55f, 0.0f, 0.0f, 0.0f};
        ops.push_back(mask);
        scene::MaterialOp tint = materialOp(Kind::Constant, 5);
        tint.constant = glm::vec4(glowColor * glow, 1.0f);
        ops.push_back(tint);
        ops.push_back(materialOp(Kind::Multiply, 5, 5, 3));
        program.emissionRegister = 5;
        program.emissionIntensity = 1.0f;
    }

    float roughSum = 0.0f;
    for (const Biome& b : biomes.biomes) {
        roughSum += b.roughness;
    }
    scene::MaterialOp roughness = materialOp(Kind::Constant, 7);
    roughness.constant = glm::vec4(roughSum / static_cast<float>(n));
    ops.push_back(roughness);

    program.baseColorRegister = 6;
    program.roughnessRegister = 7;
    return program;
}

} // namespace avgen::world
