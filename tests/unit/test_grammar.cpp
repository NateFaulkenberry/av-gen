#include "scene/grammar.hpp"
#include "scene/procedural.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::scene;
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

glm::quat rotationOf(const spatial::PointCloud& c, std::size_t i) {
    const glm::vec4 r = c.rotations()[i];
    return glm::quat(r.w, r.x, r.y, r.z);
}

std::vector<std::int32_t> column(const spatial::PointCloud& c, const char* name) {
    auto view = c.attributes.view<std::int32_t>(name);
    REQUIRE(view.has_value());
    return std::vector<std::int32_t>(view->values.begin(), view->values.end());
}

GrammarRule place(const std::string& name, float scaleAttribute = 1.0f) {
    GrammarRule r;
    r.name = name;
    r.op = GrammarOp::Place;
    r.scaleAttribute = scaleAttribute;
    return r;
}

GrammarRule repeat(const std::string& name, int count, const glm::vec3& offset, std::vector<std::string> children) {
    GrammarRule r;
    r.name = name;
    r.op = GrammarOp::Repeat;
    r.count = count;
    r.step.position = offset;
    r.children = std::move(children);
    return r;
}

} // namespace

TEST_CASE("Grammar op names round trip", "[scene][grammar]") {
    for (const auto op : {GrammarOp::Place, GrammarOp::Repeat, GrammarOp::Branch, GrammarOp::Alternate, GrammarOp::Mirror,
                          GrammarOp::Choice, GrammarOp::Conditional}) {
        const auto back = grammarOpFromName(grammarOpName(op));
        REQUIRE(back.has_value());
        CHECK(*back == op);
    }
    CHECK(std::string(grammarOpName(GrammarOp::Conditional)) == "conditional");
    CHECK_FALSE(grammarOpFromName("spawn").has_value());
}

TEST_CASE("Grammar: a Place axiom yields one point at the identity (strong definitions linked)", "[scene][grammar]") {
    Grammar g;
    g.axiom = "p";
    g.rules = {place("p", 2.0f)};
    REQUIRE(g.validate().has_value());
    REQUIRE(g.find("p") != nullptr);
    CHECK(g.find("nope") == nullptr);
    const spatial::PointCloud c = g.expand();
    REQUIRE(c.count() == 1);
    checkVec(c.positions()[0], glm::vec3(0.0f));
    checkVec(c.scales()[0], glm::vec3(2.0f));
    CHECK(c.rotations()[0] == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    CHECK(c.ids()[0] == 0);
    CHECK(column(c, "depth") == std::vector<std::int32_t>{0});
    CHECK(column(c, "rule") == std::vector<std::int32_t>{0});
    CHECK(column(c, "branch") == std::vector<std::int32_t>{0});
    CHECK(c.indices()[0] == 0.0f);
}

TEST_CASE("Grammar Repeat: count points at k * offset with cumulative rotation and scale", "[scene][grammar]") {
    Grammar g;
    g.axiom = "row";
    g.rules = {repeat("row", 5, {2.0f, 0.0f, 0.0f}, {"p"}), place("p")};
    REQUIRE(g.validate().has_value());
    const spatial::PointCloud c = g.expand();
    REQUIRE(c.count() == 5);
    const auto depths = column(c, "depth");
    const auto rules = column(c, "rule");
    const auto branches = column(c, "branch");
    for (std::size_t k = 0; k < 5; ++k) {
        checkVec(c.positions()[k], glm::vec3(2.0f * static_cast<float>(k), 0.0f, 0.0f));
        CHECK(c.ids()[k] == static_cast<std::int32_t>(k));
        CHECK(depths[k] == 1);
        CHECK(rules[k] == 1);
        CHECK(branches[k] == static_cast<std::int32_t>(k));
        CHECK_THAT(d(c.indices()[k]), WithinAbs(static_cast<double>(k) / 4.0, 1e-6));
    }

    // Rotation and scale accumulate: step = rotate 90 degrees about Y and scale 0.5.
    g.rules[0].step.position = glm::vec3(0.0f);
    g.rules[0].step.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    g.rules[0].step.scale = glm::vec3(0.5f);
    const spatial::PointCloud r = g.expand();
    REQUIRE(r.count() == 5);
    for (std::size_t k = 0; k < 5; ++k) {
        CHECK_THAT(d(r.scales()[k].x), WithinAbs(std::pow(0.5, static_cast<double>(k)), 1e-6));
        const glm::vec3 forward = rotationOf(r, k) * glm::vec3(0.0f, 0.0f, 1.0f);
        const float angle = glm::half_pi<float>() * static_cast<float>(k);
        checkVec(forward, glm::vec3(std::sin(angle), 0.0f, std::cos(angle)), 1e-5);
    }

    // `pre` applies once, before the repetitions.
    g.rules[0].step = Transform{};
    g.rules[0].step.position = {1.0f, 0.0f, 0.0f};
    g.rules[0].pre.position = {0.0f, 10.0f, 0.0f};
    const spatial::PointCloud p = g.expand();
    REQUIRE(p.count() == 5);
    checkVec(p.positions()[3], glm::vec3(3.0f, 10.0f, 0.0f));
    // count 0 emits nothing.
    g.rules[0].count = 0;
    CHECK(g.expand().count() == 0);
}

TEST_CASE("Grammar Alternate picks children round-robin; Branch is the union", "[scene][grammar]") {
    Grammar g;
    g.axiom = "alt";
    GrammarRule alt = repeat("alt", 5, {1.0f, 0.0f, 0.0f}, {"a", "b"});
    alt.op = GrammarOp::Alternate;
    g.rules = {alt, place("a", 1.0f), place("b", 2.0f)};
    REQUIRE(g.validate().has_value());
    const spatial::PointCloud c = g.expand();
    REQUIRE(c.count() == 5);
    const auto rules = column(c, "rule");
    for (std::size_t k = 0; k < 5; ++k) {
        checkVec(c.positions()[k], glm::vec3(static_cast<float>(k), 0.0f, 0.0f));
        CHECK(rules[k] == (k % 2 == 0 ? 1 : 2));
        CHECK(c.scales()[k].x == (k % 2 == 0 ? 1.0f : 2.0f));
        CHECK(column(c, "branch")[k] == static_cast<std::int32_t>(k));
    }

    Grammar b;
    b.axiom = "both";
    GrammarRule both;
    both.name = "both";
    both.op = GrammarOp::Branch;
    both.pre.position = {0.0f, 1.0f, 0.0f};
    both.children = {"left", "right"};
    GrammarRule left = place("left");
    left.pre.position = {-1.0f, 0.0f, 0.0f};
    GrammarRule right = place("right");
    right.pre.position = {1.0f, 0.0f, 0.0f};
    b.rules = {both, left, right};
    REQUIRE(b.validate().has_value());
    const spatial::PointCloud u = b.expand();
    REQUIRE(u.count() == 2);
    checkVec(u.positions()[0], glm::vec3(-1.0f, 1.0f, 0.0f));
    checkVec(u.positions()[1], glm::vec3(1.0f, 1.0f, 0.0f));
    CHECK(column(u, "depth") == std::vector<std::int32_t>{1, 1});
    CHECK(column(u, "rule") == std::vector<std::int32_t>{1, 2});
    CHECK(column(u, "branch") == std::vector<std::int32_t>{0, 0}); // no Repeat above
}

TEST_CASE("Grammar Mirror reflects positions and conjugates rotations", "[scene][grammar]") {
    Grammar g;
    g.axiom = "m";
    GrammarRule m;
    m.name = "m";
    m.op = GrammarOp::Mirror;
    m.mirrorAxis = {2.0f, 0.0f, 0.0f}; // normalised on use
    m.children = {"p"};
    GrammarRule p = place("p", 1.5f);
    p.pre.position = {1.0f, 2.0f, 3.0f};
    p.pre.rotation = glm::angleAxis(glm::half_pi<float>(), glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
    g.rules = {m, p};
    REQUIRE(g.validate().has_value());
    const spatial::PointCloud c = g.expand();
    REQUIRE(c.count() == 2);
    checkVec(c.positions()[0], glm::vec3(1.0f, 2.0f, 3.0f));
    checkVec(c.positions()[1], glm::vec3(-1.0f, 2.0f, 3.0f));
    checkVec(c.scales()[1], glm::vec3(1.5f)); // scale stays positive
    // The mirrored rotation is R M R with R the reflection matrix.
    const glm::mat3 R = glm::mat3(1.0f) - 2.0f * glm::outerProduct(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat3 M = glm::mat3_cast(rotationOf(c, 0));
    const glm::mat3 expected = R * M * R;
    const glm::mat3 actual = glm::mat3_cast(rotationOf(c, 1));
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            CHECK_THAT(d(actual[col][row]), WithinAbs(d(expected[col][row]), 1e-5));
        }
    }
    CHECK_THAT(d(glm::determinant(actual)), WithinAbs(1.0, 1e-5));

    // Mirror inside a Repeat reflects the whole subtree about the grammar origin, not the local one.
    Grammar r;
    r.axiom = "row";
    r.rules = {repeat("row", 2, {0.0f, 0.0f, 5.0f}, {"m"}), m, p};
    r.rules[1].mirrorAxis = {0.0f, 0.0f, 1.0f};
    REQUIRE(r.validate().has_value());
    const spatial::PointCloud rc = r.expand();
    REQUIRE(rc.count() == 4);
    checkVec(rc.positions()[0], glm::vec3(1.0f, 2.0f, 3.0f));
    checkVec(rc.positions()[1], glm::vec3(1.0f, 2.0f, -3.0f));
    checkVec(rc.positions()[2], glm::vec3(1.0f, 2.0f, 8.0f));
    checkVec(rc.positions()[3], glm::vec3(1.0f, 2.0f, -8.0f));
    CHECK(column(rc, "branch") == std::vector<std::int32_t>{0, 0, 1, 1});
}

TEST_CASE("Grammar Choice is deterministic, path dependent and weight respecting", "[scene][grammar]") {
    Grammar g;
    g.axiom = "row";
    g.rules = {repeat("row", 1000, {1.0f, 0.0f, 0.0f}, {"pick"}), GrammarRule{}, place("a", 1.0f), place("b", 2.0f),
               place("never", 3.0f)};
    g.rules[1].name = "pick";
    g.rules[1].op = GrammarOp::Choice;
    g.rules[1].children = {"a", "b", "never"};
    g.rules[1].weights = {3.0f, 1.0f, 0.0f};
    REQUIRE(g.validate().has_value());

    const auto countA = [](const spatial::PointCloud& c) {
        std::size_t n = 0;
        for (const glm::vec3& s : c.scales()) {
            n += s.x == 1.0f ? 1 : 0;
        }
        return n;
    };
    const spatial::PointCloud c = g.expand();
    REQUIRE(c.count() == 1000);
    for (const glm::vec3& s : c.scales()) {
        CHECK(s.x != 3.0f); // zero weight is never picked
    }
    const std::size_t a = countA(c);
    CHECK(a > 690);
    CHECK(a < 810);
    // Different repetitions make different choices (path dependent), deterministically.
    CHECK(c.contentHash() == g.expand().contentHash());
    std::size_t changes = 0;
    for (std::size_t k = 1; k < 1000; ++k) {
        changes += c.scales()[k].x != c.scales()[k - 1].x ? 1 : 0;
    }
    CHECK(changes > 200);
    // Seeds change the outcome; over many seeds the ratio holds.
    std::size_t total = 0;
    std::size_t differing = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        Grammar s = g;
        s.seed = seed;
        const spatial::PointCloud sc = s.expand();
        total += countA(sc);
        differing += sc.contentHash() != c.contentHash() ? 1 : 0;
    }
    CHECK(differing >= 19);
    CHECK(total > 14500);
    CHECK(total < 15500);
    // The rule seed is mixed in as well.
    g.rules[1].seed = 77;
    CHECK(g.expand().contentHash() != c.contentHash());

    // The same subtree under two branches picks independently.
    Grammar b;
    b.axiom = "both";
    GrammarRule both;
    both.name = "both";
    both.op = GrammarOp::Branch;
    both.children = {"row", "row"};
    b.rules = {both, g.rules[0], g.rules[1], g.rules[2], g.rules[3], g.rules[4]};
    b.rules[1].count = 200;
    REQUIRE(b.validate().has_value());
    const spatial::PointCloud bc = b.expand();
    REQUIRE(bc.count() == 400);
    std::size_t same = 0;
    for (std::size_t k = 0; k < 200; ++k) {
        same += bc.scales()[k].x == bc.scales()[k + 200].x ? 1 : 0;
    }
    CHECK(same < 190);
    CHECK(same > 80); // ~ 0.75^2 + 0.25^2 = 62.5% agreement by chance

    // Missing weights default to 1 (equal split), an all-zero list means equal weights.
    g.rules[1].weights.clear();
    g.rules[1].children = {"a", "b"};
    const std::size_t half = countA(g.expand());
    CHECK(half > 420);
    CHECK(half < 580);
    g.rules[1].weights = {0.0f, 0.0f};
    const std::size_t zero = countA(g.expand());
    CHECK(zero > 420);
    CHECK(zero < 580);
}

TEST_CASE("Grammar Conditional: recursion limited by depthLimit with an else branch", "[scene][grammar]") {
    // col = Conditional(children [p, up], else [cap]); up = Branch(pre +y) -> col.
    Grammar g;
    g.axiom = "col";
    GrammarRule col;
    col.name = "col";
    col.op = GrammarOp::Conditional;
    col.depthLimit = 5;
    col.children = {"p", "up"};
    col.elseChildren = {"cap"};
    GrammarRule up;
    up.name = "up";
    up.op = GrammarOp::Branch;
    up.pre.position = {0.0f, 1.0f, 0.0f};
    up.children = {"col"};
    g.rules = {col, place("p"), up, place("cap", 3.0f)};
    g.maxDepth = 32;
    REQUIRE(g.validate().has_value());
    const spatial::PointCloud c = g.expand();
    // col at depth 0, 2, 4 expand children (3 places at y = 0, 1, 2); col at depth 6 takes the else.
    REQUIRE(c.count() == 4);
    checkVec(c.positions()[0], glm::vec3(0.0f, 0.0f, 0.0f));
    checkVec(c.positions()[1], glm::vec3(0.0f, 1.0f, 0.0f));
    checkVec(c.positions()[2], glm::vec3(0.0f, 2.0f, 0.0f));
    checkVec(c.positions()[3], glm::vec3(0.0f, 3.0f, 0.0f));
    CHECK(c.scales()[3].x == 3.0f);
    CHECK(column(c, "depth") == std::vector<std::int32_t>{1, 3, 5, 7});
    CHECK(column(c, "rule") == std::vector<std::int32_t>{1, 1, 1, 3});
}

TEST_CASE("Grammar recursion through an ancestor stops at maxDepth; maxInstances truncates", "[scene][grammar]") {
    // r = Branch(p, up); up = Branch(pre +y) -> r: one storey costs two levels of depth.
    Grammar g;
    g.axiom = "r";
    GrammarRule r;
    r.name = "r";
    r.op = GrammarOp::Branch;
    r.children = {"p", "up"};
    GrammarRule up;
    up.name = "up";
    up.op = GrammarOp::Branch;
    up.pre.position = {0.0f, 1.0f, 0.0f};
    up.children = {"r"};
    g.rules = {r, place("p"), up};
    g.maxDepth = 8;
    REQUIRE(g.validate().has_value());
    const spatial::PointCloud c = g.expand();
    // r at depths 0, 2, 4, 6 emit one p each (at depth + 1); the r reached at depth 8 has its
    // children at depth 9 > maxDepth, so it emits nothing.
    REQUIRE(c.count() == 4);
    for (std::size_t k = 0; k < 4; ++k) {
        checkVec(c.positions()[k], glm::vec3(0.0f, static_cast<float>(k), 0.0f));
        CHECK(column(c, "depth")[k] == static_cast<std::int32_t>(2 * k + 1));
        CHECK(c.ids()[k] == static_cast<std::int32_t>(k));
    }
    g.maxDepth = 9; // one more level fits one more storey
    CHECK(g.expand().count() == 5);
    g.maxDepth = 0;
    CHECK(g.expand().count() == 0); // the axiom's children are already at depth 1

    Grammar t;
    t.axiom = "row";
    t.rules = {repeat("row", 100, {1.0f, 0.0f, 0.0f}, {"p"}), place("p")};
    const spatial::PointCloud full = t.expand();
    REQUIRE(full.count() == 100);
    t.maxInstances = 10;
    const spatial::PointCloud cut = t.expand();
    REQUIRE(cut.count() == 10);
    for (std::size_t k = 0; k < 10; ++k) {
        CHECK(cut.positions()[k] == full.positions()[k]);
        CHECK(cut.ids()[k] == static_cast<std::int32_t>(k));
    }
    CHECK(cut.indices()[9] == 1.0f); // renumbered
    // Ids are emission order and the seed column follows the grammar seed.
    CHECK(cut.seeds()[3] != 0);
    t.seed = 9;
    CHECK(t.expand().seeds()[3] != cut.seeds()[3]);
}

TEST_CASE("Grammar validate rejects bad grammars", "[scene][grammar]") {
    Grammar g;
    CHECK_FALSE(g.validate().has_value()); // no axiom
    g.axiom = "missing";
    CHECK_FALSE(g.validate().has_value());
    g.axiom = "row";
    g.rules = {repeat("row", 3, {1.0f, 0.0f, 0.0f}, {"p"})};
    CHECK_FALSE(g.validate().has_value()); // child missing
    g.rules.push_back(place("p"));
    CHECK(g.validate().has_value());
    Grammar bad = g;
    bad.rules.push_back(place("p"));
    CHECK_FALSE(bad.validate().has_value()); // duplicate name
    bad = g;
    bad.rules[0].count = -1;
    CHECK_FALSE(bad.validate().has_value());
    bad = g;
    bad.rules[0].op = GrammarOp::Mirror;
    bad.rules[0].mirrorAxis = glm::vec3(0.0f);
    CHECK_FALSE(bad.validate().has_value());
    bad = g;
    bad.rules[0].weights = {1.0f, -1.0f};
    CHECK_FALSE(bad.validate().has_value());
    bad = g;
    bad.maxInstances = 0;
    CHECK_FALSE(bad.validate().has_value());
    bad = g;
    bad.rules[1].scaleAttribute = 0.0f;
    CHECK_FALSE(bad.validate().has_value());
    bad = g;
    bad.rules[0].elseChildren = {"nope"};
    CHECK_FALSE(bad.validate().has_value());
    bad = g;
    bad.rules[1].name.clear();
    CHECK_FALSE(bad.validate().has_value());
}

TEST_CASE("Grammar JSON round trip and hash sensitivity", "[scene][grammar]") {
    Grammar g;
    g.axiom = "root";
    g.maxDepth = 12;
    g.maxInstances = 5000;
    g.seed = 42;
    GrammarRule root;
    root.name = "root";
    root.op = GrammarOp::Choice;
    root.count = 7;
    root.step.position = {1.0f, 2.0f, 3.0f};
    root.step.rotation = glm::angleAxis(0.4f, glm::normalize(glm::vec3(0.2f, 1.0f, 0.3f)));
    root.step.scale = {0.5f, 0.6f, 0.7f};
    root.pre.position = {-1.0f, 0.0f, 1.0f};
    root.children = {"a", "b"};
    root.weights = {2.0f, 1.0f};
    root.elseChildren = {"a"};
    root.depthLimit = 4;
    root.mirrorAxis = {0.0f, 1.0f, 0.0f};
    root.seed = 9;
    root.scaleAttribute = 1.25f;
    g.rules = {root, place("a"), place("b", 2.0f)};

    const nlohmann::json j = g.toJson();
    CHECK(j.at("axiom") == "root");
    CHECK(j.at("rules").size() == 3);
    CHECK(j.at("rules").at(0).at("op") == "choice");
    CHECK(j.at("rules").at(0).at("children").size() == 2);
    CHECK(j.at("rules").at(0).at("step").contains("rotation"));
    auto back = Grammar::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->axiom == "root");
    CHECK(back->maxDepth == 12);
    CHECK(back->maxInstances == 5000);
    CHECK(back->seed == 42);
    REQUIRE(back->rules.size() == 3);
    const GrammarRule& r = back->rules[0];
    CHECK(r.name == "root");
    CHECK(r.op == GrammarOp::Choice);
    CHECK(r.count == 7);
    checkVec(r.step.position, root.step.position, 1e-6);
    checkVec(r.step.scale, root.step.scale, 1e-6);
    checkVec(r.step.rotation * glm::vec3(1.0f, 2.0f, 3.0f), root.step.rotation * glm::vec3(1.0f, 2.0f, 3.0f), 1e-4);
    checkVec(r.pre.position, root.pre.position, 1e-6);
    CHECK(r.children == root.children);
    CHECK(r.weights == root.weights);
    CHECK(r.elseChildren == root.elseChildren);
    CHECK(r.depthLimit == 4);
    CHECK(r.mirrorAxis == root.mirrorAxis);
    CHECK(r.seed == 9);
    CHECK(r.scaleAttribute == 1.25f);
    CHECK(back->rules[2].scaleAttribute == 2.0f);
    CHECK(back->expand().contentHash() == g.expand().contentHash());
    // The rotation round trip is within float precision, so the hash may differ by rounding;
    // everything else hashes identically.
    Grammar exact = *back;
    exact.rules[0].step.rotation = root.step.rotation;
    CHECK(exact.structuralHash() == g.structuralHash());

    // Missing members default; malformed values fail.
    auto minimal = Grammar::fromJson(nlohmann::json::object({{"axiom", "p"}, {"rules", {{{"name", "p"}}}}}));
    REQUIRE(minimal.has_value());
    CHECK(minimal->rules.size() == 1);
    CHECK(minimal->rules[0].op == GrammarOp::Place);
    CHECK(minimal->rules[0].count == 4);
    CHECK(minimal->maxDepth == 8);
    CHECK(minimal->validate().has_value());
    nlohmann::json bad = j;
    bad["rules"][0]["op"] = "spawn";
    CHECK_FALSE(Grammar::fromJson(bad).has_value());
    bad = j;
    bad["rules"][0]["children"] = "a";
    CHECK_FALSE(Grammar::fromJson(bad).has_value());
    bad = j;
    bad["rules"][0]["step"]["position"] = 3;
    CHECK_FALSE(Grammar::fromJson(bad).has_value());
    CHECK_FALSE(Grammar::fromJson(nlohmann::json::array()).has_value());

    // Hash sensitivity: every field, and the rule order.
    const std::uint64_t base = g.structuralHash();
    const auto changed = [&](auto mutate) {
        Grammar m = g;
        mutate(m);
        return m.structuralHash() != base;
    };
    CHECK(changed([](Grammar& m) { m.axiom = "a"; }));
    CHECK(changed([](Grammar& m) { m.maxDepth = 3; }));
    CHECK(changed([](Grammar& m) { m.maxInstances = 10; }));
    CHECK(changed([](Grammar& m) { m.seed = 1; }));
    CHECK(changed([](Grammar& m) { m.rules[0].op = GrammarOp::Branch; }));
    CHECK(changed([](Grammar& m) { m.rules[0].count = 8; }));
    CHECK(changed([](Grammar& m) { m.rules[0].step.position.x = 9.0f; }));
    CHECK(changed([](Grammar& m) { m.rules[0].step.scale.y = 9.0f; }));
    CHECK(changed([](Grammar& m) { m.rules[0].pre.position.z = 9.0f; }));
    CHECK(changed([](Grammar& m) { m.rules[0].children = {"b", "a"}; }));
    CHECK(changed([](Grammar& m) { m.rules[0].weights = {1.0f, 2.0f}; }));
    CHECK(changed([](Grammar& m) { m.rules[0].elseChildren = {"b"}; }));
    CHECK(changed([](Grammar& m) { m.rules[0].depthLimit = 5; }));
    CHECK(changed([](Grammar& m) { m.rules[0].mirrorAxis = {1.0f, 0.0f, 0.0f}; }));
    CHECK(changed([](Grammar& m) { m.rules[0].seed = 10; }));
    CHECK(changed([](Grammar& m) { m.rules[0].scaleAttribute = 1.0f; }));
    CHECK(changed([](Grammar& m) { m.rules[1].name = "c"; }));
    CHECK(changed([](Grammar& m) { std::swap(m.rules[1], m.rules[2]); }));
    CHECK(changed([](Grammar& m) { m.rules.pop_back(); }));
    CHECK_FALSE(changed([](Grammar&) {}));
}

TEST_CASE("Grammar distribution: the expansion places a procedural object's instances", "[scene][grammar][procedural]") {
    ProceduralGeometry g;
    g.name = "cathedral";
    g.distribution.kind = DistributionKind::Grammar;
    g.distribution.count = 1; // ignored for grammars
    g.grammar.axiom = "row";
    g.grammar.rules = {repeat("row", 4, {2.0f, 0.0f, 0.0f}, {"col"}), GrammarRule{}, place("p", 0.5f)};
    g.grammar.rules[1].name = "col";
    g.grammar.rules[1].op = GrammarOp::Repeat;
    g.grammar.rules[1].count = 3;
    g.grammar.rules[1].step.position = {0.0f, 1.0f, 0.0f};
    g.grammar.rules[1].children = {"p"};
    g.distributionTransform.position = {0.0f, 0.0f, 10.0f};
    REQUIRE(g.validate().has_value());

    const spatial::PointCloud c = g.generateCloud();
    REQUIRE(c.count() == 12);
    for (std::size_t i = 0; i < 12; ++i) {
        const auto k = static_cast<float>(i / 3);
        const auto m = static_cast<float>(i % 3);
        checkVec(c.positions()[i], glm::vec3(2.0f * k, m, 10.0f));
        checkVec(c.scales()[i], glm::vec3(0.5f));
        CHECK(c.ids()[i] == static_cast<std::int32_t>(i));
        CHECK(c.seeds()[i] == static_cast<std::int32_t>(g.variation.seed));
        CHECK(column(c, "depth")[i] == 2);
        CHECK(column(c, "rule")[i] == 2);
        CHECK(column(c, "branch")[i] == static_cast<std::int32_t>(i % 3));
    }
    CHECK(c.indices()[11] == 1.0f);

    REQUIRE(g.rebuild());
    CHECK(g.instances.size() == 12);
    CHECK_FALSE(g.rebuild());
    g.grammar.rules[0].count = 5; // the grammar is structural
    CHECK(g.rebuild());
    CHECK(g.instances.size() == 15);
    g.grammar.maxInstances = 7;
    CHECK(g.rebuild());
    CHECK(g.instances.size() == 7);

    // JSON carries the grammar only when it is used (or has rules).
    const nlohmann::json j = g.toJson();
    REQUIRE(j.contains("grammar"));
    CHECK(j.at("distribution").at("kind") == "grammar");
    auto back = ProceduralGeometry::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->distribution.kind == DistributionKind::Grammar);
    CHECK(back->grammar.structuralHash() == g.grammar.structuralHash());
    CHECK(back->generateCloud().contentHash() == g.generateCloud().contentHash());
    ProceduralGeometry plain;
    CHECK_FALSE(plain.toJson().contains("grammar"));
    // An invalid grammar fails the object's validation when it is the distribution.
    g.grammar.axiom = "nope";
    CHECK_FALSE(g.validate().has_value());
    nlohmann::json bad = j;
    bad["grammar"]["rules"][0]["children"] = {"missing"};
    CHECK_FALSE(ProceduralGeometry::fromJson(bad).has_value());
}
