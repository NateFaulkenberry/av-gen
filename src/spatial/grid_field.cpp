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
    case GridMode::Excitable:
        return "excitable";
    }
    return "scalar";
}

std::optional<GridMode> gridModeFromName(std::string_view name) {
    for (const GridMode m :
         {GridMode::Scalar, GridMode::Vector, GridMode::ReactionDiffusion, GridMode::Agents, GridMode::Excitable}) {
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
    case GridMode::Excitable:
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

float GridField::sampleChannel(const glm::vec3& p, int channel) const {
    if (!allocated() || channel < 0 || channel >= components()) {
        return 0.0f;
    }
    return trilinear(*this, gridCoord(*this, p), channel);
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

namespace {

// ---- ADR-1201: the excitable medium. Transliterated by cs_excite in shaders/simulate.wgsl. ----

// The hash of simulate.wgsl (simMix / simHash / simUnit), bit for bit.
std::uint32_t simMix(std::uint32_t x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

std::uint32_t simHash(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    return simMix((a * 0x8da6b343u) ^ simMix((b * 0xd8163841u) ^ simMix(c)));
}

float simUnit(std::uint32_t h) { return static_cast<float>(h >> 8u) * (1.0f / 16777216.0f); }

// Below this a channel is 0, on both sides: Metal flushes denormals and the CPU does not.
constexpr float kExciteFlush = 1e-20f;
// A front may re-trigger a cell only if it arrived this much (seconds) after the cell last fired; it keeps one
// front from firing a cell twice through the two-step acceptance window.
constexpr float kExciteSameFront = 1e-5f;
// Noise is re-drawn every this many seconds (a whole number of steps), so it does not depend on simRate.
constexpr float kExciteNoisePeriod = 0.25f;

float flushed(float v) { return v < kExciteFlush ? 0.0f : v; }

void stepExcitable(GridField& g, float dt, double time, const FieldSet* set) {
    const FieldSpec* inject = (set != nullptr && !g.injectField.empty()) ? set->find(g.injectField) : nullptr;
    const FieldSpec* conductivity =
        (set != nullptr && !g.conductivityField.empty()) ? set->find(g.conductivityField) : nullptr;
    const bool stimulated = inject != nullptr && inject->enabled && g.injectRate != 0.0f;
    const bool conducted = conductivity != nullptr && conductivity->enabled;
    const auto stepIndex = static_cast<std::uint32_t>(std::llround(time * static_cast<double>(g.simRate)));
    const auto epochSteps = static_cast<std::uint32_t>(std::max(1L, std::lround(kExciteNoisePeriod * g.simRate)));
    const std::uint32_t epoch = stepIndex / epochSteps;
    const float tau = g.refractoryTime;
    const float rDecay = std::exp(-dt / tau);
    const float uDecay = std::exp(-dt / g.excitationDecay);
    const float eGain = 1.0f - std::exp(-dt / g.energyTime);
    const float wGain = 1.0f - std::exp(-dt / g.wakeTime);
    const float riseTime = 1.0f / g.riseRate;
    const float invSpeed = 1.0f / g.waveSpeed;
    const glm::vec3 h = g.cellSize();
    const int nx = g.resolution.x;
    const int nz = g.resolution.z;
    const GridField previous = g;
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            const std::size_t base = g.index(i, 0, k);
            const float u = previous.data[base];
            const float r = previous.data[base + 1];
            const float e = previous.data[base + 2];
            const float w = previous.data[base + 3];
            const glm::vec3 centre = g.cellCenter(i, 0, k);
            const float stimulus =
                stimulated ? g.injectRate * std::max(0.0f, spatial::sampleScalar(*inject, centre, time, set)) : 0.0f;
            const float conduct =
                conducted ? std::max(0.0f, spatial::sampleScalar(*conductivity, centre, time, set)) : 1.0f;
            const std::uint32_t cell = static_cast<std::uint32_t>(k * nx + i);
            const float jitter =
                1.0f + g.noise * (simUnit(simHash(cell, epoch, g.seed)) * 2.0f - 1.0f);
            // The newest front that reaches this cell in the window [0, 2 dt): how long ago it arrived.
            float best = -1.0f;
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const int d2 = dx * dx + dz * dz;
                    if (d2 == 0 || d2 > 5) {
                        continue;
                    }
                    int ii = i + dx;
                    int kk = k + dz;
                    if (g.wrap == GridWrap::Wrap) {
                        ii = wrapIndex(ii, nx, GridWrap::Wrap);
                        kk = wrapIndex(kk, nz, GridWrap::Wrap);
                    } else if (ii < 0 || ii >= nx || kk < 0 || kk >= nz) {
                        continue;
                    }
                    const float rj = previous.data[g.index(ii, 0, kk) + 1];
                    if (rj <= 0.0f) {
                        continue;
                    }
                    const float ageJ = -tau * std::log(rj);
                    const float distance = std::sqrt(static_cast<float>(dx * dx) * h.x * h.x +
                                                     static_cast<float>(dz * dz) * h.z * h.z);
                    const float arrived = ageJ + dt - distance * invSpeed * jitter;
                    if (arrived >= 0.0f && arrived < 2.0f * dt) {
                        best = std::max(best, arrived);
                    }
                }
            }
            const float age = r > 0.0f ? -tau * std::log(r) : 1e30f;
            const float ageNext = age + dt;
            const bool front = best >= 0.0f && ageNext > best + kExciteSameFront;
            const float drive = stimulus + (front ? g.coupling * conduct : 0.0f);
            const bool fire = drive > g.threshold * (1.0f + g.refractoryStrength * r);
            float rNew = r * rDecay;
            float ageNow = ageNext;
            if (fire) {
                ageNow = front ? best : 0.0f;
                rNew = std::exp(-ageNow / tau);
            }
            float uNew = u * uDecay;
            if (ageNow - dt < riseTime) {
                uNew = std::max(u, std::min(1.0f, g.riseRate * ageNow));
            }
            if (g.ceiling > 0.0f) {
                uNew = std::min(uNew, g.ceiling);
            }
            const float eNew = e + (uNew - e) * eGain;
            const float wNew = w + (uNew - w) * wGain;
            g.data[base] = flushed(uNew);
            g.data[base + 1] = flushed(rNew);
            g.data[base + 2] = flushed(eNew);
            g.data[base + 3] = flushed(wNew);
        }
    }
}

} // namespace

void GridField::step(float dt, double time, const FieldSet* set) {
    if (!allocated() || dt <= 0.0f || mode == GridMode::Agents) {
        return; // ADR-1120: agents run on the GPU only
    }
    if (mode == GridMode::Excitable) {
        stepExcitable(*this, dt, time, set);
        return;
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
    if (mode == GridMode::Excitable) {
        if (resolution.y != 1 || resolution.x > kMaxAgentGridResolution || resolution.z > kMaxAgentGridResolution) {
            return fail("grid '{}': an excitable grid is a plane: resolution [x, 1, z] with x, z <= {}", name,
                        kMaxAgentGridResolution);
        }
        if (!(threshold > 0.0f) || coupling < 0.0f || refractoryStrength < 0.0f) {
            return fail("grid '{}': threshold must be > 0, coupling and refractoryStrength >= 0", name);
        }
        if (!(waveSpeed > 0.0f && riseRate > 0.0f && excitationDecay > 0.0f && refractoryTime > 0.0f &&
              energyTime > 0.0f && wakeTime > 0.0f)) {
            return fail("grid '{}': waveSpeed, riseRate, excitationDecay, refractoryTime, energyTime and wakeTime "
                        "must be > 0",
                        name);
        }
        if (!(noise >= 0.0f && noise <= 1.0f)) {
            return fail("grid '{}': noise must be in [0, 1]", name);
        }
        if (ceiling < 0.0f) {
            return fail("grid '{}': ceiling must be >= 0", name);
        }
        if (seedAmount != 0.0f) {
            // r is the medium's clock: noise in it would be fronts that fired at random moments.
            return fail("grid '{}': an excitable grid starts at rest; seedAmount must be 0", name);
        }
    } else if (mode == GridMode::Agents) {
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
    if (mode == GridMode::Excitable) {
        h.str(conductivityField);
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
    if (mode == GridMode::Excitable) { // ADR-1201: hashed only for excitable grids
        h.str(conductivityField);
        h.f32(threshold);
        h.f32(coupling);
        h.f32(waveSpeed);
        h.f32(riseRate);
        h.f32(excitationDecay);
        h.f32(refractoryTime);
        h.f32(refractoryStrength);
        h.f32(energyTime);
        h.f32(wakeTime);
        h.f32(noise);
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
    if (mode == GridMode::Excitable) { // ADR-1201: written only for excitable grids
        j["conductivityField"] = conductivityField;
        j["threshold"] = threshold;
        j["coupling"] = coupling;
        j["waveSpeed"] = waveSpeed;
        j["riseRate"] = riseRate;
        j["excitationDecay"] = excitationDecay;
        j["refractoryTime"] = refractoryTime;
        j["refractoryStrength"] = refractoryStrength;
        j["energyTime"] = energyTime;
        j["wakeTime"] = wakeTime;
        j["noise"] = noise;
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
    AVGEN_SPATIAL_READ(g.conductivityField, "conductivityField", detail::readString);
    AVGEN_SPATIAL_READ(g.threshold, "threshold", detail::readFloat);
    AVGEN_SPATIAL_READ(g.coupling, "coupling", detail::readFloat);
    AVGEN_SPATIAL_READ(g.waveSpeed, "waveSpeed", detail::readFloat);
    AVGEN_SPATIAL_READ(g.riseRate, "riseRate", detail::readFloat);
    AVGEN_SPATIAL_READ(g.excitationDecay, "excitationDecay", detail::readFloat);
    AVGEN_SPATIAL_READ(g.refractoryTime, "refractoryTime", detail::readFloat);
    AVGEN_SPATIAL_READ(g.refractoryStrength, "refractoryStrength", detail::readFloat);
    AVGEN_SPATIAL_READ(g.energyTime, "energyTime", detail::readFloat);
    AVGEN_SPATIAL_READ(g.wakeTime, "wakeTime", detail::readFloat);
    AVGEN_SPATIAL_READ(g.noise, "noise", detail::readFloat);
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
