// Helpers shared by the graph translation units: value conversions, the flattened input
// encoding, parameter access with registry defaults, subgraph file resolution and the renaming
// of everything a subgraph instance emitted.

#include "graph/detail.hpp"

#include <algorithm>
#include <mutex>
#include <nlohmann/json.hpp>

namespace avgen::graph::detail {
namespace {

using nlohmann::json;

std::mutex& pathMutex() {
    static std::mutex mutex;
    return mutex;
}
std::filesystem::path& sourceDirectory() {
    static std::filesystem::path directory;
    return directory;
}
std::vector<std::filesystem::path>& libraryDirectories() {
    static std::vector<std::filesystem::path> directories;
    return directories;
}

// "procedural/columns/material/emissive" -> "procedural/<prefix>columns/material/emissive"
std::string prefixParameterPath(const std::string& path, const std::string& prefix) {
    const std::size_t slash = path.find('/');
    if (slash == std::string::npos) {
        return prefix + path;
    }
    return path.substr(0, slash + 1) + prefix + path.substr(slash + 1);
}

void prefixSdfReferences(spatial::SdfNode& node, const std::string& prefix) {
    if (!node.reference.empty()) {
        node.reference = prefix + node.reference;
    }
    for (spatial::SdfNode& child : node.children) {
        prefixSdfReferences(child, prefix);
    }
}

} // namespace

// ---- values ------------------------------------------------------------------------------------

std::optional<Num> asNumber(const Value& value) {
    if (const float* f = std::get_if<float>(&value)) {
        return Num{glm::vec4(*f, 0.0f, 0.0f, 0.0f), 1};
    }
    if (const int* i = std::get_if<int>(&value)) {
        return Num{glm::vec4(static_cast<float>(*i), 0.0f, 0.0f, 0.0f), 1};
    }
    if (const bool* b = std::get_if<bool>(&value)) {
        return Num{glm::vec4(*b ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f), 1};
    }
    if (const glm::vec2* v = std::get_if<glm::vec2>(&value)) {
        return Num{glm::vec4(v->x, v->y, 0.0f, 0.0f), 2};
    }
    if (const glm::vec3* v = std::get_if<glm::vec3>(&value)) {
        return Num{glm::vec4(*v, 0.0f), 3};
    }
    if (const glm::vec4* v = std::get_if<glm::vec4>(&value)) {
        return Num{*v, 4};
    }
    return std::nullopt;
}

Value numberValue(const Num& n) {
    switch (n.components) {
    case 2:
        return glm::vec2(n.v.x, n.v.y);
    case 3:
        return glm::vec3(n.v);
    case 4:
        return n.v;
    default:
        return n.v.x;
    }
}

Num numberOf(const Value& value, float fallback) {
    if (auto n = asNumber(value)) {
        return *n;
    }
    return Num{glm::vec4(fallback, 0.0f, 0.0f, 0.0f), 1};
}

float asFloat(const Value& value, float fallback) {
    return numberOf(value, fallback).v.x;
}

int asInt(const Value& value, int fallback) {
    if (const int* i = std::get_if<int>(&value)) {
        return *i;
    }
    if (auto n = asNumber(value)) {
        return static_cast<int>(n->v.x);
    }
    return fallback;
}

bool asBool(const Value& value, bool fallback) {
    if (const bool* b = std::get_if<bool>(&value)) {
        return *b;
    }
    if (auto n = asNumber(value)) {
        return n->v.x != 0.0f;
    }
    return fallback;
}

glm::vec3 asVec3(const Value& value, const glm::vec3& fallback) {
    if (auto n = asNumber(value)) {
        return n->components == 1 ? glm::vec3(n->v.x) : glm::vec3(n->v);
    }
    return fallback;
}

glm::vec4 asVec4(const Value& value, const glm::vec4& fallback) {
    if (auto n = asNumber(value)) {
        if (n->components == 1) {
            return glm::vec4(glm::vec3(n->v.x), 1.0f);
        }
        if (n->components == 3) {
            return glm::vec4(glm::vec3(n->v), 1.0f);
        }
        return n->v;
    }
    return fallback;
}

std::string asString(const Value& value, const std::string& fallback) {
    if (const std::string* s = std::get_if<std::string>(&value)) {
        return *s;
    }
    return fallback;
}

bool isEmpty(const Value& value) {
    return std::holds_alternative<std::monostate>(value);
}

json valueToJson(const Value& value) {
    if (const float* f = std::get_if<float>(&value)) {
        return *f;
    }
    if (const int* i = std::get_if<int>(&value)) {
        return *i;
    }
    if (const bool* b = std::get_if<bool>(&value)) {
        return *b;
    }
    if (const glm::vec2* v = std::get_if<glm::vec2>(&value)) {
        return json::array({v->x, v->y});
    }
    if (const glm::vec3* v = std::get_if<glm::vec3>(&value)) {
        return json::array({v->x, v->y, v->z});
    }
    if (const glm::vec4* v = std::get_if<glm::vec4>(&value)) {
        return json::array({v->x, v->y, v->z, v->w});
    }
    if (const std::string* s = std::get_if<std::string>(&value)) {
        return *s;
    }
    return json();
}

Value valueFromJson(PinType type, const json& j) {
    switch (type) {
    case PinType::Float:
        return j.is_number() ? j.get<float>() : 0.0f;
    case PinType::Int:
        return j.is_number() ? j.get<int>() : 0;
    case PinType::Bool:
        return j.is_boolean() ? j.get<bool>() : (j.is_number() ? j.get<float>() != 0.0f : false);
    case PinType::Vec2:
        if (j.is_array() && j.size() == 2) {
            return glm::vec2(j.at(0).get<float>(), j.at(1).get<float>());
        }
        return glm::vec2(0.0f);
    case PinType::Vec3:
        if (j.is_array() && j.size() == 3) {
            return glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>());
        }
        return glm::vec3(0.0f);
    case PinType::Color:
        if (j.is_array() && j.size() == 4) {
            return glm::vec4(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(),
                             j.at(3).get<float>());
        }
        if (j.is_array() && j.size() == 3) {
            return glm::vec4(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), 1.0f);
        }
        return glm::vec4(1.0f);
    case PinType::String:
        return j.is_string() ? j.get<std::string>() : std::string();
    default:
        return Value{};
    }
}

Value defaultValueFor(PinType type) {
    switch (type) {
    case PinType::Float:
        return 0.0f;
    case PinType::Int:
        return 0;
    case PinType::Bool:
        return false;
    case PinType::Vec2:
        return glm::vec2(0.0f);
    case PinType::Vec3:
        return glm::vec3(0.0f);
    case PinType::Color:
        return glm::vec4(1.0f);
    case PinType::String:
        return std::string();
    default:
        return Value{};
    }
}

// ---- inputs ------------------------------------------------------------------------------------

const Value Inputs::kEmpty{};

Inputs::Inputs(const NodeTypeInfo& info, const std::vector<Value>& values)
    : info_(&info)
    , values_(&values) {
    ranges_.reserve(info.inputs.size());
    std::size_t cursor = 0;
    for (const PinInfo& pin : info.inputs) {
        if (!pin.multi) {
            ranges_.emplace_back(cursor, cursor < values.size() ? 1u : 0u);
            cursor = std::min(cursor + 1, values.size());
            continue;
        }
        const std::size_t begin = cursor;
        while (cursor < values.size() && !std::holds_alternative<std::monostate>(values[cursor])) {
            ++cursor;
        }
        ranges_.emplace_back(begin, cursor - begin);
        if (cursor < values.size()) {
            ++cursor; // the terminator
        }
    }
}

const Value& Inputs::single(std::string_view pin) const {
    const std::span<const Value> range = multi(pin);
    return range.empty() ? kEmpty : range.front();
}

std::span<const Value> Inputs::multi(std::string_view pin) const {
    if (info_ == nullptr || values_ == nullptr) {
        return {};
    }
    for (std::size_t i = 0; i < info_->inputs.size() && i < ranges_.size(); ++i) {
        if (info_->inputs[i].name != pin) {
            continue;
        }
        const auto [begin, count] = ranges_[i];
        if (begin + count > values_->size()) {
            return {};
        }
        return std::span<const Value>(values_->data() + begin, count);
    }
    return {};
}

bool Inputs::has(std::string_view pin) const {
    const std::span<const Value> range = multi(pin);
    if (range.empty()) {
        return false;
    }
    return !std::holds_alternative<std::monostate>(range.front());
}

float Inputs::f(std::string_view pin, float fallback) const {
    return asFloat(single(pin), fallback);
}
int Inputs::i(std::string_view pin, int fallback) const {
    return asInt(single(pin), fallback);
}
bool Inputs::b(std::string_view pin, bool fallback) const {
    return asBool(single(pin), fallback);
}
glm::vec3 Inputs::v3(std::string_view pin, const glm::vec3& fallback) const {
    return asVec3(single(pin), fallback);
}
glm::vec4 Inputs::v4(std::string_view pin, const glm::vec4& fallback) const {
    return asVec4(single(pin), fallback);
}
std::string Inputs::s(std::string_view pin, const std::string& fallback) const {
    return asString(single(pin), fallback);
}

// ---- parameters --------------------------------------------------------------------------------

Params::Params(const Node& node)
    : node_(&node)
    , info_(NodeRegistry::instance().find(node.type)) {}

const nlohmann::json* Params::raw(std::string_view name) const {
    if (node_ == nullptr) {
        return nullptr;
    }
    const auto it = node_->params.find(name);
    return it == node_->params.end() ? nullptr : &*it;
}

const ParamInfo* Params::desc(std::string_view name) const {
    if (info_ == nullptr) {
        return nullptr;
    }
    for (const ParamInfo& param : info_->params) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

// True when the node carries the parameter or the node type declares it: readers then return the
// stored value, else the descriptor's default (an undeclared name leaves the caller's default).
bool Params::has(std::string_view name) const {
    return raw(name) != nullptr || desc(name) != nullptr;
}

std::string Params::label(std::string_view name) const {
    const ParamInfo* info = desc(name);
    if (info == nullptr) {
        return s(name);
    }
    const int index = enumOf(name);
    if (index >= 0 && static_cast<std::size_t>(index) < info->choices.size()) {
        return info->choices[static_cast<std::size_t>(index)];
    }
    return {};
}

float Params::f(std::string_view name) const {
    if (const json* value = raw(name); value != nullptr && value->is_number()) {
        return value->get<float>();
    }
    if (const ParamInfo* info = desc(name); info != nullptr) {
        return asFloat(info->defaultValue, 0.0f);
    }
    return 0.0f;
}

int Params::i(std::string_view name) const {
    if (const json* value = raw(name); value != nullptr && value->is_number()) {
        return value->get<int>();
    }
    if (const ParamInfo* info = desc(name); info != nullptr) {
        return asInt(info->defaultValue, 0);
    }
    return 0;
}

std::uint32_t Params::u32(std::string_view name) const {
    return static_cast<std::uint32_t>(std::max(0, i(name)));
}

bool Params::b(std::string_view name) const {
    if (const json* value = raw(name); value != nullptr) {
        if (value->is_boolean()) {
            return value->get<bool>();
        }
        if (value->is_number()) {
            return value->get<float>() != 0.0f;
        }
    }
    if (const ParamInfo* info = desc(name); info != nullptr) {
        return asBool(info->defaultValue, false);
    }
    return false;
}

glm::vec2 Params::v2(std::string_view name) const {
    if (const json* value = raw(name); value != nullptr && value->is_array() && value->size() == 2) {
        return glm::vec2(value->at(0).get<float>(), value->at(1).get<float>());
    }
    if (const ParamInfo* info = desc(name); info != nullptr) {
        if (const glm::vec2* v = std::get_if<glm::vec2>(&info->defaultValue)) {
            return *v;
        }
    }
    return glm::vec2(0.0f);
}

glm::vec3 Params::v3(std::string_view name) const {
    if (const json* value = raw(name); value != nullptr && value->is_array() && value->size() == 3) {
        return glm::vec3(value->at(0).get<float>(), value->at(1).get<float>(), value->at(2).get<float>());
    }
    if (const ParamInfo* info = desc(name); info != nullptr) {
        return asVec3(info->defaultValue, glm::vec3(0.0f));
    }
    return glm::vec3(0.0f);
}

glm::vec4 Params::v4(std::string_view name) const {
    if (const json* value = raw(name); value != nullptr && value->is_array()) {
        if (value->size() == 4) {
            return glm::vec4(value->at(0).get<float>(), value->at(1).get<float>(), value->at(2).get<float>(),
                             value->at(3).get<float>());
        }
        if (value->size() == 3) {
            return glm::vec4(value->at(0).get<float>(), value->at(1).get<float>(), value->at(2).get<float>(),
                             1.0f);
        }
    }
    if (const ParamInfo* info = desc(name); info != nullptr) {
        return asVec4(info->defaultValue, glm::vec4(1.0f));
    }
    return glm::vec4(1.0f);
}

std::string Params::s(std::string_view name) const {
    if (const json* value = raw(name); value != nullptr && value->is_string()) {
        return value->get<std::string>();
    }
    if (const ParamInfo* info = desc(name); info != nullptr) {
        return asString(info->defaultValue, {});
    }
    return {};
}

int Params::enumOf(std::string_view name) const {
    const ParamInfo* info = desc(name);
    if (const json* value = raw(name); value != nullptr) {
        if (value->is_number()) {
            return value->get<int>();
        }
        if (value->is_string() && info != nullptr) {
            const std::string label = value->get<std::string>();
            for (std::size_t i = 0; i < info->choices.size(); ++i) {
                if (info->choices[i] == label) {
                    return static_cast<int>(i);
                }
            }
        }
    }
    return info != nullptr ? asInt(info->defaultValue, 0) : 0;
}

// ---- files -------------------------------------------------------------------------------------

void setGraphSourceDirectory(std::filesystem::path dir) {
    const std::lock_guard<std::mutex> lock(pathMutex());
    sourceDirectory() = std::move(dir);
}

std::filesystem::path graphSourceDirectory() {
    const std::lock_guard<std::mutex> lock(pathMutex());
    return sourceDirectory();
}

void setGraphLibraryDirectories(std::vector<std::filesystem::path> dirs) {
    const std::lock_guard<std::mutex> lock(pathMutex());
    libraryDirectories() = std::move(dirs);
}

std::filesystem::path resolveGraphFile(const std::string& reference) {
    if (reference.empty()) {
        return {};
    }
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path asGiven(reference);
    const bool hasExtension = asGiven.has_extension();
    std::vector<std::filesystem::path> roots{std::filesystem::path()};
    {
        const std::lock_guard<std::mutex> lock(pathMutex());
        if (!sourceDirectory().empty()) {
            roots.push_back(sourceDirectory());
        }
        for (const std::filesystem::path& dir : libraryDirectories()) {
            roots.push_back(dir);
        }
    }
    for (const std::filesystem::path& root : roots) {
        const std::filesystem::path base = root.empty() ? asGiven : root / asGiven;
        candidates.push_back(base);
        if (!hasExtension) {
            candidates.push_back(std::filesystem::path(base.string() + ".graph.json"));
        }
    }
    std::error_code ec;
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

// ---- emission renaming --------------------------------------------------------------------------

void prefixOutputNames(GraphOutput& output, const std::string& prefix) {
    if (prefix.empty()) {
        return;
    }
    for (scene::ProceduralGeometry& p : output.procedurals) {
        p.name = prefix + p.name;
        if (!p.source.reference.empty()) {
            p.source.reference = prefix + p.source.reference;
        }
        if (!p.distribution.spline.empty()) {
            p.distribution.spline = prefix + p.distribution.spline;
        }
        if (!p.emissiveField.empty()) {
            p.emissiveField = prefix + p.emissiveField;
        }
        if (!p.material.program.empty()) {
            p.material.program = prefix + p.material.program;
        }
        for (scene::Deformer& d : p.deformers) {
            if (!d.field.empty()) {
                d.field = prefix + d.field;
            }
            if (!d.spline.empty()) {
                d.spline = prefix + d.spline;
            }
        }
        for (spatial::Effector& e : p.effectors) {
            if (!e.field.empty()) {
                e.field = prefix + e.field;
            }
        }
    }
    for (spatial::FieldSpec& f : output.fields) {
        f.name = prefix + f.name;
        for (std::string& child : f.children) {
            child = prefix + child;
        }
        if (!f.reference.empty()) {
            f.reference = prefix + f.reference;
        }
    }
    for (spatial::Spline& s : output.splines) {
        s.name = prefix + s.name;
    }
    for (scene::SdfObject& s : output.sdfs) {
        s.name = prefix + s.name;
        if (!s.material.program.empty()) {
            s.material.program = prefix + s.material.program;
        }
        prefixSdfReferences(s.tree.root, prefix);
    }
    for (scene::MaterialProgram& m : output.materialPrograms) {
        m.name = prefix + m.name;
        for (scene::MaterialOp& op : m.ops) {
            if (!op.field.empty()) {
                op.field = prefix + op.field;
            }
        }
    }
    for (scene::ParticleSystem& p : output.particles) {
        p.name = prefix + p.name;
        if (!p.spline.empty()) {
            p.spline = prefix + p.spline;
        }
        for (scene::FieldForce& force : p.fieldForces) {
            if (!force.field.empty()) {
                force.field = prefix + force.field;
            }
        }
    }
    for (params::ModRoute& route : output.routes) {
        route.target = prefixParameterPath(route.target, prefix);
    }
    for (std::string& warning : output.warnings) {
        warning = prefix + warning;
    }
}

} // namespace avgen::graph::detail
