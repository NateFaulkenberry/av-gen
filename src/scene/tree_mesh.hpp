#pragma once

// Turning a `TreeGraph` into renderable meshes: one mesh per semantic tier.
//
// WHY ONE MESH PER TIER AND NOT ONE PER TREE OR ONE PER LIMB. The world editor picks by object, not
// by instance -- the procedural renderer writes the object index into the identifier target and
// never the instance index -- so whatever granularity the meshes come out at *is* the granularity an
// artist can select, inspect and adjust. One mesh for the whole tree is an unselectable blob. One
// per limb puts twenty entries in the outliner that cannot usefully be edited, because a limb is an
// output of the simulation rather than an input: there is nothing on it to adjust. A mesh per tier
// is the level at which the properties an artist would actually change -- radial resolution,
// material, emissive gain, how that tier answers the wind -- are real and separable.
//
// WHY A TUBE PER AXIS AND NOT PER INTERNODE. A branch is one continuous surface. Sweeping a profile
// along the whole axis gives a single unbroken run of quads with a continuous radius, which is what
// "no obvious faceting, continuous taper, stable normals" means in practice. The alternative --
// a cylinder per internode -- produces a visible crease and a doubled vertex ring at every node,
// which is exactly the 1990s procedural tree look the brief rules out.
//
// The framing is `spatial::Spline`'s, which already computes rotation-minimising frames by double
// reflection (Wang et al. 2008). That is the single largest thing this file does not have to do.
//
// NO TANGENTS, DELIBERATELY. `scene::Vertex` is position/normal/uv and 32 bytes, asserted in three
// places. Widening it to carry a tangent would cost sixteen bytes on every vertex in the engine to
// serve one hero mesh. Bark detail is analytic (a radius perturbation baked into the sweep) until a
// render demonstrates that normal-mapped bark is what stands between this tree and the quality bar.

#include "core/error.hpp"
#include "scene/scene_types.hpp"
#include "scene/tree.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

// The canopy is split into this many meshes, each of which takes its own material.
//
// A canopy is not one colour. The palette brief asks for controlled variation across deep emerald,
// teal, blue-green and the occasional gold, and a single merged mesh cannot express that: the
// vertex format has no colour channel and per-instance variation is for instanced objects, not for
// a merged one. Splitting the clusters into three meshes by a hash of their attachment node gives
// real colour variation for two extra draws and no new vertex attribute. It also keeps the
// variation spatially incoherent, which is what stops it reading as three stripes.
inline constexpr int kFoliageTints = 3;

struct TreeMeshSettings {
    // Radial resolution per tier. The trunk is the only thing in frame big enough for a silhouette
    // edge to read as a polygon, so it gets most of the budget and the twigs get almost none.
    int trunkSides = 16;
    int primarySides = 10;
    int secondarySides = 6;
    int tertiarySides = 4;
    int rootSides = 8;
    // Lengthwise sampling, in rows per world unit. Curvature is what this is paying for, so the
    // trunk -- which is nearly straight -- needs fewer rows per metre than a gnarled twig.
    float rowsPerUnit = 2.2f;
    int maxRowsPerAxis = 160;

    // Analytic bark: a low-frequency radius perturbation swept along the trunk and primaries. This
    // is what stops the trunk reading as a smooth lathe-turned cone without a normal map.
    float barkAmount = 0.055f;
    float barkScale = 1.35f;
    int barkMinSides = 8; // below this the perturbation only adds noise to the silhouette

    // The socket: a child tube starts this far back inside its parent, as a fraction of the parent's
    // radius, and this much fatter at its first ring. Between them they hide the intersection
    // without a CSG operation, which is the cheap technique and the one this is choosing knowingly.
    float socketDepth = 0.85f;
    float socketFlare = 1.35f;

    // Foliage. A cluster is an oriented ellipsoid of cards; the card is the unit of geometry and the
    // cluster is the unit of animation.
    // Sixteen small cards, not seven big ones. At 0.42 m the cards read as paper snowflakes on the
    // silhouette edge -- individually visible polygons rather than foliage -- because a 0.42 m card
    // on a 21 m crown is a fifth of a cluster.
    int cardsPerCluster = 22;
    float leafSize = 0.150f;
    float clusterScale = 1.0f;
    // How the canopy divides between the three tints. NOT a third each: at equal shares the gold
    // read as autumn confetti across the whole crown rather than as the occasional accent the
    // palette asks for. Cumulative thresholds, so {0.58, 0.90} means 58% deep emerald, 32%
    // turquoise, 10% gold.
    std::array<float, kFoliageTints - 1> tintSplit{0.58f, 0.90f};
    // Cards are given normals pointing out of the cluster centre rather than off their own plane.
    // A flat card lit by its own normal reads as a flat card; lit by the volume's normal a cluster
    // of them reads as one soft mass, which is the standard foliage trick and costs nothing.
    float cardNormalBlend = 0.85f;
    // Clumps are stretched along their branch. A spherical clump reads as a ball stuck on a stick;
    // foliage grows along the twig that carries it, and the elongation is most of what separates
    // the two at a glance.
    float clusterElongation = 1.75f;
};


// One mesh per tier, each destined for its own `CompositionNode`.
struct TreeMeshes {
    MeshData trunk;
    MeshData primary;
    MeshData secondary;
    MeshData tertiary;
    std::array<MeshData, kFoliageTints> foliage;
    MeshData roots;
    std::uint32_t triangles = 0;
    double buildMs = 0.0;
    [[nodiscard]] std::vector<std::pair<std::string, const MeshData*>> parts() const;
};

[[nodiscard]] Result<TreeMeshes> buildTreeMeshes(const TreeGraph& graph, const TreeMeshSettings& settings);

// Appends `src` to `into`, offsetting indices. Nothing in the repository did this, which is a small
// gap but a real one: every caller that builds geometry in pieces needs it.
void appendMesh(MeshData& into, const MeshData& src);

} // namespace avgen::scene
