#include "organism/mushroom.hpp"

#include "core/noise.hpp"
#include "scene/mesh_metrics.hpp"
#include "world/art_direction.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <array>
#include <optional>

#include <fmt/format.h>

namespace avgen::organism {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// ---- the profile ------------------------------------------------------------------------------
//
// A cubic Hermite in (r, y): from (0, y0) with slope tan(a0) to (R, y1) with slope tan(a1). The
// slopes are angles because that is the parameterisation a mushroom's identity actually lives in --
// the rim tangent is what separates a parasol from a bell -- and because an angle is a thing an art
// director can ask for twenty more degrees of.
struct Profile {
    float y0 = 0.0f;
    float y1 = 0.0f;
    float m0 = 0.0f; // dy/dr at the centre
    float m1 = 0.0f; // dy/dr at the rim
    float radius = 1.0f;

    [[nodiscard]] float at(float u) const { // u in [0, 1], centre to rim
        const float t = std::clamp(u, 0.0f, 1.0f);
        const float t2 = t * t;
        const float t3 = t2 * t;
        const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        const float h10 = t3 - 2.0f * t2 + t;
        const float h01 = -2.0f * t3 + 3.0f * t2;
        const float h11 = t3 - t2;
        return h00 * y0 + h10 * radius * m0 + h01 * y1 + h11 * radius * m1;
    }
};

Profile capProfile(float centreY, float rimY, float centreDeg, float rimDeg, float radius) {
    Profile p;
    p.y0 = centreY;
    p.y1 = rimY;
    // Clamped well inside vertical: a tangent at 90 degrees is an infinite slope and a cap that
    // folds through itself, which the validity gate would catch but which is cheaper to not make.
    p.m0 = std::tan(glm::radians(std::clamp(centreDeg, -70.0f, 70.0f)));
    p.m1 = std::tan(glm::radians(std::clamp(rimDeg, -70.0f, 70.0f)));
    p.radius = radius;
    return p;
}

// ---- the per-angle radius modulation ----------------------------------------------------------
//
// The thing no engine primitive can do. Three terms, deliberately separated by scale, because
// `02-research.md` 4.7's organicity band asks for large, medium and small variation all present and
// none dominant -- and because noise alone produces a crinkled circle rather than a designed shape.
struct RadialModulation {
    float lobeDepth = 0.0f;
    int lobes = 0;
    float waviness = 0.0f;
    float noiseAmp = 0.0f;
    float noiseScale = 2.0f;
    std::uint32_t seed = 1u;

    // `u` is the radial parameter: the modulation fades to nothing at the centre, so the apex stays
    // a point rather than becoming a star.
    [[nodiscard]] float at(float theta, float u) const {
        float s = 1.0f;
        if (lobes > 0) {
            s += lobeDepth * std::cos(static_cast<float>(lobes) * theta);
            s += waviness * std::cos(2.0f * static_cast<float>(lobes) * theta + 1.1f);
        }
        if (noiseAmp > 0.0f) {
            const glm::vec3 q(std::cos(theta) * noiseScale, std::sin(theta) * noiseScale, 0.0f);
            const float rimFade = 1.0f - std::clamp((u - 0.82f) / 0.18f, 0.0f, 1.0f);
            s += noiseAmp * (noise::valueNoise(q, seed) * 2.0f - 1.0f) * rimFade;
        }
        // Fades in from the centre so the apex stays a point, and the *noise* term additionally fades
        // back out at the rim: the first sheet's caps had torn edges, which is high-frequency noise
        // applied to a silhouette rather than to a surface.
        return 1.0f + (s - 1.0f) * std::clamp(u, 0.0f, 1.0f);
    }
};

// ---- a lathe with a modulated radius ----------------------------------------------------------
//
// `segments` rings from centre to rim, `spokes` around. `flip` reverses winding for a downward
// facing surface. Normals are computed from the surface afterwards rather than analytically,
// because the modulation makes the analytic form long and the mesh is built once.
scene::MeshData lathe(const Profile& profile, const RadialModulation& mod, int segments, int spokes,
                      bool flip, float tiltRadians, float pivotY, glm::vec2 origin,
                      float innerU = 0.0f) {
    scene::MeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(segments + 1) * static_cast<std::size_t>(spokes + 1));
    const float cosT = std::cos(tiltRadians);
    const float sinT = std::sin(tiltRadians);
    // A lathe whose first ring sits at r = 0 emits one degenerate triangle per spoke, and 44 of them
    // out of 1,500 is over the validity gate's 2% -- so the first *ring* starts just off the axis and
    // the apex is closed by a fan below. The gate was right and the mesh was wrong.
    const float firstU = innerU > 0.0f ? innerU : 1.0f / static_cast<float>(segments * 3);
    for (int i = 0; i <= segments; ++i) {
        const float u = glm::mix(firstU, 1.0f, static_cast<float>(i) / static_cast<float>(segments));
        const float y = profile.at(u);
        const float baseR = u * profile.radius;
        for (int k = 0; k <= spokes; ++k) {
            // The seam column is duplicated so uv runs 0..1 without wrapping, which is the same
            // convention the engine's own revolve uses.
            const float theta = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(spokes);
            const float r = baseR * mod.at(theta, u);
            glm::vec3 p(r * std::cos(theta), y, r * std::sin(theta));
            // Tilt about X. The cap leans as a whole, which is asymmetry a viewer reads as growth
            // rather than as noise.
            // About the attachment point, not the world origin. Tilting about the origin swings the
            // whole cap off the top of the stem -- which is what put a visible gap between cap and
            // stem in four of the first six winners, and which the plausibility rule missed because a
            // drooping rim still reached below the stem's top.
            p.y -= pivotY;
            p = glm::vec3(p.x, p.y * cosT - p.z * sinT, p.y * sinT + p.z * cosT);
            p.y += pivotY;
            // ...and then onto the stem. The cap is lathed about the origin and the stem *leans*, so
            // without this the cap sits where a straight stem's top would have been. Measured over
            // the generator's own population: 260 of 660 plausible candidates had their cap off their
            // stem, the worst by 17.7 stem radii. It was invisible in a parameter-space check, which
            // is why the alignment invariant reads its anchors off the meshes.
            p.x += origin.x;
            p.z += origin.y;
            scene::Vertex v;
            v.position = p;
            v.normal = glm::vec3(0.0f, flip ? -1.0f : 1.0f, 0.0f);
            v.uv = glm::vec2(static_cast<float>(k) / static_cast<float>(spokes), u);
            mesh.vertices.push_back(v);
        }
    }
    const auto stride = static_cast<std::uint32_t>(spokes + 1);
    for (int i = 0; i < segments; ++i) {
        for (int k = 0; k < spokes; ++k) {
            const std::uint32_t a = static_cast<std::uint32_t>(i) * stride + static_cast<std::uint32_t>(k);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + stride;
            const std::uint32_t d = c + 1;
            if (flip) {
                mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
            } else {
                mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
            }
        }
    }
    if (innerU <= 0.0f) {
        // Close the apex with a fan to a single vertex on the axis.
        const auto apex = static_cast<std::uint32_t>(mesh.vertices.size());
        scene::Vertex tip;
        const float ty = profile.at(0.0f);
        tip.position = glm::vec3(origin.x, (ty - pivotY) * cosT + pivotY, (ty - pivotY) * sinT + origin.y);
        tip.normal = glm::vec3(0.0f, flip ? -1.0f : 1.0f, 0.0f);
        tip.uv = glm::vec2(0.5f, 0.0f);
        mesh.vertices.push_back(tip);
        for (int k = 0; k < spokes; ++k) {
            const auto a = static_cast<std::uint32_t>(k);
            const std::uint32_t b = a + 1;
            if (flip) {
                mesh.indices.insert(mesh.indices.end(), {apex, a, b});
            } else {
                mesh.indices.insert(mesh.indices.end(), {apex, b, a});
            }
        }
    }
    mesh.computeNormals();
    return mesh;
}

// ---- the stem ---------------------------------------------------------------------------------
//
// Swept about a curved axis (Desbenoit et al.), with a radius profile carrying the bulge. The lean
// is quadratic in height so the base stays planted and the top carries the offset -- a stem that
// leans linearly reads as a tilted cylinder rather than as something that grew.
glm::vec3 stemAxis(float t, float curvature) {
    const float lean = curvature * t * t;
    return glm::vec3(lean, t, lean * 0.35f);
}

float stemRadius(float t, float baseRadius, float taper, float bulgePos, float bulgeWidth) {
    const float linear = glm::mix(1.0f, taper, t);
    // A gaussian bump rather than a spline knot: it cannot overshoot, and a volva that pinches the
    // stem to nothing is a mesh defect rather than a species.
    const float d = (t - bulgePos) / 0.22f;
    const float bulge = bulgeWidth * std::exp(-d * d);
    return baseRadius * (linear + bulge);
}

scene::MeshData sweepStem(float height, float baseRadius, float taper, float bulgePos,
                          float bulgeWidth, float curvature, int segments, int sides) {
    scene::MeshData mesh;
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const glm::vec3 centre = stemAxis(t, curvature) * glm::vec3(1.0f, height, 1.0f);
        const float r = stemRadius(t, baseRadius, taper, bulgePos, bulgeWidth);
        // The frame follows the axis' tangent so the tube does not pinch where it bends.
        const float dt = 1.0f / static_cast<float>(segments);
        const glm::vec3 ahead = stemAxis(std::min(t + dt, 1.0f), curvature) * glm::vec3(1.0f, height, 1.0f);
        const glm::vec3 behind = stemAxis(std::max(t - dt, 0.0f), curvature) * glm::vec3(1.0f, height, 1.0f);
        glm::vec3 tangent = ahead - behind;
        tangent = glm::length(tangent) > 1e-6f ? glm::normalize(tangent) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 side = glm::cross(tangent, glm::vec3(0.0f, 0.0f, 1.0f));
        side = glm::length(side) > 1e-6f ? glm::normalize(side) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 up = glm::normalize(glm::cross(side, tangent));
        for (int k = 0; k <= sides; ++k) {
            const float theta = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(sides);
            scene::Vertex v;
            v.position = centre + (side * std::cos(theta) + up * std::sin(theta)) * r;
            v.normal = glm::normalize(side * std::cos(theta) + up * std::sin(theta));
            v.uv = glm::vec2(static_cast<float>(k) / static_cast<float>(sides), t);
            mesh.vertices.push_back(v);
        }
    }
    const auto stride = static_cast<std::uint32_t>(sides + 1);
    for (int i = 0; i < segments; ++i) {
        for (int k = 0; k < sides; ++k) {
            const std::uint32_t a = static_cast<std::uint32_t>(i) * stride + static_cast<std::uint32_t>(k);
            mesh.indices.insert(mesh.indices.end(), {a, a + stride, a + 1, a + 1, a + stride, a + stride + 1});
        }
    }
    mesh.computeNormals();
    return mesh;
}

// ---- the gills --------------------------------------------------------------------------------
//
// Radial blades hanging between the underside and a depth below it, each a two-triangle-per-segment
// ribbon. Geometry rather than a texture because these are close-up cinematic subjects and because
// the bioluminescence note's finding is that light needs a structure to come out of: the version of
// a glowing organism that read as biology was the one where the light came out of sixty-four hanging
// filaments rather than off a smooth dome.
scene::MeshData buildGills(const Profile& under, const RadialModulation& mod, int count, float depth,
                           float innerU, float tiltRadians, float pivotY, glm::vec2 origin, float blade) {
    scene::MeshData mesh;
    constexpr int kSpan = 7; // samples from the stem outward along one blade
    const float cosT = std::cos(tiltRadians);
    const float sinT = std::sin(tiltRadians);
    for (int g = 0; g < count; ++g) {
        const float theta = 2.0f * kPi * static_cast<float>(g) / static_cast<float>(count);
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int i = 0; i < kSpan; ++i) {
            const float u = glm::mix(innerU, 1.0f, static_cast<float>(i) / static_cast<float>(kSpan - 1));
            const float r = u * under.radius * mod.at(theta, u);
            const float top = under.at(u);
            // The blade is deepest in the middle of its span and vanishes at both ends, so it meets
            // the stem and the rim rather than ending in a step.
            const float span = std::sin(kPi * std::clamp((u - innerU) / std::max(1.0f - innerU, 1e-3f), 0.0f, 1.0f));
            // Never zero. A blade that tapers to nothing at both ends makes its end quads degenerate,
            // which is what rejected all 24 of the first candidate batch -- the gate was right and the
            // blade was wrong. 0.18 keeps a lip at the stem and at the rim, which is also what a real
            // gill does.
            const float drop = depth * (0.18f + 0.82f * span);
            // A blade with thickness rather than a zero-thickness sheet. A sheet needs a
            // double-sided material and its normal is a coin toss, which is why half the gills in the
            // first contact sheet rendered black; a wedge has two faces that each know which way they
            // point, for four vertices instead of two.
            const glm::vec3 tangential(-std::sin(theta), 0.0f, std::cos(theta));
            const float half = blade * 0.5f;
            for (int side = 0; side < 2; ++side) {
                const float sign = side == 0 ? -1.0f : 1.0f;
                for (int e = 0; e < 2; ++e) {
                    glm::vec3 p(r * std::cos(theta), top - (e == 0 ? 0.0f : drop), r * std::sin(theta));
                    p += tangential * (sign * half);
                    p.y -= pivotY;
                    p = glm::vec3(p.x, p.y * cosT - p.z * sinT, p.y * sinT + p.z * cosT);
                    p.y += pivotY;
                    p.x += origin.x;
                    p.z += origin.y;
                    scene::Vertex v;
                    v.position = p;
                    v.normal = tangential * sign;
                    v.uv = glm::vec2(u, static_cast<float>(e));
                    mesh.vertices.push_back(v);
                }
            }
        }
        for (int i = 0; i + 1 < kSpan; ++i) {
            const std::uint32_t a = base + static_cast<std::uint32_t>(i) * 4u;
            // side 0 (outward -tangential) and side 1, wound opposite so each faces away from the blade
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + 4, a + 1, a + 5, a + 4});
            mesh.indices.insert(mesh.indices.end(), {a + 2, a + 6, a + 3, a + 3, a + 6, a + 7});
        }
    }
    mesh.computeNormals();
    return mesh;
}

// Where the cap meets the stem, as a fraction of cap radius. Everything inside this is stem.
float attachmentU(float stemRadiusAtTop, float capRadius) {
    return std::clamp(stemRadiusAtTop / std::max(capRadius, 1e-3f), 0.06f, 0.45f);
}

} // namespace

// ---------------------------------------------------------------------------------------------

const search::GeneratorSchema& mushroomSchema() {
    static const search::GeneratorSchema schema = [] {
        search::GeneratorSchema s;
        s.generatorName = "mushroom";
        s.generatorVersion = 1;
        // Ordered by visual influence, not alphabetically. ADR-173: a low-discrepancy sequence's
        // low-dimensional projections are its good ones, so the stratification is spent where it
        // buys the most. Reordering this list silently degrades the sample.
        s.parameters = search::ParameterSchema({
            {"aspect", 0.25f, 1.6f, false, "cap radius over stem height -- the strongest silhouette determinant"},
            {"rimTangentDeg", -70.0f, 40.0f, false, "the identity parameter: parasol, bell, cone, chanterelle"},
            {"centreTangentDeg", -40.0f, 40.0f, false, "domed, flat or depressed centre"},
            {"capThickness", 0.04f, 0.35f, false, "cap thickness as a fraction of cap radius"},
            {"stemCurvature", 0.0f, 0.45f, false, "a straight stem reads as a cylinder"},
            {"stemTaper", 0.4f, 1.5f, false, "below 1 narrows upward, above 1 flares"},
            {"stemBulgePosition", 0.15f, 0.85f, false, "where along the stem the volva sits"},
            {"stemBulgeWidth", 0.0f, 0.35f, false, "how much the bulge swells"},
            {"lobeCount", 0.0f, 9.0f, true, "0 is a circular rim; 3-9 lobes break it"},
            {"lobeDepth", 0.0f, 0.30f, false, "radial asymmetry -- what no engine primitive can express"},
            {"capTiltDeg", 0.0f, 18.0f, false, "the whole cap leans, which reads as growth"},
            {"edgeWaviness", 0.0f, 0.20f, false, "a higher harmonic on the same per-angle term"},
            {"surfaceNoiseAmp", 0.0f, 0.06f, false, "small-scale breakup"},
            {"surfaceNoiseScale", 0.8f, 6.0f, false, "how fine that breakup is"},
            {"gillCount", 24.0f, 96.0f, true, "blades under the cap"},
            {"gillDepth", 0.15f, 0.75f, false, "gill depth as a fraction of cap thickness"},
            {"emissionStructure", 0.0f, 3.0f, true, "where the light comes out: gills, rim, veins, filaments"},
            {"emissionIntensity", 0.8f, 6.9f, false, "bounded by the scene's own emission ladder"},
        });
        s.featureNames = {"capWidth",   "capHeight",     "capThickness", "stemHeight",
                          "stemWidth",  "stemCurvature", "capAsymmetry", "capTilt",
                          "edgeWaviness", "gillDensity", "emissionIntensity",
                          "aspectRatio", "silhouetteComplexity"};
        return s;
    }();
    return schema;
}

Result<search::Subject> buildMushroom(const search::Parameters& v) {
    if (v.size() < static_cast<std::size_t>(MushroomParam::Count)) {
        return fail("mushroom: {} values for an {}-axis schema", v.size(),
                    static_cast<std::size_t>(MushroomParam::Count));
    }
    // The unit frame: the stem's base is the origin and the stem is 1 tall, so everything below is a
    // ratio and the scene's transform decides the size.
    constexpr float kStemHeight = 1.0f;
    const float capRadius = std::max(param(v, MushroomParam::Aspect) * kStemHeight, 0.05f);
    const float thickness = param(v, MushroomParam::CapThickness) * capRadius;
    const float taper = param(v, MushroomParam::StemTaper);
    const float baseRadius = std::clamp(capRadius * 0.16f, 0.02f, 0.30f);
    const float topRadius = stemRadius(1.0f, baseRadius, taper, param(v, MushroomParam::StemBulgePosition),
                                       param(v, MushroomParam::StemBulgeWidth));
    const float innerU = attachmentU(topRadius, capRadius);
    const float tilt = glm::radians(param(v, MushroomParam::CapTiltDeg));
    // Where the stem actually ends, which is not the origin: `stemAxis` leans by `curvature * t^2`,
    // so a stem at full curvature finishes almost half its own height to one side.
    const glm::vec3 stemEnd =
        stemAxis(1.0f, param(v, MushroomParam::StemCurvature)) * glm::vec3(1.0f, kStemHeight, 1.0f);
    const glm::vec2 attachXZ(stemEnd.x, stemEnd.z);

    // Rim height relative to the cap's underside centre. A negative rim tangent droops the edge; the
    // rim's own height follows from the profile rather than being a separate parameter, which is the
    // proc-shrooms parameterisation and the reason four numbers cover the family.
    const float rimDrop = -0.35f * capRadius * std::sin(glm::radians(param(v, MushroomParam::RimTangentDeg)));
    const Profile upper = capProfile(kStemHeight + thickness, kStemHeight + rimDrop,
                                     param(v, MushroomParam::CentreTangentDeg),
                                     param(v, MushroomParam::RimTangentDeg), capRadius);
    // The underside tucks *inside* the upper surface rather than meeting it exactly. Both profiles
    // ended at the same radius and the same height in the first version, so the two surfaces were
    // coincident along the whole rim -- which is z-fighting, and it read on the contact sheet as a
    // ragged sparkling edge that looked like torn geometry. A real cap has a lip; so does this one.
    const Profile under = capProfile(kStemHeight, kStemHeight + rimDrop + thickness * 0.14f,
                                     param(v, MushroomParam::CentreTangentDeg) * 0.4f,
                                     param(v, MushroomParam::RimTangentDeg) * 0.8f,
                                     capRadius * 0.965f);

    // The cap rotates about the ring it is *attached by*, which is the underside profile's value at
    // the attachment radius -- not the nominal stem height. Those differ by however much the
    // underside has curved by `innerU`, and tilting about the wrong one swings the attachment ring
    // sideways by sin(tilt) times that difference. It was the residual left after the lean fix: five
    // candidates still over tolerance, the worst at 1.37 stem radii.
    const float capPivotY = under.at(innerU);

    RadialModulation mod;
    mod.lobes = static_cast<int>(std::lround(param(v, MushroomParam::LobeCount)));
    if (mod.lobes > 0 && mod.lobes < 3) {
        mod.lobes = 3; // one or two lobes is a dent, not a shape
    }
    mod.lobeDepth = param(v, MushroomParam::LobeDepth);
    mod.waviness = param(v, MushroomParam::EdgeWaviness);
    mod.noiseAmp = param(v, MushroomParam::SurfaceNoiseAmp);
    mod.noiseScale = param(v, MushroomParam::SurfaceNoiseScale);
    mod.seed = 0x9e37u;

    constexpr int kRings = 18;
    // The angular sampling has to follow the angular signal. The modulation's highest term is the
    // waviness harmonic at 2x the lobe count, so 44 fixed spokes gave 2.4 samples per cycle at nine
    // lobes -- and the contact sheet showed it as a rim that looked torn rather than lobed. This is
    // ADR-159's rule in another domain: no sample may step further than the feature it reads.
    const int kSpokes = std::clamp(std::max(48, mod.lobes * 10), 48, 128);
    const int gills = std::max(6, static_cast<int>(std::lround(param(v, MushroomParam::GillCount))));

    search::Subject subject;
    subject.parts.resize(kMushroomParts);

    // The palette is the scene's, not the search's. A hero that invents its own hue breaks the
    // reserve-accent rule that makes Glowmere work, so "random rainbow coloration" is prevented by
    // construction rather than penalised by a score afterwards (ADR-172).
    static const world::ArtDirectionProfile art = [] {
        if (const world::ArtDirectionProfile* p = world::findArtProfile("glowmere")) {
            return *p;
        }
        return world::ArtDirectionProfile{};
    }();
    const float emission = param(v, MushroomParam::EmissionIntensity);

    subject.parts[0].mesh = lathe(upper, mod, kRings, kSpokes, false, tilt, capPivotY, attachXZ, 0.0f);
    subject.parts[0].role = "cap";
    subject.parts[0].baseColor = art.palette.secondary * 0.5f;
    subject.parts[0].roughness = 0.44f;

    subject.parts[1].mesh = lathe(under, mod, kRings, kSpokes, true, tilt, capPivotY, attachXZ, innerU);
    subject.parts[1].role = "under";
    subject.parts[1].baseColor = art.palette.secondary * 0.3f;
    subject.parts[1].roughness = 0.52f;

    subject.parts[2].mesh = sweepStem(kStemHeight, baseRadius, taper,
                                      param(v, MushroomParam::StemBulgePosition),
                                      param(v, MushroomParam::StemBulgeWidth),
                                      param(v, MushroomParam::StemCurvature), 22, 20);
    subject.parts[2].role = "stem";
    subject.parts[2].baseColor = art.palette.shadow + glm::vec3(0.18f, 0.09f, 0.16f);
    subject.parts[2].roughness = 0.62f;

    subject.parts[3].mesh =
        buildGills(under, mod, gills, param(v, MushroomParam::GillDepth) * thickness, innerU, tilt, capPivotY, attachXZ,
                   // Blade thickness: a fraction of the arc between blades, so a dense gill set stays
                   // a set of blades rather than becoming a solid ring.
                   std::max(0.004f, 0.30f * 2.0f * kPi * capRadius * innerU / static_cast<float>(gills)));
    subject.parts[3].role = "gills";
    subject.parts[3].baseColor = art.palette.shadow;
    subject.parts[3].roughness = 0.4f;

    // Where the light comes out. The structure is a parameter because the bioluminescence note's
    // whole finding is that *which* structure emits is the difference between an organism and a
    // lamp -- so it is searched, not fixed.
    const int structure = static_cast<int>(std::lround(param(v, MushroomParam::EmissionStructure)));
    const glm::vec3 warm = art.heroAccent;
    const glm::vec3 cool = art.palette.primary;
    switch (structure) {
    case 0: // gills
        subject.parts[3].emissiveColor = warm;
        subject.parts[3].emissiveIntensity = emission;
        subject.parts[1].emissiveColor = warm * 0.35f;
        subject.parts[1].emissiveIntensity = emission * 0.2f;
        break;
    case 1: // the rim
        subject.parts[1].emissiveColor = cool;
        subject.parts[1].emissiveIntensity = emission * 0.8f;
        break;
    case 2: // veins across the cap
        subject.parts[0].emissiveColor = art.palette.foliage;
        subject.parts[0].emissiveIntensity = emission * 0.5f;
        subject.parts[3].emissiveColor = art.palette.foliage;
        subject.parts[3].emissiveIntensity = emission * 0.6f;
        break;
    default: // filaments: the gills carry it and the stem answers faintly
        subject.parts[3].emissiveColor = warm;
        subject.parts[3].emissiveIntensity = emission * 1.1f;
        subject.parts[2].emissiveColor = warm * 0.25f;
        subject.parts[2].emissiveIntensity = emission * 0.12f;
        break;
    }

    subject.framingRadius = std::max(capRadius, kStemHeight * 0.6f);
    subject.framingCenterY = 0.62f;
    return subject;
}

Result<scene::MeshData> buildMushroomPart(const scene::GeneratedSource& source, int part) {
    if (part < 0 || part >= kMushroomParts) {
        return fail("mushroom has {} parts; asked for {}", kMushroomParts, part);
    }
    auto subject = buildMushroom(source.values);
    if (!subject) {
        return std::unexpected(subject.error());
    }
    return std::move(subject->parts[static_cast<std::size_t>(part)].mesh);
}

void registerMushroomGenerator() {
    scene::registerGenerator("mushroom", &buildMushroomPart);
}

namespace {

// The centroid of the vertices at one extreme of a mesh, and their spread about it. `pick` scores a
// vertex; the top `fraction` by that score are the ring.
struct Ring {
    glm::vec3 centre{0.0f};
    float radius = 0.0f;
    bool valid = false;
};

template <typename Score>
Ring extremeRing(const scene::MeshData& mesh, Score score, float fraction) {
    Ring out;
    if (mesh.vertices.size() < 8) {
        return out;
    }
    std::vector<std::pair<float, glm::vec3>> scored;
    scored.reserve(mesh.vertices.size());
    for (const scene::Vertex& v : mesh.vertices) {
        scored.emplace_back(score(v.position), v.position);
    }
    const auto take = std::max<std::size_t>(
        4, static_cast<std::size_t>(static_cast<float>(scored.size()) * fraction));
    std::nth_element(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(take), scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    glm::vec3 sum(0.0f);
    for (std::size_t i = 0; i < take; ++i) {
        sum += scored[i].second;
    }
    out.centre = sum / static_cast<float>(take);
    float spread = 0.0f;
    for (std::size_t i = 0; i < take; ++i) {
        spread = std::max(spread, glm::distance(scored[i].second, out.centre));
    }
    out.radius = spread;
    out.valid = true;
    return out;
}

} // namespace

MushroomAnchors mushroomAnchors(const search::Subject& subject) {
    MushroomAnchors out;
    if (subject.parts.size() < 4) {
        return out;
    }
    const scene::MeshData& under = subject.parts[1].mesh;
    const scene::MeshData& stem = subject.parts[2].mesh;
    const scene::MeshData& gills = subject.parts[3].mesh;
    if (!under.valid() || !stem.valid()) {
        return out;
    }

    // The stem's top: the vertices **furthest from its base**, not the highest ones.
    //
    // A swept tube's rings are perpendicular to its own tangent, so a leaning stem's top ring is
    // tilted -- and "the highest vertices" then samples only its upper arc, putting the centroid off
    // to one side. That is a bias in the *measurement*, and it showed up as one candidate over
    // tolerance after the geometry was already correct. Distance from the base is tilt-invariant.
    glm::vec3 base(0.0f);
    {
        const Ring low = extremeRing(stem, [](const glm::vec3& p) { return -p.y; }, 1.0f / 22.0f);
        base = low.valid ? low.centre : glm::vec3(0.0f);
    }
    const Ring top =
        extremeRing(stem, [&](const glm::vec3& p) { return glm::distance(p, base); }, 1.0f / 22.0f);

    // The cap underside's attachment: the ring nearest its own axis. Scored by *negative* horizontal
    // distance from the underside's centroid, so the innermost vertices win -- which is the
    // attachment ring whatever the cap is doing, because a lathe's innermost ring is its inner edge
    // however the surface above it is shaped or tilted.
    glm::vec3 centroid(0.0f);
    for (const scene::Vertex& v : under.vertices) {
        centroid += v.position;
    }
    centroid /= static_cast<float>(under.vertices.size());
    const Ring attach = extremeRing(
        under,
        [&](const glm::vec3& p) {
            return -glm::length(glm::vec2(p.x - centroid.x, p.z - centroid.z));
        },
        1.0f / 22.0f);

    if (!top.valid || !attach.valid) {
        return out;
    }
    out.stemTop = top.centre;
    out.stemTopRadius = std::max(top.radius, 1e-4f);
    out.capAttach = attach.centre;
    // Where spores fall from: the lowest point of the gill set, which is the underside's own lowest
    // structure. Falls back to the attachment when a mushroom has no gills to speak of.
    out.gillLow = attach.centre;
    if (gills.valid()) {
        float lowest = 1e9f;
        for (const scene::Vertex& v : gills.vertices) {
            if (v.position.y < lowest) {
                lowest = v.position.y;
                out.gillLow = v.position;
            }
        }
        // The *centre* under the cap rather than the single lowest vertex, which is on the rim: the
        // emitter wants the middle of the underside, at the depth the gills reach.
        out.gillLow = glm::vec3(attach.centre.x, lowest, attach.centre.z);
        // And how far out they reach. Spores fall from the whole underside, so an emitter sized by
        // anything other than the gills' own span is a guess: sized by the organism's *height* it
        // was 0.88 m under a cap six metres across, and the fall read as a thin dribble down the
        // stem rather than as snow off a canopy. This is the same mesh-derived quantity as the
        // anchor, one measurement serving the position and the extent both.
        float reach = 0.0f;
        for (const scene::Vertex& v : gills.vertices) {
            reach = std::max(reach, glm::length(glm::vec2(v.position.x - attach.centre.x,
                                                          v.position.z - attach.centre.z)));
        }
        out.gillRadius = reach;
    }
    if (out.gillRadius <= 0.0f) {
        out.gillRadius = std::max(attach.radius, out.stemTopRadius);
    }
    out.valid = true;
    return out;
}

std::vector<AlignmentDefect> checkMushroomAlignment(const search::Subject& subject) {
    std::vector<AlignmentDefect> out;
    const MushroomAnchors a = mushroomAnchors(subject);
    if (!a.valid) {
        out.push_back({"anchors", 0.0f, "the parts do not yield an attachment"});
        return out;
    }
    // Scaled to the stem's own radius: a 4 m mushroom and a 40 cm one do not want the same absolute
    // tolerance, and the stem's top ring is the natural unit -- the cap is attached *to* it.
    const float tolerance = a.stemTopRadius * 1.25f;

    const float planar =
        glm::length(glm::vec2(a.capAttach.x - a.stemTop.x, a.capAttach.z - a.stemTop.z));
    if (planar > tolerance) {
        out.push_back({"cap-off-stem", planar,
                       fmt::format("cap attachment is {:.4f} m from the stem's top centre, tolerance "
                                   "{:.4f} m (stem top radius {:.4f} m)",
                                   planar, tolerance, a.stemTopRadius)});
    }
    // Vertically the cap must meet the stem, not float above it or sink through it. A generous band
    // downward, because a cap whose underside dips below the stem's top is a cap sitting *on* it.
    const float vertical = a.capAttach.y - a.stemTop.y;
    if (vertical > tolerance) {
        out.push_back({"cap-floats", vertical,
                       fmt::format("cap attachment is {:.4f} m above the stem's top", vertical)});
    }
    if (vertical < -tolerance * 4.0f) {
        out.push_back({"cap-sunk", -vertical,
                       fmt::format("cap attachment is {:.4f} m below the stem's top", -vertical)});
    }

    // The gills hang under the cap rather than through it. Checked against the *upper* surface,
    // because that is the one they would come through.
    if (subject.parts[3].mesh.valid() && subject.parts[0].mesh.valid()) {
        const auto cap = subject.parts[0].mesh.bounds();
        float highest = -1e9f;
        for (const scene::Vertex& v : subject.parts[3].mesh.vertices) {
            highest = std::max(highest, v.position.y);
        }
        if (highest > cap.second.y + tolerance) {
            out.push_back({"gills-through-cap", highest - cap.second.y,
                           fmt::format("gills reach {:.4f} m above the cap's highest point",
                                       highest - cap.second.y)});
        }
        // And they hang *within* the cap's rim, not out past it. The radial half of the same rule,
        // added because the spore emitter is now sized by `gillRadius`: a quantity a scene depends
        // on has to be one an invariant covers, or the next transform bug arrives as a cloud of
        // snow falling out of thin air beside the mushroom rather than as a test failure.
        float gillReach = 0.0f;
        for (const scene::Vertex& v : subject.parts[3].mesh.vertices) {
            gillReach = std::max(gillReach, glm::length(glm::vec2(v.position.x - a.capAttach.x,
                                                                  v.position.z - a.capAttach.z)));
        }
        float capReach = 0.0f;
        for (const scene::Vertex& v : subject.parts[0].mesh.vertices) {
            capReach = std::max(capReach, glm::length(glm::vec2(v.position.x - a.capAttach.x,
                                                                v.position.z - a.capAttach.z)));
        }
        if (gillReach > capReach + tolerance) {
            out.push_back({"gills-past-rim", gillReach - capReach,
                           fmt::format("gills reach {:.4f} m beyond the cap's rim",
                                       gillReach - capReach)});
        }
    }
    return out;
}

std::optional<search::Rejection> mushroomPlausibility(const search::Subject& subject,
                                                      const search::Parameters& v) {
    // Organic plausibility, not realism. The rule is that this rejects the *incoherent* and never the
    // merely strange: a cap lobed into six drooping petals passes, a cap floating above a stem it
    // never touches does not. Tightening it into biology is how a search stops finding anything
    // interesting.
    if (subject.parts.size() < 4) {
        return search::Rejection{"parts", "a mushroom is four parts"};
    }
    const auto capBounds = subject.parts[0].mesh.bounds();
    const auto stemBounds = subject.parts[2].mesh.bounds();

    // The cap has to overhang the stem, or it is a knob rather than a cap.
    const float capHalf = std::max(capBounds.second.x - capBounds.first.x,
                                   capBounds.second.z - capBounds.first.z) * 0.5f;
    const float stemHalf = std::max(stemBounds.second.x - stemBounds.first.x,
                                    stemBounds.second.z - stemBounds.first.z) * 0.5f;
    if (capHalf <= stemHalf * 1.25f) {
        return search::Rejection{"no-overhang",
                                 fmt::format("cap half-width {:.3f} barely exceeds the stem's {:.3f}",
                                             capHalf, stemHalf)};
    }
    // The cap has to sit *on* the stem. A gap is incoherent; an overlap is normal.
    // 0.08 was too lenient: the first winners' sheet had a cap visibly floating clear of its stem and
    // this rule passed it. The bound is the cap's *lowest* point, so a drooping rim already pulls it
    // down -- a gap at all means the centre never met the stem.
    const float gap = capBounds.first.y - stemBounds.second.y;
    if (gap > 0.015f) {
        return search::Rejection{"floating-cap", fmt::format("cap starts {:.3f} above the stem", gap)};
    }
    // Something has to be taller than it is wide, or the silhouette is a disc on the ground.
    const float height = capBounds.second.y - stemBounds.first.y;
    if (height < capHalf * 0.32f) {
        return search::Rejection{"pancake",
                                 fmt::format("height {:.3f} against cap half-width {:.3f}", height, capHalf)};
    }
    // A rim that has folded through the cap's own axis is a self-intersection this can catch cheaply.
    if (param(v, MushroomParam::LobeDepth) > 0.85f) {
        return search::Rejection{"lobe-fold", "lobe depth folds the rim through itself"};
    }
    return std::nullopt;
}

search::FeatureVector MushroomGenerator::features(const search::Subject& subject,
                                                  const search::Parameters& v) const {
    const auto cap = subject.parts[0].mesh.bounds();
    const auto stem = subject.parts[2].mesh.bounds();
    const float capWidth = std::max(cap.second.x - cap.first.x, cap.second.z - cap.first.z);
    const float capHeight = cap.second.y - cap.first.y;
    const float stemHeight = stem.second.y - stem.first.y;
    const float stemWidth = std::max(stem.second.x - stem.first.x, stem.second.z - stem.first.z);
    // Asymmetry measured on the geometry rather than read back from the parameter: lobes and tilt and
    // noise all contribute, and what the diversity axis wants is the result.
    const float spanX = cap.second.x - cap.first.x;
    const float spanZ = cap.second.z - cap.first.z;
    const float asymmetry = std::fabs(spanX - spanZ) / std::max(spanX + spanZ, 1e-4f);
    const auto triangles = static_cast<float>(
        (subject.parts[0].mesh.indices.size() + subject.parts[3].mesh.indices.size()) / 3);
    return {capWidth,
            capHeight,
            param(v, MushroomParam::CapThickness),
            stemHeight,
            stemWidth,
            param(v, MushroomParam::StemCurvature),
            asymmetry,
            param(v, MushroomParam::CapTiltDeg),
            param(v, MushroomParam::EdgeWaviness),
            param(v, MushroomParam::GillCount) / std::max(capWidth, 1e-3f),
            param(v, MushroomParam::EmissionIntensity),
            capWidth / std::max(stemHeight, 1e-3f),
            triangles};
}

namespace {

// Silhouette, measured on the mesh rather than on a render.
//
// Rendering six views per candidate and reading back a mask is the thorough way and it costs a GPU
// round trip per view; projecting the vertices onto an axis-aligned raster is deterministic, runs on
// the CPU, and measures the same two things -- how much of its own bounding box a shape fills, and
// how ragged its outline is. It is an approximation and is named as one: a concavity hidden behind
// the shape is invisible to it, which for a mushroom seen from the side is the underside, and the
// underside is scored separately by `structure`.
struct Silhouette {
    float fill = 0.0f;       // covered cells over bounding-box cells: a disc is ~0.79, a star is low
    float ragged = 0.0f;     // boundary cells over covered cells: high means a broken outline
};

Silhouette silhouetteAt(const search::Subject& subject, float azimuth) {
    constexpr int kGrid = 64;
    std::array<bool, kGrid * kGrid> covered{};
    const float c = std::cos(azimuth);
    const float sn = std::sin(azimuth);
    glm::vec2 lo(1e9f);
    glm::vec2 hi(-1e9f);
    std::vector<glm::vec2> flat;
    for (const search::SubjectPart& part : subject.parts) {
        for (const scene::Vertex& vert : part.mesh.vertices) {
            const glm::vec2 p(vert.position.x * c - vert.position.z * sn, vert.position.y);
            flat.push_back(p);
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
    }
    Silhouette out;
    const glm::vec2 span = hi - lo;
    if (flat.empty() || span.x <= 1e-5f || span.y <= 1e-5f) {
        return out;
    }
    for (const glm::vec2& p : flat) {
        const auto gx = static_cast<int>(std::clamp((p.x - lo.x) / span.x, 0.0f, 0.999f) * kGrid);
        const auto gy = static_cast<int>(std::clamp((p.y - lo.y) / span.y, 0.0f, 0.999f) * kGrid);
        covered[static_cast<std::size_t>(gy) * kGrid + static_cast<std::size_t>(gx)] = true;
    }
    int filled = 0;
    int boundary = 0;
    for (int y = 0; y < kGrid; ++y) {
        for (int x = 0; x < kGrid; ++x) {
            if (!covered[static_cast<std::size_t>(y) * kGrid + static_cast<std::size_t>(x)]) {
                continue;
            }
            ++filled;
            const bool edge = x == 0 || y == 0 || x == kGrid - 1 || y == kGrid - 1 ||
                              !covered[static_cast<std::size_t>(y) * kGrid + static_cast<std::size_t>(x - 1)] ||
                              !covered[static_cast<std::size_t>(y) * kGrid + static_cast<std::size_t>(x + 1)] ||
                              !covered[static_cast<std::size_t>(y - 1) * kGrid + static_cast<std::size_t>(x)] ||
                              !covered[static_cast<std::size_t>(y + 1) * kGrid + static_cast<std::size_t>(x)];
            if (edge) {
                ++boundary;
            }
        }
    }
    out.fill = static_cast<float>(filled) / static_cast<float>(kGrid * kGrid);
    out.ragged = filled > 0 ? static_cast<float>(boundary) / static_cast<float>(filled) : 1.0f;
    return out;
}

search::ScoreComponent banded(const char* name, float raw, search::ScoreBand band, float weight) {
    return search::ScoreComponent{name, raw, band(raw), weight};
}

} // namespace

std::vector<search::ScoreComponent> MushroomGenerator::domainScores(const search::Subject& subject,
                                                                     const search::Parameters& v) const {
    // Every component is a band and none is monotone (ADR-172). A selection stage that ranks by score
    // *is* an optimiser over what the score measures, so "more is better" on any of these is an
    // instruction to produce the failure mode it was meant to prevent.
    std::vector<search::ScoreComponent> out;
    const auto cap = subject.parts[0].mesh.bounds();
    const auto stem = subject.parts[2].mesh.bounds();
    const float capWidth = std::max(cap.second.x - cap.first.x, cap.second.z - cap.first.z);
    const float stemHeight = stem.second.y - stem.first.y;
    const float total = cap.second.y - stem.first.y;

    // 1. Proportion. The addendum's "ball on a stick" and "umbrella on a pole" are two specific
    //    bands to sit outside of, and both are expressible as this one ratio.
    out.push_back(banded("proportion", capWidth / std::max(stemHeight, 1e-3f),
                         search::ScoreBand{0.30f, 0.62f, 1.25f, 1.75f}, 2.0f));

    // 2. Cap thickness against its own radius: a wafer and a dumpling are both wrong.
    out.push_back(banded("capSolidity", param(v, MushroomParam::CapThickness),
                         search::ScoreBand{0.045f, 0.10f, 0.24f, 0.33f}, 1.0f));

    // 3. Silhouette fill, averaged over four azimuths. A filled box scores 1 and a disc about 0.5;
    //    the band wants something in between -- a shape with a cap and a stem, not a slab.
    Silhouette mean{};
    float raggedMax = 0.0f;
    float fillMin = 1e9f;
    float fillMax = -1e9f;
    for (int i = 0; i < 4; ++i) {
        const Silhouette sil = silhouetteAt(subject, static_cast<float>(i) * kPi * 0.25f);
        mean.fill += sil.fill * 0.25f;
        raggedMax = std::max(raggedMax, sil.ragged);
        fillMin = std::min(fillMin, sil.fill);
        fillMax = std::max(fillMax, sil.fill);
    }
    out.push_back(banded("silhouette", mean.fill, search::ScoreBand{0.10f, 0.20f, 0.44f, 0.62f}, 2.5f));

    // 4. Multi-view consistency: a hero has to stay interesting as the camera moves, so a shape whose
    //    silhouette collapses from one azimuth to another fails however good its best view is.
    const float swing = (fillMax - fillMin) / std::max(fillMax, 1e-4f);
    out.push_back(banded("multiView", swing, search::ScoreBand{-0.01f, 0.0f, 0.18f, 0.42f}, 1.5f));

    // 5. Outline coherence. High raggedness is the detached-fragment artifact the first contact sheet
    //    showed on high-noise candidates; a *little* is what stops a cap being a machined disc.
    // Calibrated from the population rather than guessed: the first band was [0.05 .. 0.52] and the
    // measured distribution runs 0.44 to 0.65, so five of six winners scored exactly 0 on it and the
    // component was contributing nothing but a constant. This is the failure ADR-172 predicted and the
    // reason components are stored separately -- the breakdown is what said which band lied.
    out.push_back(banded("outline", raggedMax, search::ScoreBand{0.36f, 0.42f, 0.58f, 0.72f}, 2.0f));

    // 6. Organicity: all three scales present and none dominant. The large term is the lobes, the
    //    medium is the waviness and the tilt, the small is the noise -- and the score is how *even*
    //    the three are, which is what "combines large, medium and small rather than noise everywhere"
    //    means when it is made measurable.
    const float large = param(v, MushroomParam::LobeDepth) / 0.30f;
    const float medium = 0.5f * (param(v, MushroomParam::EdgeWaviness) / 0.20f +
                                 param(v, MushroomParam::CapTiltDeg) / 18.0f);
    const float small = param(v, MushroomParam::SurfaceNoiseAmp) / 0.06f;
    const float spread = std::max({large, medium, small}) - std::min({large, medium, small});
    out.push_back(banded("organicity", spread, search::ScoreBand{-0.01f, 0.0f, 0.45f, 0.85f}, 1.5f));

    // 7. Structure: the gills have to be legible at the distance a hero is seen from. Too few and
    //    the underside is a smooth dome -- which the bioluminescence note says reads as a lamp -- and
    //    too many and they merge into a solid ring at any distance.
    const float gillPitch = 2.0f * kPi * (capWidth * 0.5f) /
                            std::max(param(v, MushroomParam::GillCount), 1.0f);
    out.push_back(banded("gillPitch", gillPitch, search::ScoreBand{0.008f, 0.016f, 0.055f, 0.11f}, 1.5f));

    // 8. Stem interest: a straight stem reads as a cylinder (fungi.md), and one bent double reads as
    //    broken.
    out.push_back(banded("stemCurve", param(v, MushroomParam::StemCurvature),
                         search::ScoreBand{0.01f, 0.06f, 0.30f, 0.45f}, 1.0f));

    // 9. Performance, and deliberately not a triangle count. `scene/mesh_metrics.hpp` records that
    //    this renderer's fragment cost tracks triangle *size*, with the knee at the 2x2-quad
    //    threshold -- so the quantity that predicts cost is projected pixels per triangle, and a
    //    candidate below the knee is paying quad overdraw however pretty it is. Banded, because a
    //    mushroom made of four enormous triangles is cheap and shapeless.
    std::uint32_t triangles = 0;
    float area = 0.0f;
    for (const search::SubjectPart& part : subject.parts) {
        const scene::MeshMetrics m = scene::meshMetrics(part.mesh);
        triangles += m.triangles;
        area += m.surfaceArea;
    }
    // Pixels per triangle for a hero framed at ~45% of a 800 px frame height.
    const float pixelsPerUnit = 800.0f * 0.45f / std::max(total, 1e-3f);
    const float pxPerTri = triangles > 0 ? (area * pixelsPerUnit * pixelsPerUnit * 0.25f) /
                                               static_cast<float>(triangles)
                                         : 0.0f;
    // Likewise: the guessed band started at 40 px and the population sits between 8 and 32, so the
    // component was a near-constant 0. Re-banded around what these meshes actually are -- and the
    // lower edge is the 2x2-quad knee, which is the one number here that is not a preference.
    out.push_back(banded("pixelsPerTriangle", pxPerTri, search::ScoreBand{3.0f, 8.0f, 120.0f, 600.0f}, 1.5f));

    return out;
}

} // namespace avgen::organism
