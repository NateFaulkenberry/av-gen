#pragma once

// Internal helpers shared by the graph/*.cpp translation units (not part of the public API):
// value conversions, the flattened multi-input encoding, parameter access that falls back to the
// registry's descriptors, and subgraph file resolution.

#include "graph/graph.hpp"
#include "spatial/detail.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::graph::detail {

using Fnv = spatial::detail::Fnv;

// ---- values ------------------------------------------------------------------------------------

// A value read as a number: up to four components with the component count that decides the
// result type of the math nodes (1 = float, 2 = vec2, 3 = vec3, 4 = colour).
struct Num {
    glm::vec4 v{0.0f};
    int components = 1;
};
[[nodiscard]] std::optional<Num> asNumber(const Value& value);
[[nodiscard]] Value numberValue(const Num& n);
[[nodiscard]] Num numberOf(const Value& value, float fallback = 0.0f); // 0 when not numeric

[[nodiscard]] float asFloat(const Value& value, float fallback = 0.0f);
[[nodiscard]] int asInt(const Value& value, int fallback = 0);
[[nodiscard]] bool asBool(const Value& value, bool fallback = false);
[[nodiscard]] glm::vec3 asVec3(const Value& value, const glm::vec3& fallback = glm::vec3(0.0f));
[[nodiscard]] glm::vec4 asVec4(const Value& value, const glm::vec4& fallback = glm::vec4(0.0f));
[[nodiscard]] std::string asString(const Value& value, const std::string& fallback = {});

[[nodiscard]] nlohmann::json valueToJson(const Value& value);

// ---- inputs ------------------------------------------------------------------------------------

// `inputs` is flattened in input-pin order: a single pin contributes exactly one value (the
// linked value, or the pin's default when unlinked); a multi pin contributes its linked values in
// link order followed by one std::monostate terminator. `Inputs` recovers the per-pin ranges.
class Inputs {
public:
    Inputs(const NodeTypeInfo& info, const std::vector<Value>& values);

    [[nodiscard]] const Value& single(std::string_view pin) const;
    [[nodiscard]] std::span<const Value> multi(std::string_view pin) const;
    // True when a single pin carries something (pins whose default is empty are "unlinked" when
    // this is false), or when a multi pin has at least one link.
    [[nodiscard]] bool has(std::string_view pin) const;

    [[nodiscard]] float f(std::string_view pin, float fallback = 0.0f) const;
    [[nodiscard]] int i(std::string_view pin, int fallback = 0) const;
    [[nodiscard]] bool b(std::string_view pin, bool fallback = false) const;
    [[nodiscard]] glm::vec3 v3(std::string_view pin, const glm::vec3& fallback = glm::vec3(0.0f)) const;
    [[nodiscard]] glm::vec4 v4(std::string_view pin, const glm::vec4& fallback = glm::vec4(0.0f)) const;
    [[nodiscard]] std::string s(std::string_view pin, const std::string& fallback = {}) const;

private:
    static const Value kEmpty;
    const NodeTypeInfo* info_ = nullptr;
    const std::vector<Value>* values_ = nullptr;
    std::vector<std::pair<std::size_t, std::size_t>> ranges_; // begin, count per input pin
};

// ---- parameters --------------------------------------------------------------------------------

// Reads a node's parameters, falling back to the registry descriptor's default when the key is
// absent or malformed. Enum parameters are ints with `choices` labels; a string that matches a
// label is accepted too (readable graph files).
class Params {
public:
    explicit Params(const Node& node);

    [[nodiscard]] bool has(std::string_view name) const;
    [[nodiscard]] float f(std::string_view name) const;
    [[nodiscard]] int i(std::string_view name) const;
    [[nodiscard]] std::uint32_t u32(std::string_view name) const;
    [[nodiscard]] bool b(std::string_view name) const;
    [[nodiscard]] glm::vec2 v2(std::string_view name) const;
    [[nodiscard]] glm::vec3 v3(std::string_view name) const;
    [[nodiscard]] glm::vec4 v4(std::string_view name) const;
    [[nodiscard]] std::string s(std::string_view name) const;
    [[nodiscard]] int enumOf(std::string_view name) const;        // int, or the index of a matching label
    [[nodiscard]] std::string label(std::string_view name) const; // the choice label of an enum parameter

private:
    [[nodiscard]] const nlohmann::json* raw(std::string_view name) const;
    [[nodiscard]] const ParamInfo* desc(std::string_view name) const;
    const Node* node_ = nullptr;
    const NodeTypeInfo* info_ = nullptr;
};

// ---- files -------------------------------------------------------------------------------------

// Subgraph references resolve against (1) the path as given, (2) the directory of the graph most
// recently loaded from a file (saved and restored around nested subgraph evaluation), (3) the
// directories last passed to scanGraphLibrary(); ".graph.json" is appended when the reference has
// no extension. Returns an empty path when nothing matches.
[[nodiscard]] std::filesystem::path resolveGraphFile(const std::string& reference);
void setGraphSourceDirectory(std::filesystem::path dir);
[[nodiscard]] std::filesystem::path graphSourceDirectory();
void setGraphLibraryDirectories(std::vector<std::filesystem::path> dirs);

// Renames everything an evaluation emitted with `prefix` and rewrites the references between the
// emitted objects (field names, spline names, material programs, route targets).
void prefixOutputNames(GraphOutput& output, const std::string& prefix);

} // namespace avgen::graph::detail
