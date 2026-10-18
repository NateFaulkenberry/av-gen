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
// Excitable (ADR-1201): 4 floats, an excitable medium on the XZ plane (resolution.y must be 1):
//   u  excitation (0..1), the visible activation of a front
//   r  refractory (0..1), 1 at the moment a cell fires, then exp(-age / refractoryTime); it is also the
//      medium's clock: a neighbour's age since it fired is -refractoryTime * ln(r)
//   e  energy, u low-passed with energyTime
//   w  wake, u low-passed with wakeTime (tens of seconds)
// A cell fires when stimulus + coupling * conductivity * front > threshold * (1 + refractoryStrength * r),
// where `front` is 1 when a neighbour's front reaches it this step: neighbour j (within a 5 x 5 disk) fired
// `age_j` ago, its front arrives at distance d after d / waveSpeed seconds. The fire time is kept to the
// sub-step (r is set from the exact arrival), so the wave speed is waveSpeed in metres per second whatever
// the cell size or simRate (stepStencil in grid_field.cpp; docs/decisions/ADR-1201). Sampled as a scalar
// it reads u; a Grid field's `channel` reads any of the four.
// ADR-1407: Bake -- no simulation: every frame the grid is the scalar field `injectField` sampled at its cell centres
// (a plane, [x, 1, z] up to kMaxAgentGridResolution; ADR-1440: or a volume, every axis up to kMaxGridResolution), at
// the frame's own clock. A field too costly to evaluate per vertex (and per vertex again for a
// finite-difference normal) is evaluated once per cell instead, and everything that reads it samples the grid. Stateless,
// so a seek lands where a play does with no catch-up and nothing to checkpoint.
// ADR-1450: Oscillator -- a plane [x, 1, z] of coupled phase oscillators (Kuramoto-Sakaguchi), 4 floats per cell:
//   cos(theta), sin(theta)   the phase, as a unit vector so that every reader interpolates it smoothly
//   R                        the local order parameter (0 = neighbours disagree, 1 = in step), low-passed by persistence
//   activity                 strain, 4 (1 - mean cos(theta_j - theta_i)), plus kicks; low-passed by persistence
// Each sub-step is a kick pass (injection kicks, the frequency field's phase advance, phase noise) then `iterations`
// coupling passes: dtheta/dt = 2 pi omega_i + K m_i sum_j w_ij sin(theta_j - theta_i - phaseLag) / sum_j w_ij, over a
// Gaussian disk of `couplingRadius` cells plus `links` hashed long-range partners. Sampled as a scalar it reads cos(theta).
// ADR-1451: a ReactionDiffusion grid may also be a plane [x, 1, z] up to kMaxAgentGridResolution (isotropic 9-point
// Laplacian), may step `iterations` times a sub-step at `speed`, and may blend its (feed, kill) toward (feedB, killB) by a
// regime field.
// ADR-1452: a Scalar or Vector grid with resolution.y == 1 is a plane, up to kMaxAgentGridResolution on x and z (a dye
// or a wake that needs detail across a surface rather than depth).
enum class GridMode : std::uint8_t { Scalar, Vector, ReactionDiffusion, Agents, Excitable, Bake, Oscillator };
[[nodiscard]] const char* gridModeName(GridMode mode);
[[nodiscard]] std::optional<GridMode> gridModeFromName(std::string_view name);

enum class GridWrap : std::uint8_t { Clamp, Wrap };
[[nodiscard]] const char* gridWrapName(GridWrap wrap);
[[nodiscard]] std::optional<GridWrap> gridWrapFromName(std::string_view name);

constexpr int kMaxGridResolution = 128;
// ADR-1120: an Agents grid is a plane, and may be finer on x and z. So is an Excitable grid (ADR-1201).
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
    // ADR-1163: a scalar grid's concentration saturates here (injection cannot push a cell past it; advection and
    // diffusion only average, so the grid stays at or below it). 0 = unbounded, as before.
    float ceiling = 0.0f;
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
    // Excitable (ADR-1201). The stimulus is injectRate * max(0, injectField) (a level, not a rate).
    std::string conductivityField;  // a scalar field the front's drive is multiplied by per cell; empty = 1
    float threshold = 0.5f;         // the drive a rested cell needs to fire
    float coupling = 1.0f;          // the drive a passing front gives (x conductivity)
    float waveSpeed = 4.0f;         // metres per second a front travels
    float riseRate = 8.0f;          // 1/s: u climbs to 1 in 1 / riseRate seconds after firing
    float excitationDecay = 0.6f;   // seconds, u's decay time constant once risen
    float refractoryTime = 1.5f;    // seconds, r's decay time constant (and the clock's)
    float refractoryStrength = 4.0f; // the threshold is threshold * (1 + refractoryStrength * r)
    float energyTime = 3.0f;        // seconds, e's time constant
    float wakeTime = 30.0f;         // seconds, w's time constant
    float noise = 0.0f;             // 0..1: each cell's front delay jitters by +-noise, re-drawn every 0.25 s
    // ADR-1450/1451: reaction-diffusion and oscillator grids. Each sub-step advances speed / simRate seconds of the
    // model's own time in `iterations` explicit steps (defaults: one step of 1 / simRate, as before).
    float speed = 1.0f;
    int iterations = 1;
    // ADR-1451, reaction-diffusion: a scalar field (0..1) that blends each cell's (feed, kill) toward (feedB, killB).
    std::string regimeField;
    float feedB = 0.037f;
    float killB = 0.06f;
    // ADR-1450, oscillator. Also read: coupling (K, radians per second), conductivityField (a scalar field multiplying
    // K per cell: waveguides, regions), injectField x injectRate (a phase kick, radians per second per unit), noise
    // (phase noise, radians per sqrt(second)) and seedAmount (0 = every phase 0, 1 = uniformly random phases).
    float frequency = 0.25f;     // Hz, the mean natural frequency
    float spread = 0.05f;        // Hz, each cell's natural frequency lies within frequency +- spread (triangular)
    float phaseLag = 0.0f;       // radians, the Sakaguchi lag (travelling waves and spirals as it nears pi / 2)
    int couplingRadius = 1;      // cells, 1..4: the Gaussian disk each cell couples to
    int links = 0;               // 0..4 long-range partners per cell, hashed from the seed (the hybrid network)
    float linkRange = 32.0f;     // cells: how far a partner may be
    float linkWeight = 1.0f;     // a partner's weight, relative to a nearest neighbour's
    float populationMix = 0.0f;  // 0..1: the share of cells (hashed) in a second population (0 = one population)
    float detune = 0.0f;         // Hz added to the second population's natural frequency
    float crossCoupling = 1.0f;  // coupling between the populations, x coupling (the layered network)
    float persistence = 1.0f;    // seconds: the time constant of R and activity
    std::string frequencyField;  // a scalar field (Hz) added to each cell's natural frequency, in the kick pass
    // Checkpoints for an exact seek (ADR-1119): one every this many seconds of steps; 0 = none.
    float checkpointInterval = 5.0f;

    // Cell values, `components()` floats per cell; empty until reset() (the GPU keeps its own
    // copy in the shared table and never fills this).
    // ADR-1282: authored starting values, components() floats per cell in the storage order above (JSON
    // "initial": [...]). Empty = zero plus `seedAmount` noise, as before. Scalar and vector grids, and (ADR-1453) reaction-
// diffusion grids (A, B per cell). A reset, and a seek with no checkpoint to restore, return to these values exactly. A grid
    // with no injection, diffusion or dissipation keeps them for ever: a static, authored field (a glow map).
    std::vector<float> initial;
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
    // ADR-1201: trilinear sample of one component (0 outside [0, components())).
    [[nodiscard]] float sampleChannel(const glm::vec3& p, int channel) const;
    // Raw component c of the cell, with the wrap rule applied to the indices.
    [[nodiscard]] float at(int i, int j, int k, int c) const;

    // One sub-step of dt = 1 / simRate seconds at `time` (seconds, for the input fields).
    // `set` resolves injectField / velocityField (null = no inputs). An Excitable grid's step index (its
    // noise) is round(time * simRate), which is the GPU's when `time` is the step's own second (ADR-1119).
    void step(float dt, double time, const FieldSet* set = nullptr);

    [[nodiscard]] bool agents() const { return mode == GridMode::Agents; }
    [[nodiscard]] bool excitable() const { return mode == GridMode::Excitable; }
    [[nodiscard]] bool bake() const { return mode == GridMode::Bake; } // ADR-1407
    [[nodiscard]] bool oscillator() const { return mode == GridMode::Oscillator; } // ADR-1450
    // ADR-1201: a plane grid (agents, excitable, oscillator): resolution [x, 1, z] up to kMaxAgentGridResolution.
    [[nodiscard]] bool plane() const { return agents() || excitable() || oscillator(); }
    // ADR-1451: a reaction-diffusion grid one cell thick, stepped with the isotropic 9-point Laplacian.
    [[nodiscard]] bool reactionPlane() const { return mode == GridMode::ReactionDiffusion && resolution.y == 1; }
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
