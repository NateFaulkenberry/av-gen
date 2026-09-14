#include "scene/tree_generator.hpp"

#include "core/noise.hpp"
#include "search/candidate_search.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace avgen::scene {
namespace {

constexpr float kEpsilon = 1e-6f;

float lerp(float a, float b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

struct Projected {
    glm::vec2 pixel{0.0f};
    float radiusPixels = 0.0f;
    float viewDepth = 0.0f;
    bool visible = false;
};

Projected project(const glm::mat4& viewProjection, const glm::vec3& world, float radius, int width, int height,
                  float projScale) {
    Projected out;
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
    if (clip.w <= kEpsilon) {
        return out;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    out.pixel = {(ndc.x * 0.5f + 0.5f) * static_cast<float>(width),
                 (0.5f - ndc.y * 0.5f) * static_cast<float>(height)};
    out.viewDepth = clip.w;
    // A sphere of world radius r at view depth w subtends r / w * projScale pixels vertically.
    // Using the real radius at the real depth is what makes the trunk-versus-canopy metrics mean
    // anything: a thick near branch and a thin far one must not measure the same.
    out.radiusPixels = radius / clip.w * projScale;
    out.visible = true;
    return out;
}

void stampDisc(TreeSilhouette& target, const glm::vec2& centre, float radius, std::uint8_t value) {
    const float r = std::max(radius, 0.5f);
    const int x0 = std::max(0, static_cast<int>(std::floor(centre.x - r)));
    const int x1 = std::min(target.width - 1, static_cast<int>(std::ceil(centre.x + r)));
    const int y0 = std::max(0, static_cast<int>(std::floor(centre.y - r)));
    const int y1 = std::min(target.height - 1, static_cast<int>(std::ceil(centre.y + r)));
    const float r2 = r * r;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - centre.x;
            const float dy = static_cast<float>(y) + 0.5f - centre.y;
            if (dx * dx + dy * dy > r2) {
                continue;
            }
            std::uint8_t& cell = target.mask[static_cast<std::size_t>(y) * target.width + x];
            // Branch wins over foliage: the metrics that separate structure from canopy need to
            // know which is which where they overlap, and a branch seen through leaves is still a
            // branch as far as "is the hierarchy readable" is concerned.
            if (cell == 0 || (cell == 2 && value == 1)) {
                cell = value;
            }
        }
    }
}

void stampSegment(TreeSilhouette& target, const Projected& a, const Projected& b, std::uint8_t value) {
    if (!a.visible || !b.visible) {
        return;
    }
    const float length = glm::distance(a.pixel, b.pixel);
    if (value == 1) {
        // A capsule's projected area is length times mean diameter. Accumulated before the clamp
        // the raster applies, so it stays proportional to the real branch even when the branch is
        // narrower than a pixel.
        target.branchArea += static_cast<double>(length) * static_cast<double>(a.radiusPixels + b.radiusPixels);
    }
    const int steps = std::clamp(static_cast<int>(length) + 1, 1, 512);
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        stampDisc(target, glm::mix(a.pixel, b.pixel, t), lerp(a.radiusPixels, b.radiusPixels, t), value);
    }
}

} // namespace

glm::mat4 TreeCameraView::viewProjection() const {
    return glm::perspective(fovYRadians, aspect(), 0.1f, 500.0f) * glm::lookAt(eye, target, up);
}

float TreeCameraView::aspect() const {
    return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
}

TreeSilhouette rasteriseTree(const TreeGraph& graph, const TreeCameraView& camera) {
    TreeSilhouette out;
    out.width = std::max(camera.width, 8);
    out.height = std::max(camera.height, 8);
    out.mask.assign(static_cast<std::size_t>(out.width) * out.height, 0);
    if (graph.nodes.empty()) {
        return out;
    }

    const glm::mat4 vp = camera.viewProjection();
    const float projScale = static_cast<float>(out.height) / (2.0f * std::tan(camera.fovYRadians * 0.5f));

    std::vector<Projected> projected(graph.nodes.size());
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        projected[i] = project(vp, graph.nodes[i].position, graph.nodes[i].radius, out.width, out.height, projScale);
    }
    for (const TreeNode& node : graph.nodes) {
        if (node.parent == kNoNode) {
            continue;
        }
        stampSegment(out, projected[node.parent], projected[node.id], 1);
    }
    for (const RootStrand& root : graph.roots) {
        for (std::size_t i = 1; i < root.points.size(); ++i) {
            // Only the part of a root above ground is in the silhouette; the buried half is not
            // seen and must not be measured as if it were.
            if (root.points[i - 1].y < -0.05f && root.points[i].y < -0.05f) {
                continue;
            }
            stampSegment(out,
                         project(vp, root.points[i - 1], root.radii[i - 1], out.width, out.height, projScale),
                         project(vp, root.points[i], root.radii[i], out.width, out.height, projScale), 1);
        }
    }
    for (const FoliageSite& site : graph.foliage) {
        const Projected p = project(vp, site.position, site.radius, out.width, out.height, projScale);
        if (p.visible) {
            out.foliageArea += glm::pi<double>() * static_cast<double>(p.radiusPixels) * p.radiusPixels;
            stampDisc(out, p.pixel, p.radiusPixels, 2);
        }
    }

    out.minX = out.width;
    out.minY = out.height;
    out.maxX = -1;
    out.maxY = -1;
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            if (out.mask[static_cast<std::size_t>(y) * out.width + x] == 0) {
                continue;
            }
            ++out.filled;
            if (out.mask[static_cast<std::size_t>(y) * out.width + x] == 1) {
                ++out.branchPixels;
            }
            out.minX = std::min(out.minX, x);
            out.maxX = std::max(out.maxX, x);
            out.minY = std::min(out.minY, y);
            out.maxY = std::max(out.maxY, y);
        }
    }
    return out;
}

TreeMeasurements measureTree(const TreeGraph& graph, const TreeSilhouette& s, const TreeCameraView& camera) {
    TreeMeasurements m;
    if (s.empty() || graph.nodes.empty()) {
        return m;
    }

    const int boxW = s.maxX - s.minX + 1;
    const int boxH = s.maxY - s.minY + 1;
    m.frameFill = static_cast<float>(s.filled) / static_cast<float>(s.width * s.height);
    m.boxFill = static_cast<float>(s.filled) / static_cast<float>(std::max(boxW * boxH, 1));
    m.aspect = static_cast<float>(boxW) / static_cast<float>(std::max(boxH, 1));

    // Left/right balance and the vertical centre of mass, both taken about the silhouette's own
    // bounding box rather than the frame, so they describe the tree and not where it was placed.
    double left = 0.0;
    double right = 0.0;
    double sumY = 0.0;
    const float midX = static_cast<float>(s.minX + s.maxX) * 0.5f;
    for (int y = s.minY; y <= s.maxY; ++y) {
        for (int x = s.minX; x <= s.maxX; ++x) {
            if (s.mask[static_cast<std::size_t>(y) * s.width + x] == 0) {
                continue;
            }
            (static_cast<float>(x) < midX ? left : right) += 1.0;
            sumY += static_cast<double>(y);
        }
    }
    const double mass = left + right;
    m.balance = mass > 0.0 ? static_cast<float>(std::abs(left - right) / mass) : 0.0f;
    // 0 at the bottom of the box, 1 at the top: the image y axis runs the other way.
    m.verticalCentroid =
        mass > 0.0 ? 1.0f - static_cast<float>((sumY / mass - s.minY) / std::max(boxH - 1, 1)) : 0.0f;

    // Openness: per column, the fraction of the span between the topmost and bottommost filled
    // pixel that is empty. This is the brief's "meaningful negative space" as a number. Too little
    // and the canopy is an indistinguishable mass; too much and the tree has a dead central void.
    double interior = 0.0;
    double interiorEmpty = 0.0;
    for (int x = s.minX; x <= s.maxX; ++x) {
        int top = -1;
        int bottom = -1;
        for (int y = s.minY; y <= s.maxY; ++y) {
            if (s.mask[static_cast<std::size_t>(y) * s.width + x] != 0) {
                if (top < 0) {
                    top = y;
                }
                bottom = y;
            }
        }
        if (top < 0) {
            continue;
        }
        for (int y = top; y <= bottom; ++y) {
            interior += 1.0;
            if (s.mask[static_cast<std::size_t>(y) * s.width + x] == 0) {
                interiorEmpty += 1.0;
            }
        }
    }
    m.openness = interior > 0.0 ? static_cast<float>(interiorEmpty / interior) : 0.0f;

    // Isoperimetric quotient of the silhouette: perimeter^2 / (4 pi area). A disc is 1. A blob
    // tends toward 1; spaghetti runs to hundreds. This is the single number that separates the two
    // failure modes at either end of the brief's section 5 list, which is why it is banded on both
    // sides rather than maximised.
    double perimeter = 0.0;
    for (int y = s.minY; y <= s.maxY; ++y) {
        for (int x = s.minX; x <= s.maxX; ++x) {
            if (s.mask[static_cast<std::size_t>(y) * s.width + x] == 0) {
                continue;
            }
            const bool edge = x == 0 || x == s.width - 1 || y == 0 || y == s.height - 1 ||
                              s.mask[static_cast<std::size_t>(y) * s.width + (x - 1)] == 0 ||
                              s.mask[static_cast<std::size_t>(y) * s.width + (x + 1)] == 0 ||
                              s.mask[static_cast<std::size_t>(y - 1) * s.width + x] == 0 ||
                              s.mask[static_cast<std::size_t>(y + 1) * s.width + x] == 0;
            if (edge) {
                perimeter += 1.0;
            }
        }
    }
    m.silhouetteComplexity =
        s.filled > 0 ? static_cast<float>(perimeter * perimeter / (4.0 * glm::pi<double>() * s.filled)) : 0.0f;

    // Depth: how far the CANOPY spreads along the camera's forward axis, relative to how far it
    // spreads across the frame. This is the number that says the scene is genuinely 3D rather than
    // a flat cut-out, and it has a lower bound for exactly that reason.
    //
    // Measured over foliage sites, unweighted. The first version weighted every node by its
    // cross-sectional area, on the reasoning that mass is what you see -- and produced 0.07 to 0.08
    // for all seventy-two candidates of a search, i.e. a component that scored its floor for the
    // entire population and therefore contributed nothing to the ranking at all. The cause is that
    // r^2 weighting hands the trunk some thousands of times the weight of a twig, and the trunk is
    // a vertical line at the origin with almost no depth extent, so the metric was measuring the
    // trunk's thickness rather than the canopy's depth. Normalising against the canopy's horizontal
    // spread rather than the bounding diagonal also makes the number answer the question actually
    // being asked: is the canopy as deep as it is wide?
    const glm::vec3 forward = glm::normalize(camera.target - camera.eye);
    const glm::vec3 across = glm::normalize(glm::cross(forward, camera.up));
    if (graph.foliage.size() >= 8) {
        double depthSum = 0.0;
        double depthSq = 0.0;
        double acrossSum = 0.0;
        double acrossSq = 0.0;
        for (const FoliageSite& site : graph.foliage) {
            const auto d = static_cast<double>(glm::dot(forward, site.position));
            const auto a = static_cast<double>(glm::dot(across, site.position));
            depthSum += d;
            depthSq += d * d;
            acrossSum += a;
            acrossSq += a * a;
        }
        const auto n = static_cast<double>(graph.foliage.size());
        const double depthVar = std::max(depthSq / n - (depthSum / n) * (depthSum / n), 0.0);
        const double acrossVar = std::max(acrossSq / n - (acrossSum / n) * (acrossSum / n), 0.0);
        m.depthSpread = acrossVar > 1e-9 ? static_cast<float>(std::sqrt(depthVar / acrossVar)) : 0.0f;
    }


    // Hierarchy, straight from the graph. A primary axis only counts if it is thick enough to read
    // as a limb: counting every order-1 axis rewards the generator for sprouting twigs off the
    // trunk, which is the wrong thing to measure and the easiest thing to game.
    // The tier is now assigned by substance rather than by graph order, so the extra radius
    // threshold that used to stand in for "reads as a limb" is the tier's own definition and
    // applying it twice would just raise the bar arbitrarily.
    int primaries = 0;
    float widestPrimary = 0.0f;
    for (const TreeAxis& axis : graph.axes) {
        if (axis.tier == BranchTier::Primary) {
            ++primaries;
            widestPrimary = std::max(widestPrimary, axis.baseRadius);
        }
    }
    m.primaryCount = static_cast<float>(primaries);
    m.secondaryPerPrimary =
        primaries > 0 ? static_cast<float>(graph.stats.secondaryAxes) / static_cast<float>(primaries) : 0.0f;
    m.tertiaryPerSecondary = graph.stats.secondaryAxes > 0 ? static_cast<float>(graph.stats.tertiaryAxes) /
                                                                 static_cast<float>(graph.stats.secondaryAxes)
                                                           : 0.0f;
    m.boleFraction = graph.stats.height > kEpsilon ? graph.stats.trunkHeight / graph.stats.height : 0.0f;
    m.trunkDominance = widestPrimary > kEpsilon ? graph.stats.trunkBaseRadius / widestPrimary : 0.0f;

    float rootReach = 0.0f;
    for (const RootStrand& root : graph.roots) {
        for (const glm::vec3& p : root.points) {
            rootReach = std::max(rootReach, glm::length(glm::vec2(p.x, p.z)));
        }
    }
    m.rootSpreadRatio = graph.stats.crownWidth > kEpsilon ? rootReach / (graph.stats.crownWidth * 0.5f) : 0.0f;

    // How much of the tree the viewer sees as STRUCTURE rather than as canopy. The rasteriser lets
    // a branch win over foliage wherever they overlap, so this is the fraction of the silhouette
    // where a limb is in front of, or clear of, the leaves.
    //
    // This is the reference image's defining property -- readable radial branching seen through the
    // crown, not a solid glowing mass -- turned into a number the search can rank on. `openness`
    // measures holes in the silhouette; this measures how much of what is drawn is limb rather than
    // leaf. They are different failures: a crown can be full of gaps and still show no branch
    // structure at all.
    //
    // Taken from the analytic projected areas, NOT from the mask. Counting branch pixels put every
    // candidate at 0.53 to 0.71 against a band whose ideal topped out at 0.48, and no setting of
    // either foliage control could reach it -- because the raster clamps a disc to half a pixel so
    // a thin branch does not disappear, and a tree with thirteen hundred sub-pixel twigs therefore
    // reports thirteen hundred whole pixels of structure it does not have. Same family of mistake
    // as the r-squared weighting in depthSpread: a measurement that was really reporting an artefact
    // of how it was taken.
    const double drawn = s.branchArea + s.foliageArea;
    m.structureVisible = drawn > 0.0 ? static_cast<float>(s.branchArea / drawn) : 0.0f;
    return m;
}

const std::vector<TreeBand>& treeBands() {
    // EVERY BAND IS BOUNDED ON BOTH SIDES. A selection stage that ranks by score is an optimiser
    // over whatever the score measures, so a component that merely increases is an instruction to
    // maximise it. `search::ScoreBand` has no monotone form, so the rule is enforced by the type.
    //
    // The bands are set for the chosen reference: a dominant trunk, a luminous core, strong radial
    // branching and a clear visual hierarchy -- structure READABLE THROUGH the foliage rather than
    // a solid mass, at 30 to 35 metres rather than 23.
    static const std::vector<TreeBand> bands = {
        {"frameFill",
         "How much of the frame the tree occupies. Too little and the hero does not dominate the "
         "shot; too much and it is cropped and unreadable.",
         0.08f,
         {0.07f, 0.14f, 0.30f, 0.44f}},
        {"boxFill",
         "Silhouette area over its own bounding box. This is the skinny/blob axis: below the band "
         "the tree is a wispy stick figure, above it the canopy has merged into one indistinct mass "
         "and no branch is individually readable.",
         0.10f,
         {0.14f, 0.24f, 0.42f, 0.58f}},
        {"aspect",
         "Silhouette width over height. Banded for the monumental target -- appreciably taller than "
         "it is wide, but not a pole. A 32 m tree with a 21 m crown is 0.66.",
         0.08f,
         {0.55f, 0.72f, 1.05f, 1.30f}},
        {"balance",
         "Absolute left/right mass difference. Banded away from ZERO as well as from large values: "
         "a perfectly symmetric tree reads as manufactured, which is the controlled-asymmetry "
         "requirement stated as a measurement.",
         0.06f,
         {0.0f, 0.035f, 0.17f, 0.32f}},
        {"verticalCentroid",
         "Height of the centre of mass within the silhouette. The upper bound is the top-heavy "
         "failure; the lower bound is a canopy that has slumped into the roots.",
         0.06f,
         {0.40f, 0.52f, 0.70f, 0.82f}},
        {"openness",
         "Fraction of the silhouette's interior that is empty. Meaningful negative space as a "
         "number: too little is the indistinguishable mass, too much is the dead central void.",
         0.09f,
         {0.10f, 0.20f, 0.40f, 0.58f}},
        {"structureVisible",
         "Projected branch area over total projected area. The chosen reference's defining property "
         "-- readable radial branching through the crown -- and banded at both ends because a tree "
         "that is all visible structure has no canopy at all. The band is CALIBRATED TO THE "
         "ACHIEVABLE RANGE, not validated: a real tree's branch surface is a small share of its "
         "leaf surface, and the whole design space lands between 0.05 and 0.35, so the ideal sits "
         "in the upper-middle of that, favouring visible structure. Whether a person agrees is what "
         "the contact sheet is for.",
         0.11f,
         {0.05f, 0.14f, 0.30f, 0.45f}},
        {"depthSpread",
         "Canopy depth along the camera's forward axis over canopy spread across it. 1.0 is a crown "
         "as deep as it is wide. The lower bound stops a tree that has flattened itself toward the "
         "camera; the upper bound stops one stretched down the view axis, which reads as thin from "
         "the only viewpoint that matters. This one is a GUARD, not a discriminator: it correctly "
         "reports that nothing in this design space is flat, and so scores near full marks for the "
         "whole population. Its weight is set low for exactly that reason.",
         0.03f,
         {0.45f, 0.70f, 1.25f, 1.70f}},
        {"primaryCount",
         "Primary limbs thick enough to read as limbs. The brief asks for 7 to 14; the band is a "
         "little wider because the threshold for 'reads as a limb' is itself a judgement.",
         0.08f,
         {4.0f, 7.0f, 15.0f, 22.0f}},
        {"secondaryPerPrimary",
         "Secondary axes per primary limb. Below the band the limbs are bare; above it the "
         "intermediate scale has become noise.",
         0.05f,
         {3.0f, 6.0f, 18.0f, 30.0f}},
        {"tertiaryPerSecondary",
         "Tertiary axes per secondary. Same argument one level down: this is where canopy "
         "subdivision and foliage attachment come from.",
         0.04f,
         {2.0f, 4.0f, 12.0f, 20.0f}},
        {"boleFraction",
         "Clear trunk height over total height. The lower bound is a tree that branches at the "
         "ground; the upper bound is a lollipop on a pole. Raised for the monumental target: the "
         "trunk has to read as trunk before the crown begins.",
         0.09f,
         {0.20f, 0.30f, 0.45f, 0.56f}},
        {"trunkDominance",
         "Trunk base radius over the widest primary limb's. The lower bound is the trunk failing to "
         "dominate; the upper bound is limbs too spindly to carry the crown they hold.",
         0.07f,
         {1.4f, 2.1f, 4.6f, 7.0f}},
        {"silhouetteComplexity",
         "Isoperimetric quotient, perimeter squared over 4 pi area. A disc scores 1. The "
         "blob-to-spaghetti axis in one number, and the component most directly aimed at the "
         "noodle-tree failure.",
         0.04f,
         {1.8f, 4.0f, 18.0f, 34.0f}},
        {"rootSpreadRatio",
         "Root reach over crown half-width. Anchors the tree visually; the upper bound stops the "
         "roots becoming a second crown lying on the ground.",
         0.02f,
         {0.22f, 0.40f, 0.80f, 1.10f}},
    };
    return bands;
}

search::GeneratorSchema treeSchema() {
    // ORDER IS LOAD-BEARING AND IS NOT ALPHABETICAL. A low-discrepancy sequence's low-dimensional
    // projections are much better than its high-dimensional ones, so the most visually influential
    // parameter is index 0 and the seed -- which changes the arrangement but not the architecture --
    // is last.
    search::GeneratorSchema schema;
    schema.generatorName = "tree";
    schema.generatorVersion = 1;
    schema.parameters = search::ParameterSchema({
        {"height", 28.0f, 35.0f, false, "Total height in metres. The monumental axis."},
        {"crownRadius", 9.0f, 12.5f, false, "Horizontal semi-axis of the crown envelope."},
        {"boleFraction", 0.40f, 0.56f, false,
         "Clear trunk as a fraction of height, before the first limb. Raised: at 0.30 to 0.46 the "
         "frame showed one part trunk to two parts crown, which are the proportions of an orchard "
         "tree however tall the generated numbers say it is."},
        {"foliageSpacing", 1.15f, 2.60f, false,
         "Minimum distance between foliage clumps. The gap-maker, and the single most visually "
         "decisive parameter in the schema, which is why it is this near the front."},
        {"foliageLowerClear", 0.10f, 0.34f, false,
         "How much of the crown's lower height carries no foliage. The primary limbs rise through "
         "exactly that region, so this decides whether they are seen arriving in the canopy or "
         "merely poking out of it."},
        {"foliageClusterScale", 0.85f, 1.55f, false,
         "Clump radius as a fraction of half the spacing. Above 1 the clumps overlap their "
         "neighbours and merge into irregular masses, which is what the reference shows; below it "
         "they stand apart as discrete balls."},
        {"outwardBias", 0.30f, 0.75f, false, "How hard limbs are pushed away from the trunk axis."},
        {"lambdaYoung", 0.56f, 0.70f, false, "Apical control early: how hard the bole is driven."},
        {"lambdaOld", 0.34f, 0.48f, false, "Apical control late: how far the crown spreads."},
        {"branchDrop", -0.70f, -0.15f, false,
         "Downward tropism on branches. With a strong pull toward light this is what gnarls them."},
        {"branchAngle", 0.60f, 1.05f, false, "Radians a lateral bud leaves its parent internode at."},
        {"shoulder", 0.30f, 0.95f, false, "Crown profile: 0 is an ellipsoid, high lifts the widest point."},
        {"lumpiness", 0.45f, 0.95f, false, "Low-frequency density variation in the crown. The asymmetry knob."},
        {"shedThreshold", 0.06f, 0.22f, false, "Self-pruning, relative to the crown's own mean."},
        {"alpha", 2.9f, 4.0f, false, "Resource scale. Controls how fast the marker cloud is consumed."},
        {"rootCanopyCoupling", 0.35f, 0.90f, false, "How far root directions follow the canopy's mass."},
        {"seed", 0.0f, 4095.0f, true, "Which arrangement of this architecture. Last, deliberately."},
    });
    schema.featureNames = {"aspect",      "boxFill",          "balance",     "verticalCentroid", "openness",
                           "depthSpread", "structureVisible", "primaryAxes", "boleFraction",     "complexity"};
    return schema;
}

TreeParams treeFixedParams() {
    TreeParams p;
    p.markerCount = 52000;
    p.iterations = 34;
    p.internodeLength = 0.46f;
    p.tipRadius = 0.030f;
    return p;
}

Result<TreeParams> treeParamsFrom(std::span<const float> v) {
    if (v.size() < 17) {
        return fail("tree: expected 17 parameters, got {}", v.size());
    }
    TreeParams p = treeFixedParams();
    const float height = v[0];
    p.crown.radius = v[1];
    const float boleFraction = v[2];
    p.foliageSpacing = v[3];
    p.foliageLowerClear = v[4];
    p.foliageClusterScale = v[5];
    p.outwardBias = v[6];
    p.lambdaYoung = v[7];
    p.lambdaOld = v[8];
    p.branchTropism = glm::vec3(0.0f, v[9], 0.0f);
    p.branchAngle = v[10];
    p.crown.shoulder = v[11];
    p.crown.lumpiness = v[12];
    p.shedThreshold = v[13];
    p.alpha = v[14];
    p.rootCanopyCoupling = v[15];
    p.seed = 1u + static_cast<std::uint32_t>(std::lround(v[16]));

    // Height and bole are authored as a total and a fraction, then converted into the envelope the
    // generator wants. Sampling the envelope's own fields directly lets the search produce a crown
    // whose base sits above its top, and every such candidate is a wasted evaluation.
    p.crown.baseHeight = height * boleFraction;
    p.crown.halfHeight = std::max((height - p.crown.baseHeight) * 0.5f, 1.0f);
    p.crown.centreHeight = p.crown.baseHeight + p.crown.halfHeight;
    p.crown.trunkCorridorRadius = std::max(p.crown.radius * 0.13f, 0.6f);
    // A substantial hollow. Fewer, larger clusters hung on the outside of a hollow crown is what
    // turns one continuous canopy volume into distinct masses on distinct limbs -- the difference
    // between a hedge and an architectural tree, and the reference's defining quality.
    p.crown.coreHollow = 0.45f;
    p.rootSpread = p.crown.radius * 0.78f;
    p.flareHeight = std::max(p.crown.baseHeight * 0.26f, 1.2f);
    if (auto ok = p.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return p;
}

TreeGenerator::TreeGenerator(TreeCameraView camera, TreeMeshSettings mesh)
    : schema_(treeSchema()), camera_(camera), mesh_(mesh) {}

const TreeGraph* TreeGenerator::ensureGraph(const search::Parameters& params) const {
    if (cache_ && cache_->key == params) {
        return &cache_->graph;
    }
    auto treeParams = treeParamsFrom(params);
    if (!treeParams) {
        return nullptr;
    }
    auto graph = generateTree(*treeParams);
    if (!graph) {
        return nullptr;
    }
    cache_ = Cache{params, std::move(*graph), {}, false};
    return &cache_->graph;
}

const TreeMeasurements* TreeGenerator::ensureMeasured(const search::Parameters& params) const {
    const TreeGraph* graph = ensureGraph(params);
    if (graph == nullptr) {
        return nullptr;
    }
    if (!cache_->measuredValid) {
        cache_->measured = measureTree(*graph, rasteriseTree(*graph, camera_), camera_);
        cache_->measuredValid = true;
    }
    return &cache_->measured;
}

const TreeGraph* TreeGenerator::lastGraph(const search::Parameters& params) const {
    return ensureGraph(params);
}

Result<search::Subject> TreeGenerator::build(const search::Parameters& params) const {
    const TreeGraph* graph = ensureGraph(params);
    if (graph == nullptr) {
        auto treeParams = treeParamsFrom(params);
        return treeParams ? fail("tree: generation produced nothing") : std::unexpected(treeParams.error());
    }
    auto meshes = buildTreeMeshes(*graph, mesh_);
    if (!meshes) {
        return std::unexpected(meshes.error());
    }

    search::Subject subject;
    // One part per tier, each of which becomes one named CompositionNode. A generator that emitted
    // one merged mesh would have made its own output uninspectable: the editor picks by object, so
    // whatever granularity the parts come out at is the granularity an artist can select.
    for (const auto& [role, mesh] : meshes->parts()) {
        if (mesh->vertices.empty()) {
            continue;
        }
        search::SubjectPart part;
        part.role = role;
        part.mesh = *mesh;
        // Placeholder look. The scene's authored materials replace these; they exist so the contact
        // sheet shows something with the right relative values rather than six identical grey parts.
        if (role == "foliage") {
            part.baseColor = {0.06f, 0.22f, 0.19f};
            part.emissiveColor = {0.18f, 0.85f, 0.62f};
            part.emissiveIntensity = 0.30f;
            part.roughness = 0.75f;
        } else if (role == "roots") {
            part.baseColor = {0.10f, 0.12f, 0.13f};
            part.roughness = 0.85f;
        } else {
            part.baseColor = {0.13f, 0.16f, 0.17f};
            part.emissiveColor = {0.25f, 0.80f, 0.95f};
            // The life-force veins sit on the engine's own emission ladder rather than inventing a
            // brightness: the trunk is at the "noticeable" rung and nothing here reaches the gap
            // above it, which is what keeps the foliage reading as the bright thing.
            part.emissiveIntensity = role == "trunk" ? 0.295f : 0.12f;
            part.roughness = 0.68f;
        }
        subject.parts.push_back(std::move(part));
    }
    if (subject.parts.empty()) {
        return fail("tree: built no geometry");
    }
    const glm::vec3 extent = graph->boundsMax - graph->boundsMin;
    subject.framingRadius = 0.5f * std::max(extent.y, std::max(extent.x, extent.z));
    subject.framingCenterY = extent.y > kEpsilon ? (graph->stats.height * 0.5f) / subject.framingRadius : 0.5f;
    return subject;
}

search::FeatureVector TreeGenerator::features(const search::Subject& subject,
                                              const search::Parameters& params) const {
    (void)subject;
    const TreeMeasurements* m = ensureMeasured(params);
    if (m == nullptr) {
        return search::FeatureVector(schema_.featureNames.size(), 0.0f);
    }
    // Roughly commensurate scales, so a distance in this space is not dominated by whichever axis
    // happens to have the largest units. `selectDiverse` normalises by observed range as well, but
    // that only helps when the population actually spans the range.
    return {m->aspect,     m->boxFill,          m->balance * 2.0f,        m->verticalCentroid,
            m->openness,   m->depthSpread,      m->structureVisible,      m->primaryCount / 20.0f,
            m->boleFraction, m->silhouetteComplexity / 20.0f};
}

std::vector<search::ScoreComponent> TreeGenerator::domainScores(const search::Subject& subject,
                                                                const search::Parameters& params) const {
    (void)subject;
    std::vector<search::ScoreComponent> out;
    const TreeMeasurements* m = ensureMeasured(params);
    if (m == nullptr) {
        return out;
    }
    const auto value = [&](const std::string& name) -> float {
        if (name == "frameFill") return m->frameFill;
        if (name == "boxFill") return m->boxFill;
        if (name == "aspect") return m->aspect;
        if (name == "balance") return m->balance;
        if (name == "verticalCentroid") return m->verticalCentroid;
        if (name == "openness") return m->openness;
        if (name == "structureVisible") return m->structureVisible;
        if (name == "depthSpread") return m->depthSpread;
        if (name == "primaryCount") return m->primaryCount;
        if (name == "secondaryPerPrimary") return m->secondaryPerPrimary;
        if (name == "tertiaryPerSecondary") return m->tertiaryPerSecondary;
        if (name == "boleFraction") return m->boleFraction;
        if (name == "trunkDominance") return m->trunkDominance;
        if (name == "silhouetteComplexity") return m->silhouetteComplexity;
        if (name == "rootSpreadRatio") return m->rootSpreadRatio;
        return 0.0f;
    };
    out.reserve(treeBands().size());
    for (const TreeBand& band : treeBands()) {
        search::ScoreComponent component;
        component.name = band.name;
        component.raw = value(band.name);
        component.score = band.band(component.raw);
        component.weight = band.weight;
        out.push_back(std::move(component));
    }
    return out;
}

} // namespace avgen::scene
