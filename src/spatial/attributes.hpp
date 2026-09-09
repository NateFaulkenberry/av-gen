#pragma once

// Typed point attributes (ADR-024): structure-of-arrays columns addressed by name over a domain.
// Every column of a set has exactly `count()` elements; removing rows compacts every column.
// Values are plain: no references into columns survive a resize. Everything here is pure data
// with deterministic operations (no wall clock, no iteration-order dependence).

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace avgen::spatial {

enum class AttributeType : std::uint8_t { Bool, Int, Float, Vec2, Vec3, Vec4, Color };
[[nodiscard]] const char* attributeTypeName(AttributeType type);
[[nodiscard]] std::optional<AttributeType> attributeTypeFromName(std::string_view name);
[[nodiscard]] int attributeComponents(AttributeType type); // 1, 1, 1, 2, 3, 4, 4

enum class AttributeDomain : std::uint8_t { Point, Vertex, Primitive, Instance };
[[nodiscard]] const char* attributeDomainName(AttributeDomain domain);

// Storage for one column. `Color` is stored as vec4 (linear rgb + alpha) and only differs from
// Vec4 in its type tag (UI, serialisation, conversion rules).
using AttributeStorage = std::variant<std::vector<std::uint8_t>, std::vector<std::int32_t>, std::vector<float>,
                                      std::vector<glm::vec2>, std::vector<glm::vec3>, std::vector<glm::vec4>>;

struct AttributeBuffer {
    std::string name;
    AttributeType type = AttributeType::Float;
    AttributeStorage data; // size == owning set's count()
    [[nodiscard]] std::size_t size() const;
    void resize(std::size_t count); // new rows are zero
};

// Typed view of a column: T must match the storage of the column's type (Bool -> std::uint8_t,
// Int -> std::int32_t, Float -> float, Vec2/3/4 -> glm::vec*, Color -> glm::vec4).
template <typename T>
struct AttributeView {
    std::span<T> values;
    [[nodiscard]] std::size_t size() const { return values.size(); }
    T& operator[](std::size_t i) { return values[i]; }
    const T& operator[](std::size_t i) const { return values[i]; }
};

class AttributeSet {
public:
    explicit AttributeSet(AttributeDomain domain = AttributeDomain::Point) : domain_(domain) {}

    [[nodiscard]] AttributeDomain domain() const { return domain_; }
    [[nodiscard]] std::size_t count() const { return count_; }
    void resize(std::size_t count); // grows (zeros) or truncates every column
    void clear();                    // count 0, columns kept (empty)

    // Column management. `add` returns the existing column when the name exists with the same
    // type, fails when the name exists with another type. Names are case-sensitive.
    Result<AttributeBuffer*> add(std::string_view name, AttributeType type);
    bool remove(std::string_view name);
    [[nodiscard]] bool has(std::string_view name) const;
    [[nodiscard]] AttributeBuffer* find(std::string_view name);
    [[nodiscard]] const AttributeBuffer* find(std::string_view name) const;
    [[nodiscard]] std::optional<AttributeType> typeOf(std::string_view name) const;
    [[nodiscard]] const std::vector<AttributeBuffer>& buffers() const { return buffers_; }
    [[nodiscard]] std::vector<AttributeBuffer>& buffers() { return buffers_; }

    // Typed access; fails when the column is missing or T does not match its type.
    template <typename T>
    [[nodiscard]] Result<AttributeView<T>> view(std::string_view name);
    template <typename T>
    [[nodiscard]] Result<AttributeView<const T>> view(std::string_view name) const;
    // Convenience: get-or-add with a type, then view (fails on a type clash).
    template <typename T>
    [[nodiscard]] Result<AttributeView<T>> ensure(std::string_view name, AttributeType type);

    // Row operations, applied to every column together (deterministic, order-preserving).
    void keepRows(std::span<const std::uint8_t> keep);            // keep[i] != 0 keeps row i
    void keepIndices(std::span<const std::uint32_t> indices);      // rows in the given order (may duplicate)
    void append(const AttributeSet& other);                         // union of columns; missing values are zero
    void appendRows(const AttributeSet& other, std::span<const std::uint32_t> indices);

    // Structural hash of names/types/count and all values (for caches and determinism tests).
    [[nodiscard]] std::uint64_t contentHash() const;
    // JSON: {"domain": "point", "count": n, "attributes": [{"name", "type", "values": [...]}]}.
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<AttributeSet> fromJson(const nlohmann::json& j);

private:
    AttributeDomain domain_;
    std::size_t count_ = 0;
    std::vector<AttributeBuffer> buffers_;
};

// Generic scalar/vector conversion used by attribute ops and projections: reads any column as
// vec4 (bool/int/float broadcast to x; vec2/vec3 zero-padded) and writes back with truncation.
[[nodiscard]] glm::vec4 readAsVec4(const AttributeBuffer& buffer, std::size_t index);
void writeFromVec4(AttributeBuffer& buffer, std::size_t index, const glm::vec4& value);

// ---- attribute operations (ADR-024 §4) ----------------------------------------------------------

enum class AttributeOpKind : std::uint8_t {
    Set,        // target = value
    Add,        // target += value * (source or 1)
    Multiply,   // target *= value * (source or 1)
    Remap,      // target = remap(source, inMin..inMax -> outMin..outMax) [clamped when clamp]
    Clamp,      // target = clamp(source, outMin, outMax)
    Normalize,  // target = (source - min(source)) / (max - min) over the column
    Smooth,     // target = box-average of source over ±radius neighbouring rows (index order)
    Noise,      // target = mix(source, fbm(position * scale + offset), amount) (position from `positionAttribute`)
    Randomize,  // target = mix(source, value + hash(seed,id,channel) * range, amount)
    Lerp,       // target = mix(source, secondSource, amount)
    Fit,        // target = fit source range (min..max of column) into outMin..outMax
    Threshold,  // target = source >= value ? 1 : 0
    Compare,    // target = compare(source, value) (op: 0 <, 1 <=, 2 ==, 3 >=, 4 >, 5 !=)
};
[[nodiscard]] const char* attributeOpKindName(AttributeOpKind kind);
[[nodiscard]] std::optional<AttributeOpKind> attributeOpKindFromName(std::string_view name);

struct AttributeOp {
    AttributeOpKind kind = AttributeOpKind::Set;
    bool enabled = true;
    std::string target;                 // written column (created as Float when missing, or as
                                        // the source's type when there is a source)
    std::string source;                 // read column (defaults to target)
    std::string secondSource;           // Lerp
    std::string positionAttribute = "position"; // Noise
    glm::vec4 value{0.0f};              // Set/Add/Multiply/Randomize base/Threshold/Compare
    float amount = 1.0f;                // Noise/Randomize/Lerp blend
    float inMin = 0.0f, inMax = 1.0f;   // Remap
    float outMin = 0.0f, outMax = 1.0f; // Remap/Clamp/Fit
    bool clamp = true;                  // Remap
    float scale = 1.0f;                 // Noise spatial frequency
    glm::vec3 offset{0.0f};             // Noise
    glm::vec4 range{1.0f};              // Randomize
    std::uint32_t seed = 1;             // Noise/Randomize
    int radius = 1;                     // Smooth
    int compare = 3;                    // Compare operator
    std::string idAttribute = "id";     // Randomize hash key (row index when missing)
};
[[nodiscard]] Result<void> applyAttributeOp(AttributeSet& set, const AttributeOp& op);
[[nodiscard]] nlohmann::json attributeOpToJson(const AttributeOp& op);
[[nodiscard]] Result<AttributeOp> attributeOpFromJson(const nlohmann::json& j);

// Column statistics for the inspector (per component; count 0 gives zeros).
struct AttributeStats {
    glm::vec4 min{0.0f}, max{0.0f}, mean{0.0f};
};
[[nodiscard]] AttributeStats attributeStats(const AttributeBuffer& buffer);

// ---- template implementations -----------------------------------------------------------------

namespace detail {
template <typename T> struct StorageFor;
template <> struct StorageFor<std::uint8_t> { static constexpr AttributeType type = AttributeType::Bool; };
template <> struct StorageFor<std::int32_t> { static constexpr AttributeType type = AttributeType::Int; };
template <> struct StorageFor<float> { static constexpr AttributeType type = AttributeType::Float; };
template <> struct StorageFor<glm::vec2> { static constexpr AttributeType type = AttributeType::Vec2; };
template <> struct StorageFor<glm::vec3> { static constexpr AttributeType type = AttributeType::Vec3; };
template <> struct StorageFor<glm::vec4> { static constexpr AttributeType type = AttributeType::Vec4; };
[[nodiscard]] inline bool storageMatches(AttributeType want, AttributeType have) {
    if (want == have) return true;
    return want == AttributeType::Vec4 && have == AttributeType::Color; // Color views as vec4
}
} // namespace detail

template <typename T>
Result<AttributeView<T>> AttributeSet::view(std::string_view name) {
    AttributeBuffer* b = find(name);
    if (b == nullptr) {
        return fail("attribute '{}' not found", name);
    }
    if (!detail::storageMatches(detail::StorageFor<T>::type, b->type)) {
        return fail("attribute '{}' is {}, not {}", name, attributeTypeName(b->type),
                    attributeTypeName(detail::StorageFor<T>::type));
    }
    auto& vec = std::get<std::vector<T>>(b->data);
    return AttributeView<T>{std::span<T>(vec.data(), vec.size())};
}

template <typename T>
Result<AttributeView<const T>> AttributeSet::view(std::string_view name) const {
    const AttributeBuffer* b = find(name);
    if (b == nullptr) {
        return fail("attribute '{}' not found", name);
    }
    if (!detail::storageMatches(detail::StorageFor<T>::type, b->type)) {
        return fail("attribute '{}' is {}, not {}", name, attributeTypeName(b->type),
                    attributeTypeName(detail::StorageFor<T>::type));
    }
    const auto& vec = std::get<std::vector<T>>(b->data);
    return AttributeView<const T>{std::span<const T>(vec.data(), vec.size())};
}

template <typename T>
Result<AttributeView<T>> AttributeSet::ensure(std::string_view name, AttributeType type) {
    if (auto added = add(name, type); !added) {
        return fail(added.error().message);
    }
    return view<T>(name);
}

} // namespace avgen::spatial
