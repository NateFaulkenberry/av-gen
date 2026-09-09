#include "spatial/point_cloud.hpp"

#include "core/noise.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
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
} // namespace

TEST_CASE("PointCloud core columns and defaults", "[spatial][pointcloud]") {
    PointCloud empty;
    CHECK(empty.count() == 0);
    for (const auto name : {attr::position, attr::rotation, attr::scale, attr::id, attr::seed, attr::density, attr::color,
                            attr::emissive, attr::velocity, attr::normal, attr::bounds, attr::index}) {
        CHECK(empty.attributes.has(name));
    }
    CHECK(empty.attributes.typeOf(attr::color) == AttributeType::Color);
    CHECK(empty.attributes.typeOf(attr::id) == AttributeType::Int);
    CHECK(empty.attributes.typeOf(attr::rotation) == AttributeType::Vec4);

    PointCloud cloud(5);
    CHECK(cloud.count() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(cloud.positions()[i] == glm::vec3(0.0f));
        CHECK(cloud.rotations()[i] == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        CHECK(cloud.scales()[i] == glm::vec3(1.0f));
        CHECK(cloud.ids()[i] == static_cast<std::int32_t>(i));
        CHECK(cloud.seeds()[i] == 0);
        CHECK(cloud.densities()[i] == 1.0f);
        CHECK(cloud.colors()[i] == glm::vec4(1.0f));
        CHECK(cloud.emissives()[i] == glm::vec3(1.0f));
        CHECK(cloud.velocities()[i] == glm::vec3(0.0f));
        CHECK(cloud.normals()[i] == glm::vec3(0.0f, 1.0f, 0.0f));
        CHECK((*cloud.attributes.view<glm::vec3>(attr::bounds))[i] == glm::vec3(0.5f));
        CHECK_THAT(d(cloud.indices()[i]), WithinAbs(static_cast<double>(i) / 4.0, 1e-6));
    }
    CHECK(cloud.indices()[0] == 0.0f);
    CHECK(cloud.indices()[4] == 1.0f);

    // resize: new rows get defaults and id = row, indices re-spread.
    cloud.resize(8);
    CHECK(cloud.ids()[7] == 7);
    CHECK(cloud.scales()[6] == glm::vec3(1.0f));
    CHECK(cloud.densities()[6] == 1.0f);
    CHECK(cloud.indices()[7] == 1.0f);
    CHECK_THAT(d(cloud.indices()[1]), WithinAbs(1.0 / 7.0, 1e-6));
    cloud.resize(1);
    CHECK(cloud.indices()[0] == 0.0f);
    cloud.clear();
    CHECK(cloud.count() == 0);
    CHECK(cloud.attributes.has(attr::position));

    // ensureCore recreates a removed core column with its default.
    PointCloud c2(3);
    CHECK(c2.attributes.remove(attr::density));
    c2.ensureCore();
    CHECK(c2.densities()[2] == 1.0f);
    CHECK(c2.attributes.remove(attr::id));
    c2.ensureCore();
    CHECK(c2.ids()[2] == 2);
}

TEST_CASE("PointCloud random is hashIndex(seed, id, channel) and reseed mixes ids", "[spatial][pointcloud]") {
    PointCloud cloud(4);
    for (std::size_t i = 0; i < 4; ++i) {
        cloud.seeds()[i] = 42;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::uint32_t c = 0; c < 4; ++c) {
            CHECK(cloud.random(i, c) == noise::hashIndex(42, static_cast<std::uint32_t>(i), c));
            CHECK(cloud.random(i, c) >= 0.0f);
            CHECK(cloud.random(i, c) < 1.0f);
        }
    }
    // Randoms follow ids, not rows.
    cloud.ids()[0] = 3;
    cloud.ids()[3] = 0;
    CHECK(cloud.random(0, 1) == noise::hashIndex(42, 3, 1));
    CHECK(cloud.random(3, 1) == noise::hashIndex(42, 0, 1));
    CHECK(cloud.random(9, 0) == 0.0f); // out of range

    cloud.reseed(7);
    for (std::size_t i = 0; i < 4; ++i) {
        const noise::U3 h = noise::pcg3d({7u, static_cast<std::uint32_t>(cloud.ids()[i]), 0x9E3779B9u});
        CHECK(cloud.seeds()[i] == static_cast<std::int32_t>(h.x));
    }
    CHECK(cloud.seeds()[1] != cloud.seeds()[2]);
    PointCloud again(4);
    again.ids()[0] = 3;
    again.ids()[3] = 0;
    again.reseed(7);
    CHECK(again.seeds()[0] == cloud.seeds()[0]);
    again.reseed(8);
    CHECK(again.seeds()[0] != cloud.seeds()[0]);
}

TEST_CASE("PointCloud bounds and point matrices", "[spatial][pointcloud]") {
    PointCloud cloud(2);
    cloud.positions()[0] = glm::vec3(-1.0f, 0.0f, 2.0f);
    cloud.positions()[1] = glm::vec3(3.0f, 1.0f, -2.0f);
    cloud.scales()[1] = glm::vec3(2.0f, 1.0f, 1.0f);
    glm::vec3 lo;
    glm::vec3 hi;
    cloud.bounds(lo, hi, false);
    CHECK(lo == glm::vec3(-1.0f, 0.0f, -2.0f));
    CHECK(hi == glm::vec3(3.0f, 1.0f, 2.0f));
    cloud.bounds(lo, hi, true); // bounds 0.5 x scale
    CHECK(lo == glm::vec3(-1.5f, -0.5f, -2.5f));
    CHECK(hi == glm::vec3(4.0f, 1.5f, 2.5f));
    PointCloud none;
    none.bounds(lo, hi);
    CHECK(lo == glm::vec3(0.0f));
    CHECK(hi == glm::vec3(0.0f));

    const glm::quat q = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    cloud.rotations()[1] = glm::vec4(q.x, q.y, q.z, q.w);
    const glm::mat4 m = cloud.pointMatrix(1);
    const glm::vec3 p = glm::vec3(m * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)); // scale 2 then rotate +90 about Y -> -Z
    CHECK_THAT(d(p.x), WithinAbs(3.0, 1e-5));
    CHECK_THAT(d(p.y), WithinAbs(1.0, 1e-5));
    CHECK_THAT(d(p.z), WithinAbs(-4.0, 1e-5));
}

TEST_CASE("projectInstances and cloudFromInstances round trip", "[spatial][pointcloud]") {
    PointCloud cloud(3);
    REQUIRE(cloud.attributes.add("lane", AttributeType::Vec2).has_value());
    for (std::size_t i = 0; i < 3; ++i) {
        const float f = static_cast<float>(i);
        cloud.positions()[i] = glm::vec3(f, f * 2.0f, -f);
        cloud.rotations()[i] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        cloud.scales()[i] = glm::vec3(1.0f + f);
        cloud.ids()[i] = static_cast<std::int32_t>(10 + i);
        cloud.seeds()[i] = 5;
        cloud.densities()[i] = 0.25f * f;
        cloud.colors()[i] = glm::vec4(0.1f * f, 0.2f, 0.3f, 0.9f);
        cloud.emissives()[i] = glm::vec3(2.0f, f, 0.0f);
        (*cloud.attributes.view<glm::vec2>("lane"))[i] = glm::vec2(7.0f + f, 99.0f);
    }
    std::vector<InstanceRecord> records;
    projectInstances(cloud, records, "lane");
    REQUIRE(records.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        const float f = static_cast<float>(i);
        const InstanceRecord& r = records[i];
        CHECK(r.position == glm::vec4(f, f * 2.0f, -f, 0.25f * f));
        CHECK(r.rotation == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        CHECK(r.scale == glm::vec4(glm::vec3(1.0f + f), cloud.indices()[i]));
        for (std::uint32_t c = 0; c < 4; ++c) {
            CHECK(r.random[static_cast<int>(c)] == noise::hashIndex(5, 10 + static_cast<std::uint32_t>(i), c));
        }
        CHECK(r.color == glm::vec4(0.1f * f, 0.2f, 0.3f, 10.0f + f));
        CHECK(r.emissive == glm::vec4(2.0f, f, 0.0f, 7.0f + f));
    }
    // No extra lane, or a missing one, writes 0.
    projectInstances(cloud, records);
    CHECK(records[1].emissive.a == 0.0f);
    projectInstances(cloud, records, "missing");
    CHECK(records[1].emissive.a == 0.0f);

    const PointCloud back = cloudFromInstances(records);
    REQUIRE(back.count() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(back.positions()[i] == cloud.positions()[i]);
        CHECK(back.rotations()[i] == cloud.rotations()[i]);
        CHECK(back.scales()[i] == cloud.scales()[i]);
        CHECK(back.densities()[i] == cloud.densities()[i]);
        CHECK(back.indices()[i] == cloud.indices()[i]);
        CHECK(back.ids()[i] == cloud.ids()[i]);
        CHECK(glm::vec3(back.colors()[i]) == glm::vec3(cloud.colors()[i]));
        CHECK(back.emissives()[i] == cloud.emissives()[i]);
    }
    std::vector<InstanceRecord> again;
    projectInstances(back, again);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(again[i].position == records[i].position);
        CHECK(again[i].scale == records[i].scale);
        CHECK(again[i].color == records[i].color);
        CHECK(glm::vec3(again[i].emissive) == glm::vec3(records[i].emissive));
    }
}

TEST_CASE("PointCloud JSON round trip keeps columns and hash", "[spatial][pointcloud]") {
    PointCloud cloud(4);
    cloud.positions()[2] = glm::vec3(1.0f, 2.0f, 3.0f);
    cloud.seeds()[1] = -12;
    REQUIRE(cloud.attributes.add("mass", AttributeType::Float).has_value());
    (*cloud.attributes.view<float>("mass"))[3] = 2.5f;
    const nlohmann::json j = cloud.toJson();
    CHECK(j.at("count") == 4);
    auto back = PointCloud::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->count() == 4);
    CHECK(back->positions()[2] == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(back->seeds()[1] == -12);
    CHECK((*back->attributes.view<float>("mass"))[3] == 2.5f);
    CHECK(back->contentHash() == cloud.contentHash());
    // A cloud loaded without core columns gets them.
    nlohmann::json minimal = {{"domain", "point"}, {"count", 2}, {"attributes", nlohmann::json::array()}};
    auto min = PointCloud::fromJson(minimal);
    REQUIRE(min.has_value());
    CHECK(min->count() == 2);
    CHECK(min->ids()[1] == 1);
    CHECK(min->scales()[1] == glm::vec3(1.0f));
    CHECK(min->indices()[1] == 1.0f);
    CHECK_FALSE(PointCloud::fromJson(nlohmann::json(3)).has_value());
}
