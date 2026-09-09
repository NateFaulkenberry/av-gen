#include "spatial/attributes.hpp"

#include "core/noise.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <vector>

using namespace avgen;
using namespace avgen::spatial;

namespace {

// A set with a float column 0..n-1 and an int id column.
AttributeSet rampSet(std::size_t n) {
    AttributeSet set;
    set.resize(n);
    auto v = set.ensure<float>("v", AttributeType::Float);
    auto id = set.ensure<std::int32_t>("id", AttributeType::Int);
    REQUIRE(v.has_value());
    REQUIRE(id.has_value());
    for (std::size_t i = 0; i < n; ++i) {
        (*v)[i] = static_cast<float>(i);
        (*id)[i] = static_cast<std::int32_t>(i);
    }
    return set;
}

std::vector<float> column(AttributeSet& set, const char* name) {
    auto v = set.view<float>(name);
    REQUIRE(v.has_value());
    return std::vector<float>(v->values.begin(), v->values.end());
}

} // namespace

TEST_CASE("Attribute type and domain names", "[spatial][attributes]") {
    CHECK(attributeTypeName(AttributeType::Bool) == std::string("bool"));
    CHECK(attributeTypeName(AttributeType::Color) == std::string("color"));
    CHECK(attributeTypeFromName("vec3") == AttributeType::Vec3);
    CHECK_FALSE(attributeTypeFromName("Vec3").has_value());
    for (const auto type : {AttributeType::Bool, AttributeType::Int, AttributeType::Float, AttributeType::Vec2,
                            AttributeType::Vec3, AttributeType::Vec4, AttributeType::Color}) {
        CHECK(attributeTypeFromName(attributeTypeName(type)) == type);
    }
    CHECK(attributeComponents(AttributeType::Bool) == 1);
    CHECK(attributeComponents(AttributeType::Vec2) == 2);
    CHECK(attributeComponents(AttributeType::Color) == 4);
    CHECK(attributeDomainName(AttributeDomain::Point) == std::string("point"));
    CHECK(attributeDomainName(AttributeDomain::Instance) == std::string("instance"));
    for (const auto kind : {AttributeOpKind::Set, AttributeOpKind::Add, AttributeOpKind::Multiply, AttributeOpKind::Remap,
                            AttributeOpKind::Clamp, AttributeOpKind::Normalize, AttributeOpKind::Smooth,
                            AttributeOpKind::Noise, AttributeOpKind::Randomize, AttributeOpKind::Lerp, AttributeOpKind::Fit,
                            AttributeOpKind::Threshold, AttributeOpKind::Compare}) {
        CHECK(attributeOpKindFromName(attributeOpKindName(kind)) == kind);
    }
    CHECK(attributeOpKindName(AttributeOpKind::Randomize) == std::string("randomize"));
}

TEST_CASE("AttributeSet add, remove, type clash, resize", "[spatial][attributes]") {
    AttributeSet set(AttributeDomain::Point);
    CHECK(set.domain() == AttributeDomain::Point);
    CHECK(set.count() == 0);
    auto a = set.add("mass", AttributeType::Float);
    REQUIRE(a.has_value());
    CHECK((*a)->type == AttributeType::Float);
    CHECK((*a)->size() == 0);
    // Same name and type returns the existing column.
    auto again = set.add("mass", AttributeType::Float);
    REQUIRE(again.has_value());
    CHECK(*again == *a);
    CHECK(set.buffers().size() == 1);
    // Different type fails.
    CHECK_FALSE(set.add("mass", AttributeType::Vec3).has_value());
    CHECK_FALSE(set.add("", AttributeType::Float).has_value());
    CHECK(set.has("mass"));
    CHECK(set.typeOf("mass") == AttributeType::Float);
    CHECK_FALSE(set.typeOf("nope").has_value());

    set.resize(4);
    CHECK(set.count() == 4);
    CHECK(set.find("mass")->size() == 4);
    auto view = set.view<float>("mass");
    REQUIRE(view.has_value());
    CHECK(view->size() == 4);
    (*view)[3] = 2.5f;
    CHECK_FALSE(set.view<glm::vec3>("mass").has_value()); // type mismatch
    CHECK_FALSE(set.view<float>("missing").has_value());
    // Columns added after resize have count elements.
    REQUIRE(set.add("p", AttributeType::Vec3).has_value());
    CHECK(set.find("p")->size() == 4);
    set.resize(6);
    CHECK(set.view<float>("mass")->values[5] == 0.0f);
    CHECK(set.view<float>("mass")->values[3] == 2.5f);
    set.resize(2);
    CHECK(set.count() == 2);
    CHECK(set.find("p")->size() == 2);
    // Color views as vec4.
    REQUIRE(set.add("c", AttributeType::Color).has_value());
    CHECK(set.view<glm::vec4>("c").has_value());
    CHECK(set.remove("c"));
    CHECK_FALSE(set.remove("c"));
    CHECK_FALSE(set.has("c"));
    set.clear();
    CHECK(set.count() == 0);
    CHECK(set.has("mass")); // columns are kept
}

TEST_CASE("AttributeSet keepRows, keepIndices, append", "[spatial][attributes]") {
    AttributeSet set = rampSet(6);
    const std::vector<std::uint8_t> keep{1, 0, 1, 0, 0, 1};
    set.keepRows(keep);
    CHECK(set.count() == 3);
    CHECK(column(set, "v") == std::vector<float>{0.0f, 2.0f, 5.0f});
    CHECK(set.view<std::int32_t>("id")->values[2] == 5);

    const std::vector<std::uint32_t> order{2, 2, 0};
    set.keepIndices(order);
    CHECK(set.count() == 3);
    CHECK(column(set, "v") == std::vector<float>{5.0f, 5.0f, 0.0f});

    AttributeSet other = rampSet(2);
    REQUIRE(other.add("extra", AttributeType::Vec2).has_value());
    (*other.view<glm::vec2>("extra"))[1] = glm::vec2(7.0f, 8.0f);
    set.append(other);
    CHECK(set.count() == 5);
    CHECK(column(set, "v") == std::vector<float>{5.0f, 5.0f, 0.0f, 0.0f, 1.0f});
    // Union of columns: `extra` exists everywhere, zero for the old rows.
    REQUIRE(set.has("extra"));
    CHECK(set.find("extra")->size() == 5);
    CHECK((*set.view<glm::vec2>("extra"))[0] == glm::vec2(0.0f));
    CHECK((*set.view<glm::vec2>("extra"))[4] == glm::vec2(7.0f, 8.0f));

    const std::vector<std::uint32_t> one{1};
    set.appendRows(other, one);
    CHECK(set.count() == 6);
    CHECK(column(set, "v").back() == 1.0f);
}

TEST_CASE("readAsVec4 and writeFromVec4 broadcast, pad and truncate", "[spatial][attributes]") {
    AttributeSet set;
    set.resize(1);
    REQUIRE(set.add("b", AttributeType::Bool).has_value());
    REQUIRE(set.add("i", AttributeType::Int).has_value());
    REQUIRE(set.add("f", AttributeType::Float).has_value());
    REQUIRE(set.add("v2", AttributeType::Vec2).has_value());
    REQUIRE(set.add("v3", AttributeType::Vec3).has_value());
    REQUIRE(set.add("c", AttributeType::Color).has_value());
    const glm::vec4 value(1.7f, 2.0f, 3.0f, 4.0f);
    for (const char* name : {"b", "i", "f", "v2", "v3", "c"}) {
        writeFromVec4(*set.find(name), 0, value);
    }
    CHECK(readAsVec4(*set.find("b"), 0) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    CHECK(readAsVec4(*set.find("i"), 0) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    CHECK(readAsVec4(*set.find("f"), 0) == glm::vec4(1.7f, 0.0f, 0.0f, 0.0f));
    CHECK(readAsVec4(*set.find("v2"), 0) == glm::vec4(1.7f, 2.0f, 0.0f, 0.0f));
    CHECK(readAsVec4(*set.find("v3"), 0) == glm::vec4(1.7f, 2.0f, 3.0f, 0.0f));
    CHECK(readAsVec4(*set.find("c"), 0) == value);
    writeFromVec4(*set.find("b"), 0, glm::vec4(0.0f));
    CHECK(readAsVec4(*set.find("b"), 0).x == 0.0f);
}

TEST_CASE("Attribute ops: set, add, multiply create and combine", "[spatial][attributes]") {
    AttributeSet set = rampSet(4);
    AttributeOp op;
    op.kind = AttributeOpKind::Set;
    op.target = "mass";
    op.value = glm::vec4(2.0f, 9.0f, 9.0f, 9.0f);
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(set.typeOf("mass") == AttributeType::Float); // created as Float
    CHECK(column(set, "mass") == std::vector<float>{2.0f, 2.0f, 2.0f, 2.0f});

    op.kind = AttributeOpKind::Add;
    op.value = glm::vec4(0.5f);
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "mass") == std::vector<float>{2.5f, 2.5f, 2.5f, 2.5f});

    // Add with an explicit source scales the source: mass += 2 * v.
    op.source = "v";
    op.value = glm::vec4(2.0f);
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "mass") == std::vector<float>{2.5f, 4.5f, 6.5f, 8.5f});

    op.kind = AttributeOpKind::Multiply;
    op.source.clear();
    op.value = glm::vec4(2.0f);
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "mass") == std::vector<float>{5.0f, 9.0f, 13.0f, 17.0f});

    // Target created with the source's type.
    AttributeOp copy;
    copy.kind = AttributeOpKind::Clamp;
    copy.target = "idCopy";
    copy.source = "id";
    copy.outMin = 1.0f;
    copy.outMax = 2.0f;
    REQUIRE(applyAttributeOp(set, copy).has_value());
    CHECK(set.typeOf("idCopy") == AttributeType::Int);
    CHECK((*set.view<std::int32_t>("idCopy"))[0] == 1);
    CHECK((*set.view<std::int32_t>("idCopy"))[3] == 2);

    // Disabled op is a no-op; missing target/source fail.
    AttributeOp off = op;
    off.enabled = false;
    REQUIRE(applyAttributeOp(set, off).has_value());
    CHECK(column(set, "mass") == std::vector<float>{5.0f, 9.0f, 13.0f, 17.0f});
    AttributeOp bad;
    bad.kind = AttributeOpKind::Remap;
    bad.target = "";
    CHECK_FALSE(applyAttributeOp(set, bad).has_value());
    bad.target = "new";
    bad.source = "missing";
    CHECK_FALSE(applyAttributeOp(set, bad).has_value());
}

TEST_CASE("Attribute ops: remap, clamp, normalize, fit, threshold, compare", "[spatial][attributes]") {
    AttributeSet set = rampSet(5); // v = 0..4
    AttributeOp op;
    op.target = "out";
    op.source = "v";

    op.kind = AttributeOpKind::Remap;
    op.inMin = 0.0f;
    op.inMax = 4.0f;
    op.outMin = 10.0f;
    op.outMax = 20.0f;
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{10.0f, 12.5f, 15.0f, 17.5f, 20.0f});
    op.inMax = 2.0f; // clamp above 2
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{10.0f, 15.0f, 20.0f, 20.0f, 20.0f});
    op.clamp = false;
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out")[4] == 30.0f);

    op.kind = AttributeOpKind::Clamp;
    op.outMin = 1.0f;
    op.outMax = 3.0f;
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{1.0f, 1.0f, 2.0f, 3.0f, 3.0f});

    op.kind = AttributeOpKind::Normalize;
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{0.0f, 0.25f, 0.5f, 0.75f, 1.0f});

    op.kind = AttributeOpKind::Fit;
    op.outMin = -1.0f;
    op.outMax = 1.0f;
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{-1.0f, -0.5f, 0.0f, 0.5f, 1.0f});

    // Constant columns normalise to 0 and fit to outMin.
    AttributeOp constant;
    constant.kind = AttributeOpKind::Set;
    constant.target = "k";
    constant.value = glm::vec4(3.0f);
    REQUIRE(applyAttributeOp(set, constant).has_value());
    op.source = "k";
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{-1.0f, -1.0f, -1.0f, -1.0f, -1.0f});
    op.kind = AttributeOpKind::Normalize;
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out")[2] == 0.0f);

    op.source = "v";
    op.kind = AttributeOpKind::Threshold;
    op.value = glm::vec4(2.0f);
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{0.0f, 0.0f, 1.0f, 1.0f, 1.0f});

    op.kind = AttributeOpKind::Compare;
    op.compare = 0; // <
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{1.0f, 1.0f, 0.0f, 0.0f, 0.0f});
    op.compare = 2; // ==
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{0.0f, 0.0f, 1.0f, 0.0f, 0.0f});
    op.compare = 5; // !=
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{1.0f, 1.0f, 0.0f, 1.0f, 1.0f});
    op.compare = 1; // <=
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{1.0f, 1.0f, 1.0f, 0.0f, 0.0f});
    op.compare = 4; // >
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{0.0f, 0.0f, 0.0f, 1.0f, 1.0f});
}

TEST_CASE("Attribute ops: smooth, lerp, noise, randomize", "[spatial][attributes]") {
    AttributeSet set = rampSet(5); // v = 0..4
    AttributeOp op;
    op.target = "out";
    op.source = "v";

    op.kind = AttributeOpKind::Smooth;
    op.radius = 1;
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{0.5f, 1.0f, 2.0f, 3.0f, 3.5f});
    op.radius = 0;
    REQUIRE(applyAttributeOp(set, op).has_value());
    CHECK(column(set, "out") == std::vector<float>{0.0f, 1.0f, 2.0f, 3.0f, 4.0f});
    // In place.
    AttributeOp inPlace;
    inPlace.kind = AttributeOpKind::Smooth;
    inPlace.target = "out";
    inPlace.radius = 10;
    REQUIRE(applyAttributeOp(set, inPlace).has_value());
    for (const float x : column(set, "out")) {
        CHECK(x == 2.0f);
    }

    AttributeOp lerp;
    lerp.kind = AttributeOpKind::Lerp;
    lerp.target = "out";
    lerp.source = "v";
    lerp.secondSource = "k";
    CHECK_FALSE(applyAttributeOp(set, lerp).has_value()); // second source missing
    AttributeOp setK;
    setK.kind = AttributeOpKind::Set;
    setK.target = "k";
    setK.value = glm::vec4(10.0f);
    REQUIRE(applyAttributeOp(set, setK).has_value());
    lerp.amount = 0.5f;
    REQUIRE(applyAttributeOp(set, lerp).has_value());
    CHECK(column(set, "out") == std::vector<float>{5.0f, 5.5f, 6.0f, 6.5f, 7.0f});

    // Noise needs a position column; the value is fbm in [0, 1] and deterministic.
    AttributeOp noise;
    noise.kind = AttributeOpKind::Noise;
    noise.target = "n";
    noise.source = "v";
    noise.amount = 1.0f;
    CHECK_FALSE(applyAttributeOp(set, noise).has_value());
    REQUIRE(set.add("position", AttributeType::Vec3).has_value());
    for (std::size_t i = 0; i < 5; ++i) {
        (*set.view<glm::vec3>("position"))[i] = glm::vec3(static_cast<float>(i) * 0.37f, 0.1f, -0.2f);
    }
    REQUIRE(applyAttributeOp(set, noise).has_value());
    const std::vector<float> n1 = column(set, "n");
    for (const float x : n1) {
        CHECK(x >= 0.0f);
        CHECK(x <= 1.0f);
    }
    REQUIRE(applyAttributeOp(set, noise).has_value());
    CHECK(column(set, "n") == n1);
    noise.seed = 99;
    REQUIRE(applyAttributeOp(set, noise).has_value());
    CHECK(column(set, "n") != n1);
    noise.amount = 0.0f;
    REQUIRE(applyAttributeOp(set, noise).has_value());
    CHECK(column(set, "n") == std::vector<float>{0.0f, 1.0f, 2.0f, 3.0f, 4.0f});

    // Randomize: value + hash * range, keyed by the id column, per component channels.
    AttributeOp rnd;
    rnd.kind = AttributeOpKind::Randomize;
    rnd.target = "r";
    rnd.source = "v";
    rnd.value = glm::vec4(1.0f);
    rnd.range = glm::vec4(2.0f);
    rnd.seed = 5;
    REQUIRE(applyAttributeOp(set, rnd).has_value());
    const std::vector<float> r1 = column(set, "r");
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(r1[i] == 1.0f + noise::hashIndex(5, static_cast<std::uint32_t>(i), 0) * 2.0f);
    }
    // Ids drive the hash: permuting ids permutes the randoms.
    (*set.view<std::int32_t>("id"))[0] = 4;
    (*set.view<std::int32_t>("id"))[4] = 0;
    REQUIRE(applyAttributeOp(set, rnd).has_value());
    const std::vector<float> r2 = column(set, "r");
    CHECK(r2[0] == r1[4]);
    CHECK(r2[4] == r1[0]);
    CHECK(r2[2] == r1[2]);
    // Without an id column the row index is used.
    rnd.idAttribute = "nope";
    REQUIRE(applyAttributeOp(set, rnd).has_value());
    CHECK(column(set, "r") == r1);
    // Vec3 target gets channels 0..2.
    rnd.target = "r3";
    rnd.source = "position";
    rnd.amount = 1.0f;
    REQUIRE(applyAttributeOp(set, rnd).has_value());
    const glm::vec3 r3 = (*set.view<glm::vec3>("r3"))[1];
    CHECK(r3.y == 1.0f + noise::hashIndex(5, 1, 1) * 2.0f);
    CHECK(r3.z == 1.0f + noise::hashIndex(5, 1, 2) * 2.0f);
}

TEST_CASE("Attribute stats", "[spatial][attributes]") {
    AttributeSet set = rampSet(5);
    const AttributeStats s = attributeStats(*set.find("v"));
    CHECK(s.min.x == 0.0f);
    CHECK(s.max.x == 4.0f);
    CHECK(s.mean.x == 2.0f);
    AttributeSet empty;
    REQUIRE(empty.add("e", AttributeType::Vec3).has_value());
    const AttributeStats z = attributeStats(*empty.find("e"));
    CHECK(z.min == glm::vec4(0.0f));
    CHECK(z.max == glm::vec4(0.0f));
    CHECK(z.mean == glm::vec4(0.0f));
}

TEST_CASE("AttributeSet JSON round trip and contentHash stability", "[spatial][attributes]") {
    AttributeSet set(AttributeDomain::Instance);
    set.resize(3);
    REQUIRE(set.add("b", AttributeType::Bool).has_value());
    REQUIRE(set.add("i", AttributeType::Int).has_value());
    REQUIRE(set.add("f", AttributeType::Float).has_value());
    REQUIRE(set.add("v2", AttributeType::Vec2).has_value());
    REQUIRE(set.add("v3", AttributeType::Vec3).has_value());
    REQUIRE(set.add("v4", AttributeType::Vec4).has_value());
    REQUIRE(set.add("c", AttributeType::Color).has_value());
    (*set.view<std::uint8_t>("b"))[1] = 1;
    (*set.view<std::int32_t>("i"))[2] = -7;
    (*set.view<float>("f"))[0] = 1.5f;
    (*set.view<glm::vec2>("v2"))[1] = glm::vec2(1.0f, 2.0f);
    (*set.view<glm::vec3>("v3"))[2] = glm::vec3(3.0f, 4.0f, 5.0f);
    (*set.view<glm::vec4>("v4"))[0] = glm::vec4(6.0f, 7.0f, 8.0f, 9.0f);
    (*set.view<glm::vec4>("c"))[1] = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);

    const nlohmann::json j = set.toJson();
    CHECK(j.at("domain") == "instance");
    CHECK(j.at("count") == 3);
    CHECK(j.at("attributes").size() == 7);
    CHECK(j.at("attributes").at(4).at("type") == "vec3");
    CHECK(j.at("attributes").at(4).at("values").size() == 9);

    auto back = AttributeSet::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->domain() == AttributeDomain::Instance);
    CHECK(back->count() == 3);
    CHECK((*back->view<std::uint8_t>("b"))[1] == 1);
    CHECK((*back->view<std::int32_t>("i"))[2] == -7);
    CHECK((*back->view<float>("f"))[0] == 1.5f);
    CHECK((*back->view<glm::vec2>("v2"))[1] == glm::vec2(1.0f, 2.0f));
    CHECK((*back->view<glm::vec3>("v3"))[2] == glm::vec3(3.0f, 4.0f, 5.0f));
    CHECK((*back->view<glm::vec4>("v4"))[0] == glm::vec4(6.0f, 7.0f, 8.0f, 9.0f));
    CHECK(back->typeOf("c") == AttributeType::Color);
    CHECK((*back->view<glm::vec4>("c"))[1] == glm::vec4(0.1f, 0.2f, 0.3f, 0.4f));
    CHECK(back->contentHash() == set.contentHash());

    // The hash is a stable function of the content, sensitive to values, names and types.
    const std::uint64_t h = set.contentHash();
    CHECK(h == set.contentHash());
    (*set.view<float>("f"))[0] = 1.25f;
    CHECK(set.contentHash() != h);
    (*set.view<float>("f"))[0] = 1.5f;
    CHECK(set.contentHash() == h);
    CHECK(set.remove("b"));
    CHECK(set.contentHash() != h);
    AttributeSet empty;
    CHECK(empty.contentHash() == AttributeSet().contentHash());
    CHECK(AttributeSet(AttributeDomain::Vertex).contentHash() != empty.contentHash());

    // Errors.
    CHECK_FALSE(AttributeSet::fromJson(nlohmann::json::array()).has_value());
    nlohmann::json bad = j;
    bad["attributes"][0]["values"] = nlohmann::json::array({1});
    CHECK_FALSE(AttributeSet::fromJson(bad).has_value());
    bad = j;
    bad["attributes"][0]["type"] = "matrix";
    CHECK_FALSE(AttributeSet::fromJson(bad).has_value());
}

TEST_CASE("AttributeOp JSON round trip writes non-defaults only", "[spatial][attributes]") {
    AttributeOp op;
    op.kind = AttributeOpKind::Randomize;
    op.enabled = false;
    op.target = "t";
    op.source = "s";
    op.secondSource = "u";
    op.positionAttribute = "pos";
    op.value = glm::vec4(1.0f, 2.0f, 3.0f, 4.0f);
    op.amount = 0.25f;
    op.inMin = -1.0f;
    op.inMax = 2.0f;
    op.outMin = 3.0f;
    op.outMax = 4.0f;
    op.clamp = false;
    op.scale = 0.5f;
    op.offset = glm::vec3(1.0f, 0.0f, 2.0f);
    op.range = glm::vec4(0.5f);
    op.seed = 77;
    op.radius = 3;
    op.compare = 1;
    op.idAttribute = "pid";
    const nlohmann::json j = attributeOpToJson(op);
    CHECK(j.at("kind") == "randomize");
    CHECK(j.contains("seed"));
    auto back = attributeOpFromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->kind == AttributeOpKind::Randomize);
    CHECK_FALSE(back->enabled);
    CHECK(back->target == "t");
    CHECK(back->source == "s");
    CHECK(back->secondSource == "u");
    CHECK(back->positionAttribute == "pos");
    CHECK(back->value == op.value);
    CHECK(back->amount == 0.25f);
    CHECK(back->inMin == -1.0f);
    CHECK(back->inMax == 2.0f);
    CHECK(back->outMin == 3.0f);
    CHECK(back->outMax == 4.0f);
    CHECK_FALSE(back->clamp);
    CHECK(back->scale == 0.5f);
    CHECK(back->offset == op.offset);
    CHECK(back->range == op.range);
    CHECK(back->seed == 77);
    CHECK(back->radius == 3);
    CHECK(back->compare == 1);
    CHECK(back->idAttribute == "pid");

    // Defaults are omitted; kind and target are required.
    AttributeOp plain;
    plain.target = "x";
    const nlohmann::json pj = attributeOpToJson(plain);
    CHECK(pj.size() == 2);
    CHECK_FALSE(attributeOpFromJson(nlohmann::json::object()).has_value());
    CHECK_FALSE(attributeOpFromJson(nlohmann::json{{"kind", "set"}}).has_value());
    CHECK_FALSE(attributeOpFromJson(nlohmann::json{{"kind", "bogus"}, {"target", "x"}}).has_value());
}
