#pragma once

// SHELL: analytic proxy shells (Effect Library Wave 3, docs/design/effect-library/
// shared-infrastructure.md "SHELL", rendering-architecture.md §4-5).
//
// A shell is a canonical mesh -- a unit sphere, box, quad, disc, cylinder or cone, made once from
// `scene/mesh_generators` -- placed by a transform and shaded by an effect program: a plasma orb's
// emission march, a shield's rim, pattern, impact rings and ground line, a barrier's scrolling
// pattern. Every live shell is one 192-byte record (a transform and eight vec4 parameters) in ONE
// storage buffer; the renderer (rendering/shell_renderer) issues one instanced draw per run of
// records sharing a mesh and a shading kind. There is one PIPELINE per shading kind rather than a
// switch in one shader: ADR-118 measured what a lane-varying branch over programs costs.
//
// **Where it draws.** Pass 1's blended section, beside the particles and the ribbons: after the
// opaque geometry, depth-tested against it and never writing it, into all five colour targets (HDR
// and emission blended, velocity by coverage, normal and ids masked off).
//
// **Depth.** Intersection glow (a bright line where a shield cuts the ground) and depth fade (a
// plasma orb clamped against what stands inside it) read the SEPARATE linear-depth target the depth
// prepass resolves -- a pass may not sample the depth attachment it is drawing into. The prepass
// does not run on every frame (it runs for AO, contact shadows, the shadow mask and water), and
// without it those terms are off in the shader. The renderer reports which it was
// (`reportShellLinearDepth`); the builder turns an instance that wants depth into `Partial` with
// a reason when the last drawn frame had none, so the panel says why the ground line is missing
// rather than leaving the author to guess.
//
// **Capacity.** `kMaxShells` records a frame. An instance that finds the frame full is `Dropped`,
// with a reason naming the budget (ADR-703).
//
// **Light.** A shell type may ask LIGHTMOD's pool for a point light (a plasma orb lights what is
// around it). The Material stage's lanes (Glow spills) resolve the pool first; shells take what is
// left, ranked among themselves by the pool's own rule, and a shell whose light did not fit is
// `Partial` -- it still draws.
//
// **Gate.** No live shell: no record, no draw, no upload, no bind, no light -- the frame is
// byte-identical to one rendered by a build without this primitive (tests/rendering/
// test_shell_gpu.cpp holds it).

#include "world/effects/effect_lights.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace avgen::world {

struct EffectInstance;
struct EffectContext;
enum class EffectKind : std::uint8_t;
enum class EffectStatus : std::uint8_t;

inline constexpr std::uint32_t kMaxShells = 128;
inline constexpr std::uint32_t kShellParams = 8;
// Side data a record points into (hits, revealer positions): eight vec4 per shell at most.
inline constexpr std::uint32_t kMaxShellExtraPerShell = 8;
inline constexpr std::uint32_t kMaxShellExtra = kMaxShells * kMaxShellExtraPerShell;

// The canonical meshes, each fitting the unit cube [-1, 1]^3 of its own space:
//   Sphere    radius 1 (an ellipsoid is a sphere with a non-uniform scale)
//   Box       half-extent 1
//   Quad      the XY plane, [-1, 1]^2, facing +Z (a wall)
//   Disc      the XZ plane, radius 1, facing +Y
//   Cylinder  an open tube of radius 1 about +Y, y in [-1, 1], normals outward
//   Cone      an open cone about +Y, apex at y = +1, base radius 1 at y = -1
enum class ShellMesh : std::uint8_t { Sphere, Box, Quad, Disc, Cylinder, Cone };
inline constexpr std::size_t kShellMeshCount = 6;

// One pipeline each (ADR-118).
enum class ShellShading : std::uint8_t {
    Plasma,  // an emission-only march through the shell's interior (front faces)
    Shield,  // rim, cell pattern, impact rings, ground line; both faces (the far side dimmer)
    Barrier, // a scrolling pattern revealed near entities and the camera, and where it meets the ground
    // Phase 2 (shaders/shell_fx.wgsl). The last three are BLENDED OVER the frame (premultiplied alpha)
    // rather than added to it, and the last two write depth: see rendering/shell_renderer.cpp.
    Beam,   // a cone of lit air integrated along the view ray inside a box proxy (Light Beam)
    Glare,  // a camera-facing glare disc and ring, occlusion-faded, not depth-tested (Halo, optical)
    Ring,   // a glowing torus marched inside a box proxy (Halo, ring)
    Bubble, // a thin-film sphere: interference colour, reflection, Fresnel alpha, a pop (Bubble)
    Portal, // an opening on a disc: swirling interior and a noisy rim; writes depth (Portal)
    Tear,   // a jagged crack on a quad: void interior and white-hot edges; writes depth (Reality Tear)
};
inline constexpr std::size_t kShellShadingCount = 9;
[[nodiscard]] const char* shellShadingName(ShellShading s);

// One shell, 192 bytes. Mirrors `ShellRecord` in shaders/shell.wgsl.
//   model      mesh space -> world. Its three axis columns must be orthogonal (a rotation times a
//              per-axis scale): the shader inverts it by projection onto them, and derives the
//              normal matrix the same way, so no inverse is uploaded.
//   params[1]  COMMON to every shading kind: x = the instance's seed, y = its animation clock
//              (transport seconds, already multiplied by any speed), z = the first `extra` entry,
//              w = how many (the sink writes z and w).
//   the rest   the shading kind's own; see each kind's file and shell.wgsl.
struct ShellInstance {
    glm::mat4 model{1.0f};
    std::array<glm::vec4, kShellParams> params{};
};
static_assert(sizeof(ShellInstance) == 192);

struct ShellBatch {
    ShellShading shading = ShellShading::Shield;
    ShellMesh mesh = ShellMesh::Sphere;
    std::uint32_t first = 0; // into `instances`
    std::uint32_t count = 0;
};

// The frame block (`scene::Scene::shells`). Plain data; the renderer reads it.
struct ShellFrame {
    std::vector<ShellInstance> instances; // sorted by (shading, mesh), stack order within
    std::vector<glm::vec4> extra;         // side data the records point into
    std::vector<ShellBatch> batches;      // one per (shading, mesh) run: one instanced draw each
    std::uint32_t dropped = 0;            // instances refused this frame for want of room
    [[nodiscard]] bool empty() const { return instances.empty(); }
    // Empties the frame but keeps every vector's storage.
    void clear();
};

// What a producer writes into. Owns no storage: it appends to the frame, keeping the budget.
class ShellSink {
public:
    // False, having written nothing, when the frame holds `kMaxShells` already. `extra` (at most
    // `kMaxShellExtraPerShell`) is copied beside the record, which is pointed at it through
    // params[1].zw.
    bool append(ShellShading shading, ShellMesh mesh, const ShellInstance& shell,
                std::span<const glm::vec4> extra = {});
    [[nodiscard]] std::uint32_t remaining() const;
    // LIGHTMOD: a point light this instance would like. Ranked and granted by the builder after
    // every instance has asked.
    void requestLight(const EffectLight& light);
    // The instance's shading reads the linear depth (intersection glow, depth fade), so a frame
    // without the prepass draws it without those terms and the builder says so.
    void usesDepth() { usesDepth_ = true; }

    // What the builder reads back after `emit`.
    [[nodiscard]] bool wantsDepth() const { return usesDepth_; }
    [[nodiscard]] bool wantsLight() const { return wantsLight_; }
    [[nodiscard]] const EffectLight& light() const { return light_; }

    // `keys` is the builder's scratch (one sort key per appended record), kept across frames.
    ShellSink(ShellFrame& frame, std::vector<std::uint16_t>& keys) : frame_(frame), keys_(keys) {}

private:
    ShellFrame& frame_;
    std::vector<std::uint16_t>& keys_;
    bool usesDepth_ = false;
    bool wantsLight_ = false;
    EffectLight light_{};
};

// ---- per-type producers --------------------------------------------------------------------------
//
// The builder is type-agnostic: a type in the `Shell` bucket registers how it turns one live
// instance into shells, from its own file, when its schema is built -- so this file names no type.
// `emit` returns the status (`Drawn`, `Dormant`, `Dropped`, `Partial`) and may write a reason.
struct ShellProducer {
    EffectStatus (*emit)(const EffectInstance&, const EffectContext&, ShellSink&, std::string& reason) = nullptr;
};
void registerShellProducer(EffectKind kind, ShellProducer producer);
[[nodiscard]] const ShellProducer* shellProducer(EffectKind kind);

// The Shell stage's builder (ADR-703's convention, plus the light pool it appends to). Clears
// `out`, walks `order`, writes the status (and a reason for Dropped and Partial) of every instance
// whose type is in the `Shell` bucket, then appends the granted core lights to `lights` after
// whatever the lanes put there. Allocation-free once the frame has grown to its budget.
void buildShellFrame(std::span<const EffectInstance> effects, const EffectContext& ctx, ShellFrame& out,
                     EffectLightFrame& lights, std::span<const std::uint32_t> order,
                     std::span<EffectStatus> status, std::span<std::string> reasons);

// ---- the renderer's report ---------------------------------------------------------------------
//
// Whether the last frame that drew shells had the linear depth to shade them with. Written by the
// renderer when it draws shells, read by the builder for the next frame's reasons -- a status
// sentence one frame late, never a pixel: the shader reads the frame's own flag.
enum class ShellDepthState : std::uint8_t { Unknown, Available, Missing };
void reportShellLinearDepth(bool available);
[[nodiscard]] ShellDepthState shellLinearDepthState();

// A deterministic unit vector from a seed and an event time: where a Shield's `k`-th hit lands.
// The same (seed, time) is the same direction on every run, in play and after a seek.
[[nodiscard]] glm::vec3 shellHitDirection(float seed, double eventSeconds);

} // namespace avgen::world
