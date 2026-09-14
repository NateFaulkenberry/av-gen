#include "scene/tree_evaluator.hpp"

#include "core/noise.hpp"

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
    const float limbThreshold = graph.stats.trunkBaseRadius * 0.12f;
    int primaries = 0;
    float widestPrimary = 0.0f;
    for (const TreeAxis& axis : graph.axes) {
        if (axis.tier == BranchTier::Primary && axis.baseRadius >= limbThreshold) {
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
    return m;
}

std::vector<ScoredMetric> TreeMeasurements::asMetrics() const {
    const auto entry = [](const char* name, float value) { return ScoredMetric{name, value, 0.0f, 0.0f}; };
    return {entry("frameFill", frameFill),
            entry("boxFill", boxFill),
            entry("aspect", aspect),
            entry("balance", balance),
            entry("verticalCentroid", verticalCentroid),
            entry("openness", openness),
            entry("depthSpread", depthSpread),
            entry("primaryCount", primaryCount),
            entry("secondaryPerPrimary", secondaryPerPrimary),
            entry("tertiaryPerSecondary", tertiaryPerSecondary),
            entry("boleFraction", boleFraction),
            entry("trunkDominance", trunkDominance),
            entry("silhouetteComplexity", silhouetteComplexity),
            entry("rootSpreadRatio", rootSpreadRatio)};
}

std::vector<float> TreeMeasurements::asFeatures() const {
    // Roughly commensurate scales, so a euclidean distance in this space is not dominated by
    // whichever feature happens to have the largest units.
    return {aspect,          boxFill,       balance * 2.0f,
            verticalCentroid, openness,      depthSpread * 2.0f,
            primaryCount / 20.0f, boleFraction, silhouetteComplexity / 20.0f};
}

const std::vector<ScoreComponent>& treeScoreComponents() {
    // The weights follow the brief's section 16 in spirit -- silhouette heaviest, then branching,
    // then proportion -- but its seven headings are split into fourteen measurable components, and
    // the weights are apportioned within each heading rather than copied.
    //
    // EVERY BAND IS BOUNDED ON BOTH SIDES. That is the rule, not a stylistic preference: a ranking
    // stage is an optimiser, so any component that merely increases will be driven to its extreme.
    static const std::vector<ScoreComponent> components = {
        {"frameFill",
         "How much of the frame the tree occupies. Too little and the hero does not dominate the "
         "shot; too much and it is cropped and unreadable.",
         0.10f,
         {0.08f, 0.16f, 0.32f, 0.46f}},
        {"boxFill",
         "Silhouette area over its own bounding box. This is the skinny/blob axis: below the band "
         "the tree is a wispy stick figure, above it the canopy has merged into one indistinct "
         "mass and no branch is individually readable.",
         0.11f,
         {0.16f, 0.26f, 0.44f, 0.60f}},
        {"aspect",
         "Silhouette width over height. A monumental tree is roughly as broad as it is tall; a tall "
         "narrow one reads as a pole and a wide flat one as a hedge.",
         0.08f,
         {0.62f, 0.85f, 1.30f, 1.65f}},
        {"balance",
         "Absolute left/right mass difference. Banded away from ZERO as well as from large values: "
         "a perfectly symmetric tree reads as manufactured, which is the controlled-asymmetry "
         "requirement stated as a measurement.",
         0.07f,
         {0.0f, 0.035f, 0.17f, 0.32f}},
        {"verticalCentroid",
         "Height of the centre of mass within the silhouette. The upper bound is the top-heavy "
         "failure; the lower bound is a canopy that has slumped into the roots.",
         0.07f,
         {0.36f, 0.46f, 0.62f, 0.75f}},
        {"openness",
         "Fraction of the silhouette's interior that is empty. Meaningful negative space, as a "
         "number: too little is the indistinguishable mass, too much is the dead central void. Both "
         "are named failures in the brief.",
         0.09f,
         {0.08f, 0.18f, 0.38f, 0.55f}},
        {"depthSpread",
         "Canopy depth along the camera's forward axis over canopy spread across it. 1.0 is a crown "
         "as deep as it is wide. The lower bound stops a tree that has flattened itself toward the "
         "camera from winning; the upper bound stops one stretched away down the view axis, which "
         "reads as thin from the only viewpoint that matters.",
         0.09f,
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
         "subdivision and foliage attachment come from, and where microscopic branching that "
         "contributes nothing visually would show up.",
         0.04f,
         {2.0f, 4.0f, 12.0f, 20.0f}},
        {"boleFraction",
         "Clear trunk height over total height. The lower bound is a tree that branches at the "
         "ground; the upper bound is a lollipop on a pole.",
         0.08f,
         {0.15f, 0.24f, 0.40f, 0.52f}},
        {"trunkDominance",
         "Trunk base radius over the widest primary limb's. The lower bound is the trunk failing to "
         "dominate; the upper bound is limbs too spindly to carry the crown they hold.",
         0.06f,
         {1.4f, 2.1f, 4.6f, 7.0f}},
        {"silhouetteComplexity",
         "Isoperimetric quotient, perimeter squared over 4 pi area. A disc scores 1. This is the "
         "blob-to-spaghetti axis in one number, and it is the component most directly aimed at the "
         "noodle-tree failure.",
         0.05f,
         {1.8f, 4.0f, 16.0f, 30.0f}},
        {"rootSpreadRatio",
         "Root reach over crown half-width. Anchors the tree visually; the upper bound stops the "
         "roots from becoming a second crown lying on the ground.",
         0.03f,
         {0.22f, 0.40f, 0.80f, 1.10f}},
    };
    return components;
}

TreeSubject::TreeSubject(TreeDesignSpace space, TreeCameraView camera)
    : space_(std::move(space)), camera_(camera) {}

const std::vector<ScoreComponent>& TreeSubject::components() const {
    return treeScoreComponents();
}

int TreeSubject::dimensions() const {
    return 14;
}

TreeParams TreeSubject::paramsFrom(std::span<const float> u) const {
    const auto at = [&u](int i) { return i < static_cast<int>(u.size()) ? u[static_cast<std::size_t>(i)] : 0.5f; };
    TreeParams p = space_.fixed;

    p.seed = 1u + static_cast<std::uint32_t>(at(0) * static_cast<float>(space_.seedCount));

    // Height and bole are authored as *fractions and totals*, then converted into the envelope the
    // generator wants. Sampling the envelope's own fields directly lets the search produce a crown
    // whose base is above its top, and every such candidate is a wasted evaluation.
    const float height = lerp(space_.heightMin, space_.heightMax, at(1));
    const float boleFraction = lerp(space_.boleFractionMin, space_.boleFractionMax, at(2));
    p.crown.radius = lerp(space_.crownRadiusMin, space_.crownRadiusMax, at(3));
    p.crown.baseHeight = height * boleFraction;
    const float crownTop = height;
    p.crown.halfHeight = std::max((crownTop - p.crown.baseHeight) * 0.5f, 1.0f);
    p.crown.centreHeight = p.crown.baseHeight + p.crown.halfHeight;
    p.crown.shoulder = lerp(space_.shoulderMin, space_.shoulderMax, at(4));
    p.crown.lumpiness = lerp(space_.lumpinessMin, space_.lumpinessMax, at(5));
    p.crown.trunkCorridorRadius = std::max(p.crown.radius * 0.13f, 0.5f);

    p.lambdaYoung = lerp(space_.lambdaYoungMin, space_.lambdaYoungMax, at(6));
    p.lambdaOld = lerp(space_.lambdaOldMin, space_.lambdaOldMax, at(7));
    p.branchAngle = lerp(space_.branchAngleMin, space_.branchAngleMax, at(8));
    p.outwardBias = lerp(space_.outwardBiasMin, space_.outwardBiasMax, at(9));
    p.branchTropism = glm::vec3(0.0f, lerp(space_.branchDropMin, space_.branchDropMax, at(10)), 0.0f);
    p.shedThreshold = lerp(space_.shedMin, space_.shedMax, at(11));
    p.alpha = lerp(space_.alphaMin, space_.alphaMax, at(12));
    p.rootCanopyCoupling = lerp(space_.rootCoupleMin, space_.rootCoupleMax, at(13));
    return p;
}

nlohmann::json TreeSubject::sample(std::span<const float> u) const {
    return paramsFrom(u).toJson();
}

nlohmann::json TreeSubject::perturb(const nlohmann::json& base, std::span<const float> u, float scale) const {
    auto parsed = TreeParams::fromJson(base);
    if (!parsed) {
        return sample(u);
    }
    TreeParams p = *parsed;
    const auto at = [&u](int i) {
        return i < static_cast<int>(u.size()) ? u[static_cast<std::size_t>(i)] * 2.0f - 1.0f : 0.0f;
    };
    // A step proportional to each range, clamped back into it. Perturbing in design-space fractions
    // rather than in absolute units means one `scale` means the same thing to every parameter.
    const auto nudge = [&](float value, float lo, float hi, int dim) {
        return std::clamp(value + at(dim) * scale * (hi - lo), lo, hi);
    };
    // The seed is not perturbed continuously -- it is not a continuous quantity. It is redrawn, so
    // a refinement step explores the shape of the neighbourhood rather than one arrangement of it.
    p.seed = 1u + static_cast<std::uint32_t>(std::abs(at(0)) * static_cast<float>(space_.seedCount));

    const float height = std::max(p.crown.baseHeight + 2.0f * p.crown.halfHeight, 1.0f);
    const float newHeight = nudge(height, space_.heightMin, space_.heightMax, 1);
    const float boleFraction =
        nudge(p.crown.baseHeight / height, space_.boleFractionMin, space_.boleFractionMax, 2);
    p.crown.radius = nudge(p.crown.radius, space_.crownRadiusMin, space_.crownRadiusMax, 3);
    p.crown.baseHeight = newHeight * boleFraction;
    p.crown.halfHeight = std::max((newHeight - p.crown.baseHeight) * 0.5f, 1.0f);
    p.crown.centreHeight = p.crown.baseHeight + p.crown.halfHeight;
    p.crown.shoulder = nudge(p.crown.shoulder, space_.shoulderMin, space_.shoulderMax, 4);
    p.crown.lumpiness = nudge(p.crown.lumpiness, space_.lumpinessMin, space_.lumpinessMax, 5);
    p.crown.trunkCorridorRadius = std::max(p.crown.radius * 0.13f, 0.5f);

    p.lambdaYoung = nudge(p.lambdaYoung, space_.lambdaYoungMin, space_.lambdaYoungMax, 6);
    p.lambdaOld = nudge(p.lambdaOld, space_.lambdaOldMin, space_.lambdaOldMax, 7);
    p.branchAngle = nudge(p.branchAngle, space_.branchAngleMin, space_.branchAngleMax, 8);
    p.outwardBias = nudge(p.outwardBias, space_.outwardBiasMin, space_.outwardBiasMax, 9);
    p.branchTropism.y = nudge(p.branchTropism.y, space_.branchDropMin, space_.branchDropMax, 10);
    p.shedThreshold = nudge(p.shedThreshold, space_.shedMin, space_.shedMax, 11);
    p.alpha = nudge(p.alpha, space_.alphaMin, space_.alphaMax, 12);
    p.rootCanopyCoupling = nudge(p.rootCanopyCoupling, space_.rootCoupleMin, space_.rootCoupleMax, 13);
    return p.toJson();
}

Result<void> TreeSubject::measure(const nlohmann::json& parameters, CandidateRecord& out) const {
    auto params = TreeParams::fromJson(parameters);
    if (!params) {
        return std::unexpected(params.error());
    }
    const auto started = std::chrono::steady_clock::now();
    auto graph = generateTree(*params);
    if (!graph) {
        return std::unexpected(graph.error());
    }
    out.generateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    const TreeSilhouette silhouette = rasteriseTree(*graph, camera_);
    if (silhouette.empty()) {
        return fail("tree candidate: nothing projected into the evaluation camera");
    }
    const TreeMeasurements measurements = measureTree(*graph, silhouette, camera_);
    out.metrics = measurements.asMetrics();
    out.features = measurements.asFeatures();
    return {};
}

} // namespace avgen::scene
