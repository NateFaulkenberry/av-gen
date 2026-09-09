#include "scene/procedural.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace avgen;
using namespace avgen::scene;
using Catch::Matchers::WithinAbs;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

constexpr float kPi = glm::pi<float>();

void checkVec(const glm::vec3& v, const glm::vec3& expected, double tol = 1e-5) {
    CHECK_THAT(d(v.x), WithinAbs(d(expected.x), tol));
    CHECK_THAT(d(v.y), WithinAbs(d(expected.y), tol));
    CHECK_THAT(d(v.z), WithinAbs(d(expected.z), tol));
}

// Sum over triangles of dot(a, cross(b, c)) / 6: positive for a closed CCW-outward mesh.
double signedVolume(const MeshData& m) {
    double volume = 0.0;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const glm::vec3& a = m.vertices[m.indices[i]].position;
        const glm::vec3& b = m.vertices[m.indices[i + 1]].position;
        const glm::vec3& c = m.vertices[m.indices[i + 2]].position;
        volume += d(glm::dot(a, glm::cross(b, c)));
    }
    return volume / 6.0;
}

void checkUnitNormals(const MeshData& m) {
    for (const auto& v : m.vertices) {
        CHECK_THAT(d(glm::length(v.normal)), WithinAbs(1.0, 1e-5));
    }
}

bool sameMesh(const MeshData& a, const MeshData& b) {
    if (a.vertices.size() != b.vertices.size() || a.indices != b.indices) {
        return false;
    }
    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        if (a.vertices[i].position != b.vertices[i].position || a.vertices[i].normal != b.vertices[i].normal ||
            a.vertices[i].uv != b.vertices[i].uv) {
            return false;
        }
    }
    return true;
}

glm::vec3 forwardOf(const glm::quat& q) {
    return q * glm::vec3(0.0f, 0.0f, 1.0f);
}

glm::vec3 upOf(const glm::quat& q) {
    return q * glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace

// ---- primitives -------------------------------------------------------------------------------

TEST_CASE("Box generator: per-face grids, faceted normals, CCW outward", "[scene][procedural]") {
    const MeshData box = makeBox({1.0f, 2.0f, 3.0f}, 2);
    REQUIRE(box.valid());
    CHECK(box.vertices.size() == 6 * 9);  // 6 (n+1)^2
    CHECK(box.indices.size() == 36 * 4);  // 36 n^2
    const auto [lo, hi] = box.bounds();
    checkVec(lo, {-0.5f, -1.0f, -1.5f});
    checkVec(hi, {0.5f, 1.0f, 1.5f});
    checkUnitNormals(box);
    for (const auto& v : box.vertices) {
        // Faceted: the normal is one of the six axes and the vertex lies on that face.
        CHECK(std::abs(glm::dot(v.normal, glm::vec3(1.0f))) == 1.0f);
        CHECK(glm::dot(v.normal, v.position) > 0.0f);
        CHECK(v.uv.x >= 0.0f);
        CHECK(v.uv.x <= 1.0f);
    }
    CHECK_THAT(signedVolume(box), WithinAbs(6.0, 1e-4));
    CHECK(sameMesh(box, makeBox({1.0f, 2.0f, 3.0f}, 2)));
    CHECK(makeBox({1.0f, 1.0f, 1.0f}, 1).vertices.size() == 24);
    CHECK(makeBox({1.0f, 1.0f, 1.0f}, 1).indices.size() == 36);
}

TEST_CASE("Cylinder generator: smooth side, cap fans, exact counts", "[scene][procedural]") {
    const int radial = 24;
    const int heightSegs = 2;
    const MeshData cyl = makeCylinder(0.5f, 2.0f, radial, heightSegs, true);
    REQUIRE(cyl.valid());
    const std::size_t sideVertices = static_cast<std::size_t>((radial + 1) * (heightSegs + 1));
    CHECK(cyl.vertices.size() == sideVertices + 2 * static_cast<std::size_t>(radial + 2));
    CHECK(cyl.indices.size() == static_cast<std::size_t>(6 * radial * heightSegs + 6 * radial));
    const auto [lo, hi] = cyl.bounds();
    checkVec(lo, {-0.5f, -1.0f, -0.5f});
    checkVec(hi, {0.5f, 1.0f, 0.5f});
    checkUnitNormals(cyl);
    for (std::size_t i = 0; i < sideVertices; ++i) {
        const Vertex& v = cyl.vertices[i];
        CHECK(glm::dot(v.normal, glm::vec3(v.position.x, 0.0f, v.position.z)) > 0.0f); // outward
        CHECK_THAT(d(v.normal.y), WithinAbs(0.0, 1e-6));                                 // smooth side
    }
    for (std::size_t i = sideVertices; i < cyl.vertices.size(); ++i) {
        CHECK(std::abs(cyl.vertices[i].normal.y) == 1.0f); // caps are flat
    }
    // Polygon area * height.
    const double polygonArea = 0.5 * radial * 0.25 * std::sin(2.0 * M_PI / radial);
    CHECK_THAT(signedVolume(cyl), WithinAbs(polygonArea * 2.0, 1e-4));

    const MeshData open = makeCylinder(0.5f, 2.0f, radial, heightSegs, false);
    REQUIRE(open.valid());
    CHECK(open.vertices.size() == sideVertices);
    CHECK(open.indices.size() == static_cast<std::size_t>(6 * radial * heightSegs));
    CHECK(sameMesh(cyl, makeCylinder(0.5f, 2.0f, radial, heightSegs, true)));
}

TEST_CASE("UV sphere generator: smooth outward normals and seam duplication", "[scene][procedural]") {
    const int segments = 16;
    const int rings = 8;
    const MeshData sphere = makeUvSphere(1.0f, segments, rings);
    REQUIRE(sphere.valid());
    CHECK(sphere.vertices.size() == static_cast<std::size_t>((segments + 1) * (rings + 1)));
    CHECK(sphere.indices.size() == static_cast<std::size_t>(6 * segments * (rings - 1)));
    checkUnitNormals(sphere);
    for (const auto& v : sphere.vertices) {
        CHECK_THAT(d(glm::length(v.position)), WithinAbs(1.0, 1e-5));
        CHECK(glm::dot(v.normal, v.position) > 0.99f);
    }
    // Seam vertices share positions but not u.
    CHECK(sphere.vertices[1].uv.x != sphere.vertices[static_cast<std::size_t>(segments) + 1].uv.x);
    const double exact = 4.0 / 3.0 * M_PI;
    const double volume = signedVolume(sphere);
    CHECK(volume > exact * 0.85);
    CHECK(volume < exact);
    CHECK(sameMesh(sphere, makeUvSphere(1.0f, segments, rings)));
}

TEST_CASE("Torus generator: smooth outward normals", "[scene][procedural]") {
    const int major = 24;
    const int minor = 12;
    const MeshData torus = makeTorus(1.0f, 0.25f, major, minor);
    REQUIRE(torus.valid());
    CHECK(torus.vertices.size() == static_cast<std::size_t>((major + 1) * (minor + 1)));
    CHECK(torus.indices.size() == static_cast<std::size_t>(6 * major * minor));
    checkUnitNormals(torus);
    for (const auto& v : torus.vertices) {
        const glm::vec3 ringCentre = glm::normalize(glm::vec3(v.position.x, 0.0f, v.position.z));
        CHECK(glm::dot(v.normal, v.position - ringCentre) > 0.99f * 0.25f);
    }
    const auto [lo, hi] = torus.bounds();
    checkVec(lo, {-1.25f, -0.25f, -1.25f});
    checkVec(hi, {1.25f, 0.25f, 1.25f});
    const double exact = 2.0 * M_PI * M_PI * 1.0 * 0.0625;
    const double volume = signedVolume(torus);
    CHECK(volume > exact * 0.85);
    CHECK(volume < exact);
    CHECK(sameMesh(torus, makeTorus(1.0f, 0.25f, major, minor)));
}

TEST_CASE("makeSourceMesh validates and dispatches", "[scene][procedural]") {
    SourceSpec spec;
    spec.kind = PrimitiveKind::Sphere;
    spec.radius = 2.0f;
    auto mesh = makeSourceMesh(spec);
    REQUIRE(mesh.has_value());
    CHECK(mesh->name == "sphere");
    spec.rings = 1;
    CHECK_FALSE(makeSourceMesh(spec).has_value());
    spec.rings = 16;
    spec.kind = PrimitiveKind::Box;
    spec.size.y = 0.0f;
    CHECK_FALSE(spec.validate().has_value());
    // Hash covers only the fields the current primitive uses.
    SourceSpec a;
    SourceSpec b = a;
    b.rings = 4; // cylinder is the default kind: rings do not matter
    CHECK(a.structuralHash() == b.structuralHash());
    b.radius = 0.7f;
    CHECK(a.structuralHash() != b.structuralHash());
    CHECK(primitiveKindFromName("torus") == PrimitiveKind::Torus);
    CHECK_FALSE(primitiveKindFromName("cone").has_value());
}

// ---- distributions -----------------------------------------------------------------------------

TEST_CASE("Single distribution is the identity", "[scene][procedural]") {
    Distribution dist;
    dist.kind = DistributionKind::Single;
    CHECK(dist.instanceCount() == 1);
    const Transform t = dist.placement(0);
    checkVec(t.position, glm::vec3(0.0f));
    checkVec(t.scale, glm::vec3(1.0f));
    CHECK_THAT(d(t.rotation.w), WithinAbs(1.0, 1e-6));
}

TEST_CASE("Linear distribution: endpoints, spacing override, orientAlong", "[scene][procedural]") {
    Distribution dist;
    dist.kind = DistributionKind::Linear;
    dist.count = 5;
    dist.start = {-2.0f, 0.0f, 0.0f};
    dist.end = {2.0f, 0.0f, 0.0f};
    checkVec(dist.placement(0).position, dist.start);
    checkVec(dist.placement(4).position, dist.end);
    checkVec(dist.placement(2).position, glm::vec3(0.0f));
    CHECK_THAT(d(dist.placement(1).rotation.w), WithinAbs(1.0, 1e-6));

    dist.spacing = 1.0f;
    checkVec(dist.placement(4).position, {2.0f, 0.0f, 0.0f});
    checkVec(dist.placement(1).position, {-1.0f, 0.0f, 0.0f});

    dist.orientAlong = true;
    dist.end = {-2.0f, 0.0f, 4.0f}; // direction +Z
    const glm::quat q = dist.placement(3).rotation;
    checkVec(forwardOf(q), {0.0f, 0.0f, 1.0f});
    checkVec(upOf(q), {0.0f, 1.0f, 0.0f});
    dist.end = {5.0f, 0.0f, 0.0f}; // direction +X: +Z faces +X, +Y stays up
    const glm::quat qx = dist.placement(0).rotation;
    checkVec(forwardOf(qx), {1.0f, 0.0f, 0.0f});
    checkVec(upOf(qx), {0.0f, 1.0f, 0.0f});

    dist.count = 1;
    checkVec(dist.placement(0).position, dist.start);
}

TEST_CASE("Grid distribution: x fastest, then y, then z, centred", "[scene][procedural]") {
    Distribution dist;
    dist.kind = DistributionKind::Grid;
    dist.gridCount = {2, 1, 3};
    dist.gridSpacing = {2.0f, 2.0f, 2.0f};
    CHECK(dist.instanceCount() == 6);
    checkVec(dist.placement(0).position, {-1.0f, 0.0f, -2.0f});
    checkVec(dist.placement(1).position, {1.0f, 0.0f, -2.0f});
    checkVec(dist.placement(2).position, {-1.0f, 0.0f, 0.0f});
    checkVec(dist.placement(5).position, {1.0f, 0.0f, 2.0f});
    dist.gridCount = {3, 2, 1};
    dist.gridSpacing = {1.0f, 4.0f, 1.0f};
    checkVec(dist.placement(0).position, {-1.0f, -2.0f, 0.0f});
    checkVec(dist.placement(3).position, {-1.0f, 2.0f, 0.0f});
    checkVec(dist.placement(5).position, {1.0f, 2.0f, 0.0f});
    dist.gridCount = {0, 1, 1};
    CHECK_FALSE(dist.validate().has_value());
}

TEST_CASE("Radial distribution: closed circle, arc, planes and orientation modes", "[scene][procedural]") {
    Distribution dist;
    dist.kind = DistributionKind::Radial;
    dist.count = 4;
    dist.radius = 2.0f;
    dist.startAngle = 0.0f;
    dist.endAngle = 2.0f * kPi;
    dist.plane = DistributionPlane::XZ;

    SECTION("closed circle divides 2pi by count, counter-clockwise about +Y") {
        checkVec(dist.placement(0).position, {2.0f, 0.0f, 0.0f});
        checkVec(dist.placement(1).position, {0.0f, 0.0f, -2.0f});
        checkVec(dist.placement(2).position, {-2.0f, 0.0f, 0.0f});
        checkVec(dist.placement(3).position, {0.0f, 0.0f, 2.0f});
    }
    SECTION("an arc divides by count - 1") {
        dist.endAngle = kPi;
        dist.count = 3;
        checkVec(dist.placement(0).position, {2.0f, 0.0f, 0.0f});
        checkVec(dist.placement(1).position, {0.0f, 0.0f, -2.0f});
        checkVec(dist.placement(2).position, {-2.0f, 0.0f, 0.0f});
    }
    SECTION("centre offsets the circle") {
        dist.center = {1.0f, 5.0f, 0.0f};
        checkVec(dist.placement(0).position, {3.0f, 5.0f, 0.0f});
    }
    SECTION("XY and YZ planes") {
        dist.plane = DistributionPlane::XY;
        checkVec(dist.placement(1).position, {0.0f, 2.0f, 0.0f});
        checkVec(upOf(dist.placement(1).rotation), {0.0f, 0.0f, 1.0f}); // +Y along the plane normal
        dist.plane = DistributionPlane::YZ;
        checkVec(dist.placement(0).position, {0.0f, 2.0f, 0.0f});
        checkVec(dist.placement(1).position, {0.0f, 0.0f, 2.0f});
    }
    SECTION("orientation modes steer the instance's +Z") {
        for (int i = 0; i < 4; ++i) {
            const Transform t = dist.placement(i);
            const glm::vec3 outward = glm::normalize(t.position);
            checkVec(forwardOf(t.rotation), outward);
            checkVec(upOf(t.rotation), {0.0f, 1.0f, 0.0f});
        }
        dist.orientation = OrientationMode::Inward;
        checkVec(forwardOf(dist.placement(1).rotation), {0.0f, 0.0f, 1.0f});
        dist.orientation = OrientationMode::Tangent;
        checkVec(forwardOf(dist.placement(0).rotation), {0.0f, 0.0f, -1.0f}); // direction of increasing angle
        checkVec(forwardOf(dist.placement(1).rotation), {-1.0f, 0.0f, 0.0f});
        dist.orientation = OrientationMode::None;
        CHECK_THAT(d(dist.placement(2).rotation.w), WithinAbs(1.0, 1e-6));
    }
    SECTION("count limits") {
        dist.count = 0;
        CHECK_FALSE(dist.validate().has_value());
        dist.count = 100001;
        CHECK_FALSE(dist.validate().has_value());
        dist.count = 100000;
        CHECK(dist.validate().has_value());
    }
}

TEST_CASE("Spiral distribution: radius growth, height, turns, tangent", "[scene][procedural]") {
    Distribution dist;
    dist.kind = DistributionKind::Spiral;
    dist.count = 3;
    dist.radius = 1.0f;
    dist.radiusGrowth = 1.0f;
    dist.turns = 1.0f;
    dist.spiralHeight = 4.0f;
    dist.plane = DistributionPlane::XZ;
    checkVec(dist.placement(0).position, {1.0f, 0.0f, 0.0f});
    checkVec(dist.placement(1).position, {-1.5f, 2.0f, 0.0f});
    checkVec(dist.placement(2).position, {2.0f, 4.0f, 0.0f});
    checkVec(forwardOf(dist.placement(0).rotation), {1.0f, 0.0f, 0.0f}); // outward

    dist.orientation = OrientationMode::Tangent;
    const glm::vec3 expectedTangent = glm::normalize(glm::vec3(1.0f, 4.0f, -2.0f * kPi));
    checkVec(forwardOf(dist.placement(0).rotation), expectedTangent);

    dist.spiralAngle = kPi;
    dist.orientation = OrientationMode::Outward;
    checkVec(dist.placement(0).position, {-1.0f, 0.0f, 0.0f});
    CHECK(distributionKindFromName("spiral") == DistributionKind::Spiral);
    CHECK(orientationModeFromName("tangent") == OrientationMode::Tangent);
    CHECK(distributionPlaneFromName("yz") == DistributionPlane::YZ);
}

// ---- hashing and variation -------------------------------------------------------------------

TEST_CASE("hashInstance is deterministic, uniform-ish and channel/seed sensitive", "[scene][procedural]") {
    CHECK(hashInstance(7, 3, 1) == hashInstance(7, 3, 1));
    double sum = 0.0;
    float lo = 1.0f;
    float hi = 0.0f;
    for (std::uint32_t i = 0; i < 10000; ++i) {
        const float h = hashInstance(42, i, 0);
        REQUIRE(h >= 0.0f);
        REQUIRE(h < 1.0f);
        sum += d(h);
        lo = std::min(lo, h);
        hi = std::max(hi, h);
    }
    CHECK_THAT(sum / 10000.0, WithinAbs(0.5, 0.02));
    CHECK(lo < 0.01f);
    CHECK(hi > 0.99f);
    int channelDiffers = 0;
    int seedDiffers = 0;
    for (std::uint32_t i = 0; i < 100; ++i) {
        channelDiffers += hashInstance(1, i, 0) != hashInstance(1, i, 1) ? 1 : 0;
        seedDiffers += hashInstance(1, i, 0) != hashInstance(2, i, 0) ? 1 : 0;
    }
    CHECK(channelDiffers == 100);
    CHECK(seedDiffers == 100);
}

TEST_CASE("Variation transform: identity at zero, seeded, positive scale", "[scene][procedural]") {
    Variation none;
    const Transform id = variationTransform(none, 5);
    checkVec(id.position, glm::vec3(0.0f));
    checkVec(id.scale, glm::vec3(1.0f));
    CHECK_THAT(d(id.rotation.w), WithinAbs(1.0, 1e-6));

    Variation v;
    v.seed = 9;
    v.randomPosition = {1.0f, 2.0f, 0.0f};
    v.randomRotation = {0.0f, kPi, 0.0f};
    v.randomScale = {1.0f, 1.0f, 1.0f};
    v.randomUniformScale = 1.0f;
    const Transform a = variationTransform(v, 3);
    const Transform b = variationTransform(v, 3);
    CHECK(a.position == b.position);
    CHECK(a.rotation == b.rotation);
    CHECK(a.scale == b.scale);
    Variation other = v;
    other.seed = 10;
    CHECK(variationTransform(other, 3).position != a.position);
    CHECK(variationTransform(v, 4).position != a.position);
    for (std::uint32_t i = 0; i < 1000; ++i) {
        const Transform t = variationTransform(v, i);
        CHECK(t.scale.x > 0.0f);
        CHECK(t.scale.y > 0.0f);
        CHECK(t.scale.z > 0.0f);
        CHECK(std::abs(t.position.x) <= 1.0f);
        CHECK(std::abs(t.position.y) <= 2.0f);
        CHECK(t.position.z == 0.0f);
        CHECK_THAT(d(glm::length(t.rotation)), WithinAbs(1.0, 1e-5));
    }
    CHECK(v.structuralHash() != other.structuralHash());
}

// ---- transform order --------------------------------------------------------------------------

TEST_CASE("Instance matrix is distribution * placement * variation * source", "[scene][procedural]") {
    ProceduralGeometry g;
    g.source.kind = PrimitiveKind::Sphere;
    g.sourceTransform.position = {0.0f, 1.0f, 0.0f};
    g.sourceTransform.scale = glm::vec3(2.0f);
    g.distribution.kind = DistributionKind::Radial;
    g.distribution.count = 4;
    g.distribution.radius = 2.0f;
    g.distributionTransform.position = {10.0f, 0.0f, 0.0f};
    g.distributionTransform.rotation = glm::angleAxis(kPi * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(g.rebuild());
    REQUIRE(g.instances.size() == 4);

    // Hand computed: origin -> source (0,1,0) -> placement 1 (0,1,-2) -> rotate 90 deg about +Y
    // (-2,1,0) -> translate (8,1,0).
    const glm::mat4 m = g.instanceMatrix(1);
    checkVec(glm::vec3(m * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)), {8.0f, 1.0f, 0.0f});
    // A source point (1,0,0): scaled to (2,0,0), lifted to (2,1,0), outward rotation at
    // placement 1 (+Z -> -Z, +X -> -X): (-2,1,-2), then the distribution rotation: (-2,1,2) + (10,0,0).
    checkVec(glm::vec3(m * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)), {8.0f, 1.0f, 2.0f});

    // Explicit product of the four matrices.
    const glm::mat4 expected = g.distributionTransform.matrix() * g.distribution.placement(1).matrix() *
                               variationTransform(g.variation, 1).matrix() * g.sourceTransform.matrix();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK_THAT(d(m[c][r]), WithinAbs(d(expected[c][r]), 1e-5));
        }
    }
    // The record's transform times the source transform reproduces it.
    const InstanceRecord& rec = g.instances[1];
    Transform t;
    t.position = glm::vec3(rec.position);
    t.rotation = glm::quat(rec.rotation.w, rec.rotation.x, rec.rotation.y, rec.rotation.z);
    t.scale = glm::vec3(rec.scale);
    const glm::mat4 fromRecord = t.matrix() * g.sourceTransform.matrix();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK_THAT(d(fromRecord[c][r]), WithinAbs(d(m[c][r]), 1e-5));
        }
    }
    CHECK_THAT(d(rec.scale.w), WithinAbs(1.0 / 3.0, 1e-6)); // normalised index
    CHECK(rec.color.a == 1.0f);                             // instance id
    CHECK(rec.position.w == 1.0f);
}

// ---- deformers -----------------------------------------------------------------------------------

TEST_CASE("Twist rotates about the axis proportionally to height", "[scene][procedural]") {
    Deformer twist;
    twist.kind = DeformerKind::Twist;
    twist.amount = 1.0f;
    const glm::vec3 p = applyDeformer(twist, {1.0f, 1.0f, 0.0f}, 0.0);
    checkVec(p, {std::cos(1.0f), 1.0f, -std::sin(1.0f)});
    checkVec(applyDeformer(twist, {0.0f, 0.0f, 0.0f}, 0.0), glm::vec3(0.0f));
    checkVec(applyDeformer(twist, {1.0f, 0.0f, 0.0f}, 0.0), {1.0f, 0.0f, 0.0f});
    // speed * t adds a rigid rotation; falloff ramps the effect from the centre.
    twist.amount = 0.0f;
    twist.speed = kPi * 0.5f;
    checkVec(applyDeformer(twist, {1.0f, 0.5f, 0.0f}, 1.0), {0.0f, 0.5f, -1.0f});
    twist.falloff = 2.0f;
    checkVec(applyDeformer(twist, {1.0f, 1.0f, 0.0f}, 1.0), glm::vec3(std::cos(kPi * 0.25f), 1.0f, -std::sin(kPi * 0.25f)));
    checkVec(applyDeformer(twist, {1.0f, 0.0f, 0.0f}, 1.0), {1.0f, 0.0f, 0.0f});
}

TEST_CASE("Bend maps a straight column onto an arc of radius 1 / amount", "[scene][procedural]") {
    Deformer bend;
    bend.kind = DeformerKind::Bend;
    bend.amount = kPi * 0.5f; // curvature: 1 unit of height sweeps 90 degrees
    const float radius = 1.0f / bend.amount;
    const glm::vec3 centreOfCurvature(radius, 0.0f, 0.0f);
    checkVec(applyDeformer(bend, glm::vec3(0.0f), 0.0), glm::vec3(0.0f));
    checkVec(applyDeformer(bend, {0.0f, 1.0f, 0.0f}, 0.0), {radius, radius, 0.0f});
    for (const float y : {0.25f, 0.5f, 0.75f, 1.0f, 2.0f}) {
        const glm::vec3 q = applyDeformer(bend, {0.0f, y, 0.0f}, 0.0);
        CHECK_THAT(d(glm::length(q - centreOfCurvature)), WithinAbs(d(radius), 1e-5));
        CHECK_THAT(d(q.z), WithinAbs(0.0, 1e-6));
    }
    // Points off the axis keep their distance to the centre of curvature (R - x).
    const glm::vec3 side = applyDeformer(bend, {0.1f, 1.0f, 0.3f}, 0.0);
    CHECK_THAT(d(glm::length(glm::vec3(side.x, side.y, 0.0f) - centreOfCurvature)), WithinAbs(d(radius - 0.1f), 1e-5));
    CHECK_THAT(d(side.z), WithinAbs(0.3, 1e-6));
    // Negative curvature bends the other way; zero is the identity.
    bend.amount = -kPi * 0.5f;
    checkVec(applyDeformer(bend, {0.0f, 1.0f, 0.0f}, 0.0), {-radius, radius, 0.0f});
    bend.amount = 0.0f;
    checkVec(applyDeformer(bend, {0.3f, 1.0f, 0.2f}, 0.0), {0.3f, 1.0f, 0.2f});
    // Falloff limits the bent region: above it the column continues straight along the tangent.
    bend.amount = kPi * 0.5f;
    bend.falloff = 0.5f;
    const glm::vec3 end = applyDeformer(bend, {0.0f, 0.5f, 0.0f}, 0.0);   // 45 degrees into the arc
    const glm::vec3 beyond = applyDeformer(bend, {0.0f, 1.5f, 0.0f}, 0.0); // one unit further, straight
    const glm::vec3 tangent = glm::normalize(glm::vec3(std::sin(kPi * 0.25f), std::cos(kPi * 0.25f), 0.0f));
    checkVec(beyond, end + tangent);
    // A different bend direction: displacementAxis picks the plane.
    bend.falloff = 0.0f;
    bend.displacementAxis = {0.0f, 0.0f, 1.0f};
    checkVec(applyDeformer(bend, {0.0f, 1.0f, 0.0f}, 0.0), {0.0f, radius, radius});
}

TEST_CASE("Sine, noise and displacement deformers", "[scene][procedural]") {
    Deformer sine;
    sine.kind = DeformerKind::Sine;
    sine.amount = 0.5f;
    sine.frequency = 1.0f;
    checkVec(applyDeformer(sine, {0.0f, kPi * 0.5f, 0.0f}, 0.0), {0.5f, kPi * 0.5f, 0.0f}); // peak
    checkVec(applyDeformer(sine, {0.0f, 0.0f, 0.0f}, 0.0), glm::vec3(0.0f));
    sine.phase = kPi * 0.5f;
    checkVec(applyDeformer(sine, {0.0f, 0.0f, 0.0f}, 0.0), {0.5f, 0.0f, 0.0f});

    Deformer noise;
    noise.kind = DeformerKind::Noise;
    noise.amount = 0.0f;
    checkVec(applyDeformer(noise, {0.3f, 0.7f, -1.2f}, 0.5), {0.3f, 0.7f, -1.2f});
    noise.amount = 0.3f;
    noise.axisMask = {1.0f, 0.0f, 1.0f};
    bool moved = false;
    for (const glm::vec3 p : {glm::vec3(0.3f, 0.7f, -1.2f), glm::vec3(2.5f, -0.4f, 0.1f), glm::vec3(-3.3f, 1.1f, 4.2f)}) {
        const glm::vec3 q = applyDeformer(noise, p, 0.25);
        CHECK(std::abs(q.x - p.x) <= 0.3f);
        CHECK(q.y == p.y); // masked axis
        CHECK(std::abs(q.z - p.z) <= 0.3f);
        CHECK(q == applyDeformer(noise, p, 0.25));
        moved = moved || q != p;
    }
    CHECK(moved);

    Deformer disp;
    disp.kind = DeformerKind::Displacement;
    disp.amount = 0.4f;
    disp.axis = {0.0f, 1.0f, 0.0f};
    const glm::vec3 p{0.4f, 0.2f, 0.9f};
    const glm::vec3 q = applyDeformer(disp, p, 0.0);
    CHECK(q.x == p.x);
    CHECK(q.z == p.z);
    CHECK(q.y >= p.y);
    CHECK(q.y - p.y <= 0.4f);
    CHECK(q.y != p.y);
}

TEST_CASE("Deformer stack order, spaces and enabled flags", "[scene][procedural]") {
    Deformer twist;
    twist.kind = DeformerKind::Twist;
    twist.amount = 1.2f;
    Deformer noise;
    noise.kind = DeformerKind::Noise;
    noise.amount = 0.4f;
    noise.scale = 2.0f;
    const glm::vec3 p{0.7f, 1.3f, -0.2f};
    const glm::mat4 identity(1.0f);
    const glm::vec3 twistThenNoise = deformPoint({twist, noise}, p, identity, 0.0);
    const glm::vec3 noiseThenTwist = deformPoint({noise, twist}, p, identity, 0.0);
    CHECK(twistThenNoise != noiseThenTwist);
    CHECK(twistThenNoise == deformPoint({twist, noise}, p, identity, 0.0));
    checkVec(twistThenNoise, applyDeformer(noise, applyDeformer(twist, p, 0.0), 0.0), 1e-6);

    // A disabled deformer is skipped.
    Deformer off = noise;
    off.enabled = false;
    checkVec(deformPoint({twist, off}, p, identity, 0.0), applyDeformer(twist, p, 0.0), 1e-6);

    // Local vs world: with a rotated instance the twist axis differs.
    const glm::mat4 rotated = glm::mat4_cast(glm::angleAxis(kPi * 0.5f, glm::vec3(1.0f, 0.0f, 0.0f)));
    Deformer worldTwist = twist;
    worldTwist.space = DeformSpace::World;
    const glm::vec3 local = deformPoint({twist}, p, rotated, 0.0);
    const glm::vec3 world = deformPoint({worldTwist}, p, rotated, 0.0);
    CHECK(local != world);
    checkVec(local, glm::vec3(rotated * glm::vec4(applyDeformer(twist, p, 0.0), 1.0f)), 1e-6);
    checkVec(world, applyDeformer(twist, glm::vec3(rotated * glm::vec4(p, 1.0f)), 0.0), 1e-6);
    // No deformers: just the instance transform.
    checkVec(deformPoint({}, p, rotated, 0.0), glm::vec3(rotated * glm::vec4(p, 1.0f)), 1e-6);
}

TEST_CASE("fbm3 stays in [0, 1], is deterministic and seed dependent", "[scene][procedural]") {
    float lo = 1.0f;
    float hi = 0.0f;
    int seedDiffers = 0;
    for (int i = 0; i < 2000; ++i) {
        const glm::vec3 p(static_cast<float>(i) * 0.137f - 90.0f, static_cast<float>(i % 17) * 0.61f, static_cast<float>(i % 5) * -1.3f);
        const float n = fbm3(p, 1);
        REQUIRE(n >= 0.0f);
        REQUIRE(n <= 1.0f);
        CHECK(n == fbm3(p, 1));
        lo = std::min(lo, n);
        hi = std::max(hi, n);
        seedDiffers += fbm3(p, 2) != n ? 1 : 0;
    }
    CHECK(lo < 0.35f);
    CHECK(hi > 0.65f);
    CHECK(seedDiffers > 1900);
    // Continuity across a cell boundary (value noise is C0 with a smoothstep blend).
    const float a = fbm3({0.999f, 0.5f, 0.5f}, 3);
    const float b = fbm3({1.001f, 0.5f, 0.5f}, 3);
    CHECK(std::abs(a - b) < 0.05f);
    CHECK(deformerKindFromName("displacement") == DeformerKind::Displacement);
    CHECK(deformSpaceFromName("world") == DeformSpace::World);
}

// ---- rebuild / records / bounds --------------------------------------------------------------

TEST_CASE("rebuild reports structural changes only and bumps the version", "[scene][procedural]") {
    ProceduralGeometry g;
    g.distribution.count = 8;
    CHECK(g.structureVersion == 0);
    CHECK(g.rebuild());
    CHECK(g.structureVersion == 1);
    CHECK(g.instances.size() == 8);
    CHECK(g.meshHash == g.source.structuralHash());
    CHECK_FALSE(g.rebuild());
    CHECK(g.structureVersion == 1);

    Deformer twist;
    twist.amount = 2.0f;
    g.deformers.push_back(twist);
    CHECK_FALSE(g.rebuild()); // deformers are per-frame uniforms
    g.material.baseColor = {0.1f, 0.2f, 0.3f};
    CHECK_FALSE(g.rebuild()); // material colour is a uniform
    g.distribution.count = 9;
    CHECK(g.rebuild());
    CHECK(g.structureVersion == 2);
    CHECK(g.instances.size() == 9);
    g.materialVariation.hueGradient = 0.5f;
    CHECK(g.rebuild()); // bakes into the records
    CHECK(g.structureVersion == 3);
    g.sourceTransform.scale = glm::vec3(3.0f);
    CHECK(g.rebuild());
    g.variation.seed = 99;
    CHECK(g.rebuild());
    CHECK(g.structureVersion == 5);

    for (std::size_t i = 0; i < g.instances.size(); ++i) {
        const InstanceRecord& r = g.instances[i];
        CHECK(r.color.a == static_cast<float>(i));
        CHECK(r.scale.w >= 0.0f);
        CHECK(r.scale.w <= 1.0f);
        for (int c = 0; c < 4; ++c) {
            CHECK(r.random[c] >= 0.0f);
            CHECK(r.random[c] < 1.0f);
            CHECK(r.random[c] == hashInstance(99, static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(c)));
        }
        CHECK(r.emissive.r >= 0.0f);
        CHECK(r.color.r >= 0.0f);
    }
    CHECK(g.instances.front().scale.w == 0.0f);
    CHECK(g.instances.back().scale.w == 1.0f);
}

TEST_CASE("Material variation bakes multipliers into the records", "[scene][procedural]") {
    ProceduralGeometry g;
    g.distribution.count = 16;
    REQUIRE(g.rebuild());
    for (const auto& r : g.instances) {
        checkVec(glm::vec3(r.color), glm::vec3(1.0f), 1e-6);
        checkVec(glm::vec3(r.emissive), glm::vec3(1.0f), 1e-6);
    }
    g.materialVariation.emissiveGradient = 2.0f;
    REQUIRE(g.rebuild());
    checkVec(glm::vec3(g.instances.front().emissive), glm::vec3(1.0f), 1e-6);
    checkVec(glm::vec3(g.instances.back().emissive), glm::vec3(3.0f), 1e-5);
    checkVec(glm::vec3(g.instances[5].emissive), glm::vec3(1.0f + 2.0f / 3.0f), 1e-5);
    // Half a turn of hue changes the base colour multiplier of the last instance, not the first.
    g.materialVariation.hueGradient = 0.5f;
    REQUIRE(g.rebuild());
    checkVec(glm::vec3(g.instances.front().color), glm::vec3(1.0f), 1e-6);
    CHECK(glm::vec3(g.instances.back().color) != glm::vec3(1.0f));
    // A hue rotation preserves the luminance (the grey axis is the rotation axis).
    const glm::vec3 shifted = glm::vec3(g.instances.back().color) * g.material.baseColor;
    CHECK_THAT(d(shifted.r + shifted.g + shifted.b),
               WithinAbs(d(g.material.baseColor.r + g.material.baseColor.g + g.material.baseColor.b), 1e-3));
    g.materialVariation.valueRandom = 0.5f;
    REQUIRE(g.rebuild());
    bool differs = false;
    for (const auto& r : g.instances) {
        CHECK(r.color.r >= 0.0f);
        differs = differs || r.color.r != g.instances.front().color.r;
    }
    CHECK(differs);
}

TEST_CASE("Bounds cover instance origins plus the source extent", "[scene][procedural]") {
    ProceduralGeometry g;
    g.source.kind = PrimitiveKind::Sphere;
    g.source.radius = 1.0f;
    g.distribution.kind = DistributionKind::Radial;
    g.distribution.count = 4;
    g.distribution.radius = 6.0f;
    REQUIRE(g.rebuild());
    CHECK(g.boundsMax.x >= 7.0f);
    CHECK(g.boundsMin.x <= -7.0f);
    CHECK(g.boundsMax.z >= 7.0f);
    CHECK(g.boundsMax.y >= 1.0f);
    CHECK(g.boundsMax.x < 9.0f); // conservative but not absurd
    g.distribution.kind = DistributionKind::Single;
    g.distributionTransform.position = {0.0f, 5.0f, 0.0f};
    REQUIRE(g.rebuild());
    CHECK(g.boundsMin.y <= 4.0f);
    CHECK(g.boundsMax.y >= 6.0f);
    CHECK(g.boundsMin.y > 2.0f);
}

TEST_CASE("Non-uniform distribution scale composes through matrices", "[scene][procedural]") {
    ProceduralGeometry g;
    g.distribution.kind = DistributionKind::Linear;
    g.distribution.count = 3;
    g.distributionTransform.scale = {2.0f, 1.0f, 0.5f};
    g.distributionTransform.rotation = glm::angleAxis(0.3f, glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f)));
    REQUIRE(g.rebuild());
    for (std::uint32_t i = 0; i < 3; ++i) {
        const InstanceRecord& rec = g.instances[i];
        Transform t;
        t.position = glm::vec3(rec.position);
        t.rotation = glm::quat(rec.rotation.w, rec.rotation.x, rec.rotation.y, rec.rotation.z);
        t.scale = glm::vec3(rec.scale);
        const glm::mat4 expected = g.distributionTransform.matrix() * g.distribution.placement(static_cast<int>(i)).matrix();
        // Rotation-free placements: the decomposition is exact.
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                CHECK_THAT(d(t.matrix()[c][r]), WithinAbs(d(expected[c][r]), 1e-5));
            }
        }
    }
}

// ---- validation and JSON ---------------------------------------------------------------------

TEST_CASE("ProceduralGeometry::validate", "[scene][procedural]") {
    ProceduralGeometry g;
    CHECK(g.validate().has_value());
    g.deformers.assign(static_cast<std::size_t>(kMaxDeformers) + 1, Deformer{});
    CHECK_FALSE(g.validate().has_value());
    g.deformers.resize(static_cast<std::size_t>(kMaxDeformers));
    CHECK(g.validate().has_value());
    g.deformers[2].axis = glm::vec3(0.0f);
    CHECK_FALSE(g.validate().has_value());
    g.deformers.clear();
    g.name.clear();
    CHECK_FALSE(g.validate().has_value());
    g.name = "x";
    g.distribution.count = 0;
    CHECK_FALSE(g.validate().has_value());
    g.distribution.count = 1;
    g.material.roughness = 2.0f;
    CHECK_FALSE(g.validate().has_value());
}

TEST_CASE("ProceduralGeometry JSON round trip covers every field", "[scene][procedural]") {
    ProceduralGeometry g;
    g.name = "columns";
    g.visible = false;
    g.source.kind = PrimitiveKind::Torus;
    g.source.size = {1.5f, 2.5f, 3.5f};
    g.source.subdivisions = 3;
    g.source.radius = 0.7f;
    g.source.height = 3.0f;
    g.source.radialSegments = 12;
    g.source.heightSegments = 4;
    g.source.caps = false;
    g.source.segments = 20;
    g.source.rings = 10;
    g.source.majorRadius = 2.0f;
    g.source.minorRadius = 0.4f;
    g.source.majorSegments = 30;
    g.source.minorSegments = 9;
    g.sourceTransform.position = {0.1f, 0.2f, 0.3f};
    g.sourceTransform.rotation = glm::angleAxis(0.4f, glm::normalize(glm::vec3(0.2f, 1.0f, 0.3f)));
    g.sourceTransform.scale = {1.0f, 2.0f, 3.0f};
    g.distribution.kind = DistributionKind::Spiral;
    g.distribution.count = 17;
    g.distribution.start = {1.0f, 2.0f, 3.0f};
    g.distribution.end = {4.0f, 5.0f, 6.0f};
    g.distribution.orientAlong = true;
    g.distribution.spacing = 0.75f;
    g.distribution.gridCount = {2, 3, 4};
    g.distribution.gridSpacing = {1.5f, 2.5f, 3.5f};
    g.distribution.radius = 4.5f;
    g.distribution.startAngle = 0.5f;
    g.distribution.endAngle = 3.0f;
    g.distribution.plane = DistributionPlane::YZ;
    g.distribution.center = {7.0f, 8.0f, 9.0f};
    g.distribution.orientation = OrientationMode::Tangent;
    g.distribution.radiusGrowth = 2.5f;
    g.distribution.turns = 1.5f;
    g.distribution.spiralHeight = 6.0f;
    g.distribution.spiralAngle = 0.25f;
    g.distributionTransform.position = {-1.0f, -2.0f, -3.0f};
    g.distributionTransform.rotation = glm::angleAxis(-0.7f, glm::normalize(glm::vec3(1.0f, 0.1f, 0.0f)));
    g.distributionTransform.scale = {2.0f, 2.0f, 2.0f};
    g.variation.seed = 777;
    g.variation.randomPosition = {0.1f, 0.2f, 0.3f};
    g.variation.randomRotation = {0.4f, 0.5f, 0.6f};
    g.variation.randomScale = {0.1f, 0.15f, 0.2f};
    g.variation.randomUniformScale = 0.35f;
    Deformer bend;
    bend.kind = DeformerKind::Bend;
    bend.enabled = false;
    bend.amount = 0.8f;
    bend.space = DeformSpace::World;
    bend.speed = 0.3f;
    bend.phase = 0.9f;
    bend.axis = {1.0f, 0.0f, 0.0f};
    bend.center = {0.0f, 1.0f, 0.0f};
    bend.falloff = 2.0f;
    bend.frequency = 3.0f;
    bend.displacementAxis = {0.0f, 0.0f, 1.0f};
    bend.scale = 0.5f;
    bend.seed = 5;
    bend.axisMask = {1.0f, 0.0f, 1.0f};
    bend.pattern = 0;
    Deformer noise;
    noise.kind = DeformerKind::Noise;
    noise.amount = 0.2f;
    g.deformers = {bend, noise};
    g.material.baseColor = {0.1f, 0.2f, 0.3f};
    g.material.opacity = 0.9f;
    g.material.emissiveColor = {0.4f, 0.5f, 0.6f};
    g.material.emissiveIntensity = 2.5f;
    g.material.roughness = 0.6f;
    g.material.metallic = 0.7f;
    g.material.doubleSided = true;
    g.material.unlit = true;
    g.materialVariation = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};

    const nlohmann::json j = g.toJson();
    CHECK(j.at("source").at("kind") == "torus");
    CHECK(j.at("distribution").at("kind") == "spiral");
    CHECK(j.at("distribution").at("plane") == "yz");
    CHECK(j.at("distribution").at("orientation") == "tangent");
    CHECK(j.at("deformers").size() == 2);
    CHECK(j.at("deformers").at(0).at("kind") == "bend");
    CHECK(j.at("deformers").at(0).at("space") == "world");

    auto back = ProceduralGeometry::fromJson(j);
    REQUIRE(back.has_value());
    const ProceduralGeometry& b = *back;
    CHECK(b.name == "columns");
    CHECK_FALSE(b.visible);
    CHECK(b.source.kind == PrimitiveKind::Torus);
    CHECK(b.source.size == g.source.size);
    CHECK(b.source.subdivisions == 3);
    CHECK(b.source.radius == 0.7f);
    CHECK(b.source.height == 3.0f);
    CHECK(b.source.radialSegments == 12);
    CHECK(b.source.heightSegments == 4);
    CHECK_FALSE(b.source.caps);
    CHECK(b.source.segments == 20);
    CHECK(b.source.rings == 10);
    CHECK(b.source.majorRadius == 2.0f);
    CHECK(b.source.minorRadius == 0.4f);
    CHECK(b.source.majorSegments == 30);
    CHECK(b.source.minorSegments == 9);
    checkVec(b.sourceTransform.position, g.sourceTransform.position, 1e-6);
    checkVec(b.sourceTransform.scale, g.sourceTransform.scale, 1e-6);
    checkVec(b.sourceTransform.rotation * glm::vec3(1.0f, 2.0f, 3.0f), g.sourceTransform.rotation * glm::vec3(1.0f, 2.0f, 3.0f),
             1e-4);
    CHECK(b.distribution.kind == DistributionKind::Spiral);
    CHECK(b.distribution.count == 17);
    CHECK(b.distribution.start == g.distribution.start);
    CHECK(b.distribution.end == g.distribution.end);
    CHECK(b.distribution.orientAlong);
    CHECK(b.distribution.spacing == 0.75f);
    CHECK(b.distribution.gridCount == g.distribution.gridCount);
    CHECK(b.distribution.gridSpacing == g.distribution.gridSpacing);
    CHECK(b.distribution.radius == 4.5f);
    CHECK(b.distribution.startAngle == 0.5f);
    CHECK(b.distribution.endAngle == 3.0f);
    CHECK(b.distribution.plane == DistributionPlane::YZ);
    CHECK(b.distribution.center == g.distribution.center);
    CHECK(b.distribution.orientation == OrientationMode::Tangent);
    CHECK(b.distribution.radiusGrowth == 2.5f);
    CHECK(b.distribution.turns == 1.5f);
    CHECK(b.distribution.spiralHeight == 6.0f);
    CHECK(b.distribution.spiralAngle == 0.25f);
    checkVec(b.distributionTransform.position, g.distributionTransform.position, 1e-6);
    checkVec(b.distributionTransform.scale, g.distributionTransform.scale, 1e-6);
    checkVec(b.distributionTransform.rotation * glm::vec3(1.0f, 2.0f, 3.0f),
             g.distributionTransform.rotation * glm::vec3(1.0f, 2.0f, 3.0f), 1e-4);
    CHECK(b.variation.seed == 777);
    CHECK(b.variation.randomPosition == g.variation.randomPosition);
    CHECK(b.variation.randomRotation == g.variation.randomRotation);
    CHECK(b.variation.randomScale == g.variation.randomScale);
    CHECK(b.variation.randomUniformScale == 0.35f);
    REQUIRE(b.deformers.size() == 2);
    const Deformer& bd = b.deformers[0];
    CHECK(bd.kind == DeformerKind::Bend);
    CHECK_FALSE(bd.enabled);
    CHECK(bd.amount == 0.8f);
    CHECK(bd.space == DeformSpace::World);
    CHECK(bd.speed == 0.3f);
    CHECK(bd.phase == 0.9f);
    CHECK(bd.axis == bend.axis);
    CHECK(bd.center == bend.center);
    CHECK(bd.falloff == 2.0f);
    CHECK(bd.frequency == 3.0f);
    CHECK(bd.displacementAxis == bend.displacementAxis);
    CHECK(bd.scale == 0.5f);
    CHECK(bd.seed == 5);
    CHECK(bd.axisMask == bend.axisMask);
    CHECK(bd.pattern == 0);
    CHECK(b.deformers[1].kind == DeformerKind::Noise);
    CHECK(b.deformers[1].amount == 0.2f);
    CHECK(b.material.baseColor == g.material.baseColor);
    CHECK(b.material.opacity == 0.9f);
    CHECK(b.material.emissiveColor == g.material.emissiveColor);
    CHECK(b.material.emissiveIntensity == 2.5f);
    CHECK(b.material.roughness == 0.6f);
    CHECK(b.material.metallic == 0.7f);
    CHECK(b.material.doubleSided);
    CHECK(b.material.unlit);
    CHECK(b.materialVariation.hueShift == 0.1f);
    CHECK(b.materialVariation.hueGradient == 0.2f);
    CHECK(b.materialVariation.valueRandom == 0.3f);
    CHECK(b.materialVariation.emissiveRandom == 0.4f);
    CHECK(b.materialVariation.emissiveGradient == 0.5f);
    CHECK(b.source.structuralHash() == g.source.structuralHash());
    CHECK(b.distribution.structuralHash() == g.distribution.structuralHash());
    CHECK(b.variation.structuralHash() == g.variation.structuralHash());

    // Errors: unknown names and malformed values.
    nlohmann::json bad = j;
    bad["source"]["kind"] = "cone";
    CHECK_FALSE(ProceduralGeometry::fromJson(bad).has_value());
    bad = j;
    bad["deformers"][0]["axis"] = "up";
    CHECK_FALSE(ProceduralGeometry::fromJson(bad).has_value());
    bad = j;
    bad["distribution"]["count"] = 0;
    CHECK_FALSE(ProceduralGeometry::fromJson(bad).has_value());
    // Defaults fill in missing fields.
    auto minimal = ProceduralGeometry::fromJson(nlohmann::json::object({{"name", "m"}}));
    REQUIRE(minimal.has_value());
    CHECK(minimal->distribution.kind == DistributionKind::Radial);
    CHECK(minimal->deformers.empty());
}
