// Typed point attributes (ADR-024): SoA columns, row operations, attribute ops, stats, JSON.
//
// Conventions chosen here:
// * readAsVec4 broadcasts bool/int/float into x (y, z, w = 0) and zero-pads vec2/vec3;
//   writeFromVec4 truncates: Bool = x != 0, Int = trunc(x) (toward zero), vector types drop the
//   trailing components.
// * Attribute ops act on the target's components (1..4). Set/Add/Multiply do not need a source
//   column: `source` only scales Add/Multiply when it names an existing column. Every other op
//   reads `source` (defaulting to the target) and fails when it is missing.
// * Noise broadcasts one fbm3 sample (of the row's position * scale + offset, seed) to every
//   component; Randomize draws hashIndex(seed, id, component) per component (ids from
//   `idAttribute` when it is an Int column, else the row index).
// * Smooth averages the available rows in [i - radius, i + radius] (windows shrink at the ends).
// * Normalize/Fit with a constant column write 0 / outMin.
// * contentHash is FNV-1a over the domain, the count and every column's name, type and raw bytes.

#include "spatial/attributes.hpp"

#include "core/noise.hpp"
#include "spatial/detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace avgen::spatial {

using nlohmann::json;

namespace {

AttributeStorage makeStorage(AttributeType type, std::size_t count) {
    switch (type) {
    case AttributeType::Bool:
        return std::vector<std::uint8_t>(count, 0u);
    case AttributeType::Int:
        return std::vector<std::int32_t>(count, 0);
    case AttributeType::Float:
        return std::vector<float>(count, 0.0f);
    case AttributeType::Vec2:
        return std::vector<glm::vec2>(count, glm::vec2(0.0f));
    case AttributeType::Vec3:
        return std::vector<glm::vec3>(count, glm::vec3(0.0f));
    case AttributeType::Vec4:
    case AttributeType::Color:
        return std::vector<glm::vec4>(count, glm::vec4(0.0f));
    }
    return std::vector<float>(count, 0.0f);
}

// Applies `fn(vector)` to whichever vector the storage holds.
template <typename Fn>
decltype(auto) withVector(AttributeStorage& storage, Fn&& fn) {
    return std::visit([&](auto& vec) -> decltype(auto) { return fn(vec); }, storage);
}
template <typename Fn>
decltype(auto) withVector(const AttributeStorage& storage, Fn&& fn) {
    return std::visit([&](const auto& vec) -> decltype(auto) { return fn(vec); }, storage);
}

float compareValues(float a, float b, int op) {
    switch (op) {
    case 0:
        return a < b ? 1.0f : 0.0f;
    case 1:
        return a <= b ? 1.0f : 0.0f;
    case 2:
        return a == b ? 1.0f : 0.0f;
    case 4:
        return a > b ? 1.0f : 0.0f;
    case 5:
        return a != b ? 1.0f : 0.0f;
    case 3:
    default:
        return a >= b ? 1.0f : 0.0f;
    }
}

std::int32_t rowId(const AttributeBuffer* ids, std::size_t row) {
    if (ids != nullptr && ids->type == AttributeType::Int) {
        return std::get<std::vector<std::int32_t>>(ids->data)[row];
    }
    return static_cast<std::int32_t>(row);
}

} // namespace

// ================================================================================================
// Names
// ================================================================================================

const char* attributeTypeName(AttributeType type) {
    switch (type) {
    case AttributeType::Bool:
        return "bool";
    case AttributeType::Int:
        return "int";
    case AttributeType::Float:
        return "float";
    case AttributeType::Vec2:
        return "vec2";
    case AttributeType::Vec3:
        return "vec3";
    case AttributeType::Vec4:
        return "vec4";
    case AttributeType::Color:
        return "color";
    }
    return "float";
}

std::optional<AttributeType> attributeTypeFromName(std::string_view name) {
    for (const auto type : {AttributeType::Bool, AttributeType::Int, AttributeType::Float, AttributeType::Vec2,
                            AttributeType::Vec3, AttributeType::Vec4, AttributeType::Color}) {
        if (name == attributeTypeName(type)) {
            return type;
        }
    }
    return std::nullopt;
}

int attributeComponents(AttributeType type) {
    switch (type) {
    case AttributeType::Bool:
    case AttributeType::Int:
    case AttributeType::Float:
        return 1;
    case AttributeType::Vec2:
        return 2;
    case AttributeType::Vec3:
        return 3;
    case AttributeType::Vec4:
    case AttributeType::Color:
        return 4;
    }
    return 1;
}

const char* attributeDomainName(AttributeDomain domain) {
    switch (domain) {
    case AttributeDomain::Point:
        return "point";
    case AttributeDomain::Vertex:
        return "vertex";
    case AttributeDomain::Primitive:
        return "primitive";
    case AttributeDomain::Instance:
        return "instance";
    }
    return "point";
}

namespace {
std::optional<AttributeDomain> attributeDomainFromName(std::string_view name) {
    for (const auto domain :
         {AttributeDomain::Point, AttributeDomain::Vertex, AttributeDomain::Primitive, AttributeDomain::Instance}) {
        if (name == attributeDomainName(domain)) {
            return domain;
        }
    }
    return std::nullopt;
}
} // namespace

const char* attributeOpKindName(AttributeOpKind kind) {
    switch (kind) {
    case AttributeOpKind::Set:
        return "set";
    case AttributeOpKind::Add:
        return "add";
    case AttributeOpKind::Multiply:
        return "multiply";
    case AttributeOpKind::Remap:
        return "remap";
    case AttributeOpKind::Clamp:
        return "clamp";
    case AttributeOpKind::Normalize:
        return "normalize";
    case AttributeOpKind::Smooth:
        return "smooth";
    case AttributeOpKind::Noise:
        return "noise";
    case AttributeOpKind::Randomize:
        return "randomize";
    case AttributeOpKind::Lerp:
        return "lerp";
    case AttributeOpKind::Fit:
        return "fit";
    case AttributeOpKind::Threshold:
        return "threshold";
    case AttributeOpKind::Compare:
        return "compare";
    }
    return "set";
}

std::optional<AttributeOpKind> attributeOpKindFromName(std::string_view name) {
    for (const auto kind : {AttributeOpKind::Set, AttributeOpKind::Add, AttributeOpKind::Multiply, AttributeOpKind::Remap,
                            AttributeOpKind::Clamp, AttributeOpKind::Normalize, AttributeOpKind::Smooth,
                            AttributeOpKind::Noise, AttributeOpKind::Randomize, AttributeOpKind::Lerp,
                            AttributeOpKind::Fit, AttributeOpKind::Threshold, AttributeOpKind::Compare}) {
        if (name == attributeOpKindName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

// ================================================================================================
// AttributeBuffer
// ================================================================================================

std::size_t AttributeBuffer::size() const {
    return withVector(data, [](const auto& vec) { return vec.size(); });
}

void AttributeBuffer::resize(std::size_t count) {
    withVector(data, [count](auto& vec) { vec.resize(count); });
}

// ================================================================================================
// AttributeSet
// ================================================================================================

void AttributeSet::resize(std::size_t count) {
    count_ = count;
    for (AttributeBuffer& b : buffers_) {
        b.resize(count);
    }
}

void AttributeSet::clear() {
    resize(0);
}

Result<AttributeBuffer*> AttributeSet::add(std::string_view name, AttributeType type) {
    if (name.empty()) {
        return fail("attribute name must not be empty");
    }
    if (AttributeBuffer* existing = find(name)) {
        if (existing->type != type) {
            return fail("attribute '{}' already exists as {} (requested {})", name, attributeTypeName(existing->type),
                        attributeTypeName(type));
        }
        return existing;
    }
    AttributeBuffer b;
    b.name = std::string(name);
    b.type = type;
    b.data = makeStorage(type, count_);
    buffers_.push_back(std::move(b));
    return &buffers_.back();
}

bool AttributeSet::remove(std::string_view name) {
    const auto it = std::find_if(buffers_.begin(), buffers_.end(), [&](const AttributeBuffer& b) { return b.name == name; });
    if (it == buffers_.end()) {
        return false;
    }
    buffers_.erase(it);
    return true;
}

bool AttributeSet::has(std::string_view name) const {
    return find(name) != nullptr;
}

AttributeBuffer* AttributeSet::find(std::string_view name) {
    for (AttributeBuffer& b : buffers_) {
        if (b.name == name) {
            return &b;
        }
    }
    return nullptr;
}

const AttributeBuffer* AttributeSet::find(std::string_view name) const {
    for (const AttributeBuffer& b : buffers_) {
        if (b.name == name) {
            return &b;
        }
    }
    return nullptr;
}

std::optional<AttributeType> AttributeSet::typeOf(std::string_view name) const {
    const AttributeBuffer* b = find(name);
    return b != nullptr ? std::optional<AttributeType>(b->type) : std::nullopt;
}

void AttributeSet::keepRows(std::span<const std::uint8_t> keep) {
    std::vector<std::uint32_t> indices;
    const std::size_t n = std::min(keep.size(), count_);
    indices.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (keep[i] != 0) {
            indices.push_back(static_cast<std::uint32_t>(i));
        }
    }
    keepIndices(indices);
}

void AttributeSet::keepIndices(std::span<const std::uint32_t> indices) {
    for (AttributeBuffer& b : buffers_) {
        withVector(b.data, [&](auto& vec) {
            using Vec = std::remove_reference_t<decltype(vec)>;
            Vec out;
            out.reserve(indices.size());
            for (const std::uint32_t i : indices) {
                out.push_back(i < vec.size() ? vec[i] : typename Vec::value_type{});
            }
            vec = std::move(out);
        });
    }
    count_ = indices.size();
}

void AttributeSet::append(const AttributeSet& other) {
    std::vector<std::uint32_t> all(other.count());
    for (std::size_t i = 0; i < all.size(); ++i) {
        all[i] = static_cast<std::uint32_t>(i);
    }
    appendRows(other, all);
}

void AttributeSet::appendRows(const AttributeSet& other, std::span<const std::uint32_t> indices) {
    // Union of columns: columns only in `other` are created (zero for the existing rows). A name
    // clash with a different type keeps this set's column and pads it with zeros.
    for (const AttributeBuffer& ob : other.buffers_) {
        if (find(ob.name) == nullptr) {
            (void)add(ob.name, ob.type);
        }
    }
    const std::size_t newCount = count_ + indices.size();
    for (AttributeBuffer& b : buffers_) {
        const AttributeBuffer* ob = other.find(b.name);
        withVector(b.data, [&](auto& vec) {
            using Vec = std::remove_reference_t<decltype(vec)>;
            vec.reserve(newCount);
            const Vec* src = (ob != nullptr && ob->type == b.type) ? &std::get<Vec>(ob->data) : nullptr;
            for (const std::uint32_t i : indices) {
                vec.push_back(src != nullptr && i < src->size() ? (*src)[i] : typename Vec::value_type{});
            }
        });
    }
    count_ = newCount;
}

std::uint64_t AttributeSet::contentHash() const {
    detail::Fnv h;
    h.u8(static_cast<std::uint8_t>(domain_));
    h.u64(count_);
    for (const AttributeBuffer& b : buffers_) {
        h.str(b.name);
        h.u8(static_cast<std::uint8_t>(b.type));
        withVector(b.data, [&](const auto& vec) {
            using T = typename std::remove_reference_t<decltype(vec)>::value_type;
            h.bytes(vec.data(), vec.size() * sizeof(T));
        });
    }
    return h.value();
}

json AttributeSet::toJson() const {
    json j = json::object();
    j["domain"] = attributeDomainName(domain_);
    j["count"] = count_;
    json arr = json::array();
    for (const AttributeBuffer& b : buffers_) {
        json a = json::object();
        a["name"] = b.name;
        a["type"] = attributeTypeName(b.type);
        json values = json::array();
        const int comps = attributeComponents(b.type);
        for (std::size_t i = 0; i < b.size(); ++i) {
            if (b.type == AttributeType::Bool) {
                values.push_back(static_cast<int>(std::get<std::vector<std::uint8_t>>(b.data)[i]));
            } else if (b.type == AttributeType::Int) {
                values.push_back(std::get<std::vector<std::int32_t>>(b.data)[i]);
            } else {
                const glm::vec4 v = readAsVec4(b, i);
                for (int c = 0; c < comps; ++c) {
                    values.push_back(v[c]);
                }
            }
        }
        a["values"] = std::move(values);
        arr.push_back(std::move(a));
    }
    j["attributes"] = std::move(arr);
    return j;
}

Result<AttributeSet> AttributeSet::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("attribute set must be a JSON object");
    }
    AttributeDomain domain = AttributeDomain::Point;
    AVGEN_SPATIAL_READ_ENUM(domain, "domain", &attributeDomainFromName, "attribute domain");
    AttributeSet set(domain);
    int count = 0;
    AVGEN_SPATIAL_READ(count, "count", detail::readInt);
    if (count < 0) {
        return fail("'count' must be >= 0");
    }
    set.resize(static_cast<std::size_t>(count));
    if (!j.contains("attributes")) {
        return set;
    }
    const json& arr = j.at("attributes");
    if (!arr.is_array()) {
        return fail("'attributes' must be an array");
    }
    for (const json& a : arr) {
        if (!a.is_object()) {
            return fail("each attribute must be an object");
        }
        auto name = detail::readString(a, "name", "");
        if (!name) {
            return std::unexpected(name.error());
        }
        AttributeType type = AttributeType::Float;
        if (auto ok = detail::readEnum(a, "type", type, &attributeTypeFromName, "attribute type"); !ok) {
            return std::unexpected(ok.error());
        }
        auto added = set.add(*name, type);
        if (!added) {
            return std::unexpected(added.error());
        }
        AttributeBuffer& b = **added;
        if (!a.contains("values")) {
            continue;
        }
        const json& values = a.at("values");
        const auto comps = static_cast<std::size_t>(attributeComponents(type));
        if (!values.is_array() || values.size() != comps * static_cast<std::size_t>(count)) {
            return fail("attribute '{}': 'values' must be an array of {} numbers", *name, comps * static_cast<std::size_t>(count));
        }
        for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
            glm::vec4 v(0.0f);
            for (std::size_t c = 0; c < comps; ++c) {
                const json& e = values.at(i * comps + c);
                if (!e.is_number()) {
                    return fail("attribute '{}': values must be numbers", *name);
                }
                v[static_cast<glm::length_t>(c)] = e.get<float>();
            }
            if (type == AttributeType::Int) {
                std::get<std::vector<std::int32_t>>(b.data)[i] = values.at(i).get<std::int32_t>();
            } else {
                writeFromVec4(b, i, v);
            }
        }
    }
    return set;
}

// ================================================================================================
// Generic access
// ================================================================================================

glm::vec4 readAsVec4(const AttributeBuffer& buffer, std::size_t index) {
    switch (buffer.type) {
    case AttributeType::Bool:
        return glm::vec4(std::get<std::vector<std::uint8_t>>(buffer.data)[index] != 0 ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    case AttributeType::Int:
        return glm::vec4(static_cast<float>(std::get<std::vector<std::int32_t>>(buffer.data)[index]), 0.0f, 0.0f, 0.0f);
    case AttributeType::Float:
        return glm::vec4(std::get<std::vector<float>>(buffer.data)[index], 0.0f, 0.0f, 0.0f);
    case AttributeType::Vec2:
        return glm::vec4(std::get<std::vector<glm::vec2>>(buffer.data)[index], 0.0f, 0.0f);
    case AttributeType::Vec3:
        return glm::vec4(std::get<std::vector<glm::vec3>>(buffer.data)[index], 0.0f);
    case AttributeType::Vec4:
    case AttributeType::Color:
        return std::get<std::vector<glm::vec4>>(buffer.data)[index];
    }
    return glm::vec4(0.0f);
}

void writeFromVec4(AttributeBuffer& buffer, std::size_t index, const glm::vec4& value) {
    switch (buffer.type) {
    case AttributeType::Bool:
        std::get<std::vector<std::uint8_t>>(buffer.data)[index] = value.x != 0.0f ? 1u : 0u;
        return;
    case AttributeType::Int:
        std::get<std::vector<std::int32_t>>(buffer.data)[index] = static_cast<std::int32_t>(value.x);
        return;
    case AttributeType::Float:
        std::get<std::vector<float>>(buffer.data)[index] = value.x;
        return;
    case AttributeType::Vec2:
        std::get<std::vector<glm::vec2>>(buffer.data)[index] = glm::vec2(value);
        return;
    case AttributeType::Vec3:
        std::get<std::vector<glm::vec3>>(buffer.data)[index] = glm::vec3(value);
        return;
    case AttributeType::Vec4:
    case AttributeType::Color:
        std::get<std::vector<glm::vec4>>(buffer.data)[index] = value;
        return;
    }
}

// ================================================================================================
// Attribute ops
// ================================================================================================

namespace {

// Per-component min/max over a column (count 0 gives zeros).
void columnRange(const AttributeBuffer& b, glm::vec4& lo, glm::vec4& hi) {
    const std::size_t n = b.size();
    if (n == 0) {
        lo = hi = glm::vec4(0.0f);
        return;
    }
    lo = glm::vec4(std::numeric_limits<float>::max());
    hi = glm::vec4(std::numeric_limits<float>::lowest());
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec4 v = readAsVec4(b, i);
        lo = glm::min(lo, v);
        hi = glm::max(hi, v);
    }
}

glm::vec4 safeDivide(const glm::vec4& num, const glm::vec4& den) {
    glm::vec4 out(0.0f);
    for (int c = 0; c < 4; ++c) {
        out[c] = den[c] != 0.0f ? num[c] / den[c] : 0.0f;
    }
    return out;
}

} // namespace

Result<void> applyAttributeOp(AttributeSet& set, const AttributeOp& op) {
    if (!op.enabled) {
        return {};
    }
    if (op.target.empty()) {
        return fail("attribute op '{}' needs a target", attributeOpKindName(op.kind));
    }
    const std::string sourceName = op.source.empty() ? op.target : op.source;
    const bool needsSource = op.kind != AttributeOpKind::Set && op.kind != AttributeOpKind::Add &&
                             op.kind != AttributeOpKind::Multiply;
    if (needsSource && !set.has(sourceName)) {
        return fail("attribute op '{}': source attribute '{}' not found", attributeOpKindName(op.kind), sourceName);
    }
    if (op.kind == AttributeOpKind::Lerp && !set.has(op.secondSource)) {
        return fail("attribute op 'lerp': second source attribute '{}' not found", op.secondSource);
    }
    if (op.kind == AttributeOpKind::Noise && !set.has(op.positionAttribute)) {
        return fail("attribute op 'noise': position attribute '{}' not found", op.positionAttribute);
    }

    // Create the target when missing (source's type, else Float).
    if (!set.has(op.target)) {
        const AttributeBuffer* src = set.find(sourceName);
        auto added = set.add(op.target, src != nullptr ? src->type : AttributeType::Float);
        if (!added) {
            return std::unexpected(added.error());
        }
    }

    // Snapshot the source column so in-place ops (source == target) and window ops read stable data.
    const std::size_t n = set.count();
    std::vector<glm::vec4> source;
    if (const AttributeBuffer* src = set.find(sourceName); src != nullptr) {
        source.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            source[i] = readAsVec4(*src, i);
        }
    }
    const bool hasExplicitSource = !op.source.empty() && !source.empty();
    const AttributeBuffer* ids = set.find(op.idAttribute);
    const AttributeBuffer* second = op.kind == AttributeOpKind::Lerp ? set.find(op.secondSource) : nullptr;
    const AttributeBuffer* positions = op.kind == AttributeOpKind::Noise ? set.find(op.positionAttribute) : nullptr;
    AttributeBuffer& target = *set.find(op.target);

    glm::vec4 lo(0.0f);
    glm::vec4 hi(0.0f);
    if (op.kind == AttributeOpKind::Normalize || op.kind == AttributeOpKind::Fit) {
        columnRange(*set.find(sourceName), lo, hi);
    }

    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec4 s = source.empty() ? glm::vec4(0.0f) : source[i];
        const glm::vec4 existing = readAsVec4(target, i);
        glm::vec4 out = existing;
        switch (op.kind) {
        case AttributeOpKind::Set:
            out = op.value;
            break;
        case AttributeOpKind::Add:
            out = existing + op.value * (hasExplicitSource ? s : glm::vec4(1.0f));
            break;
        case AttributeOpKind::Multiply:
            out = existing * op.value * (hasExplicitSource ? s : glm::vec4(1.0f));
            break;
        case AttributeOpKind::Remap: {
            const float span = op.inMax - op.inMin;
            for (int c = 0; c < 4; ++c) {
                float t = span != 0.0f ? (s[c] - op.inMin) / span : 0.0f;
                if (op.clamp) {
                    t = std::clamp(t, 0.0f, 1.0f);
                }
                out[c] = op.outMin + (op.outMax - op.outMin) * t;
            }
            break;
        }
        case AttributeOpKind::Clamp:
            out = glm::clamp(s, glm::vec4(op.outMin), glm::vec4(op.outMax));
            break;
        case AttributeOpKind::Normalize:
            out = safeDivide(s - lo, hi - lo);
            break;
        case AttributeOpKind::Smooth: {
            const auto radius = static_cast<std::size_t>(std::max(op.radius, 0));
            const std::size_t from = i >= radius ? i - radius : 0;
            const std::size_t to = std::min(n - 1, i + radius);
            glm::vec4 sum(0.0f);
            for (std::size_t k = from; k <= to; ++k) {
                sum += source[k];
            }
            out = sum / static_cast<float>(to - from + 1);
            break;
        }
        case AttributeOpKind::Noise: {
            const glm::vec3 p = glm::vec3(readAsVec4(*positions, i));
            const float value = noise::fbm3(p * op.scale + op.offset, op.seed);
            out = glm::mix(s, glm::vec4(value), op.amount);
            break;
        }
        case AttributeOpKind::Randomize: {
            const auto id = static_cast<std::uint32_t>(rowId(ids, i));
            glm::vec4 r(0.0f);
            for (int c = 0; c < 4; ++c) {
                r[c] = op.value[c] + noise::hashIndex(op.seed, id, static_cast<std::uint32_t>(c)) * op.range[c];
            }
            out = glm::mix(s, r, op.amount);
            break;
        }
        case AttributeOpKind::Lerp:
            out = glm::mix(s, readAsVec4(*second, i), op.amount);
            break;
        case AttributeOpKind::Fit:
            out = glm::vec4(op.outMin) + safeDivide(s - lo, hi - lo) * (op.outMax - op.outMin);
            break;
        case AttributeOpKind::Threshold:
            for (int c = 0; c < 4; ++c) {
                out[c] = s[c] >= op.value[c] ? 1.0f : 0.0f;
            }
            break;
        case AttributeOpKind::Compare:
            for (int c = 0; c < 4; ++c) {
                out[c] = compareValues(s[c], op.value[c], op.compare);
            }
            break;
        }
        writeFromVec4(target, i, out);
    }
    return {};
}

json attributeOpToJson(const AttributeOp& op) {
    const AttributeOp def;
    json j = json::object();
    j["kind"] = attributeOpKindName(op.kind);
    if (op.enabled != def.enabled) {
        j["enabled"] = op.enabled;
    }
    j["target"] = op.target;
    if (op.source != def.source) {
        j["source"] = op.source;
    }
    if (op.secondSource != def.secondSource) {
        j["secondSource"] = op.secondSource;
    }
    if (op.positionAttribute != def.positionAttribute) {
        j["positionAttribute"] = op.positionAttribute;
    }
    if (op.value != def.value) {
        j["value"] = detail::vecToJson(op.value);
    }
    if (op.amount != def.amount) {
        j["amount"] = op.amount;
    }
    if (op.inMin != def.inMin) {
        j["inMin"] = op.inMin;
    }
    if (op.inMax != def.inMax) {
        j["inMax"] = op.inMax;
    }
    if (op.outMin != def.outMin) {
        j["outMin"] = op.outMin;
    }
    if (op.outMax != def.outMax) {
        j["outMax"] = op.outMax;
    }
    if (op.clamp != def.clamp) {
        j["clamp"] = op.clamp;
    }
    if (op.scale != def.scale) {
        j["scale"] = op.scale;
    }
    if (op.offset != def.offset) {
        j["offset"] = detail::vecToJson(op.offset);
    }
    if (op.range != def.range) {
        j["range"] = detail::vecToJson(op.range);
    }
    if (op.seed != def.seed) {
        j["seed"] = op.seed;
    }
    if (op.radius != def.radius) {
        j["radius"] = op.radius;
    }
    if (op.compare != def.compare) {
        j["compare"] = op.compare;
    }
    if (op.idAttribute != def.idAttribute) {
        j["idAttribute"] = op.idAttribute;
    }
    return j;
}

Result<AttributeOp> attributeOpFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("attribute op must be a JSON object");
    }
    if (!j.contains("kind")) {
        return fail("attribute op needs a 'kind'");
    }
    AttributeOp op;
    AVGEN_SPATIAL_READ_ENUM(op.kind, "kind", &attributeOpKindFromName, "attribute op kind");
    AVGEN_SPATIAL_READ(op.enabled, "enabled", detail::readBool);
    AVGEN_SPATIAL_READ(op.target, "target", detail::readString);
    AVGEN_SPATIAL_READ(op.source, "source", detail::readString);
    AVGEN_SPATIAL_READ(op.secondSource, "secondSource", detail::readString);
    AVGEN_SPATIAL_READ(op.positionAttribute, "positionAttribute", detail::readString);
    AVGEN_SPATIAL_READ(op.value, "value", detail::readVec4);
    AVGEN_SPATIAL_READ(op.amount, "amount", detail::readFloat);
    AVGEN_SPATIAL_READ(op.inMin, "inMin", detail::readFloat);
    AVGEN_SPATIAL_READ(op.inMax, "inMax", detail::readFloat);
    AVGEN_SPATIAL_READ(op.outMin, "outMin", detail::readFloat);
    AVGEN_SPATIAL_READ(op.outMax, "outMax", detail::readFloat);
    AVGEN_SPATIAL_READ(op.clamp, "clamp", detail::readBool);
    AVGEN_SPATIAL_READ(op.scale, "scale", detail::readFloat);
    AVGEN_SPATIAL_READ(op.offset, "offset", detail::readVec3);
    AVGEN_SPATIAL_READ(op.range, "range", detail::readVec4);
    AVGEN_SPATIAL_READ(op.seed, "seed", detail::readU32);
    AVGEN_SPATIAL_READ(op.radius, "radius", detail::readInt);
    AVGEN_SPATIAL_READ(op.compare, "compare", detail::readInt);
    AVGEN_SPATIAL_READ(op.idAttribute, "idAttribute", detail::readString);
    if (op.target.empty()) {
        return fail("attribute op '{}' needs a 'target'", attributeOpKindName(op.kind));
    }
    return op;
}

AttributeStats attributeStats(const AttributeBuffer& buffer) {
    AttributeStats stats;
    const std::size_t n = buffer.size();
    if (n == 0) {
        return stats;
    }
    columnRange(buffer, stats.min, stats.max);
    glm::vec4 sum(0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        sum += readAsVec4(buffer, i);
    }
    stats.mean = sum / static_cast<float>(n);
    return stats;
}

} // namespace avgen::spatial
