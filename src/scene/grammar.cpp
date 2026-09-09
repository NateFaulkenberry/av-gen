// Compositional grammar (scene/grammar.hpp): validation, hashing, JSON and the expansion.
//
// Expansion semantics (the header fixes the meaning of every op; these are the remaining
// choices, all deterministic):
//
// * Expansion is depth-first in rule/child/repetition order. The axiom expands at depth 0 with
//   the identity transform; every child expands at depth + 1. A rule reached at depth > maxDepth
//   emits nothing (this is what terminates recursion through an ancestor). Every op applies its
//   `pre` transform once before doing anything else (T' = T * pre), including Place.
// * Repeat/Alternate: repetition k uses T_k = T' * step^k (T_0 = T', T_{k+1} = T_k * step, so
//   the step accumulates: offsets add, rotations compound, scales multiply). Repeat expands every
//   child at each T_k; Alternate expands child[k % n]. The repetition index k becomes `branch`
//   for everything emitted below it (until the next Repeat/Alternate).
// * Mirror expands its children twice: as they are, and reflected across the plane through the
//   *grammar origin* with unit normal `mirrorAxis`. The reflection applies to the whole mirrored
//   subtree (every emitted transform X becomes R X): position p -> p - 2 n (n . p); rotation by
//   conjugation with the reflection, R M R, which for a quaternion (w, v) is (w, 2 n (n . v) - v)
//   (the mirrored axis with the angle negated); scale stays positive (the source mesh is not
//   mirrored, only placed). Nested mirrors apply innermost first.
// * Choice picks child c with probability weight_c / sum (missing weights are 1; non-positive
//   weights are never picked; an all-zero sum means equal weights) from u = pcg3d(grammar.seed ^
//   rule.seed, depth, path).x / 2^32. `path` is the cumulative hash of the expansion path: it
//   starts at pcg3d(seed, 0x9E3779B9, 0).x for the axiom and, when descending into child index c
//   at repetition k (0 for ops without repetitions; 1 for the mirrored copy of a Mirror) of rule
//   r, becomes pcg3d(path, r * 65599 + c, k).x. The same subtree reached through different
//   branches or repetitions therefore makes different choices, and the whole expansion is a pure
//   function of the grammar.
// * Conditional expands `children` while depth < depthLimit, else `elseChildren`.
// * Place emits one point at T' with scale * scaleAttribute; id = emission order; attributes
//   depth (int), rule (int index into `rules`), branch (int). Emission stops at maxInstances and
//   the expansion also stops after 8 * maxInstances + 1024 rule visits (a guard against
//   grammars that visit enormous non-emitting subtrees); both truncate in expansion order.
// * The seed column is reseed(seed) (seed[i] = hash(seed, id[i])) and `index` is renumbered.

#include "scene/grammar.hpp"

#include "core/noise.hpp"
#include "scene/struct_hash.hpp"

#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace avgen::scene {

using nlohmann::json;

namespace {

constexpr int kGrammarMaxInstances = 1048576; // the 1M cap shared with procedural.cpp
constexpr int kGrammarMaxDepth = 64;

// ---- transforms --------------------------------------------------------------------------------

bool uniformScale(const glm::vec3& s) {
    return std::abs(s.x - s.y) < 1e-6f && std::abs(s.x - s.z) < 1e-6f;
}

// outer * inner (inner applied first): the same rule as procedural.cpp's compose.
Transform compose(const Transform& outer, const Transform& inner) {
    if (uniformScale(outer.scale)) {
        Transform t;
        t.position = outer.position + outer.rotation * (inner.position * outer.scale.x);
        t.rotation = outer.rotation * inner.rotation;
        t.scale = inner.scale * outer.scale.x;
        return t;
    }
    return Transform::fromMatrix(outer.matrix() * inner.matrix());
}

// Reflection across the plane through the origin with unit normal n: p -> p - 2 n (n . p) and
// q = (w, v) -> (w, 2 n (n . v) - v) (the conjugation R M R expressed on the quaternion).
Transform reflect(const Transform& t, const glm::vec3& n) {
    Transform out = t;
    out.position = t.position - 2.0f * n * glm::dot(n, t.position);
    const glm::vec3 v(t.rotation.x, t.rotation.y, t.rotation.z);
    const glm::vec3 rv = 2.0f * n * glm::dot(n, v) - v;
    out.rotation = glm::quat(t.rotation.w, rv.x, rv.y, rv.z);
    return out;
}

// Euler (degrees) <-> quaternion, the composition-node convention (glm::quat(vec3) = Rz*Ry*Rx).
glm::quat quatFromEulerDegrees(const glm::vec3& degrees) {
    return glm::quat(glm::radians(degrees));
}

glm::vec3 eulerDegrees(const glm::quat& q) {
    const glm::mat3 m = glm::mat3_cast(q);
    const float m00 = m[0][0];
    const float m10 = m[0][1];
    const float m20 = m[0][2];
    const float m01 = m[1][0];
    const float m11 = m[1][1];
    const float m21 = m[1][2];
    const float m22 = m[2][2];
    const float cy = std::sqrt(m00 * m00 + m10 * m10);
    const float y = std::atan2(-m20, cy);
    float x = 0.0f;
    float z = 0.0f;
    if (cy > 1e-6f) {
        x = std::atan2(m21, m22);
        z = std::atan2(m10, m00);
    } else {
        x = std::atan2(-m20 * m01, m11);
    }
    return glm::degrees(glm::vec3(x, y, z));
}

// ---- JSON helpers ------------------------------------------------------------------------------

json vecToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

json transformToJson(const Transform& t) {
    json j = json::object();
    j["position"] = vecToJson(t.position);
    j["rotation"] = vecToJson(eulerDegrees(t.rotation));
    j["scale"] = vecToJson(t.scale);
    return j;
}

Result<glm::vec3> readVec3(const json& j, const char* key, const glm::vec3& def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& a = j.at(key);
    if (!a.is_array() || a.size() != 3) {
        return fail("'{}' must be an array of 3 numbers", key);
    }
    glm::vec3 out{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!a.at(i).is_number()) {
            return fail("'{}' must be an array of 3 numbers", key);
        }
        out[static_cast<glm::length_t>(i)] = a.at(i).get<float>();
    }
    return out;
}

Result<float> readFloat(const json& j, const char* key, float def) {
    if (!j.contains(key)) {
        return def;
    }
    if (!j.at(key).is_number()) {
        return fail("'{}' must be a number", key);
    }
    return j.at(key).get<float>();
}

Result<int> readInt(const json& j, const char* key, int def) {
    if (!j.contains(key)) {
        return def;
    }
    if (!j.at(key).is_number_integer()) {
        return fail("'{}' must be an integer", key);
    }
    return j.at(key).get<int>();
}

Result<std::uint32_t> readU32(const json& j, const char* key, std::uint32_t def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_number_unsigned() && !(v.is_number_integer() && v.get<long long>() >= 0)) {
        return fail("'{}' must be a non-negative integer", key);
    }
    return v.get<std::uint32_t>();
}

Result<std::string> readString(const json& j, const char* key, const std::string& def) {
    if (!j.contains(key)) {
        return def;
    }
    if (!j.at(key).is_string()) {
        return fail("'{}' must be a string", key);
    }
    return j.at(key).get<std::string>();
}

Result<std::vector<std::string>> readStrings(const json& j, const char* key) {
    std::vector<std::string> out;
    if (!j.contains(key)) {
        return out;
    }
    const json& a = j.at(key);
    if (!a.is_array()) {
        return fail("'{}' must be an array of strings", key);
    }
    for (const json& e : a) {
        if (!e.is_string()) {
            return fail("'{}' must be an array of strings", key);
        }
        out.push_back(e.get<std::string>());
    }
    return out;
}

Result<std::vector<float>> readFloats(const json& j, const char* key) {
    std::vector<float> out;
    if (!j.contains(key)) {
        return out;
    }
    const json& a = j.at(key);
    if (!a.is_array()) {
        return fail("'{}' must be an array of numbers", key);
    }
    for (const json& e : a) {
        if (!e.is_number()) {
            return fail("'{}' must be an array of numbers", key);
        }
        out.push_back(e.get<float>());
    }
    return out;
}

Result<Transform> readTransform(const json& parent, const char* key, const Transform& def) {
    if (!parent.contains(key)) {
        return def;
    }
    const json& j = parent.at(key);
    if (!j.is_object()) {
        return fail("'{}' must be an object", key);
    }
    Transform t = def;
    auto position = readVec3(j, "position", t.position);
    if (!position) {
        return std::unexpected(position.error());
    }
    auto rotation = readVec3(j, "rotation", eulerDegrees(t.rotation));
    if (!rotation) {
        return std::unexpected(rotation.error());
    }
    auto scale = readVec3(j, "scale", t.scale);
    if (!scale) {
        return std::unexpected(scale.error());
    }
    t.position = *position;
    t.rotation = quatFromEulerDegrees(*rotation);
    t.scale = *scale;
    return t;
}

#define AVGEN_GRAMMAR_READ(target, key, reader)                                                        \
    do {                                                                                                \
        auto value_ = reader(j, key, target);                                                            \
        if (!value_) {                                                                                  \
            return std::unexpected(value_.error());                                                     \
        }                                                                                               \
        target = *value_;                                                                               \
    } while (false)

// ---- expansion ---------------------------------------------------------------------------------

std::uint32_t mixPath(std::uint32_t path, std::size_t rule, std::size_t child, std::uint32_t repetition) {
    return noise::pcg3d({path, static_cast<std::uint32_t>(rule) * 65599u + static_cast<std::uint32_t>(child), repetition}).x;
}

class Expander {
public:
    explicit Expander(const Grammar& g) : g_(g), budget_(static_cast<std::size_t>(g.maxInstances) * 8 + 1024) {
        for (std::size_t i = 0; i < g.rules.size(); ++i) {
            index_.emplace(g.rules[i].name, i);
        }
    }

    spatial::PointCloud run() {
        const auto axiom = index_.find(g_.axiom);
        if (axiom != index_.end() && g_.maxInstances > 0) {
            const std::uint32_t path = noise::pcg3d({g_.seed, 0x9E3779B9u, 0u}).x;
            expand(axiom->second, Transform{}, 0, 0, path);
        }
        return materialise();
    }

private:
    void expand(std::size_t ruleIndex, const Transform& t, int depth, int branch, std::uint32_t path) {
        if (done() || depth > g_.maxDepth) {
            return;
        }
        ++visits_;
        const GrammarRule& rule = g_.rules[ruleIndex];
        const Transform base = compose(t, rule.pre);
        switch (rule.op) {
        case GrammarOp::Place:
            emit(base, rule, depth, branch);
            break;
        case GrammarOp::Repeat: {
            Transform step = base;
            for (int k = 0; k < rule.count && !done(); ++k) {
                for (std::size_t c = 0; c < rule.children.size(); ++c) {
                    expandChild(rule.children[c], ruleIndex, c, static_cast<std::uint32_t>(k), step, depth + 1, k, path);
                }
                step = compose(step, rule.step);
            }
            break;
        }
        case GrammarOp::Alternate: {
            if (rule.children.empty()) {
                break;
            }
            Transform step = base;
            for (int k = 0; k < rule.count && !done(); ++k) {
                const std::size_t c = static_cast<std::size_t>(k) % rule.children.size();
                expandChild(rule.children[c], ruleIndex, c, static_cast<std::uint32_t>(k), step, depth + 1, k, path);
                step = compose(step, rule.step);
            }
            break;
        }
        case GrammarOp::Branch:
            for (std::size_t c = 0; c < rule.children.size(); ++c) {
                expandChild(rule.children[c], ruleIndex, c, 0u, base, depth + 1, branch, path);
            }
            break;
        case GrammarOp::Mirror: {
            for (std::size_t c = 0; c < rule.children.size(); ++c) {
                expandChild(rule.children[c], ruleIndex, c, 0u, base, depth + 1, branch, path);
            }
            const float len = glm::length(rule.mirrorAxis);
            const glm::vec3 n = len > 1e-8f ? rule.mirrorAxis / len : glm::vec3(1.0f, 0.0f, 0.0f);
            mirrors_.push_back(n);
            for (std::size_t c = 0; c < rule.children.size(); ++c) {
                expandChild(rule.children[c], ruleIndex, c, 1u, base, depth + 1, branch, path);
            }
            mirrors_.pop_back();
            break;
        }
        case GrammarOp::Choice: {
            if (rule.children.empty()) {
                break;
            }
            const std::size_t c = choose(rule, depth, path);
            expandChild(rule.children[c], ruleIndex, c, 0u, base, depth + 1, branch, path);
            break;
        }
        case GrammarOp::Conditional: {
            const std::vector<std::string>& list = depth < rule.depthLimit ? rule.children : rule.elseChildren;
            for (std::size_t c = 0; c < list.size(); ++c) {
                expandChild(list[c], ruleIndex, c, 0u, base, depth + 1, branch, path);
            }
            break;
        }
        }
    }

    void expandChild(const std::string& name, std::size_t parentRule, std::size_t childIndex, std::uint32_t repetition,
                     const Transform& t, int depth, int branch, std::uint32_t path) {
        const auto it = index_.find(name);
        if (it == index_.end()) {
            return; // validate() rejects this; expansion is lenient
        }
        expand(it->second, t, depth, branch, mixPath(path, parentRule, childIndex, repetition));
    }

    std::size_t choose(const GrammarRule& rule, int depth, std::uint32_t path) const {
        const std::size_t n = rule.children.size();
        const auto weight = [&](std::size_t c) {
            return c < rule.weights.size() ? std::max(rule.weights[c], 0.0f) : 1.0f;
        };
        double total = 0.0;
        for (std::size_t c = 0; c < n; ++c) {
            total += static_cast<double>(weight(c));
        }
        const bool equal = !(total > 0.0);
        if (equal) {
            total = static_cast<double>(n);
        }
        const std::uint32_t h = noise::pcg3d({g_.seed ^ rule.seed, static_cast<std::uint32_t>(depth), path}).x;
        const double u = static_cast<double>(h) * (1.0 / 4294967296.0) * total;
        double acc = 0.0;
        for (std::size_t c = 0; c < n; ++c) {
            acc += equal ? 1.0 : static_cast<double>(weight(c));
            if (u < acc) {
                return c;
            }
        }
        return n - 1;
    }

    void emit(const Transform& t, const GrammarRule& rule, int depth, int branch) {
        if (done()) {
            return;
        }
        Transform p = t;
        p.scale *= rule.scaleAttribute;
        for (auto it = mirrors_.rbegin(); it != mirrors_.rend(); ++it) {
            p = reflect(p, *it);
        }
        positions_.push_back(p.position);
        rotations_.emplace_back(p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w);
        scales_.push_back(p.scale);
        depths_.push_back(depth);
        rules_.push_back(static_cast<std::int32_t>(&rule - g_.rules.data()));
        branches_.push_back(branch);
    }

    [[nodiscard]] bool done() const {
        return positions_.size() >= static_cast<std::size_t>(std::min(g_.maxInstances, kGrammarMaxInstances)) ||
               visits_ >= budget_;
    }

    spatial::PointCloud materialise() {
        const std::size_t n = positions_.size();
        spatial::PointCloud out(n);
        std::copy(positions_.begin(), positions_.end(), out.positions().begin());
        std::copy(rotations_.begin(), rotations_.end(), out.rotations().begin());
        std::copy(scales_.begin(), scales_.end(), out.scales().begin());
        auto ids = out.ids();
        for (std::size_t i = 0; i < n; ++i) {
            ids[i] = static_cast<std::int32_t>(i);
        }
        const auto column = [&](std::string_view name, const std::vector<std::int32_t>& values) {
            auto view = out.attributes.ensure<std::int32_t>(name, spatial::AttributeType::Int);
            if (view) {
                std::copy(values.begin(), values.end(), view->values.begin());
            }
        };
        column("depth", depths_);
        column("rule", rules_);
        column("branch", branches_);
        out.reseed(g_.seed);
        out.renumberIndices();
        return out;
    }

    const Grammar& g_;
    std::size_t budget_;
    std::size_t visits_ = 0;
    std::unordered_map<std::string, std::size_t> index_;
    std::vector<glm::vec3> mirrors_;
    std::vector<glm::vec3> positions_;
    std::vector<glm::vec4> rotations_;
    std::vector<glm::vec3> scales_;
    std::vector<std::int32_t> depths_;
    std::vector<std::int32_t> rules_;
    std::vector<std::int32_t> branches_;
};

} // namespace

// ================================================================================================
// Names
// ================================================================================================

const char* grammarOpName(GrammarOp op) {
    switch (op) {
    case GrammarOp::Place:
        return "place";
    case GrammarOp::Repeat:
        return "repeat";
    case GrammarOp::Branch:
        return "branch";
    case GrammarOp::Alternate:
        return "alternate";
    case GrammarOp::Mirror:
        return "mirror";
    case GrammarOp::Choice:
        return "choice";
    case GrammarOp::Conditional:
        return "conditional";
    }
    return "place";
}

std::optional<GrammarOp> grammarOpFromName(std::string_view name) {
    for (const auto op : {GrammarOp::Place, GrammarOp::Repeat, GrammarOp::Branch, GrammarOp::Alternate, GrammarOp::Mirror,
                          GrammarOp::Choice, GrammarOp::Conditional}) {
        if (name == grammarOpName(op)) {
            return op;
        }
    }
    return std::nullopt;
}

// ================================================================================================
// Grammar
// ================================================================================================

const GrammarRule* Grammar::find(std::string_view name) const {
    for (const GrammarRule& r : rules) {
        if (r.name == name) {
            return &r;
        }
    }
    return nullptr;
}

Result<void> Grammar::validate() const {
    if (axiom.empty()) {
        return fail("grammar needs an axiom");
    }
    if (find(axiom) == nullptr) {
        return fail("grammar axiom '{}' is not a rule", axiom);
    }
    if (maxDepth < 0 || maxDepth > kGrammarMaxDepth) {
        return fail("grammar maxDepth must be in 0..{} (got {})", kGrammarMaxDepth, maxDepth);
    }
    if (maxInstances < 1 || maxInstances > kGrammarMaxInstances) {
        return fail("grammar maxInstances must be in 1..{} (got {})", kGrammarMaxInstances, maxInstances);
    }
    std::unordered_set<std::string_view> names;
    for (const GrammarRule& r : rules) {
        if (r.name.empty()) {
            return fail("grammar rules need names");
        }
        if (!names.insert(r.name).second) {
            return fail("grammar rule '{}' is defined twice", r.name);
        }
    }
    for (const GrammarRule& r : rules) {
        if (r.count < 0) {
            return fail("grammar rule '{}': count must be >= 0 (got {})", r.name, r.count);
        }
        if (r.depthLimit < 0) {
            return fail("grammar rule '{}': depthLimit must be >= 0 (got {})", r.name, r.depthLimit);
        }
        if (!(r.scaleAttribute > 0.0f)) {
            return fail("grammar rule '{}': scaleAttribute must be > 0", r.name);
        }
        if (r.op == GrammarOp::Mirror && glm::length(r.mirrorAxis) < 1e-8f) {
            return fail("grammar rule '{}': mirrorAxis must not be zero", r.name);
        }
        for (const float w : r.weights) {
            if (!(w >= 0.0f) || !std::isfinite(w)) {
                return fail("grammar rule '{}': weights must be finite and >= 0", r.name);
            }
        }
        for (const std::string& child : r.children) {
            if (find(child) == nullptr) {
                return fail("grammar rule '{}': child '{}' is not a rule", r.name, child);
            }
        }
        for (const std::string& child : r.elseChildren) {
            if (find(child) == nullptr) {
                return fail("grammar rule '{}': else child '{}' is not a rule", r.name, child);
            }
        }
    }
    return {};
}

std::uint64_t Grammar::structuralHash() const {
    detail::StructHash h;
    h.str(axiom);
    h.i32(maxDepth);
    h.i32(maxInstances);
    h.u32(seed);
    h.u64(rules.size());
    for (const GrammarRule& r : rules) { // rule order is part of the hash
        h.str(r.name);
        h.u32(static_cast<std::uint32_t>(r.op));
        h.i32(r.count);
        h.transform(r.step);
        h.transform(r.pre);
        h.u64(r.children.size());
        for (const std::string& c : r.children) {
            h.str(c);
        }
        h.u64(r.weights.size());
        for (const float w : r.weights) {
            h.f32(w);
        }
        h.u64(r.elseChildren.size());
        for (const std::string& c : r.elseChildren) {
            h.str(c);
        }
        h.i32(r.depthLimit);
        h.v3(r.mirrorAxis);
        h.u32(r.seed);
        h.f32(r.scaleAttribute);
    }
    return h.value();
}

spatial::PointCloud Grammar::expand() const {
    return Expander(*this).run();
}

json Grammar::toJson() const {
    json j = json::object();
    j["axiom"] = axiom;
    j["maxDepth"] = maxDepth;
    j["maxInstances"] = maxInstances;
    j["seed"] = seed;
    json arr = json::array();
    for (const GrammarRule& r : rules) {
        json s = json::object();
        s["name"] = r.name;
        s["op"] = grammarOpName(r.op);
        s["count"] = r.count;
        s["step"] = transformToJson(r.step);
        s["pre"] = transformToJson(r.pre);
        s["children"] = r.children;
        s["weights"] = r.weights;
        s["elseChildren"] = r.elseChildren;
        s["depthLimit"] = r.depthLimit;
        s["mirrorAxis"] = vecToJson(r.mirrorAxis);
        s["seed"] = r.seed;
        s["scaleAttribute"] = r.scaleAttribute;
        arr.push_back(std::move(s));
    }
    j["rules"] = std::move(arr);
    return j;
}

Result<Grammar> Grammar::fromJson(const json& root) {
    if (!root.is_object()) {
        return fail("grammar must be a JSON object");
    }
    Grammar g;
    {
        const json& j = root;
        AVGEN_GRAMMAR_READ(g.axiom, "axiom", readString);
        AVGEN_GRAMMAR_READ(g.maxDepth, "maxDepth", readInt);
        AVGEN_GRAMMAR_READ(g.maxInstances, "maxInstances", readInt);
        AVGEN_GRAMMAR_READ(g.seed, "seed", readU32);
    }
    if (root.contains("rules")) {
        const json& arr = root.at("rules");
        if (!arr.is_array()) {
            return fail("'rules' must be an array");
        }
        for (const json& j : arr) {
            if (!j.is_object()) {
                return fail("each grammar rule must be an object");
            }
            GrammarRule r;
            AVGEN_GRAMMAR_READ(r.name, "name", readString);
            if (j.contains("op")) {
                auto name = readString(j, "op", "");
                if (!name) {
                    return std::unexpected(name.error());
                }
                const auto op = grammarOpFromName(*name);
                if (!op) {
                    return fail("unknown grammar op '{}'", *name);
                }
                r.op = *op;
            }
            AVGEN_GRAMMAR_READ(r.count, "count", readInt);
            AVGEN_GRAMMAR_READ(r.step, "step", readTransform);
            AVGEN_GRAMMAR_READ(r.pre, "pre", readTransform);
            auto children = readStrings(j, "children");
            if (!children) {
                return std::unexpected(children.error());
            }
            r.children = std::move(*children);
            auto weights = readFloats(j, "weights");
            if (!weights) {
                return std::unexpected(weights.error());
            }
            r.weights = std::move(*weights);
            auto elseChildren = readStrings(j, "elseChildren");
            if (!elseChildren) {
                return std::unexpected(elseChildren.error());
            }
            r.elseChildren = std::move(*elseChildren);
            AVGEN_GRAMMAR_READ(r.depthLimit, "depthLimit", readInt);
            AVGEN_GRAMMAR_READ(r.mirrorAxis, "mirrorAxis", readVec3);
            AVGEN_GRAMMAR_READ(r.seed, "seed", readU32);
            AVGEN_GRAMMAR_READ(r.scaleAttribute, "scaleAttribute", readFloat);
            g.rules.push_back(std::move(r));
        }
    }
    return g;
}

#undef AVGEN_GRAMMAR_READ

} // namespace avgen::scene
