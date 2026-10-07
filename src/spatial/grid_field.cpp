// Grid fields (ADR-032): the CPU reference implementation of the storage, the trilinear sample
// and one simulation sub-step. `shaders/simulate.wgsl` transliterates `step()` kernel by kernel
// and `shaders/fields.wgsl` transliterates `sampleScalar` / `sampleVector`; the render tests
// compare them (tests/rendering/test_simulation_gpu.cpp, within 1e-3).

#include "spatial/grid_field.hpp"

#include "core/noise.hpp"
#include "spatial/detail.hpp"
#include "spatial/field.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::spatial {

using nlohmann::json;

namespace {

int wrapIndex(int i, int n, GridWrap wrap) {
    if (n <= 0) {
        return 0;
    }
    if (wrap == GridWrap::Wrap) {
        const int m = i % n;
        return m < 0 ? m + n : m;
    }
    return std::clamp(i, 0, n - 1);
}

} // namespace

const char* gridModeName(GridMode mode) {
    switch (mode) {
    case GridMode::Scalar:
        return "scalar";
    case GridMode::Vector:
        return "vector";
    case GridMode::ReactionDiffusion:
        return "reactionDiffusion";
    case GridMode::Agents:
        return "agents";
    }
    return "scalar";
}

std::optional<GridMode> gridModeFromName(std::string_view name) {
    for (const GridMode m : {GridMode::Scalar, GridMode::Vector, GridMode::ReactionDiffusion, GridMode::Agents}) {
        if (name == gridModeName(m)) {
            return m;
        }
    }
    return std::nullopt;
}

const char* gridWrapName(GridWrap wrap) {
    return wrap == GridWrap::Wrap ? "wrap" : "clamp";
}

std::optional<GridWrap> gridWrapFromName(std::string_view name) {
    if (name == "wrap") {
        return GridWrap::Wrap;
    }
    if (name == "clamp") {
        return GridWrap::Clamp;
    }
    return std::nullopt;
}

int GridField::components() const {
    switch (mode) {
    case GridMode::Scalar:
        return 1;
    case GridMode::Vector:
        return 4;
    case GridMode::ReactionDiffusion:
        return 2;
    case GridMode::Agents:
        return 4;
    }
    return 1;
}

std::size_t GridField::cellCount() const {
    if (resolution.x <= 0 || resolution.y <= 0 || resolution.z <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(resolution.x) * static_cast<std::size_t>(resolution.y) *
           static_cast<std::size_t>(resolution.z);
}

glm::vec3 GridField::cellSize() const {
    const glm::vec3 extent = boundsMax - boundsMin;
    return glm::vec3(extent.x / std::max(resolution.x, 1), extent.y / std::max(resolution.y, 1),
                     extent.z / std::max(resolution.z, 1));
}

glm::vec3 GridField::cellCenter(int i, int j, int k) const {
    const glm::vec3 s = cellSize();
    return boundsMin + glm::vec3((static_cast<float>(i) + 0.5f) * s.x, (static_cast<float>(j) + 0.5f) * s.y,
                                 (static_cast<float>(k) + 0.5f) * s.z);
}

std::size_t GridField::index(int i, int j, int k) const {
    const auto nx = static_cast<std::size_t>(std::max(resolution.x, 1));
    const auto ny = static_cast<std::size_t>(std::max(resolution.y, 1));
    return ((static_cast<std::size_t>(k) * ny + static_cast<std::size_t>(j)) * nx + static_cast<std::size_t>(i)) *
           static_cast<std::size_t>(components());
}

void GridField::reset() {
    const std::size_t floats = floatCount();
    data.assign(floats, 0.0f);
    if (floats == 0) {
        return;
    }
    const int comps = components();
    for (int k = 0; k < resolution.z; ++k) {
        for (int j = 0; j < resolution.y; ++j) {
            for (int i = 0; i < resolution.x; ++i) {
                const std::size_t base = index(i, j, k);
                const glm::vec3 c = cellCenter(i, j, k);
                if (mode == GridMode::ReactionDiffusion) {
                    // The canonical Gray-Scott seed: A = 1 everywhere, and blobs of
                    // (A, B) = (0.5, 0.25) where a low-frequency fBM crosses 0.6.
                    const float mask =
                        (seedAmount != 0.0f && noise::fbm3(c * 0.25f, seed) > 0.6f) ? std::clamp(seedAmount, 0.0f, 1.0f)
                                                                                   : 0.0f;
                    data[base] = 1.0f - 0.5f * mask;
                    data[base + 1] = 0.25f * mask;
                    continue;
                }
                const float n = seedAmount != 0.0f ? seedAmount * (noise::fbm3(c, seed) * 2.0f - 1.0f) : 0.0f;
                {
                    for (int c2 = 0; c2 < comps; ++c2) {
                        data[base + static_cast<std::size_t>(c2)] = c2 < 3 ? n : 0.0f;
                    }
                }
            }
        }
    }
}

float GridField::at(int i, int j, int k, int c) const {
    if (!allocated() || c < 0 || c >= components()) {
        return 0.0f;
    }
    const int ii = wrapIndex(i, resolution.x, wrap);
    const int jj = wrapIndex(j, resolution.y, wrap);
    const int kk = wrapIndex(k, resolution.z, wrap);
    return data[index(ii, jj, kk) + static_cast<std::size_t>(c)];
}

namespace {

// Continuous cell coordinates of a point (the cell centre of cell i is at i).
glm::vec3 gridCoord(const GridField& g, const glm::vec3& p) {
    const glm::vec3 extent = g.boundsMax - g.boundsMin;
    const glm::vec3 res(static_cast<float>(g.resolution.x), static_cast<float>(g.resolution.y),
                        static_cast<float>(g.resolution.z));
    const glm::vec3 inv(extent.x != 0.0f ? 1.0f / extent.x : 0.0f, extent.y != 0.0f ? 1.0f / extent.y : 0.0f,
                        extent.z != 0.0f ? 1.0f / extent.z : 0.0f);
    return (p - g.boundsMin) * inv * res - glm::vec3(0.5f);
}

// Trilinear gather of one component. Matches gridTrilinear() in simulate.wgsl / fields.wgsl.
float trilinear(const GridField& g, const glm::vec3& coord, int c) {
    const glm::vec3 base = glm::floor(coord);
    const glm::vec3 f = coord - base;
    const int i = static_cast<int>(base.x);
    const int j = static_cast<int>(base.y);
    const int k = static_cast<int>(base.z);
    const float c000 = g.at(i, j, k, c);
    const float c100 = g.at(i + 1, j, k, c);
    const float c010 = g.at(i, j + 1, k, c);
    const float c110 = g.at(i + 1, j + 1, k, c);
    const float c001 = g.at(i, j, k + 1, c);
    const float c101 = g.at(i + 1, j, k + 1, c);
    const float c011 = g.at(i, j + 1, k + 1, c);
    const float c111 = g.at(i + 1, j + 1, k + 1, c);
    const float x00 = c000 + (c100 - c000) * f.x;
    const float x10 = c010 + (c110 - c010) * f.x;
    const float x01 = c001 + (c101 - c001) * f.x;
    const float x11 = c011 + (c111 - c011) * f.x;
    const float y0 = x00 + (x10 - x00) * f.y;
    const float y1 = x01 + (x11 - x01) * f.y;
    return y0 + (y1 - y0) * f.z;
}

} // namespace

float GridField::sampleScalar(const glm::vec3& p) const {
    if (!allocated()) {
        return 0.0f;
    }
    const glm::vec3 coord = gridCoord(*this, p);
    if (mode == GridMode::Vector || mode == GridMode::Agents) {
        return glm::length(sampleVector(p));
    }
    return trilinear(*this, coord, mode == GridMode::ReactionDiffusion ? 1 : 0);
}

glm::vec3 GridField::sampleVector(const glm::vec3& p) const {
    if (!allocated()) {
        return glm::vec3(0.0f);
    }
    const glm::vec3 coord = gridCoord(*this, p);
    if (mode != GridMode::Vector && mode != GridMode::Agents) {
        return glm::vec3(sampleScalar(p));
    }
    return glm::vec3(trilinear(*this, coord, 0), trilinear(*this, coord, 1), trilinear(*this, coord, 2));
}

void GridField::step(float dt, double time, const FieldSet* set) {
    if (!allocated() || dt <= 0.0f || mode == GridMode::Agents) {
        return; // ADR-1120: agents run on the GPU only
    }
    const int comps = components();
    const FieldSpec* inject = (set != nullptr && !injectField.empty()) ? set->find(injectField) : nullptr;
    const FieldSpec* velocity = (set != nullptr && !velocityField.empty()) ? set->find(velocityField) : nullptr;

    // ---- 1. injection (gather: every cell reads the field at its own centre) ----
    if (inject != nullptr && inject->enabled && injectRate != 0.0f) {
        const int channel = mode == GridMode::ReactionDiffusion ? 1 : 0;
        for (int k = 0; k < resolution.z; ++k) {
            for (int j = 0; j < resolution.y; ++j) {
                for (int i = 0; i < resolution.x; ++i) {
                    const glm::vec3 centre = cellCenter(i, j, k);
                    const std::size_t base = index(i, j, k);
                    if (mode == GridMode::Vector) {
                        const glm::vec3 v = spatial::sampleVector(*inject, centre, time, set);
                        for (int c = 0; c < 3; ++c) {
                            data[base + static_cast<std::size_t>(c)] += injectRate * dt * v[c];
                        }
                    } else {
                        const float s = std::max(0.0f, spatial::sampleScalar(*inject, centre, time, set));
                        float& cell = data[base + static_cast<std::size_t>(channel)];
                        cell += injectRate * dt * s;
                        if (mode == GridMode::Scalar && ceiling > 0.0f) {
                            cell = std::min(cell, ceiling); // ADR-1163
                        }
                    }
                }
            }
        }
    }

    // ---- 2. semi-Lagrangian advection ----
    if (velocity != nullptr && velocity->enabled && advect != 0.0f) {
        const GridField previous = *this;
        for (int k = 0; k < resolution.z; ++k) {
            for (int j = 0; j < resolution.y; ++j) {
                for (int i = 0; i < resolution.x; ++i) {
                    const glm::vec3 c = cellCenter(i, j, k);
                    const glm::vec3 v = spatial::sampleVector(*velocity, c, time, set) * advect;
                    const glm::vec3 src = gridCoord(previous, c - v * dt);
                    const std::size_t base = index(i, j, k);
                    for (int comp = 0; comp < comps; ++comp) {
                        data[base + static_cast<std::size_t>(comp)] = trilinear(previous, src, comp);
                    }
                }
            }
        }
    }

    if (mode == GridMode::ReactionDiffusion) {
        // ---- Gray-Scott, explicit Euler with the 6-neighbour Laplacian ----
        const GridField previous = *this;
        for (int k = 0; k < resolution.z; ++k) {
            for (int j = 0; j < resolution.y; ++j) {
                for (int i = 0; i < resolution.x; ++i) {
                    const float a = previous.at(i, j, k, 0);
                    const float b = previous.at(i, j, k, 1);
                    const float la = previous.at(i - 1, j, k, 0) + previous.at(i + 1, j, k, 0) +
                                     previous.at(i, j - 1, k, 0) + previous.at(i, j + 1, k, 0) +
                                     previous.at(i, j, k - 1, 0) + previous.at(i, j, k + 1, 0) - 6.0f * a;
                    const float lb = previous.at(i - 1, j, k, 1) + previous.at(i + 1, j, k, 1) +
                                     previous.at(i, j - 1, k, 1) + previous.at(i, j + 1, k, 1) +
                                     previous.at(i, j, k - 1, 1) + previous.at(i, j, k + 1, 1) - 6.0f * b;
                    const float reaction = a * b * b;
                    const std::size_t base = index(i, j, k);
                    data[base] = std::clamp(a + (diffusionA * la - reaction + feed * (1.0f - a)) * dt, 0.0f, 1.0f);
                    data[base + 1] =
                        std::clamp(b + (diffusionB * lb + reaction - (kill + feed) * b) * dt, 0.0f, 1.0f);
                }
            }
        }
        return;
    }

    // ---- 3. diffusion: fixed Jacobi sweeps ----
    if (diffusion > 0.0f && diffuseIterations > 0) {
        const float a = diffusion * dt;
        const GridField initial = *this;
        GridField previous = *this;
        for (int it = 0; it < diffuseIterations; ++it) {
            for (int k = 0; k < resolution.z; ++k) {
                for (int j = 0; j < resolution.y; ++j) {
                    for (int i = 0; i < resolution.x; ++i) {
                        const std::size_t base = index(i, j, k);
                        for (int c = 0; c < comps; ++c) {
                            const float sum = previous.at(i - 1, j, k, c) + previous.at(i + 1, j, k, c) +
                                              previous.at(i, j - 1, k, c) + previous.at(i, j + 1, k, c) +
                                              previous.at(i, j, k - 1, c) + previous.at(i, j, k + 1, c);
                            data[base + static_cast<std::size_t>(c)] =
                                (initial.at(i, j, k, c) + a * sum) / (1.0f + 6.0f * a);
                        }
                    }
                }
            }
            previous.data = data;
        }
    }

    // ---- 4. dissipation ----
    if (dissipation != 0.0f) {
        const float keep = std::max(0.0f, 1.0f - dissipation * dt);
        for (float& v : data) {
            v *= keep;
        }
    }
}

Result<void> GridField::validate() const {
    if (name.empty()) {
        return fail("grid field needs a name");
    }
    if (resolution.x < 1 || resolution.y < 1 || resolution.z < 1) {
        return fail("grid '{}': resolution must be >= 1 on every axis", name);
    }
    if (mode == GridMode::Agents) {
        if (resolution.y != 1 || resolution.x > kMaxAgentGridResolution || resolution.z > kMaxAgentGridResolution) {
            return fail("grid '{}': an agents grid is a plane: resolution [x, 1, z] with x, z <= {}", name,
                        kMaxAgentGridResolution);
        }
        if (agentCount < 0 || agentCount > kMaxAgents) {
            return fail("grid '{}': agentCount must be in [0, {}]", name, kMaxAgents);
        }
        if (species < 1 || species > 3) {
            return fail("grid '{}': species must be 1, 2 or 3", name);
        }
        if (sensorDistance < 0.0f || stepSize < 0.0f || depositAmount < 0.0f || diffusion > 1.0f) {
            return fail("grid '{}': sensorDistance, stepSize and depositAmount must be >= 0, diffusion <= 1", name);
        }
    } else if (resolution.x > kMaxGridResolution || resolution.y > kMaxGridResolution ||
               resolution.z > kMaxGridResolution) {
        return fail("grid '{}': resolution must be <= {} on every axis", name, kMaxGridResolution);
    }
    if (checkpointInterval < 0.0f) {
        return fail("grid '{}': checkpointInterval must be >= 0", name);
    }
    if (!(boundsMax.x > boundsMin.x && boundsMax.y > boundsMin.y && boundsMax.z > boundsMin.z)) {
        return fail("grid '{}': boundsMax must be greater than boundsMin on every axis", name);
    }
    if (floatCount() > kMaxGridTableFloats) {
        return fail("grid '{}': {} cells x {} components exceeds the {} float table", name, cellCount(), components(),
                    kMaxGridTableFloats);
    }
    if (diffusion < 0.0f) {
        return fail("grid '{}': diffusion must be >= 0", name);
    }
    if (diffuseIterations < 0 || diffuseIterations > 32) {
        return fail("grid '{}': diffuseIterations must be in [0, 32]", name);
    }
    if (!(simRate > 0.0f)) {
        return fail("grid '{}': simRate must be > 0", name);
    }
    if (maxSubSteps < 1 || maxSubSteps > 32) {
        return fail("grid '{}': maxSubSteps must be in [1, 32]", name);
    }
    return {};
}

std::uint64_t GridField::layoutHash() const {
    detail::Fnv h;
    h.str(name);
    h.boolean(enabled);
    h.u8(static_cast<std::uint8_t>(mode));
    h.u8(static_cast<std::uint8_t>(wrap));
    h.i32(resolution.x);
    h.i32(resolution.y);
    h.i32(resolution.z);
    h.v3(boundsMin);
    h.v3(boundsMax);
    h.str(injectField);
    h.str(velocityField);
    h.i32(diffuseIterations);
    h.f32(simRate);
    h.i32(maxSubSteps);
    h.u32(seed);
    h.f32(seedAmount);
    if (mode == GridMode::Agents) {
        h.i32(agentCount);
        h.i32(species);
        h.str(depositField);
    }
    return h.value();
}

std::uint64_t GridField::structuralHash() const {
    detail::Fnv h;
    h.str(name);
    h.boolean(enabled);
    h.u8(static_cast<std::uint8_t>(mode));
    h.u8(static_cast<std::uint8_t>(wrap));
    h.i32(resolution.x);
    h.i32(resolution.y);
    h.i32(resolution.z);
    h.v3(boundsMin);
    h.v3(boundsMax);
    h.str(injectField);
    h.str(velocityField);
    h.f32(injectRate);
    h.f32(advect);
    h.f32(diffusion);
    h.i32(diffuseIterations);
    h.f32(dissipation);
    h.f32(feed);
    h.f32(kill);
    h.f32(diffusionA);
    h.f32(diffusionB);
    h.f32(simRate);
    h.i32(maxSubSteps);
    h.u32(seed);
    h.f32(seedAmount);
    if (ceiling != 0.0f) { // ADR-1163: hashed only when set, so every other grid keeps its hash
        h.f32(ceiling);
    }
    if (mode == GridMode::Agents) { // ADR-1120: hashed only for agents, so every other grid keeps its hash
        h.i32(agentCount);
        h.i32(species);
        h.f32(sensorAngle);
        h.f32(sensorDistance);
        h.f32(turnAngle);
        h.f32(stepSize);
        h.f32(depositAmount);
        h.f32(repel);
        h.str(depositField);
    }
    return h.value();
}

json GridField::toJson() const {
    json j = json::object();
    j["name"] = name;
    j["enabled"] = enabled;
    j["mode"] = gridModeName(mode);
    j["wrap"] = gridWrapName(wrap);
    j["resolution"] = json::array({resolution.x, resolution.y, resolution.z});
    j["boundsMin"] = detail::vecToJson(boundsMin);
    j["boundsMax"] = detail::vecToJson(boundsMax);
    j["injectField"] = injectField;
    j["velocityField"] = velocityField;
    j["injectRate"] = injectRate;
    j["advect"] = advect;
    j["diffusion"] = diffusion;
    j["diffuseIterations"] = diffuseIterations;
    j["dissipation"] = dissipation;
    if (ceiling != 0.0f) {
        j["ceiling"] = ceiling; // ADR-1163: written only when set
    }
    j["feed"] = feed;
    j["kill"] = kill;
    j["diffusionA"] = diffusionA;
    j["diffusionB"] = diffusionB;
    j["simRate"] = simRate;
    j["maxSubSteps"] = maxSubSteps;
    j["seed"] = seed;
    j["seedAmount"] = seedAmount;
    if (checkpointInterval != 5.0f) {
        j["checkpointInterval"] = checkpointInterval;
    }
    if (mode == GridMode::Agents) {
        j["agentCount"] = agentCount;
        j["species"] = species;
        j["sensorAngle"] = sensorAngle;
        j["sensorDistance"] = sensorDistance;
        j["turnAngle"] = turnAngle;
        j["stepSize"] = stepSize;
        j["depositAmount"] = depositAmount;
        j["repel"] = repel;
        j["depositField"] = depositField;
    }
    return j;
}

Result<GridField> GridField::fromJson(const json& root) {
    if (!root.is_object()) {
        return fail("grid field must be a JSON object");
    }
    GridField g;
    const json& j = root;
    AVGEN_SPATIAL_READ(g.name, "name", detail::readString);
    AVGEN_SPATIAL_READ(g.enabled, "enabled", detail::readBool);
    AVGEN_SPATIAL_READ_ENUM(g.mode, "mode", &gridModeFromName, "grid mode");
    AVGEN_SPATIAL_READ_ENUM(g.wrap, "wrap", &gridWrapFromName, "grid wrap");
    if (root.contains("resolution")) {
        const json& r = root.at("resolution");
        if (r.is_number_integer()) {
            const int n = r.get<int>();
            g.resolution = glm::ivec3(n);
        } else if (r.is_array() && r.size() == 3 && r[0].is_number_integer()) {
            g.resolution = glm::ivec3(r[0].get<int>(), r[1].get<int>(), r[2].get<int>());
        } else {
            return fail("'resolution' must be an integer or an array of three integers");
        }
    }
    AVGEN_SPATIAL_READ(g.boundsMin, "boundsMin", detail::readVec3);
    AVGEN_SPATIAL_READ(g.boundsMax, "boundsMax", detail::readVec3);
    AVGEN_SPATIAL_READ(g.injectField, "injectField", detail::readString);
    AVGEN_SPATIAL_READ(g.velocityField, "velocityField", detail::readString);
    AVGEN_SPATIAL_READ(g.injectRate, "injectRate", detail::readFloat);
    AVGEN_SPATIAL_READ(g.advect, "advect", detail::readFloat);
    AVGEN_SPATIAL_READ(g.diffusion, "diffusion", detail::readFloat);
    AVGEN_SPATIAL_READ(g.diffuseIterations, "diffuseIterations", detail::readInt);
    AVGEN_SPATIAL_READ(g.dissipation, "dissipation", detail::readFloat);
    AVGEN_SPATIAL_READ(g.ceiling, "ceiling", detail::readFloat); // ADR-1163; absent = 0, unbounded
    AVGEN_SPATIAL_READ(g.feed, "feed", detail::readFloat);
    AVGEN_SPATIAL_READ(g.kill, "kill", detail::readFloat);
    AVGEN_SPATIAL_READ(g.diffusionA, "diffusionA", detail::readFloat);
    AVGEN_SPATIAL_READ(g.diffusionB, "diffusionB", detail::readFloat);
    AVGEN_SPATIAL_READ(g.simRate, "simRate", detail::readFloat);
    AVGEN_SPATIAL_READ(g.maxSubSteps, "maxSubSteps", detail::readInt);
    AVGEN_SPATIAL_READ(g.seed, "seed", detail::readU32);
    AVGEN_SPATIAL_READ(g.seedAmount, "seedAmount", detail::readFloat);
    AVGEN_SPATIAL_READ(g.checkpointInterval, "checkpointInterval", detail::readFloat);
    AVGEN_SPATIAL_READ(g.agentCount, "agentCount", detail::readInt);
    AVGEN_SPATIAL_READ(g.species, "species", detail::readInt);
    AVGEN_SPATIAL_READ(g.sensorAngle, "sensorAngle", detail::readFloat);
    AVGEN_SPATIAL_READ(g.sensorDistance, "sensorDistance", detail::readFloat);
    AVGEN_SPATIAL_READ(g.turnAngle, "turnAngle", detail::readFloat);
    AVGEN_SPATIAL_READ(g.stepSize, "stepSize", detail::readFloat);
    AVGEN_SPATIAL_READ(g.depositAmount, "depositAmount", detail::readFloat);
    AVGEN_SPATIAL_READ(g.repel, "repel", detail::readFloat);
    AVGEN_SPATIAL_READ(g.depositField, "depositField", detail::readString);
    if (auto ok = g.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return g;
}

std::size_t gridTableOffset(const std::vector<GridField>& grids, std::size_t index) {
    std::size_t offset = 0;
    for (std::size_t i = 0; i < index && i < grids.size(); ++i) {
        offset += grids[i].floatCount();
    }
    return offset;
}

std::size_t gridTableFloats(const std::vector<GridField>& grids) {
    return gridTableOffset(grids, grids.size());
}

} // namespace avgen::spatial
