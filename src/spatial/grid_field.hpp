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
enum class GridMode : std::uint8_t { Scalar, Vector, ReactionDiffusion };
[[nodiscard]] const char* gridModeName(GridMode mode);
[[nodiscard]] std::optional<GridMode> gridModeFromName(std::string_view name);

enum class GridWrap : std::uint8_t { Clamp, Wrap };
[[nodiscard]] const char* gridWrapName(GridWrap wrap);
[[nodiscard]] std::optional<GridWrap> gridWrapFromName(std::string_view name);

constexpr int kMaxGridResolution = 128;
// Total floats every grid of a scene may occupy together (8 MB; one 128^3 scalar grid).
constexpr std::size_t kMaxGridTableFloats = 2u * 1024u * 1024u;

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

    [[nodiscard]] Result<void> validate() const;
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
