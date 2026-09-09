#include "spatial/spatial_ops.hpp"

#include "core/noise.hpp"
#include "scene/procedural.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <vector>

using namespace avgen;
using namespace avgen::spatial;
using Catch::Matchers::WithinAbs;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

void checkVec(const glm::vec3& v, const glm::vec3& expected, double tol = 1e-5) {
    CHECK_THAT(d(v.x), WithinAbs(d(expected.x), tol));
    CHECK_THAT(d(v.y), WithinAbs(d(expected.y), tol));
    CHECK_THAT(d(v.z), WithinAbs(d(expected.z), tol));
}

// n points on the x axis (position (i, 0, 0)), ids = row, seed column = `seed`, density = u.
PointCloud line(std::size_t n, std::int32_t seed = 11) {
    PointCloud cloud(n);
    for (std::size_t i = 0; i < n; ++i) {
        cloud.positions()[i] = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);
        cloud.seeds()[i] = seed;
        cloud.densities()[i] = cloud.indices()[i];
    }
    return cloud;
}

std::vector<std::int32_t> idsOf(const PointCloud& c) {
    return std::vector<std::int32_t>(c.ids().begin(), c.ids().end());
}

glm::quat quatOf(const PointCloud& c, std::size_t i) {
    const glm::vec4 r = c.rotations()[i];
    return glm::quat(r.w, r.x, r.y, r.z);
}

} // namespace

TEST_CASE("Point op names round trip", "[spatial][ops]") {
    for (const auto kind :
         {PointOpKind::Transform, PointOpKind::Translate, PointOpKind::Rotate, PointOpKind::Scale, PointOpKind::Noise,
          PointOpKind::Randomize, PointOpKind::Scatter, PointOpKind::FilterDensity, PointOpKind::FilterAttribute,
          PointOpKind::FilterDistance, PointOpKind::FilterProbability, PointOpKind::FilterBounds, PointOpKind::Delete,
          PointOpKind::Sort, PointOpKind::Merge, PointOpKind::Duplicate, PointOpKind::Sample, PointOpKind::Attribute}) {
        CHECK(pointOpKindFromName(pointOpKindName(kind)) == kind);
    }
    CHECK(pointOpKindName(PointOpKind::FilterDensity) == std::string("filterDensity"));
    CHECK_FALSE(pointOpKindFromName("FilterDensity").has_value());
}

TEST_CASE("Transform, translate, rotate and scale ops", "[spatial][ops]") {
    PointCloud cloud = line(2);
    PointOp t;
    t.kind = PointOpKind::Transform;
    t.position = glm::vec3(1.0f, 0.0f, 0.0f);
    t.rotationDegrees = glm::vec3(0.0f, 90.0f, 0.0f);
    t.scale = glm::vec3(2.0f);
    REQUIRE(applyPointOp(cloud, t).has_value());
    checkVec(cloud.positions()[0], {1.0f, 0.0f, 0.0f});
    checkVec(cloud.positions()[1], {1.0f, 0.0f, -2.0f});
    checkVec(cloud.scales()[1], glm::vec3(2.0f));
    checkVec(quatOf(cloud, 1) * glm::vec3(1.0f, 0.0f, 0.0f), {0.0f, 0.0f, -1.0f});

    cloud = line(2);
    PointOp tr;
    tr.kind = PointOpKind::Translate;
    tr.offset = glm::vec3(0.0f, 2.0f, 0.0f);
    tr.amount = 0.5f;
    REQUIRE(applyPointOp(cloud, tr).has_value());
    checkVec(cloud.positions()[1], {1.0f, 1.0f, 0.0f});
    tr.enabled = false;
    REQUIRE(applyPointOp(cloud, tr).has_value());
    checkVec(cloud.positions()[1], {1.0f, 1.0f, 0.0f});

    cloud = line(3);
    PointOp rot;
    rot.kind = PointOpKind::Rotate;
    rot.axis = glm::vec3(0.0f, 1.0f, 0.0f);
    rot.angle = glm::half_pi<float>();
    rot.pivot = glm::vec3(1.0f, 0.0f, 0.0f);
    REQUIRE(applyPointOp(cloud, rot).has_value());
    checkVec(cloud.positions()[0], {1.0f, 0.0f, 1.0f});
    checkVec(cloud.positions()[1], {1.0f, 0.0f, 0.0f});
    checkVec(cloud.positions()[2], {1.0f, 0.0f, -1.0f});
    checkVec(quatOf(cloud, 2) * glm::vec3(1.0f, 0.0f, 0.0f), {0.0f, 0.0f, -1.0f});
    // Rotation premultiplies: two quarter turns make a half turn.
    REQUIRE(applyPointOp(cloud, rot).has_value());
    checkVec(quatOf(cloud, 2) * glm::vec3(1.0f, 0.0f, 0.0f), {-1.0f, 0.0f, 0.0f});
    rot.amount = 0.0f;
    const glm::vec3 before = cloud.positions()[0];
    REQUIRE(applyPointOp(cloud, rot).has_value());
    checkVec(cloud.positions()[0], before);

    cloud = line(3);
    PointOp sc;
    sc.kind = PointOpKind::Scale;
    sc.pivot = glm::vec3(1.0f, 0.0f, 0.0f);
    sc.factor = glm::vec3(3.0f, 1.0f, 1.0f);
    REQUIRE(applyPointOp(cloud, sc).has_value());
    checkVec(cloud.positions()[0], {-2.0f, 0.0f, 0.0f});
    checkVec(cloud.positions()[2], {4.0f, 0.0f, 0.0f});
    checkVec(cloud.scales()[2], {3.0f, 1.0f, 1.0f});
    sc.scaleInstances = false;
    REQUIRE(applyPointOp(cloud, sc).has_value());
    checkVec(cloud.scales()[2], {3.0f, 1.0f, 1.0f});
    checkVec(cloud.positions()[2], {10.0f, 0.0f, 0.0f});
}

TEST_CASE("Noise op is deterministic, masked and amount-scaled", "[spatial][ops]") {
    PointCloud cloud = line(4);
    PointOp op;
    op.kind = PointOpKind::Noise;
    op.frequency = 0.7f;
    op.offset = glm::vec3(1.0f, 2.0f, 3.0f);
    op.seed = 3;
    op.amount = 0.5f;
    op.axisMask = glm::vec3(0.0f, 1.0f, 1.0f);
    REQUIRE(applyPointOp(cloud, op).has_value());
    for (std::size_t i = 0; i < 4; ++i) {
        const glm::vec3 p(static_cast<float>(i), 0.0f, 0.0f);
        const glm::vec3 expected = p + noise::fbm3Vec(p * 0.7f + op.offset, 3) * glm::vec3(0.0f, 1.0f, 1.0f) * 0.5f;
        checkVec(cloud.positions()[i], expected, 1e-6);
        CHECK(cloud.positions()[i].x == p.x);
    }
    PointCloud again = line(4);
    REQUIRE(applyPointOp(again, op).has_value());
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(again.positions()[i] == cloud.positions()[i]);
    }
    PointCloud zero = line(4);
    op.amount = 0.0f;
    REQUIRE(applyPointOp(zero, op).has_value());
    CHECK(zero.positions()[3] == glm::vec3(3.0f, 0.0f, 0.0f));
}

TEST_CASE("Randomize op reproduces scene::variationTransform", "[spatial][ops]") {
    scene::Variation v;
    v.seed = 4242;
    v.randomPosition = {0.5f, 1.0f, 2.0f};
    v.randomRotation = {0.3f, 0.6f, 0.9f};
    v.randomScale = {0.1f, 0.2f, 0.3f};
    v.randomUniformScale = 0.25f;

    PointCloud cloud = line(16, static_cast<std::int32_t>(v.seed));
    PointOp op;
    op.kind = PointOpKind::Randomize;
    op.seed = 0; // use the point's seed column as-is
    op.randomPosition = v.randomPosition;
    op.randomRotation = v.randomRotation;
    op.randomScale = v.randomScale;
    op.randomUniformScale = v.randomUniformScale;
    REQUIRE(applyPointOp(cloud, op).has_value());
    for (std::size_t i = 0; i < 16; ++i) {
        const scene::Transform t = scene::variationTransform(v, static_cast<std::uint32_t>(i));
        CHECK(cloud.positions()[i] == glm::vec3(static_cast<float>(i), 0.0f, 0.0f) + t.position);
        CHECK(cloud.scales()[i] == t.scale);
        CHECK(quatOf(cloud, i).x == t.rotation.x);
        CHECK(quatOf(cloud, i).y == t.rotation.y);
        CHECK(quatOf(cloud, i).z == t.rotation.z);
        CHECK(quatOf(cloud, i).w == t.rotation.w);
    }
    // amount scales every range; op.seed decorrelates from the point seed.
    PointCloud half = line(16, static_cast<std::int32_t>(v.seed));
    op.amount = 0.5f;
    REQUIRE(applyPointOp(half, op).has_value());
    scene::Variation hv = v;
    hv.randomPosition *= 0.5f;
    hv.randomRotation *= 0.5f;
    hv.randomScale *= 0.5f;
    hv.randomUniformScale *= 0.5f;
    for (std::size_t i = 0; i < 16; ++i) {
        const scene::Transform t = scene::variationTransform(hv, static_cast<std::uint32_t>(i));
        CHECK(half.positions()[i] == glm::vec3(static_cast<float>(i), 0.0f, 0.0f) + t.position);
        CHECK(half.scales()[i] == t.scale);
    }
    PointCloud other = line(16, static_cast<std::int32_t>(v.seed));
    op.amount = 1.0f;
    op.seed = 1;
    REQUIRE(applyPointOp(other, op).has_value());
    CHECK(other.positions()[3] != cloud.positions()[3]);
}

TEST_CASE("Scatter op jitters within range deterministically", "[spatial][ops]") {
    PointCloud cloud = line(50);
    PointOp op;
    op.kind = PointOpKind::Scatter;
    op.range = glm::vec3(0.5f, 0.0f, 2.0f);
    op.amount = 0.5f;
    op.seed = 9;
    REQUIRE(applyPointOp(cloud, op).has_value());
    bool moved = false;
    for (std::size_t i = 0; i < 50; ++i) {
        const glm::vec3 delta = cloud.positions()[i] - glm::vec3(static_cast<float>(i), 0.0f, 0.0f);
        CHECK(std::abs(delta.x) <= 0.25f);
        CHECK(delta.y == 0.0f);
        CHECK(std::abs(delta.z) <= 1.0f);
        const auto seed = static_cast<std::uint32_t>(11) + 9u * 0x9E3779B9u;
        CHECK(delta.z == (noise::hashIndex(seed, static_cast<std::uint32_t>(i), 42) * 2.0f - 1.0f) * 2.0f * 0.5f);
        moved = moved || delta.x != 0.0f;
    }
    CHECK(moved);
    PointCloud other = line(50);
    op.seed = 10;
    REQUIRE(applyPointOp(other, op).has_value());
    CHECK(other.positions()[7] != cloud.positions()[7]);
}

TEST_CASE("Filter ops keep ids and honour invert", "[spatial][ops]") {
    // FilterDensity: density = u = i / 9.
    PointCloud cloud = line(10);
    PointOp den;
    den.kind = PointOpKind::FilterDensity;
    den.threshold = 0.5f;
    REQUIRE(applyPointOp(cloud, den).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{5, 6, 7, 8, 9});
    CHECK(cloud.indices()[0] == 5.0f / 9.0f); // filters keep the index column
    cloud = line(10);
    den.invert = true;
    REQUIRE(applyPointOp(cloud, den).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{0, 1, 2, 3, 4});
    // Probabilistic: keep when density >= hash channel 44, deterministic.
    cloud = line(64);
    PointCloud again = line(64);
    den.invert = false;
    den.probabilistic = true;
    REQUIRE(applyPointOp(cloud, den).has_value());
    REQUIRE(applyPointOp(again, den).has_value());
    CHECK(cloud.count() > 8);
    CHECK(cloud.count() < 56);
    CHECK(idsOf(cloud) == idsOf(again));
    for (std::size_t i = 0; i < cloud.count(); ++i) {
        const auto id = static_cast<std::uint32_t>(cloud.ids()[i]);
        CHECK(cloud.densities()[i] >= noise::hashIndex(11u + 0x9E3779B9u, id, 44));
    }

    // FilterAttribute.
    cloud = line(6);
    REQUIRE(cloud.attributes.add("v", AttributeType::Vec2).has_value());
    for (std::size_t i = 0; i < 6; ++i) {
        (*cloud.attributes.view<glm::vec2>("v"))[i] = glm::vec2(static_cast<float>(i), 100.0f);
    }
    PointOp fa;
    fa.kind = PointOpKind::FilterAttribute;
    fa.attribute = "v";
    fa.value = 2.0f;
    fa.compare = 4; // >
    REQUIRE(applyPointOp(cloud, fa).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{3, 4, 5});
    fa.attribute = "missing";
    CHECK_FALSE(applyPointOp(cloud, fa).has_value());

    // FilterDistance around a pivot.
    cloud = line(10);
    PointOp fd;
    fd.kind = PointOpKind::FilterDistance;
    fd.pivot = glm::vec3(4.0f, 0.0f, 0.0f);
    fd.minDistance = 1.0f;
    fd.maxDistance = 2.0f;
    REQUIRE(applyPointOp(cloud, fd).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{2, 3, 5, 6});
    cloud = line(10);
    fd.invert = true;
    REQUIRE(applyPointOp(cloud, fd).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{0, 1, 4, 7, 8, 9});

    // FilterProbability: 0 keeps nothing, 1 keeps everything, invert is the complement.
    PointOp fp;
    fp.kind = PointOpKind::FilterProbability;
    fp.probability = 0.0f;
    cloud = line(20);
    REQUIRE(applyPointOp(cloud, fp).has_value());
    CHECK(cloud.count() == 0);
    fp.probability = 1.0f;
    cloud = line(20);
    REQUIRE(applyPointOp(cloud, fp).has_value());
    CHECK(cloud.count() == 20);
    fp.probability = 0.5f;
    cloud = line(200);
    PointCloud complement = line(200);
    REQUIRE(applyPointOp(cloud, fp).has_value());
    fp.invert = true;
    REQUIRE(applyPointOp(complement, fp).has_value());
    CHECK(cloud.count() + complement.count() == 200);
    CHECK(cloud.count() > 60);
    CHECK(cloud.count() < 140);
    for (std::size_t i = 0; i < cloud.count(); ++i) {
        CHECK(noise::hashIndex(11u + 0x9E3779B9u, static_cast<std::uint32_t>(cloud.ids()[i]), 43) < 0.5f);
    }

    // FilterBounds.
    cloud = line(10);
    PointOp fb;
    fb.kind = PointOpKind::FilterBounds;
    fb.boundsMin = glm::vec3(2.0f, -1.0f, -1.0f);
    fb.boundsMax = glm::vec3(4.0f, 1.0f, 1.0f);
    REQUIRE(applyPointOp(cloud, fb).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{2, 3, 4});
    cloud = line(10);
    fb.invert = true;
    REQUIRE(applyPointOp(cloud, fb).has_value());
    CHECK(cloud.count() == 7);

    // Delete by attribute (default "delete"); missing attribute is a no-op.
    cloud = line(4);
    PointOp del;
    del.kind = PointOpKind::Delete;
    REQUIRE(applyPointOp(cloud, del).has_value());
    CHECK(cloud.count() == 4);
    REQUIRE(cloud.attributes.add("delete", AttributeType::Bool).has_value());
    (*cloud.attributes.view<std::uint8_t>("delete"))[1] = 1;
    (*cloud.attributes.view<std::uint8_t>("delete"))[3] = 1;
    REQUIRE(applyPointOp(cloud, del).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{0, 2});
    del.attribute = "other";
    REQUIRE(cloud.attributes.add("other", AttributeType::Float).has_value());
    (*cloud.attributes.view<float>("other"))[0] = 2.0f;
    REQUIRE(applyPointOp(cloud, del).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{2});
}

TEST_CASE("Sort op is stable and renumbers indices", "[spatial][ops]") {
    PointCloud cloud = line(6);
    REQUIRE(cloud.attributes.add("key", AttributeType::Vec2).has_value());
    const float keys[6] = {2.0f, 1.0f, 2.0f, 0.0f, 1.0f, 0.0f};
    for (std::size_t i = 0; i < 6; ++i) {
        (*cloud.attributes.view<glm::vec2>("key"))[i] = glm::vec2(99.0f, keys[i]);
    }
    PointOp op;
    op.kind = PointOpKind::Sort;
    op.attribute = "key";
    op.component = 1;
    REQUIRE(applyPointOp(cloud, op).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{3, 5, 1, 4, 0, 2});
    CHECK(cloud.indices()[0] == 0.0f);
    CHECK(cloud.indices()[5] == 1.0f);
    CHECK_THAT(d(cloud.indices()[1]), WithinAbs(0.2, 1e-6));
    CHECK(cloud.positions()[0] == glm::vec3(3.0f, 0.0f, 0.0f)); // every column moves together
    op.descending = true;
    REQUIRE(applyPointOp(cloud, op).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{0, 2, 1, 4, 3, 5});
    op.attribute = "nope";
    CHECK_FALSE(applyPointOp(cloud, op).has_value());
}

TEST_CASE("Duplicate op appends transformed copies with fresh ids", "[spatial][ops]") {
    PointCloud cloud = line(3);
    cloud.ids()[2] = 10; // ids continue after the max id
    PointOp op;
    op.kind = PointOpKind::Duplicate;
    op.copies = 2;
    op.offset = glm::vec3(0.0f, 1.0f, 0.0f);
    op.axis = glm::vec3(0.0f, 1.0f, 0.0f);
    op.angle = glm::half_pi<float>();
    op.pivot = glm::vec3(0.0f);
    op.factor = glm::vec3(2.0f);
    REQUIRE(applyPointOp(cloud, op).has_value());
    REQUIRE(cloud.count() == 9);
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{0, 1, 10, 11, 12, 13, 14, 15, 16});
    // Copy 1 of point (1,0,0): scale 2 -> (2,0,0), rotate 90 about Y -> (0,0,-2), + offset.
    checkVec(cloud.positions()[4], {0.0f, 1.0f, -2.0f});
    checkVec(cloud.scales()[4], glm::vec3(2.0f));
    checkVec(quatOf(cloud, 4) * glm::vec3(1.0f, 0.0f, 0.0f), {0.0f, 0.0f, -1.0f});
    // Copy 2: factor^2 = 4, angle 180, offset x2.
    checkVec(cloud.positions()[7], {-4.0f, 2.0f, 0.0f});
    checkVec(cloud.scales()[7], glm::vec3(4.0f));
    checkVec(quatOf(cloud, 7) * glm::vec3(1.0f, 0.0f, 0.0f), {-1.0f, 0.0f, 0.0f});
    CHECK(cloud.indices()[0] == 0.0f);
    CHECK(cloud.indices()[8] == 1.0f);
    CHECK(cloud.seeds()[4] == cloud.seeds()[1]); // columns copied
    op.copies = 0;
    REQUIRE(applyPointOp(cloud, op).has_value());
    CHECK(cloud.count() == 9);
}

TEST_CASE("Sample op and sampleCloud", "[spatial][ops]") {
    PointCloud cloud = line(10);
    PointOp op;
    op.kind = PointOpKind::Sample;
    op.stride = 3;
    op.start = 1;
    REQUIRE(applyPointOp(cloud, op).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{1, 4, 7});
    cloud = line(10);
    op.count = 3;
    REQUIRE(applyPointOp(cloud, op).has_value());
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{0, 4, 9});
    cloud = line(10);
    sampleCloud(cloud, 4);
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{0, 3, 6, 9});
    sampleCloud(cloud, 10);
    CHECK(cloud.count() == 4);
    sampleCloud(cloud, 1);
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{0});
    sampleCloud(cloud, 0);
    CHECK(cloud.count() == 0);
}

TEST_CASE("Attribute op through a point op and mergeClouds", "[spatial][ops]") {
    PointCloud cloud = line(4);
    PointOp op;
    op.kind = PointOpKind::Attribute;
    op.attributeOp.kind = AttributeOpKind::Set;
    op.attributeOp.target = "mass";
    op.attributeOp.value = glm::vec4(3.0f);
    REQUIRE(applyPointOp(cloud, op).has_value());
    CHECK((*cloud.attributes.view<float>("mass"))[2] == 3.0f);
    op.attributeOp.target.clear();
    CHECK_FALSE(applyPointOp(cloud, op).has_value());

    PointCloud b = line(2);
    b.positions()[1] = glm::vec3(0.0f, 5.0f, 0.0f);
    b.ids()[0] = 100;
    REQUIRE(b.attributes.add("weight", AttributeType::Float).has_value());
    (*b.attributes.view<float>("weight"))[1] = 0.5f;
    mergeClouds(cloud, b);
    REQUIRE(cloud.count() == 6);
    CHECK(idsOf(cloud) == std::vector<std::int32_t>{0, 1, 2, 3, 4, 5});
    CHECK(cloud.positions()[5] == glm::vec3(0.0f, 5.0f, 0.0f));
    CHECK((*cloud.attributes.view<float>("mass"))[5] == 0.0f);
    CHECK((*cloud.attributes.view<float>("weight"))[5] == 0.5f);
    CHECK((*cloud.attributes.view<float>("weight"))[0] == 0.0f);
    CHECK(cloud.indices()[5] == 1.0f);

    // Merge kind is a no-op; applyPointOps reports the failing op.
    PointOp merge;
    merge.kind = PointOpKind::Merge;
    PointOp bad;
    bad.kind = PointOpKind::Sort;
    bad.attribute = "nope";
    const std::vector<PointOp> ops{merge, bad};
    auto result = applyPointOps(cloud, ops);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("op 2") != std::string::npos);
    CHECK(cloud.count() == 6);
}

TEST_CASE("PointOp JSON round trip and hash sensitivity", "[spatial][ops]") {
    PointOp op;
    op.kind = PointOpKind::Duplicate;
    op.enabled = false;
    op.amount = 0.5f;
    op.offset = {1.0f, 2.0f, 3.0f};
    op.axis = {0.0f, 0.0f, 1.0f};
    op.angle = 0.25f;
    op.pivot = {4.0f, 5.0f, 6.0f};
    op.factor = {2.0f, 3.0f, 4.0f};
    op.scaleInstances = false;
    op.position = {7.0f, 8.0f, 9.0f};
    op.rotationDegrees = {10.0f, 20.0f, 30.0f};
    op.scale = {0.5f, 0.5f, 0.5f};
    op.frequency = 2.5f;
    op.axisMask = {1.0f, 0.0f, 1.0f};
    op.seed = 77;
    op.randomPosition = {0.1f, 0.2f, 0.3f};
    op.randomRotation = {0.4f, 0.5f, 0.6f};
    op.randomScale = {0.7f, 0.8f, 0.9f};
    op.randomUniformScale = 0.15f;
    op.range = {1.5f, 2.5f, 3.5f};
    op.threshold = 0.75f;
    op.probabilistic = true;
    op.attribute = "mass";
    op.value = 1.25f;
    op.compare = 5;
    op.minDistance = 0.5f;
    op.maxDistance = 20.0f;
    op.probability = 0.3f;
    op.boundsMin = {-2.0f, -3.0f, -4.0f};
    op.boundsMax = {2.0f, 3.0f, 4.0f};
    op.invert = true;
    op.component = 2;
    op.descending = true;
    op.copies = 3;
    op.stride = 4;
    op.count = 5;
    op.start = 6;
    op.attributeOp.kind = AttributeOpKind::Remap;
    op.attributeOp.target = "t";

    const nlohmann::json j = pointOpToJson(op);
    CHECK(j.at("kind") == "duplicate");
    CHECK_FALSE(j.contains("attributeOp")); // only written for Attribute ops
    auto back = pointOpFromJson(j);
    REQUIRE(back.has_value());
    const PointOp& b = *back;
    CHECK(b.kind == PointOpKind::Duplicate);
    CHECK_FALSE(b.enabled);
    CHECK(b.amount == 0.5f);
    CHECK(b.offset == op.offset);
    CHECK(b.axis == op.axis);
    CHECK(b.angle == 0.25f);
    CHECK(b.pivot == op.pivot);
    CHECK(b.factor == op.factor);
    CHECK_FALSE(b.scaleInstances);
    CHECK(b.position == op.position);
    CHECK(b.rotationDegrees == op.rotationDegrees);
    CHECK(b.scale == op.scale);
    CHECK(b.frequency == 2.5f);
    CHECK(b.axisMask == op.axisMask);
    CHECK(b.seed == 77);
    CHECK(b.randomPosition == op.randomPosition);
    CHECK(b.randomRotation == op.randomRotation);
    CHECK(b.randomScale == op.randomScale);
    CHECK(b.randomUniformScale == 0.15f);
    CHECK(b.range == op.range);
    CHECK(b.threshold == 0.75f);
    CHECK(b.probabilistic);
    CHECK(b.attribute == "mass");
    CHECK(b.value == 1.25f);
    CHECK(b.compare == 5);
    CHECK(b.minDistance == 0.5f);
    CHECK(b.maxDistance == 20.0f);
    CHECK(b.probability == 0.3f);
    CHECK(b.boundsMin == op.boundsMin);
    CHECK(b.boundsMax == op.boundsMax);
    CHECK(b.invert);
    CHECK(b.component == 2);
    CHECK(b.descending);
    CHECK(b.copies == 3);
    CHECK(b.stride == 4);
    CHECK(b.count == 5);
    CHECK(b.start == 6);

    op.kind = PointOpKind::Attribute;
    const nlohmann::json aj = pointOpToJson(op);
    REQUIRE(aj.contains("attributeOp"));
    auto aback = pointOpFromJson(aj);
    REQUIRE(aback.has_value());
    CHECK(aback->attributeOp.kind == AttributeOpKind::Remap);
    CHECK(aback->attributeOp.target == "t");

    // Defaults are omitted; kind is required.
    const nlohmann::json plain = pointOpToJson(PointOp{});
    CHECK(plain.size() == 1);
    CHECK_FALSE(pointOpFromJson(nlohmann::json::object()).has_value());
    CHECK_FALSE(pointOpFromJson(nlohmann::json{{"kind", "bogus"}}).has_value());
    CHECK_FALSE(pointOpFromJson(nlohmann::json{{"kind", "translate"}, {"offset", 3}}).has_value());

    // Hash: stable, and sensitive to every kind of field.
    const std::uint64_t h = pointOpHash(op);
    CHECK(h == pointOpHash(*aback));
    PointOp m = op;
    m.copies = 4;
    CHECK(pointOpHash(m) != h);
    m = op;
    m.attribute = "other";
    CHECK(pointOpHash(m) != h);
    m = op;
    m.enabled = true;
    CHECK(pointOpHash(m) != h);
    m = op;
    m.offset.z += 1.0f;
    CHECK(pointOpHash(m) != h);
    m = op;
    m.attributeOp.amount = 0.5f;
    CHECK(pointOpHash(m) != h);
    m = op;
    m.kind = PointOpKind::Translate;
    CHECK(pointOpHash(m) != h);
}
