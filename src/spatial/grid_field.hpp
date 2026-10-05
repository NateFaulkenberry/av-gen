#pragma once

// Simulated grid fields (ADR-032): a 3D scalar / vector / reaction-diffusion grid that is
// stepped with a fixed sub-step and fixed iteration counts, and sampled through the ordinary
// field interface (`FieldKind::Grid`, `FieldSpec::reference` = the grid's name, resolved through
// `FieldSet::grids`). The GPU runs the same kernels in `shaders/simulate.wgsl` over the shared
// grid table (`rendering::Simulation`); the code below is the reference the tests compare with.
//
// Storage: cell (i, j, k) of a grid with `resolution` (nx, ny, nz) and `components()` floats per
// cell lives at `((k * ny + j) * nx + i) * components + c`, so a grid is one flat float range and
// several grids share one table (`gridTableOffsets`). Cell centres are
// `boundsMin + (i + 0.5) / nx * (boundsMax - boundsMin)`; sampling is trilinear over the cell
// centres with `wrap` deciding what happens outside.
//
// One step, in this order (`step()`), all gather-only (no scatter, no atomics):
//   1. inject     value += injectRate * dt * max(0, injectField(cellCentre))   (Rd: into B)
//   2. advect     semi-Lagrangian back-trace by advect * velocityField(p) * dt, trilinear gather
//   3. diffuse    `diffuseIterations` Jacobi sweeps of (x0 + a * sum(6 neighbours)) / (1 + 6a)
//                 with a = diffusion * dt
//   4. dissipate  value *= max(0, 1 - dissipation * dt)
//   Rd mode replaces 3/4 with a Gray-Scott explicit Euler step (feed / kill, diffusionA/B).
//
// Determinism: `dt` is always `1 / simRate`; the number of steps a frame takes is derived from
// the render time, never from the wall clock (see rendering::Simulation).

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::spatial {

struct FieldSet;

// What a cell holds. Scalar: 1 float. Vector: 4 floats (xyz + pad, so the GPU reads are aligned).
// ReactionDiffusion: 2 floats (A, B); sampled as a scalar it reports B, the pattern channel.
// Agents (ADR-1120): 4 floats, the trail of species 0..2 and their sum. A population of `agentCount`
// agents lives on the grid's XZ plane (resolution.y must be 1): each step every agent senses the
// trail ahead (its own species attracts, the others repel by `repel`), turns towards the stronger
// side, moves and deposits into its species' channel with a u32 fixed-point atomic add (order-free,
// so bit-exact); then the trail blurs by `diffusion` and fades by `dissipation`. GPU only: the CPU
// `step()` does nothing for it (a chaotic population cannot be mirrored to the last bit), and it is
// sampled as a vector (the three trails), so a scalar read is the trails' length.
enum class GridMode : std::uint8_t { Scalar, Vector, ReactionDiffusion, Agents };
[[nodiscard]] const char* gridModeName(GridMode mode);
[[nodiscard]] std::optional<GridMode> gridModeFromName(std::string_view name);

enum class GridWrap : std::uint8_t { Clamp, Wrap };
[[nodiscard]] const char* gridWrapName(GridWrap wrap);
[[nodiscard]] std::optional<GridWrap> gridWrapFromName(std::string_view name);

constexpr int kMaxGridResolution = 128;
// ADR-1120: an Agents grid is a plane, and may be finer on x and z.
constexpr int kMaxAgentGridResolution = 1024;
constexpr int kMaxAgents = 4 * 1024 * 1024;
// Total floats every grid of a scene may occupy together (16 MB: one 1024 x 1 x 1024 agents trail,
// or two 128^3 scalar grids; ADR-1120 doubled it from 8 MB).
constexpr std::size_t kMaxGridTableFloats = 4u * 1024u * 1024u;

struct GridField {
    std::string name = "grid";
    bool enabled = true;
    GridMode mode = GridMode::Scalar;
    GridWrap wrap = GridWrap::Clamp;
    glm::ivec3 resolution{32, 32, 32};
    glm::vec3 boundsMin{-8.0f};
    glm::vec3 boundsMax{8.0f};
    // Simulation inputs (names of other fields in the same FieldSet; empty = none).
    std::string injectField;   // scalar field added every step
    std::string velocityField; // vector field the grid is advected by
    float injectRate = 1.0f;
    float advect = 1.0f;      // multiplier on the velocity field
    float diffusion = 0.0f;   // Jacobi diffusion coefficient (cell units per second)
    int diffuseIterations = 4;
    float dissipation = 0.0f; // fraction lost per second
    // Gray-Scott (ReactionDiffusion mode)
    float feed = 0.037f;
    float kill = 0.06f;
    float diffusionA = 1.0f;
    float diffusionB = 0.5f;
    // Stepping
    float simRate = 60.0f; // fixed sub-steps per second
    int maxSubSteps = 4;   // most sub-steps one frame may take (a stall does not explode)
    std::uint32_t seed = 11;
    float seedAmount = 0.0f; // amplitude of the initial fbm3 noise in the grid
    // Agents (ADR-1120). Distances are in cells of the XZ plane.
    int agentCount = 0;
    int species = 3;              // 1..3, one trail channel each
    float sensorAngle = 0.45f;    // radians either side of the heading
    float sensorDistance = 9.0f;  // cells
    float turnAngle = 0.35f;      // radians per step
    float stepSize = 1.0f;        // cells per step
    float depositAmount = 1.0f;   // trail added per agent per step (x the deposit field, when set)
    float repel = 0.6f;           // how much the other species' trails count against a direction
    std::string depositField;     // a scalar field the deposit is multiplied by (audio, typically); empty = 1
    // Checkpoints for an exact seek (ADR-1119): one every this many seconds of steps; 0 = none.
    float checkpointInterval = 5.0f;

    // Cell values, `components()` floats per cell; empty until reset() (the GPU keeps its own
    // copy in the shared table and never fills this).
    std::vector<float> data;

    [[nodiscard]] int components() const;
    [[nodiscard]] std::size_t cellCount() const;
    [[nodiscard]] std::size_t floatCount() const { return cellCount() * static_cast<std::size_t>(components()); }
    [[nodiscard]] glm::vec3 cellSize() const;
    [[nodiscard]] glm::vec3 cellCenter(int i, int j, int k) const;
    [[nodiscard]] std::size_t index(int i, int j, int k) const;

    // Fills `data` with the initial state: zero (Rd: A = 1, B = 0) plus `seedAmount` of fbm3
    // noise, deterministic in `seed`. Idempotent.
    void reset();
    [[nodiscard]] bool allocated() const { return data.size() == floatCount() && !data.empty(); }

    // Trilinear sample of the grid at a point in the grid's own space (the field's local frame).
    [[nodiscard]] float sampleScalar(const glm::vec3& p) const;   // Rd: the B channel
    [[nodiscard]] glm::vec3 sampleVector(const glm::vec3& p) const;
    // Raw component c of the cell, with the wrap rule applied to the indices.
    [[nodiscard]] float at(int i, int j, int k, int c) const;

    // One sub-step of dt = 1 / simRate seconds at `time` (seconds, for the input fields).
    // `set` resolves injectField / velocityField (null = no inputs).
    void step(float dt, double time, const FieldSet* set = nullptr);

    [[nodiscard]] bool agents() const { return mode == GridMode::Agents; }
    [[nodiscard]] Result<void> validate() const;
    // ADR-1122: what the state's shape and seed depend on -- mode, wrap, resolution, bounds, the sim rate, the
    // seed, the agents' count and species, and the names of the fields it reads. A change here re-seeds the grid
    // (rendering::Simulation compares it); every other setting is behaviour, a coefficient the next step reads,
    // and changing it changes only what the grid does from then on (scene/grid_params.hpp).
    [[nodiscard]] std::uint64_t layoutHash() const;
    [[nodiscard]] std::uint64_t structuralHash() const; // settings only, never the cell values
    [[nodiscard]] nlohmann::json toJson() const;        // settings only
    static Result<GridField> fromJson(const nlohmann::json& j);
};

// Offset (in floats) of grid `i` in a table holding `grids` back to back, in list order. The GPU
// table (rendering::Simulation) and spatial::packField both use this, so a packed field record
// points at the same range the simulation writes.
[[nodiscard]] std::size_t gridTableOffset(const std::vector<GridField>& grids, std::size_t index);
[[nodiscard]] std::size_t gridTableFloats(const std::vector<GridField>& grids);

} // namespace avgen::spatial
