#pragma once

// Draws a scene::Scene with WebGPU (ADR-001). Frame = ordered passes:
//
//   1. compute: simulation, particles, procedural effectors/culling, SDF packing
//   2. cluster build (ADR-033): the 16x8x24 froxel grid of light indices
//   3. shadow depth passes (ADR-034): one per cascade / spot map into the shadow atlas
//   4. background: HDR colour cleared, background user-shader layers
//   5. depth prepass: opaque rasterised geometry and raymarched SDFs into the scene depth
//   6. linear depth: the depth buffer resolved to R32F for AO and contact shadows
//   7. GTAO (ADR-034): half-resolution horizon occlusion + bent normal, temporally reprojected
//   8. scene -> HDR + auxiliary targets (ADR-035): opaque PBR, procedural instances, SDF meshes,
//      the SDF raymarch pass, skybox, additive grid, particles, blended PBR
//   9. volumetric atmosphere -> HDR (ADR-032), debug geometry, post chain, tonemap
//
// The scene pass writes five colour targets at once: HDR radiance, normal + roughness, velocity,
// emission and identifiers (ADR-035). The caller owns the command encoder so it can append passes
// (UI) and submit; renderToImage() wraps that for tests and offline output.

#include "core/error.hpp"
#include "core/time.hpp"
#include "core/wind.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/readback.hpp"
#include "gpu/render_target.hpp"
#include "gpu/texture.hpp"
#include "gpu/transient_pool.hpp"
#include "rendering/renderer_diagnostics.hpp"
#include "rendering/ao_renderer.hpp"
#include "rendering/shadow_mask_renderer.hpp"
#include "rendering/water_renderer.hpp"
#include "rendering/ribbon_renderer.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/frame_overlay.hpp"
#include "rendering/light_data.hpp"
#include "rendering/material_programs.hpp"
#include "rendering/particle_renderer.hpp"
#include "rendering/post_processor.hpp"
#include "rendering/temporal_effects.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/representation.hpp"
#include "rendering/render_stats.hpp"
#include "rendering/sdf_renderer.hpp"
#include "rendering/shader_layer.hpp"
#include "rendering/shadow_renderer.hpp"
#include "rendering/skinning.hpp"
#include "rendering/simulation.hpp"
#include "rendering/spline_buffers.hpp"
#include "rendering/volume_renderer.hpp"
#include "rendering/debug_draw.hpp"
#include "rendering/distortion_renderer.hpp"
#include "scene/rebuild_deferral.hpp"
#include "scene/scene.hpp"
#include "shaders/shader_layers.hpp"
#include "world/atmospherics.hpp"
#include "world/wave_effect.hpp"

#include "analysis/analyzer.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

class EnvironmentProcessor;

// Optional per-frame inputs for user shader layers.
struct ShaderFrameInputs {
    const shaders::ShaderLayerSet* layers = nullptr;
    const analysis::AnalysisFrame* frame = nullptr; // for the audio spectrum texture (may be null)
};

struct RenderStats {
    double gpuFrameMs = -1.0; // -1 when timestamp queries are unavailable
    std::uint32_t drawCalls = 0;       // draws recorded by the camera-side passes
    std::uint32_t indirectDraws = 0;   // of the frame's draws, those sourced from a GPU buffer
    std::uint32_t emptyDraws = 0;      // indirect draws whose instance count was last read as zero
    std::uint32_t skippedDraws = 0;    // draws not recorded because the level has been empty
    std::uint32_t computeDispatches = 0; // compute passes encoded this frame, over every subsystem
    std::uint32_t shadowDraws = 0;     // draws recorded across every shadow view
    std::uint32_t gpuPasses = 0;       // render + compute passes the timeline measured
    std::uint64_t visibleInstances = 0; // procedural instances that survived culling
    std::uint64_t culledInstances = 0;
    std::uint64_t lodCounts[4] = {0, 0, 0, 0};
    // ADR-351: what the entity LOD selector did this frame. Separate from `lodCounts`, which is the
    // *procedural scatter's* ladder: the two answer the same question about different populations,
    // and folding them together would make "the tree demoted" and "eighty thousand ferns demoted"
    // one number nobody could read. All zero in a scene with no chain, which is every scene that
    // does not ask for one.
    struct EntityLod {
        std::uint32_t drawables = 0;       // entities whose mesh carries a chain
        std::uint32_t demoted = 0;         // of those, drawn below LOD0
        std::uint32_t changed = 0;         // rung differs from last frame's
        std::uint32_t held = 0;            // the dead zone kept them where they were
        std::uint64_t sourceTriangles = 0; // what LOD0 would have submitted for them
        std::uint64_t drawnTriangles = 0;  // what the chosen rungs did
        std::uint32_t rungs[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        [[nodiscard]] float ratio() const {
            return sourceTriangles == 0
                       ? 1.0f
                       : static_cast<float>(drawnTriangles) / static_cast<float>(sourceTriangles);
        }
    };
    EntityLod entityLod{};
    // Triangles the frame submitted: `geometry.camera.triangles` -- entity meshes, procedural
    // instances at the LOD levels the cull pass chose, and meshed SDFs -- plus the one-triangle
    // fullscreen draws (the skybox and the tone map) this field has always counted. It used to
    // fold in
    // `ProceduralStats::logicalTriangles` -- source triangles times every instance record, before
    // LOD and before culling -- which made Glowmere report 15.1 M triangles for a frame that
    // submitted a fraction of that, and made every LOD or culling change invisible to the one
    // geometry number anybody reads (ADR-077). The pre-cull figure is still there, under a name
    // that says what it is: `geometry.logicalTriangles`.
    //
    // Particles and raymarched SDFs are not in it: they are billboards and fullscreen boxes whose
    // cost is fragments, not vertices, and "two triangles per raymarched object" says nothing true
    // about either. Their draws are still in `drawCalls`. `geometry` below excludes the fullscreen
    // triangles as well, for the same reason -- a geometry budget that moves when the resolution
    // changes is not a geometry budget -- so the two differ by however many fullscreen draws the
    // frame made, which is one or two.
    std::uint32_t triangles = 0;
    // ADR-077. The full geometry split -- what the world contains against what each class of pass
    // was handed -- and the CPU and state counters that go with it. `triangles` above is
    // `geometry.camera.triangles`, kept because the editor status line and the profile capture
    // read it by that name.
    GeometryCounters geometry;
    StateChangeCounters state;
    CpuFrameBreakdown cpu;
    // Entities selected as shadow casters this frame, before each cascade culls its own. Draws
    // are `shadowDraws`: one caster drawn into two cascades is one caster and two draws.
    std::uint32_t shadowCasters = 0;
    // Passes whose caller did not say whether they were render or compute, so `state.renderPasses`
    // and `state.computePasses` do not cover them. Non-zero means the pass split is a floor.
    std::uint32_t unclassifiedPasses = 0;
    std::uint32_t entities = 0;
    // Lights written into the *uniform fallback* array, which is `kMaxLights` = 8 long. It is not
    // the scene's light count and never was: Glowmere shades with 230 and this field reads 8,
    // because it is counting slots in an array whose size is the cap. `shadedLights` below is the
    // number the question "how many lights is this frame paying for" is actually asking about.
    std::uint32_t lights = 0;
    // Every enabled light the frame shaded with: directional (evaluated per fragment) plus local
    // (routed through the froxel grid). `clusteredLights` is the local half.
    std::uint32_t shadedLights = 0;
    std::uint32_t directionalLights = 0;
    std::uint32_t textures = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool ibl = false;
    ParticleStats particles;
    ProceduralStats procedural; // ADR-023; its draws/triangles are also folded into the totals
    SdfStats sdf;               // ADR-027; its draws/triangles are also folded into the totals
    VolumeStats volume;         // ADR-032; the raymarched atmosphere (steps 0 = off)
    ShadowStats shadows;        // ADR-034; cascades and spot maps rendered this frame
    AoStats ao;                 // ADR-034; ground-truth ambient occlusion
    ShadowMaskStats shadowMask; // ADR-087; the half-resolution directional shadow mask
    std::uint32_t clusteredLights = 0; // lights that went through the froxel grid (0 = fallback path)
    // ADR-114. How hard the froxel grid was worked this frame -- only when
    // `SceneRenderer::setClusterStatsEnabled(true)`, because it is a CPU replica of the cluster
    // compute pass and running it inside a measured frame perturbs the wall clock. `haveClusters`
    // is how a reader tells "not measured" from "an empty grid".
    ClusterOccupancy clusters;
    bool haveClusters = false;
    SimulationStats simulation; // ADR-032; the simulated grid fields stepped this frame
    SkinningStats skinning;     // ADR-086; the skinned rigs whose palettes reached the GPU
    PostStats post;
    TemporalStats temporal;     // ADR-410; ring occupancy, settling state, and what it costs
    std::uint32_t transientTextures = 0;
    std::uint32_t waves = 0; // ADR-207/702: surface-wave records in the frame block this frame
    std::uint32_t comets = 0;       // ADR-230: comets live in the frame block this frame
    std::uint32_t auroras = 0;      // ADR-230: auroras live in the frame block this frame
};

constexpr std::uint32_t kMaxLights = 8; // the uniform fallback path (ADR-033); clustered has no such limit

// Which auxiliary target the debug view displays over the frame (ADR-035).
// The auxiliary targets, shown one at a time full-screen before tone mapping (ADR-035), plus the
// depth readings Phase 4.4 asks for. `Depth` is the compressed display the engine has always had --
// an exponential ramp, legible across a whole scene and useless for reading a number off. The three
// after it are the ones a forensic question needs: metres against the far plane, where the depth
// buffer has silhouettes, and the selected object's own depth with everything else removed.
// Overdraw and FragmentDensity (ADR-115) are the fragment-density diagnostics: how many times each
// pixel was shaded, not what a visible surface looks like. They are backed by their own opt-in
// counting pass (overdraw_count.wgsl) rather than by a target every frame writes -- see that file's
// header for why a fragment shader that counts cannot be part of the normal path.
enum class AuxDebugView : std::uint8_t {
    None,
    Normal,
    Roughness,
    Velocity,
    Emission,
    Ids,
    Occlusion,
    Depth,
    LinearDepth,
    DepthEdges,
    ObjectDepth,
    Overdraw,
    FragmentDensity,
    // ADR-410. APPENDED, not inserted: `shaders/aux_debug.wgsl` dispatches on the raw enum
    // ordinal, so putting a new view anywhere but the end silently renumbers every mode after it.
    //
    // Unlike its neighbours this one is not an ADR-035 target -- it is a picture of the temporal
    // ring itself, drawn by `TemporalEffects::encodeDebugView` from a texture array the aux debug
    // bind group does not carry. It is in this enum anyway because this enum is how a debug view
    // is *named*, and a view with no name is reachable only by editing code.
    TemporalHistory,
};
[[nodiscard]] const char* auxDebugViewName(AuxDebugView view);

// GPU-side mirrors of the WGSL uniform structs in shaders/common.wgsl and tonemap.wgsl.
struct LightUniform {
    glm::vec4 positionType;
    glm::vec4 directionRange;
    glm::vec4 colorIntensity;
    glm::vec4 cone; // x = cos(outer), y = 1/(cos(inner)-cos(outer)), z = volumetric strength, w = 0
};
static_assert(sizeof(LightUniform) == 64);

// Mirrors `AuxDebugUniforms` in shaders/aux_debug.wgsl.
struct AuxDebugUniforms {
    glm::vec4 info;  // x = mode, y = scale, zw = target size
    glm::vec4 depth; // x = near, y = far, z = selected pick id (-1 for none), w = 0
};
static_assert(sizeof(AuxDebugUniforms) == 32);

struct FrameUniforms {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::mat4 prevViewProj;   // ADR-035: last frame's, for velocity and temporal reprojection
    glm::vec4 cameraPos;
    glm::vec4 cameraRight;    // xyz = camera right axis (world), for billboards
    glm::vec4 cameraUp;       // xyz = camera up axis (world)
    glm::vec4 cameraForward;  // xyz = camera view axis (world), w = 0; view depth for the froxel grid
    glm::vec4 params;
    glm::vec4 envParams;
    glm::vec4 skyParams;
    glm::vec4 skyExtra;       // ADR-036: x = 1 when the IBL is the procedural sky, y = draw it as
                              // the background; ADR-049: z = sky (background) intensity,
                              // w = how much of the sky reaches the bloom mask
    // Where the sky puts its sun or moon: xyz = the direction *towards* it, w = 1 when there is
    // one to draw. The background pass used to take this from `lights[0]`, which is whichever light
    // happened to be first in the scene and not necessarily the key -- so the disc it drew and the
    // one the sky itself contains ended up in two different places, and Glowmere showed two moons.
    glm::vec4 skySun{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 fogParams;      // rgb = fog colour, w = density (0 = off)
    glm::vec4 fogHeight;      // ADR-058: x = layer top (m), y = falloff per metre above it,
                              // z = how much of that layer the surface fog sees, w = 0
    glm::vec4 styledSky;      // ADR-058: rgb = styled ambient towards +Y, w = the AO floor
    glm::vec4 styledGround;   // ADR-058: rgb = styled ambient towards -Y, w = 0
    glm::vec4 audio;          // ADR-030 material inputs: rms, bass, mid, treble
    glm::vec4 audioBands;     // lowMid, highMid, spectral centroid, flux
    glm::vec4 beat;           // beat phase 0..1, pulse (1 - phase), onset strength, bar phase
    glm::vec4 clusterParams;  // xyz = froxel grid dimensions, w = 1 when the clustered path is on
    glm::vec4 clusterDepth;   // x = slice scale, y = slice bias, z = near, w = far
    glm::vec4 lightCounts;    // x = directional lights (always shaded), y = total lights, zw = 0
    glm::vec4 shadowParams;   // x = atlas resolution, y = PCF radius (texels), z = contact steps, w = contact length
    glm::vec4 aoParams;       // x = strength, y = 1 when AO is on, zw = the AO texture size
    glm::vec4 targetSize;     // x = width, y = height, z = 1 / width, w = 1 / height
    // ADR-087: x = 1 when the half-resolution shadow mask was built this frame, y = how many
    // leading directional lights it covers (0..3), zw = its size in texels.
    glm::vec4 shadowMaskParams;
    // ADR-133: material tiers. x = the tier every draw shades at (0 full, 1 reduced lights,
    // 2 flat); y = tier 1's local-light budget; z = tier 2's; w = 0. Frame-global rather than per
    // draw because ObjectUniforms has no free lane -- see ADR-135.
    glm::vec4 materialTier{0.0f};
    // ADR-055: the wind field, packed by wind::packWind. Frame-global because the air is; the
    // shadow views copy the whole block, so a swaying plant and its shadow cannot disagree.
    wind::WindUniforms wind;
    LightUniform lights[kMaxLights];
    // ADR-207/702: the surface waves this frame. Appended *after* `lights` so no offset above moved,
    // and frame-global for the same reason the wind is -- a phenomenon propagating through the
    // world is a property of the world, and the shadow views copy the whole block.
    // x = how many of `effects` are live; the rest of the vector is spare.
    glm::vec4 waveCount{0.0f};
    world::WaveGpu waves[world::kMaxGpuWaves];
    // ADR-230: the atmospheric effects this frame. Appended after the surface waves for the same
    // reason those were appended after `lights` -- no offset above moves -- and frame-global for the
    // same reason again: the sky is a property of the world, not of a draw.
    // x = live comets, y = live auroras, z = tail march samples (the §12 quality control), w = 0.
    glm::vec4 atmosCount{0.0f};
    world::CometGpu comets[world::kMaxGpuComets];
    world::AuroraGpu auroras[world::kMaxGpuAuroras];
    world::SkyGroundGpu skyGround;
    // ADR-345: the analytic sky's own parameters, so the background pass can evaluate it directly
    // instead of sampling whatever cube the IBL happens to be built from. Appended at the very end
    // for the same reason `atmosCount` was appended after the surface waves: no offset above moves,
    // so every other pass's view of this block is byte-identical to what it was.
    //
    // Before this, "which thing lights the scene" and "which thing is drawn behind it" were one
    // choice: with an environment map bound the skybox *is* that map's cube, and a scene wanting
    // HDRI lighting under a procedural sky had no way to ask for it. `skySunRadiance.w` is that
    // ask, and it is off unless a scene sets it.
    glm::vec4 skyZenithColor{0.0f};   // rgb = zenith, w = haze width
    glm::vec4 skyHorizonColor{0.0f};  // rgb = horizon, w = sun angular radius (radians)
    glm::vec4 skyGroundColor{0.0f};   // rgb = below the horizon, w = sun glow width
    glm::vec4 skySunRadiance{0.0f};   // rgb = sun/moon colour, w = 1 when the analytic sky is drawn
    // ADR-379: the cosmic vortex's light on the surfaces above it. Appended last for the reason
    // every block before it was: no offset above moves, so every other pass's view of this
    // structure is byte-identical. `w` of the second is the gate -- zero and the surface shader
    // returns before it reads the first.
    glm::vec4 vortexGlow{0.0f};       // xyz = mouth centre in world space, w = mouth radius
    glm::vec4 vortexGlowColor{0.0f};  // rgb = radiance, w = intensity (0 = no vortex)
    // ADR-568 (§7): the height layer's shape, for the surface fog's analytic integral. Appended
    // last for the reason every block before it was -- no offset above moves, so every other
    // pass's view of this structure is byte-identical and none of the `offsetof` assertions
    // below change. x = fogUpperDensity, y = fogHeightCurve, w = 0.
    // ADR-715: z = fogGroundFollow, forced to 0 when the scene has no terrain -- so a scene with no
    // ground to follow takes the flat branch and is the frame it always was.
    glm::vec4 fogShape{0.0f};
    // ADR-715: where the terrain height texture (group 0 binding 12) sits in the world, as
    // `world::TerrainGround::map0/map1`. Appended last, for the reason every block above was.
    glm::vec4 terrainMap0{0.0f}; // world origin xz, 1 / spacing xz
    glm::vec4 terrainMap1{0.0f}; // height scale, height offset, fade metres, 1 when there is a terrain
    // ADR-717: x = fogPooling, forced to 0 when the scene has no terrain; yzw = 0. The march reads
    // this lane too (volume.wgsl), so the surface fog and the march cannot be handed different
    // pooling. Appended last. MERGE NOTE: Effect Library Wave 2 appends starsA/B/C here as well --
    // keep both, in the same order here and in common.wgsl, and add both terms to the sum below.
    glm::vec4 fogPool{0.0f};
};
// 192 matrices + 368 of vec4 blocks + 64 wind + 512 lights + 16 + 8x144 surface waves. The middle
// term grew by one vec4 when `skySun` was added; this assert is what caught the WGSL side needing
// the same field in the same place, which is the whole reason it is written as a sum rather than a
// number.
static_assert(sizeof(FrameUniforms) == 192 + 384 + 64 + 512 + 16 + 144 * world::kMaxGpuWaves +
                                       16 + 160 * world::kMaxGpuComets + 224 * world::kMaxGpuAuroras + 48 +
                                       64 + // ADR-345: four vec4s of analytic sky
                                       32 + // ADR-379: two vec4s of vortex glow
                                       16 + // ADR-568: one vec4 of height-fog shape
                                       32 + // ADR-715: two vec4s of terrain height placement
                                       16); // ADR-717: one vec4 of fog pooling, appended last
static_assert(offsetof(FrameUniforms, viewProj) == 0);
static_assert(offsetof(FrameUniforms, invViewProj) == 64);
static_assert(offsetof(FrameUniforms, prevViewProj) == 128);
static_assert(offsetof(FrameUniforms, cameraPos) == 192);
// ADR-034 / the Shadow Lab: `ShadowRenderer::upload` overwrites these two lanes per shadow view so
// a camera-facing impostor faces the light it is being rasterised from. The offsets live there as
// named constants and are checked here, where the struct is.
static_assert(offsetof(FrameUniforms, cameraRight) == ShadowRenderer::kFrameCameraRightOffset);
static_assert(offsetof(FrameUniforms, cameraUp) == ShadowRenderer::kFrameCameraUpOffset);
static_assert(offsetof(FrameUniforms, params) == 256);
static_assert(offsetof(FrameUniforms, shadowMaskParams) == 544);
static_assert(offsetof(FrameUniforms, materialTier) == 560);
static_assert(offsetof(FrameUniforms, wind) == 576);
static_assert(offsetof(FrameUniforms, lights) == 640);
static_assert(offsetof(FrameUniforms, waveCount) == 1152);
static_assert(offsetof(FrameUniforms, waves) == 1168);
static_assert(offsetof(FrameUniforms, atmosCount) == 2320);
static_assert(offsetof(FrameUniforms, comets) == 2336);
static_assert(offsetof(FrameUniforms, auroras) == 3296);
static_assert(offsetof(FrameUniforms, skyGround) == 3744);

struct ObjectUniforms {
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::mat4 prevModel; // ADR-035: last frame's model matrix, for the velocity target
    glm::vec4 baseColor;
    glm::vec4 emissive;
    glm::vec4 material;
    glm::vec4 flags;
    glm::vec4 ids; // x = object id (its index in the scene's list; the ADR-030 `objectId` input),
                   // y = material id, z = bloom weight of this object's emission,
                   // w = the skinned joint count. ADR-135: there is no free lane here for a
                   // per-draw material tier, which is why ADR-133's tier is frame-global.
    // ADR-360: mesh wind. `windShape.y` is the gate and the amplitude at once; zero means
    // `meshWindOffset` returns early and the draw is byte-identical to one from before this
    // existed. Every entity of one body carries the SAME origin and extent -- that sharing is what
    // makes the deformation continuous across meshes that touch, and it is why these live here
    // rather than being derived per mesh from its own bounds.
    glm::vec4 windOrigin{0.0f}; // xyz = the body's root in world space, w = 1 / body height
    glm::vec4 windShape{0.0f};  // x = 1 / body radius, y = strength (0 = off), z = branch influence,
                                // w = foliage influence
    glm::vec4 windTune{0.0f};   // x = trunk influence, y = leaf flutter, z = response lag (s),
                                // w = the previous frame's time
    // ADR-376: tree energy and canopy shimmer, sharing the wind body's frame above. energy0.x and
    // energy3.x are the two gates; both zero means the fragment stage returns immediately.
    glm::vec4 energy0{0.0f};    // intensity, pulse speed, pulse width, propagation speed
    glm::vec4 energy1{0.0f};    // root, trunk, branch, canopy share
    glm::vec4 energy2{0.0f};    // noise amount, noise scale, noise speed, bloom contribution
    glm::vec4 energy3{0.0f};    // shimmer intensity, speed, scale, variation
    glm::vec4 energyA{0.0f};
    glm::vec4 energyB{0.0f};
    // ADR-703 (FXL, world/effects/entity_fx.hpp): the per-entity effect lanes. These are the first
    // two of the 96 padding bytes ADR-135 recorded as "no free lane" and left to the object-layout
    // work it called Phase E; that work has not landed, and FXL is the consumer that needed them, so
    // it takes them here and says so. Four vec4s of padding remain. Zero on every entity no lane
    // effect touches: `fxA.z == 0` is the lit shader's uniform early-out, so such a draw is
    // byte-identical to one from before these existed.
    glm::vec4 fxA{0.0f}; // x = emission gain, y = bloom share, z = flags, w = record index in `entityFx`
    glm::vec4 fxB{0.0f}; // rgb = tint on the material's own emission, w = 0
};
static_assert(sizeof(ObjectUniforms) == 448);
static_assert(offsetof(ObjectUniforms, model) == 0);
static_assert(offsetof(ObjectUniforms, normalMatrix) == 64);
static_assert(offsetof(ObjectUniforms, prevModel) == 128);
static_assert(offsetof(ObjectUniforms, baseColor) == 192);
static_assert(offsetof(ObjectUniforms, emissive) == 208);
static_assert(offsetof(ObjectUniforms, material) == 224);
static_assert(offsetof(ObjectUniforms, flags) == 240);
static_assert(offsetof(ObjectUniforms, ids) == 256);
static_assert(offsetof(ObjectUniforms, windOrigin) == 272);
static_assert(offsetof(ObjectUniforms, windShape) == 288);
static_assert(offsetof(ObjectUniforms, windTune) == 304);
static_assert(offsetof(ObjectUniforms, energyB) == 400);
static_assert(offsetof(ObjectUniforms, fxA) == 416);
static_assert(offsetof(ObjectUniforms, fxB) == 432);

struct TonemapUniforms {
    float exposure;
    float operatorId;
    float vignette;
    float grain;
    float size[2];
    float seed;
    float chromaRetention;
};
static_assert(sizeof(TonemapUniforms) == 32);

// Image-based-lighting inputs shared by the PBR and skybox passes.
struct IblResources {
    wgpu::TextureView irradiance;  // cube
    wgpu::TextureView prefiltered; // cube, mips by roughness
    wgpu::TextureView brdfLut;     // 2D RG
    // ADR-049: the source equirect itself, mipped, kept resident so the background pass can read
    // the sky at its own resolution. Null for a procedural sky, which has no map behind it.
    wgpu::TextureView background;
    std::uint32_t prefilteredMips = 1;
    bool valid = false;
    bool fromSky = false; // ADR-036: synthesised from the procedural sky, not from an HDR map
};

class SceneRenderer {
public:
    SceneRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~SceneRenderer();

    [[nodiscard]] Result<void> init();
    // (Re)creates the HDR target. Idempotent for equal sizes.
    [[nodiscard]] Result<void> resize(std::uint32_t width, std::uint32_t height);
    // Invalidates camera/model/AO temporal state after an in-place scene reload or camera cut.
    // The next frame is treated as a new temporal sequence rather than as motion from the old one.
    // This is the *seek* reset: it also restarts the particle pools, because a seek moves the
    // world's clock and the pools are the world's state at that clock.
    void resetTemporalHistory();
    // The same invalidation without the particle pools: what a change of render-target extent
    // needs, and nothing more. A resize throws away screen-space history because the buffers
    // holding it were reallocated at a new size; it has no claim on the simulation, whose alive
    // lists, emit carry and trail rings are not indexed by a pixel. Keeping the two apart is what
    // makes a render-scale change a presentation change (§33/§34) rather than a world event, and
    // it fixes the editor defect that every splitter drag emptied the particle field.
    void resetScreenHistory();

    // Encodes the scene and tonemap passes. `target` must match the size passed to resize().
    [[nodiscard]] Result<void> render(wgpu::CommandEncoder& encoder, const scene::Scene& scene,
                                      const FrameTime& time, const gpu::TargetView& target,
                                      const ShaderFrameInputs* shaderInputs = nullptr);

    // A full frame, submitted, with nothing read back. Offline rendering only needs pixels on the
    // frames it writes; reading a 2880x1800 image back every frame is twenty megabytes of
    // synchronous transfer the live path never performs, and it dominated every headless
    // measurement taken before anyone noticed.
    [[nodiscard]] Result<void> renderFrame(const scene::Scene& scene, const FrameTime& time,
                                           std::uint32_t width, std::uint32_t height,
                                           const ShaderFrameInputs* shaderInputs = nullptr);
    // Full frame into a fresh RGBA8 texture, submitted and read back. For tests and offline use.
    [[nodiscard]] Result<gpu::Image8> renderToImage(const scene::Scene& scene, const FrameTime& time,
                                                    std::uint32_t width, std::uint32_t height,
                                                    const ShaderFrameInputs* shaderInputs = nullptr);
    // Full frame, then the scene-linear HDR image the tonemap pass read (RGBA16F after the post
    // chain, before tone mapping), submitted and read back as floats. For EXR output and tests.
    [[nodiscard]] Result<gpu::ImageF> renderToImageFloat(const scene::Scene& scene, const FrameTime& time,
                                                         std::uint32_t width, std::uint32_t height,
                                                         const ShaderFrameInputs* shaderInputs = nullptr);
    // The RGBA16F texture the tonemap pass sampled in the last render() (the HDR target or the
    // last post output; CopySrc usage), for asynchronous HDR readback. Null before the first frame.
    [[nodiscard]] const wgpu::Texture& hdrOutputTexture() const { return hdrOutput_; }

    // Hot reload of the engine's own WGSL files: rebuilds every pipeline whose shader compiles,
    // keeps the previous pipeline for any that fails, and returns the first error.
    [[nodiscard]] Result<void> reloadEngineShaders();
    [[nodiscard]] std::uint32_t engineShaderReloads() const { return engineReloads_; }
    [[nodiscard]] ShaderStack& shaderStack() { return *shaderStack_; }
    [[nodiscard]] ParticleRenderer& particles() { return *particles_; }
    // ADR-703: the camera-facing strips (Trail), and what they drew last frame.
    [[nodiscard]] const RibbonRenderer& ribbons() const { return *ribbons_; }
    // ADR-360's bounded, opt-in particle warm-up, in frames (capped at
    // ParticleRenderer::kMaxWarmUpFrames). 0 -- the default -- keeps the seek behaviour this
    // renderer has always had: pools are emptied and the field refills over one lifetime. Set it
    // on an offline render so the head of a range is not required to accept that bloom.
    void setParticleWarmUpFrames(std::uint32_t frames) { particleWarmUpFrames_ = frames; }
    [[nodiscard]] std::uint32_t particleWarmUpFrames() const { return particleWarmUpFrames_; }
    [[nodiscard]] ProceduralRenderer& procedurals() { return *procedurals_; }
    [[nodiscard]] SdfRenderer& sdfs() { return *sdfs_; } // ADR-027
    [[nodiscard]] VolumeRenderer& volumes() { return *volumes_; } // ADR-032
    [[nodiscard]] Simulation& simulation() { return *simulation_; } // ADR-032
    [[nodiscard]] FieldUniforms& fields() { return *fields_; } // the per-frame field block (ADR-025)
    [[nodiscard]] MaterialPrograms& materialPrograms() { return *materialPrograms_; } // ADR-030
    [[nodiscard]] SplineBuffers& splines() { return *splines_; } // the spline tables (ADR-026)
    [[nodiscard]] PostProcessor& post() { return *postProcessor_; }
    [[nodiscard]] gpu::TransientPool& transientPool() { return *pool_; }

    // Installs image-based lighting (normally driven automatically from scene.environment).
    void setIbl(const IblResources& ibl);
    [[nodiscard]] EnvironmentProcessor& environment() { return *environment_; }
    [[nodiscard]] const IblResources& ibl() const { return ibl_; }

    [[nodiscard]] const RenderStats& stats() const { return stats_; }

    // Developer-only snapshot of the last frame's scene-side object and camera state. The
    // renderer owns the snapshot and replaces it at the next frame boundary; callers must not
    // retain references into it across renders.
    void setDiagnosticEntity(std::string name) { diagnosticEntity_ = std::move(name); }
    [[nodiscard]] const std::string& diagnosticEntity() const { return diagnosticEntity_; }
    [[nodiscard]] const RendererDiagnosticFrame& diagnosticFrame() const { return diagnosticFrame_; }
    [[nodiscard]] const RenderObjectDiagnostic* diagnosticObject(std::string_view name) const;

    // ---- lighting and quality (ADR-033/034/035) ----
    // Sample counts, resolutions and history lengths only; the scene and its determinism are the
    // same at every tier. Takes effect on the next render().
    void setQuality(QualityTier tier);
    [[nodiscard]] QualityTier quality() const { return tier_; }
    [[nodiscard]] const QualitySettings& qualitySettings() const { return qualitySettings_; }
    // Overrides individual counts without changing the named tier (tests and benchmarks).
    // Out of line because `renderScale` is consumed by `resize()` and by nothing else, so a caller
    // that changes it on a renderer whose targets already exist has to be re-sized or the change is
    // a silent no-op. ADR-212 hit exactly that and fixed it at one call site by ordering the two
    // statements; the benchmark harness sets a quality arm per block, *after* the targets are
    // sized, so the ordering fix did not reach it. Resolving it here makes the setter mean what it
    // says for every caller, which is also what the adaptive render scale needs.
    void setQualitySettings(const QualitySettings& settings);

    // A/B switches for attributing cost. The only trustworthy way to know what a phase costs is
    // to run the frame without it and diff the medians, so the phases the frame timeline reports
    // separately can each be switched off without editing a scene. `--disable` on the command
    // line drives this and logs what it turned off, so an A/B whose two arms are accidentally
    // identical is visible in the run's own output instead of reading as a null result.
    struct PassToggles {
        bool shadows = true; // the depth-only cascade / spot passes
        bool ao = true;      // GTAO + its temporal resolve
        bool volume = true;  // the volumetric march + composite
        bool post = true;    // the built-in post chain (bloom, DoF, grading)
        // ADR-087. Off means the lit pass computes the directional shadow term per pixel, which
        // is what it did before the mask pass existed, so this is the A/B arm for it.
        bool shadowMask = true;

        // ---- forensic isolation (renderer-forensics Phase 4.2) ----------------------------------
        //
        // Each of these removes one subsystem from the frame so a symptom can be attributed to it or
        // cleared of it. They are deliberately *arms of an A/B*, not quality settings: a frame with
        // one of them off is not a frame anybody should ship, and the plan's rule is that every
        // control here isolates a real path rather than merely existing.
        //
        // The list is short on purpose. "Disable terrain" is absent because the renderer cannot
        // honestly answer it: a terrain chunk arrives as an ordinary lit entity with no flag saying
        // where it came from, and a name-prefix guess would be a control that lies at the first
        // scene that names something `chunk`.
        // ADR-207. Off: the frame block reports zero surface waves, so the per-fragment loop
        // returns on its first compare. The arm for "what does the system cost when it is idle".
        bool waves = true;
        // ADR-230. Off: the atmospheric sky layer is not drawn at all -- not a uniform branch, the
        // whole draw is skipped. The arm §12 asks for: "Glowmere + effects disabled" measured
        // against "Glowmere baseline" is vacuous unless the two really differ in what runs.
        bool atmospherics = true;
        // ADR-390. Off: the Cosmic Ocean's draw is not recorded and its uniform is not uploaded.
        // ADR-187. Off: the FXAA output stage does not run, whatever `post/output/antialias` says.
        // Its own arm rather than part of `post`, because the whole-post arm removes the tone map
        // too -- the frame's transfer function moves with it and every threshold in a measurement
        // moves along, which is how the first flicker inventory attributed a stage it had not
        // isolated. This one removes exactly one filter.
        bool antialias = true;
        bool culling = true;      // off: draw everything, whatever the camera cull decided
        bool water = true;        // off: no water surfaces
        bool transparency = true; // off: no blended entities
        bool particles = true;    // off: no particle simulation or draw
        bool animation = true;    // off: skinned meshes draw in bind pose
        // Off: hold the view and projection of the frame the freeze began, while everything else
        // goes on moving. The one control that separates "the object moved" from "the camera moved"
        // without touching the scene at all.
        bool cameraMotion = true;
        // Off: every skinned character holds the pose it had when the arm was set, while the rest of
        // the frame goes on. Deliberately *not* the same control as `animation`, which draws the
        // bind pose: "stop the character moving" and "take the character's pose away" are different
        // questions, and a scene where only one of them changes the picture says which.
        bool animationMotion = true;
        // ADR-119, a probe rather than a setting: off means the scene pass's four *auxiliary*
        // colour targets are not written back out of tile memory at all (StoreOp::Discard). The
        // fragment shader still computes and writes them, so this measures exactly one thing --
        // the cost of storing 24 bytes a pixel to system memory -- and nothing else. The frame it
        // produces is wrong wherever post reads one of them, which is what makes it an arm and not
        // a quality setting.
        bool auxTargetStores = true;
    };

    // The arms by name, in one place (renderer forensics Phase 8.3). The CLI's `--disable <list>`,
    // the panel and the automatic bisection all read this table rather than each keeping a private
    // if-chain -- three enumerations of the same set is how an arm ends up reachable from one of
    // them and not the others, and a bisection that cannot see an arm silently clears the subsystem
    // behind it.
    struct PassArm {
        const char* name;
        bool PassToggles::*flag;
    };
    [[nodiscard]] static std::span<const PassArm> passArms();
    // Sets one arm by name. False when the name is not one of `passArms()`.
    [[nodiscard]] static bool setPassArm(PassToggles& toggles, std::string_view name, bool on);
    // Every arm's name, comma separated -- for a message telling somebody what they may write.
    [[nodiscard]] static std::string passArmNames();

    // ---- quality arms (ADR-117) -------------------------------------------------------------
    //
    // A *quality* arm is an A/B arm that changes `QualitySettings` rather than removing a pass.
    // It exists because the two most decision-relevant questions in Phase B cannot be asked with
    // a pass toggle: ADR-112's shortened shadow range is a *setting* (`shadowTexelTarget`, zero
    // restores the pre-ADR-112 rule), and so are the contact march, PCSS and the mask's
    // resolution. Before this, settling any of them meant a rebuild between the arms, which is
    // exactly the block-not-interleaved evidence ADR-112 had to flag as weak about itself.
    //
    // The distinction from a `PassArm` is deliberate and is not cosmetic: a pass arm removes work
    // the frame asked for and produces a frame nobody should ship, while a quality arm selects a
    // configuration that is, by construction, shippable -- every one of these is a value the tier
    // table already sets somewhere. So a quality arm may also be read as "what would this tier
    // choice cost", which a pass toggle may not.
    struct QualityArm {
        const char* name;
        void (*apply)(QualitySettings&);
        const char* what; // what the arm does, for the log line that records the conditions
    };
    [[nodiscard]] static std::span<const QualityArm> qualityArms();
    // Applies one arm by name to `settings`. False when the name is not one of `qualityArms()`.
    [[nodiscard]] static bool setQualityArm(QualitySettings& settings, std::string_view name);
    // Every quality arm's name, comma separated.
    [[nodiscard]] static std::string qualityArmNames();
    // What an arm does, or an empty view when the name is not an arm.
    [[nodiscard]] static std::string_view qualityArmDescription(std::string_view name);

    // ADR-114: compute the froxel grid's occupancy on the CPU each frame. Off by default. It is
    // `kClusterCount * localLights` sphere-against-box tests on the submitting thread, so it
    // belongs to a diagnostic run and not to a timing one; `RenderStats::haveClusters` records
    // which kind of run a number came from.
    void setClusterStatsEnabled(bool on) { clusterStats_ = on; }
    [[nodiscard]] bool clusterStatsEnabled() const { return clusterStats_; }

    void setPassToggles(const PassToggles& toggles);
    [[nodiscard]] const PassToggles& passToggles() const { return toggles_; }
    [[nodiscard]] ShadowRenderer& shadows() { return *shadows_; }     // ADR-034
    [[nodiscard]] SkinningRenderer& skinning() { return *skinning_; } // ADR-086
    [[nodiscard]] AoRenderer& ambientOcclusion() { return *ao_; }     // ADR-034
    [[nodiscard]] ShadowMaskRenderer& shadowMask() { return *shadowMask_; } // ADR-087
    // Displays one auxiliary target full-screen instead of the shaded frame (ADR-035).
    void setAuxDebugView(AuxDebugView view) { auxDebugView_ = view; }
    [[nodiscard]] AuxDebugView auxDebugView() const { return auxDebugView_; }
    // How hard the view's own ramp is driven; zero means the view's default. It exists because a
    // far plane is not a scene: RendererQA's camera sees three kilometres and its geometry is
    // thirty metres away, so a linear depth ramped over the far plane puts every surface in the
    // scene inside one of 256 steps. The knob says "show me the first N metres" without the view
    // having to lie about what it is showing -- the displayed value stays proportional to distance,
    // and the constant it is proportional to is in the uniform rather than in somebody's head.
    void setAuxDebugScale(float scale) { auxDebugScale_ = scale; }
    [[nodiscard]] float auxDebugScale() const { return auxDebugScale_; }
    // The auxiliary targets of the last frame (debug tools and tests).
    [[nodiscard]] const wgpu::TextureView& normalRoughnessView() const { return normalRough_.view; }
    [[nodiscard]] const wgpu::TextureView& velocityView() const { return velocity_.view; }
    [[nodiscard]] const wgpu::TextureView& emissionView() const { return emission_.view; }
    [[nodiscard]] const wgpu::TextureView& identifierView() const { return ids_.view; }
    [[nodiscard]] const wgpu::TextureView& linearDepthView() const { return linearDepth_.view; }
    [[nodiscard]] const wgpu::Texture& normalRoughnessTexture() const { return normalRough_.texture; }
    [[nodiscard]] const wgpu::Texture& velocityTexture() const { return velocity_.texture; }
    [[nodiscard]] const wgpu::Texture& emissionTexture() const { return emission_.texture; }
    [[nodiscard]] const wgpu::Texture& identifierTexture() const { return ids_.texture; }
    [[nodiscard]] const wgpu::Texture& linearDepthTexture() const { return linearDepth_.texture; }
    // ADR-255: the shadow AOV's source. Unlike the five above this target is NOT written every
    // frame -- it exists only when something asked for it, which for an export is
    // `shadowMask().setExportRequested(true)`. `shadowMask().encoded()` is the question to ask
    // before reading it; a texture read when no pass ran is the 1x1 white stand-in, and a constant
    // that looks like a render is the failure ADR-242 and ADR-255 are both about.
    [[nodiscard]] const wgpu::Texture& shadowTexture() const { return shadowMask_->exportTexture(); }
    // The packed lights and the froxel grid of the last frame (tests and tools). The cluster buffer
    // holds `kClusterCount` counts followed by `kMaxLightsPerCluster` indices per cluster.
    [[nodiscard]] const wgpu::Buffer& lightBuffer() const { return lightBuffer_; }
    [[nodiscard]] const wgpu::Buffer& clusterBuffer() const { return clusterBuffer_; }
    // The overdraw counter (ADR-115): one u32 per pixel of the render target, valid after a frame
    // rendered with AuxDebugView::Overdraw or ::FragmentDensity selected (tools and tests).
    [[nodiscard]] const wgpu::Buffer& overdrawBuffer() const { return overdrawBuffer_; }

    // The 2D composition (ADR-083). The one hook the renderer offers whatever draws over the
    // finished picture: it is called after the tone map has written `target` and before the frame
    // timeline is resolved, and the renderer knows nothing else about it. Null by default, so a
    // frame with no composition is encoded exactly as it was before.
    void setOverlay(FrameOverlay* overlay) { overlay_ = overlay; }
    [[nodiscard]] FrameOverlay* overlay() const { return overlay_; }

    // Debug drawing (ADR-031): the host fills this before render() and the geometry is drawn over
    // the lit scene. Empty by default, so a frame with no debug geometry is encoded as before.
    [[nodiscard]] DebugDraw& debugDraw() { return *debug_; }
    void setDebugDepthTest(bool on) { debugDepthTest_ = on; }

    // ADR-233. Above this cost, a procedural-sky IBL rebuild waits for the slider driving it to
    // stop moving rather than taking the frame away on every frame of the drag -- the same policy,
    // and the same pure function, ADR-084 gave procedural geometry.
    //
    // **Zero -- the default -- means rebuild whenever the sky's hash moves**, which is the
    // behaviour an offline render requires and gets: the deferral reads a wall clock, and a wall
    // clock has no business deciding what a deterministic render contains. Only the live editor
    // sets it, in `Application::runLive`.
    void setInteractiveEnvironmentBudget(double budgetMs) { interactiveEnvBudgetMs_ = budgetMs; }
    // True while the sky on screen is behind the sky the parameters ask for. The canvas says so;
    // a picture that is deliberately a few frames stale must never be silently stale.
    [[nodiscard]] bool environmentAwaitingRebuild() const { return skyDeferral_.deferring; }
    // The frame's GPU timestamp timeline (gpu/frame_timeline.hpp). Every pass marks itself on it;
    // `passes()` is the per-pass breakdown of the last completed frame, in submission order.
    [[nodiscard]] gpu::FrameTimeline& timeline() { return *timeline_; }
    // Pumps the timeline and fans the per-pass numbers out into stats(). Call after Submit();
    // never blocks. renderFrame()/renderToImage() do it for you.
    void collectFrameTimings();
    [[nodiscard]] const gpu::RenderTarget& hdrTarget() const { return hdr_; }
    // DF (Effect Library Wave 1): what the distortion passes did on the last frame -- whether the
    // gate held (`encoded` false), how many proxies, the rects, and the two passes' GPU times.
    [[nodiscard]] const DistortionStats& distortionStats() const { return distortion_->stats(); }
    [[nodiscard]] bool initialised() const { return initialised_; }

    // ADR-128: the object slot buffer grows to fit the frame rather than being a 256-slot ceiling.
    // `kInitialObjects` is only what init() allocates so a small scene pays what it used to;
    // `ensureObjectCapacity` raises it before the entity loop runs, and `objectCapacity()` reports
    // what the frame actually has. The one number that is still a limit is the byte budget below.
    static constexpr std::uint32_t kInitialObjects = 256;
    static constexpr std::uint32_t kObjectStride = 512; // dynamic-offset alignment (256) x 2
    // The ceiling is a memory budget, not a slot count: 64 MiB of object uniforms. It exists so a
    // scene that asks for a preposterous number of entities fails loudly at a documented number
    // instead of asking the driver for a gigabyte. 131,072 slots is ~470x Glowmere's 278 entities,
    // and eight times the 16,383 distinct entity indices the pick-id encoding can even name
    // (scene/scene_types.hpp, kPickIndexBits) -- so the identifier target runs out of names long
    // before this runs out of slots, and that is deliberate: this must never be the binding limit.
    // Written as a plain decimal mebibyte count and multiplied up, so the CPU/WGSL layout guards
    // can scrape it the way they scrape every other constant they refuse to retype.
    static constexpr std::uint32_t kObjectBufferBudgetMiB = 64;
    static constexpr std::uint64_t kObjectBufferByteBudget =
        static_cast<std::uint64_t>(kObjectBufferBudgetMiB) * 1024ull * 1024ull;
    static constexpr std::uint32_t kMaxObjectCapacity =
        static_cast<std::uint32_t>(kObjectBufferByteBudget / kObjectStride);
    static_assert(kObjectStride % 256 == 0);
    static_assert(sizeof(ObjectUniforms) <= kObjectStride);
    static_assert(kMaxObjectCapacity >= kInitialObjects);
    // Slots the object buffer currently holds. Not a constant: it is what the last
    // `ensureObjectCapacity` settled on, and tests read it to prove the cap moved.
    [[nodiscard]] std::uint32_t objectCapacity() const { return objectCapacity_; }
    static constexpr wgpu::TextureFormat kHdrFormat = wgpu::TextureFormat::RGBA16Float;
    static constexpr wgpu::TextureFormat kDepthFormat = wgpu::TextureFormat::Depth24Plus;
    // Auxiliary targets (ADR-035). Normal + roughness packs an octahedral normal in rg, the
    // roughness in b and a flag byte in a; identifiers pack the object id in the low 16 bits and
    // the material id in the high 16.
    static constexpr wgpu::TextureFormat kNormalFormat = wgpu::TextureFormat::RGBA16Float;
    static constexpr wgpu::TextureFormat kVelocityFormat = wgpu::TextureFormat::RG16Float;
    static constexpr wgpu::TextureFormat kEmissionFormat = wgpu::TextureFormat::RGBA16Float;
    static constexpr wgpu::TextureFormat kIdFormat = wgpu::TextureFormat::R32Uint;
    static constexpr wgpu::TextureFormat kLinearDepthFormat = wgpu::TextureFormat::R32Float;
    static constexpr std::uint32_t kAuxTargetCount = 4;

private:
    struct GpuMesh {
        wgpu::Buffer vertices;
        wgpu::Buffer indices;
        // ADR-086: the joint indices and weights, in a second vertex buffer. Null for a static
        // mesh, which is every mesh in every scene that has no character in it.
        wgpu::Buffer skin;
        std::uint32_t indexCount = 0;
    };
    // ADR-351: one mesh's coarser rungs on the GPU, and what the selector needs to cost them.
    // `levels[0]` is LOD1 -- LOD0 is `meshes_[base]` and is never copied, which is what keeps the
    // source mesh the only source mesh. Empty for every mesh with no chain, which is all of them
    // until a scene asks.
    struct GpuMeshLod {
        std::vector<GpuMesh> levels;
        // Rung 0 is the source, so this is one longer than `levels` and is indexed by the level the
        // selector returns. Surface areas are in the mesh's own units; the instance's scale is
        // applied where the record is built.
        std::vector<LodRung> rungs;
        float hysteresis = 0.0f;
        float maxScreenError = -1.0f; // negative takes the policy's calibrated default
    };
    enum class LitVariant : std::uint8_t { OpaqueCull, OpaqueNoCull, Blend };
    // One auxiliary colour target: its texture, its view and nothing else.
    struct AuxTarget {
        wgpu::Texture texture;
        wgpu::TextureView view;
        [[nodiscard]] bool valid() const { return texture != nullptr; }
    };

    // Renders into a fresh RGBA8 texture with CopySrc usage, submits and waits (the sync path).
    Result<wgpu::Texture> renderSubmitted(const scene::Scene& scene, const FrameTime& time, std::uint32_t width,
                                          std::uint32_t height, const ShaderFrameInputs* shaderInputs);
    Result<void> createPipelines();
    Result<void> createAuxTargets(std::uint32_t width, std::uint32_t height);
    Result<void> createLightResources();
    Result<wgpu::RenderPipeline> createDepthOnlyPipeline(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> createLinearDepthPipeline(const wgpu::ShaderModule& module);
    Result<void> createAuxDebugResources(const wgpu::ShaderModule& module);
    [[nodiscard]] Result<wgpu::RenderPipeline> auxDebugPipelineFor(wgpu::TextureFormat format);
    // ADR-115: the opt-in overdraw / fragment-density counting pass. See overdraw_count.wgsl for why
    // it is a separate pipeline layout (frame + object + its own storage buffer) rather than a
    // variant of the ordinary opaque pipelines.
    Result<void> createOverdrawResources();
    Result<wgpu::RenderPipeline> createOverdrawCountPipeline(const wgpu::ShaderModule& module);
    void rebuildFrameBindGroups();
    // Packs this frame's lights, uploads them and encodes the cluster build.
    void updateLights(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const glm::mat4& view,
                      float aspect, FrameUniforms& frame);
    Result<wgpu::RenderPipeline> createLitPipeline(const wgpu::ShaderModule& module, LitVariant variant);
    Result<wgpu::RenderPipeline> createGridPipeline(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> createSkyboxPipeline(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> createAtmospherePipeline(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> tonemapPipelineFor(wgpu::TextureFormat format);
    Result<wgpu::RenderPipeline> finishPipeline(const wgpu::RenderPipelineDescriptor& desc, const char* label);
    void uploadMeshes(const scene::Scene& scene);
    void uploadTextures(const scene::Scene& scene);
    // ADR-715: uploads the scene's terrain height bake when it is not the one already resident.
    void uploadTerrainHeight(const scene::Scene& scene);
    [[nodiscard]] const wgpu::TextureView& terrainHeightView() const {
        return terrainHeight_.valid() ? terrainHeight_.view : terrainHeightDefault_.view;
    }
    void updateEnvironment(const scene::Scene& scene);
    void updateSpectrum(const analysis::AnalysisFrame* frame);
    Result<void> ensurePostTargets(std::uint32_t width, std::uint32_t height);
    wgpu::BindGroup tonemapBindGroupFor(const wgpu::TextureView& view);
    void ensureTonemapBindGroup();
    void rebuildIblBindGroup();
    const wgpu::BindGroup& materialBindGroup(const scene::Material& material);
    const gpu::GpuTexture& textureOrDefault(const scene::TextureRef& ref, const gpu::GpuTexture& fallback) const;

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    std::unique_ptr<gpu::FrameTimeline> timeline_;
    std::unique_ptr<gpu::SamplerCache> samplers_;
    std::unique_ptr<EnvironmentProcessor> environment_;
    std::unique_ptr<ShaderStack> shaderStack_;
    std::unique_ptr<FieldUniforms> fields_;
    std::unique_ptr<MaterialPrograms> materialPrograms_;
    std::unique_ptr<SplineBuffers> splines_;
    std::unique_ptr<ParticleRenderer> particles_;
    std::uint32_t particleWarmUpFrames_ = 0; // ADR-360, opt-in

    std::unique_ptr<ProceduralRenderer> procedurals_;
    std::unique_ptr<SdfRenderer> sdfs_;
    std::unique_ptr<VolumeRenderer> volumes_;
    std::unique_ptr<DebugDraw> debug_;
    FrameOverlay* overlay_ = nullptr;
    bool debugDepthTest_ = true;
    std::unique_ptr<Simulation> simulation_;
    std::unique_ptr<ShadowRenderer> shadows_; // ADR-034
    std::unique_ptr<SkinningRenderer> skinning_; // ADR-086
    std::unique_ptr<AoRenderer> ao_;          // ADR-034
    std::unique_ptr<ShadowMaskRenderer> shadowMask_; // ADR-087
    std::unique_ptr<WaterRenderer> water_;           // ADR-099
    std::unique_ptr<RibbonRenderer> ribbons_;        // ADR-703 (Wave 1): RIBBON, pass 1's blended section
    std::unique_ptr<PostProcessor> postProcessor_;
    // ADR-410. Runs between the scene pass and the post chain: captures the clean scene radiance
    // into a bounded ring and applies whatever temporal effects the scene authored. Reset by
    // `resetTemporalHistory()` along with the AO history and the particle pools -- one hook for
    // every temporal consumer, never a second one.
    std::unique_ptr<TemporalEffects> temporal_;
    std::unique_ptr<DistortionRenderer> distortion_; // DF: after the volume composite, before post
    std::unique_ptr<gpu::TransientPool> pool_;
    glm::mat4 prevViewProj_{1.0f};
    bool havePrevViewProj_ = false;
    double previousRenderTime_ = -std::numeric_limits<double>::infinity();
    // ADR-360: the previous frame's render time, captured before `previousRenderTime_` is
    // overwritten, so the wind's contribution to the velocity target is a real difference.
    float windPrevTime_ = 0.0f;
    QualityTier tier_ = QualityTier::Realtime;
    QualitySettings qualitySettings_ = QualitySettings::forTier(QualityTier::Realtime);
    PassToggles toggles_;
    bool clusterStats_ = false; // ADR-114
    // Compute passes this frame's encoding counted for itself (the froxel build). The rest are
    // read off the subsystems in collectFrameTimings(), which runs more than once per frame.
    std::uint32_t clusterDispatches_ = 0;
    AuxDebugView auxDebugView_ = AuxDebugView::None;
    float auxDebugScale_ = 0.0f;
    bool warnedOverdrawScope_ = false; // the overdraw view's partial-coverage warning, once per run
    gpu::RenderTarget post_[2];      // ping-pong HDR colour targets for post layers
    gpu::GpuTexture spectrum_;       // binCount x 1 RGBA16F audio spectrum for user shaders
    std::size_t spectrumBins_ = 0;
    std::vector<std::uint16_t> spectrumStaging_;
    std::uint32_t engineReloads_ = 0;
    scene::TextureId environmentTexture_ = scene::kInvalidTexture;
    const scene::Scene* environmentScene_ = nullptr;
    std::uint64_t environmentIdentity_ = 0;
    std::uint64_t environmentVersion_ = ~0ull;
    // ADR-036: the procedural sky is rebuilt only when its resolved parameters change.
    std::uint64_t skyHash_ = 0;
    bool skyBuilt_ = false;
    // ADR-233: and, in the live editor only, not on every frame of the drag that is changing them.
    // The full IBL chain -- cube, irradiance, GGX prefilter, all of it blocking -- costs 40-70 ms
    // on this machine, and `processSky`'s own header says it is load-time work. A lighting drag
    // called it sixty times a second.
    double interactiveEnvBudgetMs_ = 0.0; // 0 = rebuild whenever the hash moves (offline, always)
    scene::RebuildDeferral skyDeferral_;
    std::chrono::steady_clock::time_point lastSkyPollTime_{};
    bool initialised_ = false;

    gpu::RenderTarget hdr_;
    AuxTarget normalRough_;
    AuxTarget velocity_;
    AuxTarget emission_;
    AuxTarget ids_;
    AuxTarget linearDepth_;
    wgpu::Texture hdrOutput_;
    wgpu::BindGroupLayout frameLayout_;
    wgpu::BindGroupLayout objectLayout_;
    wgpu::BindGroupLayout materialLayout_;
    wgpu::BindGroupLayout iblLayout_;
    wgpu::BindGroupLayout tonemapLayout_;
    wgpu::PipelineLayout scenePipelineLayout_;
    wgpu::PipelineLayout tonemapPipelineLayout_;
    wgpu::RenderPipeline litOpaqueCull_;
    wgpu::RenderPipeline litOpaqueNoCull_;
    wgpu::RenderPipeline litBlend_;
    wgpu::RenderPipeline gridPipeline_;
    wgpu::RenderPipeline skyboxPipeline_;
    // ADR-230: the atmospheric sky layer, drawn immediately after the sky. Its own pipeline rather
    // than lines inside the skybox's, because it must draw when the skybox does not, must bloom at
    // its own weight rather than through `Environment::skyBloom`, and must be skippable entirely.
    wgpu::RenderPipeline atmospherePipeline_;
    // Whether anything is live this frame; written when the frame block is packed and read at the
    // draw site, so the toggle removes the uniform content and the fragment work together.
    bool drawAtmosphere_ = false;
    wgpu::RenderPipeline depthOnlyPipeline_;   // entities and meshed SDFs, depth prepass and shadows
    wgpu::RenderPipeline linearDepthPipeline_; // Depth24Plus -> R32Float view-space distance
    // The auxiliary view is drawn *after* the tone map, straight onto the target, so one pipeline
    // per target format -- the same reason the tone map itself is a map rather than a pipeline.
    // Drawing it before the tone map, which is where it used to live, meant every diagnostic went
    // through exposure and a filmic curve on its way to the screen: a normal encoded as 0.5 arrived
    // as something else, an identifier's palette moved with the scene's brightness, and a depth
    // could not be read as a number at all. See the pass in `render`.
    wgpu::ShaderModule auxDebugModule_;
    wgpu::PipelineLayout auxDebugPipelineLayout_;
    std::unordered_map<std::uint32_t, wgpu::RenderPipeline> auxDebugPipelines_;
    wgpu::BindGroupLayout auxDebugLayout_;
    wgpu::BindGroup auxDebugGroup_;
    wgpu::Buffer auxDebugUniforms_;
    wgpu::ShaderModule pbrModule_;
    wgpu::ShaderModule tonemapModule_;
    std::unordered_map<std::uint32_t, wgpu::RenderPipeline> tonemapPipelines_;

    // ADR-115: overdraw / fragment-density counting. `overdrawBuffer_` is a viewport-sized array of
    // atomic<u32>, one per pixel, resized alongside the other auxiliary targets; the counting pass
    // that writes it is encoded only while the view is selected (see render()).
    wgpu::ShaderModule overdrawModule_;
    wgpu::BindGroupLayout overdrawLayout_;      // group 2: the storage buffer alone
    wgpu::PipelineLayout overdrawPipelineLayout_;
    wgpu::RenderPipeline overdrawCountPipeline_;
    wgpu::Buffer overdrawBuffer_;
    std::uint64_t overdrawBufferBytes_ = 0;
    wgpu::BindGroup overdrawGroup_;

    wgpu::Buffer frameUniforms_;
    wgpu::Buffer lightBuffer_;        // group 0 binding 1: the packed scene lights
    wgpu::Buffer clusterBuffer_;      // group 0 binding 2: froxel counts then index lists
    wgpu::Buffer clusterParams_;      // the cluster build pass's own uniforms
    wgpu::BindGroupLayout clusterLayout_;
    wgpu::ComputePipeline clusterPipeline_;
    wgpu::BindGroup clusterBindGroup_;
    wgpu::ShaderModule clusterModule_;
    gpu::GpuTexture linearDepthDefault_; // 1x1 stand-in before the first frame's prepass
    // ADR-715: group 0 binding 12, the terrain's baked height (RG32F since ADR-717: ground, basin). `terrainHeight_` is the
    // uploaded bake, re-created only when `Scene::terrainGround`'s field hash moves;
    // `terrainHeightDefault_` is the 1x1 bound when the scene has none.
    gpu::GpuTexture terrainHeight_;
    gpu::GpuTexture terrainHeightDefault_;
    std::uint64_t terrainHeightHash_ = 0;
    const void* terrainHeightField_ = nullptr;
    gpu::GpuTexture ltc1_;            // group 0 bindings 8/9: the linearly-transformed-cone table
    gpu::GpuTexture ltc2_;
    wgpu::Sampler ltcSampler_;
    std::vector<GpuLight> lightStaging_;
    std::vector<std::uint32_t> clusterStaging_;
    std::vector<const scene::PunctualLight*> lightOrder_;
    wgpu::Buffer objectUniforms_;
    // Slots in objectUniforms_ / objectStaging_. Zero until init() allocates the first buffer.
    std::uint32_t objectCapacity_ = 0;
    // Grows the object slot buffer (and the staging mirror, and every bind group that names it) to
    // hold at least `objects` slots. Cheap and idempotent when it already does. ADR-128.
    void ensureObjectCapacity(std::uint32_t objects);
    // ADR-703 (FXL): the per-entity effect records, group 1 binding 2 of every pipeline that uses the
    // object layout (the entity passes, the skinned ones, meshed SDFs, water, the sky draws). Grows
    // like the object buffer and never shrinks; record 0 is neutral. At least one record always
    // exists, so the binding is valid on a frame with no lane effect at all.
    wgpu::Buffer entityFxBuffer_;
    std::uint32_t entityFxCapacity_ = 0; // records
    void ensureEntityFxCapacity(std::uint32_t records);
    // Makes the object bind group (and hands the skinned path its copy) from the current object and
    // effect-record buffers. Called by both growth routines, so neither can leave the other's
    // buffer bound stale.
    void rebuildObjectBindGroups();
    // ADR-703 (LIGHTMOD): this frame's effect pool lights as the light type `updateLights` orders,
    // storage owned here so `lightOrder_` can point at it without allocating.
    std::array<scene::PunctualLight, world::kEffectLightBudget> effectLightScratch_{};
    // Whether this entity can be drawn at all: visible, with a mesh that reached the GPU. It is a
    // member rather than a lambda inside render() because two places ask -- the entity loop, and
    // ensureObjectCapacity's count of how many slots the frame needs -- and a second copy of this
    // predicate drifting from the first is how the buffer ends up one slot short of the loop.
    // Meaningless before uploadMeshes() has run for the scene in question.
    [[nodiscard]] bool drawable(const scene::Entity& entity) const;
    wgpu::Buffer tonemapUniforms_;
    wgpu::BindGroup frameBindGroup_;
    // The same group with the shadow atlas and the AO target replaced by placeholders, for the
    // passes that write them (a pass may not sample what it renders into).
    wgpu::BindGroup frameBindGroupAux_;
    // And once more for the shadow-mask pass (ADR-087), which needs the real atlas and the real
    // linear depth -- it computes the shading pass's own shadow terms -- but must not bind the
    // mask it is writing.
    wgpu::BindGroup frameBindGroupMask_;
    std::array<wgpu::BindGroup, kMaxShadowViews> shadowFrameGroups_{};
    wgpu::BindGroupLayout linearDepthLayout_;
    wgpu::BindGroup linearDepthGroup_;
    wgpu::TextureView linearDepthBoundView_;
    wgpu::BindGroup objectBindGroup_;
    wgpu::BindGroup tonemapBindGroup_;
    wgpu::BindGroup iblBindGroup_;
    wgpu::TextureView tonemapBoundView_;
    std::unordered_map<WGPUTextureView, wgpu::BindGroup> tonemapGroups_;

    // Defaults for absent textures and IBL.
    gpu::GpuTexture whiteSrgb_;
    gpu::GpuTexture whiteLinear_;
    gpu::GpuTexture flatNormal_;
    gpu::GpuTexture blackCube_;
    wgpu::TextureView blackCubeView_;
    gpu::GpuTexture blackLut_;
    wgpu::Sampler iblSampler_;
    wgpu::Sampler skySampler_; // ADR-049: as iblSampler_, but wrapping in longitude
    wgpu::Sampler tonemapSampler_; // ADR-137: the tonemap's upscale from a scaled scene target
    // ADR-137: what the caller asked for, which is what the tonemap writes into. The scene target
    // may be smaller; `stats_` reports the scene's size, not this one.
    std::uint32_t outputWidth_ = 0;
    std::uint32_t outputHeight_ = 0;
    IblResources ibl_;

    std::vector<GpuMesh> meshes_;
    // ADR-351: parallel to `meshes_`, by MeshId. A mesh with no chain carries an empty GpuMeshLod,
    // which costs three empty vectors and keeps the lookup a subscript rather than a map.
    std::vector<GpuMeshLod> meshLods_;
    // Whether this frame's scene had any chain at all. The whole selection pass is skipped when it
    // did not, so a scene that never asked for LOD pays nothing and -- more to the point -- cannot
    // be changed by it.
    bool anyMeshLod_ = false;
    RepresentationSelector representation_;
    // What the selector's memory was last keyed to. Identity as well as count, because every fresh
    // Scene starts at a recycled address and two different scenes can have the same entity count.
    std::uint64_t lodSceneIdentity_ = 0;
    std::size_t lodEntityCount_ = 0;
    // What the selector actually decided this frame, for the diagnostics and the tests: one entry
    // per rung of every chain-bearing mesh, counted across entities.
    struct EntityLodStats {
        std::uint32_t drawables = 0;      // entities offered to the selector
        std::uint32_t demoted = 0;        // entities drawn at a rung below LOD0
        std::uint32_t changed = 0;        // entities whose rung differs from last frame's
        std::uint32_t held = 0;           // entities the dead zone kept where they were
        std::uint64_t sourceTriangles = 0; // what LOD0 would have submitted for those entities
        std::uint64_t drawnTriangles = 0;  // what the chosen rungs did submit
        std::array<std::uint32_t, 8> rungCounts{};
    };
    EntityLodStats entityLod_{};
    // The scene the uploaded meshes/textures came from: its address AND its identity, because a
    // recycled address is not the same scene (scene::SceneIdentity).
    const scene::Scene* meshScene_ = nullptr;
    std::uint64_t meshIdentity_ = 0;
    std::uint64_t meshVersion_ = ~0ull;
    std::vector<gpu::GpuTexture> textures_;
    const scene::Scene* textureScene_ = nullptr;
    std::uint64_t textureIdentity_ = 0;
    std::uint64_t textureVersion_ = ~0ull;
    std::unordered_map<std::uint64_t, wgpu::BindGroup> materialBindGroups_;
    std::vector<std::uint8_t> objectStaging_;
    std::vector<WaterUniforms> waterUniforms_; // ADR-099; rebuilt each frame from Scene::waters
    // Previous-frame model matrices by entity name, so velocity survives reordering (ADR-035).
    std::unordered_map<std::string, glm::mat4> prevModels_;
    std::unordered_map<std::string, glm::mat4> prevModelsNext_;
    const scene::Scene* temporalScene_ = nullptr;
    // The camera the freeze is holding (PassToggles::cameraMotion).
    glm::mat4 frozenView_{1.0f};
    glm::mat4 frozenProjection_{1.0f};
    glm::vec3 frozenCameraPosition_{0.0f};
    bool haveFrozenCamera_ = false;

    std::string diagnosticEntity_;
    RendererDiagnosticFrame diagnosticFrame_;
    std::optional<RenderObjectDiagnostic> previousDiagnosticObject_;
    RendererDiagnosticFrame previousDiagnosticFrame_;
    RenderStats stats_;
};

} // namespace avgen::rendering
