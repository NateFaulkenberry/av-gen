#pragma once

// The procedural tree generator: a self-organising developmental model.
//
// WHAT THIS IS, AND WHY IT IS NOT PLAIN SPACE COLONIZATION
// -------------------------------------------------------
// Runions, Lane & Prusinkiewicz's space colonization algorithm (2007) grows a skeleton by letting
// every node reach toward the attraction points nearest to it. Its defining property is that every
// node is *equal*: a node either has points in range or it does not. There is no quantity in the
// algorithm that says "this limb matters more than that twig", so nothing makes a trunk, and the
// branch statistics come out uniform -- which is the coral/noodle look. Every reference
// implementation compensates by prepending a straight trunk before colonization starts.
//
// Palubicki et al., "Self-organizing tree models for image synthesis" (SIGGRAPH 2009), keep space
// colonization as the *environmental* term and add the internal signalling it lacks. That is the
// model implemented here. Each iteration:
//
//   1. Environment. Each bud has a spherical occupancy zone (radius `occupancyRadius`) and a
//      conical perception volume (half-angle `perceptionAngle`, reach `perceptionDistance`).
//      Markers inside any bud's occupancy zone die. Each surviving marker is claimed by the nearest
//      bud whose cone contains it. The bud's space quality Q comes from how many it claimed, and
//      its optimal growth direction V is the mean direction to them.
//   2. Basipetal pass. Q flows toward the base and accumulates in every internode.
//   3. Acropetal pass. A resource v_base = alpha * Q_base flows back out. At every fork it splits
//      between the continuing main axis and the laterals by the extended Borchert-Honda rule,
//          v_m = v * lambda*Q_m / (lambda*Q_m + (1-lambda)*Q_l)
//          v_l = v * (1-lambda)*Q_l / (lambda*Q_m + (1-lambda)*Q_l)
//      lambda > 0.5 biases the main axis (excurrent: one dominant leader), lambda < 0.5 biases the
//      laterals (decurrent: spreading). This single number is what makes the hierarchy readable.
//   4. Bud fate. A bud holding resource v makes n = floor(v) metamers of length v/n. Branch length
//      is therefore an *output* of competition, not a parameter.
//   5. Shedding, then the pipe model for diameter.
//
// WHERE THIS DEPARTS FROM THE PAPER, DELIBERATELY
// -----------------------------------------------
// * Q IS GRADED, NOT BINARY. The paper's space-colonization Q is 0 or 1. With a binary Q the BH
//   split degenerates -- lambda*1/(lambda*1 + (1-lambda)*1) is just lambda regardless of what the
//   environment said -- and the paper itself notes binary Q makes Takenaka shedding unusable. Here
//   Q is the claimed-marker count normalised by `perceptionCapacity`, so competition stays graded
//   at every fork. The cost is that the paper's tuning numbers become starting points rather than
//   answers.
// * A BUD WITH NO SPACE STILL HAS A LITTLE VIGOUR (`quiescentQ`). Without it the seedling bud,
//   which starts below a crown envelope that holds no markers within reach, has Q = 0, receives
//   nothing, and the simulation deadlocks at one node. With it, and with apical control high early,
//   almost all of that trickle goes to the terminal bud: the trunk grows up toward the crown and
//   the laterals stay dormant. That is the excurrent young tree, generated rather than prepended.
// * NO SEASONS. We are not animating growth, so the prolepsis/syllepsis distinction that motivates
//   much of the paper's machinery buys nothing. One shoot per bud per iteration.
// * ROOTS ARE NOT GROWN BY THIS SIMULATION. Running it downward produces a mirrored crown, which
//   is a different bad look rather than a good one. Roots are guided splines seeded from the
//   canopy's own mass distribution; see `growRoots`.
//
// DETERMINISM. Every random draw is `noise::hashIndex(seed, index, channel)` over a stable index,
// never a stateful stream, and the spatial index (`spatial::PointGrid`) has no observable hash
// order. Generation is a pure function of `TreeParams`. The unit tests assert this by generating
// twice and comparing every node.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

// The semantic tier a branch belongs to. This is the animation hierarchy, so it is part of the
// generator's output rather than something the renderer works out later.
enum class BranchTier : std::uint8_t { Trunk, Primary, Secondary, Tertiary };
[[nodiscard]] const char* branchTierName(BranchTier tier);

inline constexpr std::uint32_t kNoNode = 0xFFFFFFFFu;

// The shape of the volume the crown colonises. The attraction cloud *is* the silhouette, so this is
// the most direct control over the tree's outline that the system has.
struct CrownEnvelope {
    float baseHeight = 11.5f;   // the crown ellipsoid does not reach below this
    float centreHeight = 21.5f; // centre of the ellipsoid
    float radius = 10.5f;       // horizontal semi-axis
    float halfHeight = 10.0f;   // vertical semi-axis
    // Vertical profile: the horizontal extent is scaled by a function of the normalised height in
    // the ellipsoid. 0 = a plain ellipsoid; positive values pull the underside in and push the
    // shoulders out, which is the broad-shouldered monumental outline rather than a ball.
    float shoulder = 0.45f;
    // Low-frequency density variation across the cloud. This is where controlled asymmetry comes
    // from: biasing *where there is space to grow* rather than jittering the branches afterwards.
    float lumpiness = 0.55f;
    float lumpScale = 0.13f;
    // A hollow at the centre of the crown keeps the interior readable instead of a solid mass.
    // THE ENVELOPE IS NOT A SURFACE OF REVOLUTION, AND THAT IS THE WHOLE POINT OF THESE THREE.
    //
    // It was one, and that turned out to be why every candidate in every search came out a rounded
    // ball on a straight trunk. Space colonization fills the envelope it is given, so the crown's
    // outline is the envelope's outline; an envelope whose radius is the same in every compass
    // direction cannot produce an asymmetric crown, and no search inside it can find one, because
    // asymmetry is not in the parameterisation at all. Widening the three bands that were suspected
    // of preferring balls reordered the ranking and surfaced nothing new -- which is what says the
    // problem was the space and not the scoring.
    //
    // `lumpiness` was supposed to supply this and does not: it thins marker density in regions, at
    // an amplitude and scale that never breaks the outline.
    //
    // Two angular harmonics with seeded phases give lobes that do not repeat, and a lateral offset
    // lets the crown sit off the trunk's axis the way an old tree's does.
    int crownLobes = 3;
    float crownLobeAmount = 0.26f;
    float crownOffset = 0.12f;      // as a fraction of the crown radius

    // A hollow at the centre of the crown. Not cosmetic: without it the foliage fills the volume the
    // primary limbs occupy and buries them, and the readable branching the reference is chosen for
    // is invisible however good the skeleton underneath is.
    float coreHollow = 0.30f;
    // The corridor of colonisable space running from the ground up to the crown.
    //
    // This is what makes the bole a *generated* structure rather than a prepended stick. Without
    // it the seedling bud sits below an envelope holding no markers within its perception distance,
    // receives nothing, and the simulation stops at one node -- which is exactly what happened the
    // first time this ran. Narrow enough that a lateral bud on the bole finds almost no space and
    // is shed, wide enough that the terminal bud always has somewhere to go. The trunk's height,
    // taper and slight lean are then all outputs of the same competition as everything else.
    float trunkCorridorRadius = 1.35f;
    float trunkCorridorBottom = 0.4f;
};

struct TreeParams {
    std::uint32_t seed = 1;

    // --- Envelope and marker cloud -------------------------------------------------------------
    CrownEnvelope crown{};
    int markerCount = 52000;

    // --- Simulation ----------------------------------------------------------------------------
    int iterations = 32;
    // The unit every other length is quoted in. It sets the tree's resolution: a crown of a given
    // volume supports roughly (crown volume / occupancy sphere volume) growing tips, so halving the
    // internode length multiplies the achievable branch count by about eight. 0.38 against a 9 m
    // crown radius is the point where the twig count is in the thousands and generation is still
    // well under a second.
    float internodeLength = 0.46f;
    float occupancyRadius = 1.6f;       // rho, in internode lengths (paper: 2)
    float perceptionDistance = 5.0f;    // r, in internode lengths (paper: 4 to 6)
    float perceptionAngle = 1.4f;       // theta, radians half-angle (paper: ~90 degrees)
    float perceptionCapacity = 22.0f;   // markers that saturate Q; see the header note on graded Q
    float quiescentQ = 0.07f;           // the trickle a bud with no space still receives
    // v_base = alpha * Q_base. Because Q_base is the sum over buds, the resource each bud ends up
    // with averages alpha * mean(Q) -- so alpha has to be read against how saturated the perception
    // volumes actually are, not as "metamers per bud". Below about 2 here the tree stalls; above
    // about 5 it exhausts the marker cloud in a handful of iterations and comes out coarse.
    float alpha = 3.4f;

    // Apical control. High early builds the bole; removing it later lets the crown spread. This
    // progression -- excurrent young tree to decurrent old tree -- is the paper's own account of
    // how a real temperate tree ages, and it is exactly the Tree of Life outline.
    float lambdaYoung = 0.62f;
    float lambdaOld = 0.42f;
    float apicalReleaseStart = 0.30f;   // fraction of the run where lambda begins to fall
    float apicalReleaseEnd = 0.75f;     // fraction where it has reached lambdaOld

    // --- Direction -----------------------------------------------------------------------------
    float defaultWeight = 1.0f;         // keep going the way the parent went
    float optimalWeight = 0.72f;        // xi: pull toward claimed space
    float tropismWeight = 0.30f;        // eta: the gravity/light preference
    glm::vec3 trunkTropism{0.0f, 1.0f, 0.0f};
    glm::vec3 branchTropism{0.0f, -0.42f, 0.0f}; // downward: with a strong optimal pull this is
                                                 // what produces gnarled decurrent limbs
    float outwardBias = 0.34f;          // push limbs away from the trunk axis
    float wanderAmount = 0.30f;         // fbm3 jitter on the growth direction
    float wanderScale = 0.22f;
    float branchAngle = 0.82f;          // radians a lateral bud leaves its parent internode at
    float phyllotaxis = 2.39996f;       // golden angle, radians

    // --- Shedding ------------------------------------------------------------------------------
    float shedThreshold = 0.11f;        // mean light per internode per iteration, below which a
                                        // branch is a liability to the tree (Takenaka 1994)
    int shedGrace = 5;                  // iterations a branch is exempt after it appears
    // Only the trunk is protected. A primary limb that stops reaching anything MUST be sheddable:
    // the tall clean bole of a monumental tree is a self-pruning artefact, and protecting order 1
    // leaves a bole covered in stunted low branches that never grew and never left.
    int shedMinOrder = 1;

    // --- Diameter ------------------------------------------------------------------------------
    float pipeExponent = 2.3f;          // n in d^n = d1^n + d2^n (Macdonald 1983: 2 to 3)
    float tipRadius = 0.028f;
    // BACK TO 1.0, AND THE ROUND TRIP IS THE POINT.
    //
    // This was 1.0, was cut to 0.55 because the pipe model with shed memory produced a 4.8 m bole on
    // a 30 m tree that read as a stump, and is now 1.0 again. That is not indecision: the earlier
    // judgement was correct about a tree that no longer exists. It was 23 m tall, the camera was
    // 44 degrees from 37 m -- nearly orthographic -- and the crown was a solid shell, so a thick
    // trunk had nothing to carry and everything to compete with.
    //
    // The tree is now 30 to 32 m, the camera is 55 degrees from 34 m and low, and the canopy is
    // alpha-cut sprays with air in it. Rendered side by side at 0.55, 0.78 and 1.0, the unscaled
    // model reads as a trunk carrying a crown and the reduced one reads as spindly: 12.9:1 height
    // to diameter at 0.55, 7.3:1 at 1.0, and a monumental broadleaf is nearer the second.
    //
    // The pipe model was faithful all along. What was wrong was the proportions around it.
    float radiusScale = 1.0f;
    float trunkFlare = 0.55f;           // extra radius at the very base, as a fraction
    float flareHeight = 3.0f;           // the height it decays over

    // --- Foliage attachment --------------------------------------------------------------------
    //
    // FOLIAGE HANGS AT THE ENDS OF LIMBS, WITH ENFORCED GAPS BETWEEN CLUMPS.
    //
    // The first design put a cluster on every eligible TIP NODE and thinned them randomly. Tips are
    // everywhere inside the crown volume, so the result was a continuous shell sitting on top of the
    // branches with limbs poking through it -- one mass, not distinct clumps, and the readable
    // branch structure the chosen reference exists for was invisible. Thinning that arrangement does
    // not fix it: a randomly thinned shell is a shell with holes in it, and enlarging the clusters
    // to compensate buries the limbs again (measured, and recorded as a regression).
    //
    // What produces clumps is placing them at the END OF AN AXIS -- one per limb, not one per twig
    // -- and then rejecting any clump within `foliageSpacing` of one already placed. That is a
    // Poisson-disc condition, and it is the only thing here that can guarantee a gap. Candidates are
    // considered thickest-limb-first, so when two compete the substantial limb keeps its foliage.
    int foliageMinOrder = 2;            // order 2 = the ends of secondary axes
    float foliageMaxRadius = 0.16f;     // an axis thicker than this at its tip is structure
    // Minimum distance between clump centres. The gap-maker. Must comfortably exceed twice the
    // cluster radius or the clumps touch and the canopy closes again.
    float foliageSpacing = 1.7f;
    // Cluster radius, as a fraction of half the spacing. ABOVE 1 ON PURPOSE: at 0.55 to 0.92 the
    // clumps stood apart as discrete spheres and the result was pom-poms on bare branches -- the
    // opposite overcorrection to the shell, and just as wrong. What the reference actually shows is
    // masses that MERGE IN GROUPS and leave voids between the groups, which needs clumps that
    // overlap their neighbours while the spacing rule and the size variation keep the groups apart.
    float foliageClusterScale = 1.15f;
    // A final random drop, so some limb ends are bare and the canopy is not a regular lattice.
    float foliageFraction = 0.86f;
    // The lowest part of the crown carries no foliage, as a fraction of crown height. The primary
    // limbs leave the trunk and rise through exactly that region, and clumps hung there bury the
    // one piece of structure the whole composition is built around. Clearing it is what lets the
    // limbs be seen ARRIVING in the canopy rather than merely poking out of it.
    float foliageLowerClear = 0.26f;

    // --- Foliage colour ---------------------------------------------------------------------
    //
    // Tints are assigned from a LOW-FREQUENCY SPATIAL FIELD, not per clump independently. A 10%
    // share sprinkled uniformly reads as speckle -- confetti through the whole canopy -- rather than
    // as an accent, because an accent is a thing with a location. Sampling a noise field at the
    // clump's position clusters each tint into regions a few limbs across, which is what an accent
    // looks like.
    float tintFieldScale = 0.17f;  // period ~6 m: a few limbs' worth per region, not one big blob
    // SHARE AND PLACEMENT TOGETHER, NOT SHARE ALONE. Twelve per cent with a location was still an
    // accent nobody could see at the showcase distance -- it read as speckle, which is the same
    // failure as the uniform sprinkle it replaced, arrived at from the other side. The reference's
    // power comes substantially from warm against cool dark, so the warm has to occupy enough area
    // to register as a colour rather than as noise.
    float goldShare = 0.30f;
    float turquoiseShare = 0.26f;
    // And it goes where the light is. Biasing the tint field along the key's incoming direction
    // puts the warm mass in the lit half of the crown, which is both where the composition wants
    // the eye and where warm light would actually fall. This couples the generator to the light
    // rig, deliberately: an accent's placement is a composition decision, and composition knows
    // which way the key points.
    glm::vec3 tintLitDirection{0.42f, 0.0f, 0.60f};
    float tintLitBias = 0.50f;

    // --- Roots ---------------------------------------------------------------------------------
    // BUTTRESSES, NOT PIPES. The first design ran each root out from ground level and dived it
    // immediately, so the base of the tree was a small cone and the roots were buried within a
    // metre. That is the least developed part of the picture and it is also the part of a
    // monumental tree that says "old" most directly, and the only place where geometry at the
    // showcase camera is large enough to read at all.
    //
    // A buttress is a FIN: it meets the trunk high up, descends steeply, and only then runs out
    // along the ground. The centreline then undulates about the surface with a decaying amplitude,
    // so a root is alternately visible and buried along its length -- which is section 8's
    // "partially disappear into the ground" and is also what carves the negative space it asks for.
    int rootCount = 13;
    float rootAttachHeight = 2.6f;  // how far up the trunk the highest buttress meets it
    float rootUndulation = 0.78f;   // how far the centreline rises and falls about the surface
    float rootWaves = 2.4f;         // undulations over a root's length
    float rootSpread = 9.5f;
    // Shallow. At 1.9 m the sink term buried every root within a couple of metres of the trunk and
    // the base read as a ring of stubs; a buttress root runs a long way at or just under the
    // surface before it finally goes down, and that run is the whole of what makes a tree look
    // anchored.
    float rootDepth = 1.0f;
    float rootCurvature = 0.55f;
    // Fraction of the trunk base radius the largest root starts at. Reduced from 0.85 when the
    // trunk went back to the unscaled pipe model: the roots are sized FROM the trunk, so widening
    // it widened them by the same factor, and they came out heavier than anything the user was
    // shown when they chose this candidate. 0.52 against a 2.12 m base is 1.10 m, which is where
    // the roots sat at 0.85 against the 1.17 m base of the contact sheet they judged.
    float rootRadiusScale = 0.52f;
    int rootSegments = 16;
    float rootSplit = 0.45f;            // probability a root forks once
    // How far root directions follow the canopy's own mass distribution (brief section 38: the
    // tree should read as one organism). 0 spreads roots evenly, 1 puts them entirely under the
    // heaviest limbs.
    float rootCanopyCoupling = 0.6f;

    // --- Guards --------------------------------------------------------------------------------
    int maxNodes = 90000;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<TreeParams> fromJson(const nlohmann::json& j);
};

// One internode. `parent` is the node it grew from; `axis` identifies the continuous branch run it
// belongs to, so a whole limb can be addressed without walking the graph.
struct TreeNode {
    std::uint32_t id = 0;
    std::uint32_t parent = kNoNode;
    std::uint32_t axis = 0;
    std::uint8_t order = 0;      // axis order: 0 is the trunk
    BranchTier tier = BranchTier::Trunk;
    std::uint16_t birth = 0;     // the iteration it appeared in
    glm::vec3 position{0.0f};    // the far end of the internode
    glm::vec3 direction{0.0f, 1.0f, 0.0f}; // unit, parent -> this
    float length = 0.0f;
    float radius = 0.0f;
    float parentRadius = 0.0f;
    float distanceFromBase = 0.0f;    // arc length along the graph
    float distanceAlongAxis = 0.0f;   // arc length since this axis started
    // Animation data, produced here so the animator never has to re-derive it (brief section 39).
    float phase = 0.0f;              // per-node phase offset, decorrelates neighbouring branches
    float animationWeight = 0.0f;    // how far this node moves: 0 at the base, 1 at a tip
    float audioResponseWeight = 0.0f;
    std::vector<std::uint32_t> children;
};

// A maximal run of nodes that share an axis: one limb, from where it left its parent to its tip.
struct TreeAxis {
    std::uint32_t id = 0;
    std::uint32_t parentAxis = kNoNode;
    std::uint8_t order = 0;
    BranchTier tier = BranchTier::Trunk;
    std::uint32_t firstNode = kNoNode;
    std::vector<std::uint32_t> nodes; // in order from the fork outward
    float length = 0.0f;
    float baseRadius = 0.0f;
    glm::vec3 baseDirection{0.0f, 1.0f, 0.0f};
    float divergenceAngle = 0.0f;    // radians between this axis and its parent at the fork
};

// Where a foliage cluster attaches. The cluster is the unit of animation and of instancing; the
// leaf is the unit of geometry (brief section 12).
struct FoliageSite {
    std::uint32_t node = kNoNode;
    std::uint32_t axis = kNoNode;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float radius = 1.0f;
    float phase = 0.0f;
    float exposure = 0.0f;   // 0 deep inside the crown, 1 on the outer surface
    std::uint8_t tint = 0;   // which of the canopy's material tints this clump takes
};

// A root is a polyline, not a simulated axis; see the header note.
struct RootStrand {
    std::uint32_t id = 0;
    std::uint32_t parent = kNoNode;   // kNoNode for a root that starts at the trunk
    std::vector<glm::vec3> points;
    std::vector<float> radii;
    float length = 0.0f;
};

struct TreeStats {
    int nodeCount = 0;
    int axisCount = 0;
    int trunkNodes = 0, primaryNodes = 0, secondaryNodes = 0, tertiaryNodes = 0;
    int primaryAxes = 0, secondaryAxes = 0, tertiaryAxes = 0;
    int foliageSites = 0;
    int rootStrands = 0;
    int shedNodes = 0;
    int maxOrder = 0;
    float height = 0.0f;
    float crownWidth = 0.0f;
    float trunkHeight = 0.0f;     // base to the first fork that starts a primary axis
    float trunkBaseRadius = 0.0f;
    float totalBranchLength = 0.0f;
    double generationMs = 0.0;
};

struct TreeGraph {
    TreeParams params{};
    std::vector<TreeNode> nodes;
    std::vector<TreeAxis> axes;
    std::vector<FoliageSite> foliage;
    std::vector<RootStrand> roots;
    TreeStats stats{};
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    [[nodiscard]] bool empty() const { return nodes.empty(); }
    // Checks the invariants the tests assert: parents precede children, no cycles, every axis
    // membership agrees with its nodes. Returns the first violation found.
    [[nodiscard]] Result<void> validateTopology() const;
    // A cheap order-sensitive digest of the geometry, for "did this change" comparisons.
    [[nodiscard]] std::uint64_t contentHash() const;
};

// Generates a tree. Pure: the same params always produce the same graph.
[[nodiscard]] Result<TreeGraph> generateTree(const TreeParams& params);

// The marker cloud on its own, for the debug view (brief section 32) and for tests.
[[nodiscard]] std::vector<glm::vec3> generateMarkers(const TreeParams& params);

// Roots for an already-grown crown. Separated so the coupling in section 38 of the brief -- roots
// that answer the canopy rather than mirror it -- is visible as an argument rather than buried in
// the simulation. `graph.nodes` and `graph.stats` must already be populated.
[[nodiscard]] std::vector<RootStrand> growRoots(const TreeGraph& graph);

} // namespace avgen::scene
