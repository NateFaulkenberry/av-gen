// Point clouds (ADR-024): the core columns, seeding, the instance projection and its inverse.
//
// Conventions chosen here:
// * Core defaults: rotation (0,0,0,1), scale 1, id = row, seed 0, density 1, color (1,1,1,1),
//   emissive (1,1,1), velocity 0, normal (0,1,0), bounds 0.5, index = row / (n - 1).
// * renumberIndices only recomputes `index`; seeds are set by reseed() (or by the generator).
// * reseed(g): seed[i] = int(pcg3d({g, uint(id[i]), 0x9E3779B9}).x); random(i, ch) =
//   hashIndex(uint(seed[i]), uint(id[i]), ch), so a point's randoms follow its id through
//   filters and sorts.
// * cloudFromInstances recovers ids from color.a and density/index from position.w/scale.w; the
//   random lanes are not invertible, so the seed column is left at 0.

#include "spatial/point_cloud.hpp"

#include "core/noise.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>

namespace avgen::spatial {

namespace {

template <typename T>
std::span<T> spanOf(AttributeSet& set, std::string_view name) {
    auto view = set.view<T>(name);
    return view ? view->values : std::span<T>{};
}

template <typename T>
std::span<const T> spanOf(const AttributeSet& set, std::string_view name) {
    auto view = set.view<T>(name);
    return view ? view->values : std::span<const T>{};
}

template <typename T>
void fillRows(AttributeSet& set, std::string_view name, std::size_t from, std::size_t to, const T& value) {
    auto s = spanOf<T>(set, name);
    for (std::size_t i = from; i < to && i < s.size(); ++i) {
        s[i] = value;
    }
}

} // namespace

PointCloud::PointCloud() {
    ensureCore();
}

PointCloud::PointCloud(std::size_t count) {
    attributes.resize(count);
    ensureCore();
}

void PointCloud::ensureCore() {
    const std::size_t n = attributes.count();
    const auto ensure = [&](std::string_view name, AttributeType type, auto fill) {
        if (attributes.has(name)) {
            return;
        }
        (void)attributes.add(name, type);
        fill();
    };
    ensure(attr::position, AttributeType::Vec3, [] {});
    ensure(attr::rotation, AttributeType::Vec4, [&] { fillRows(attributes, attr::rotation, 0, n, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)); });
    ensure(attr::scale, AttributeType::Vec3, [&] { fillRows(attributes, attr::scale, 0, n, glm::vec3(1.0f)); });
    ensure(attr::id, AttributeType::Int, [&] {
        auto s = spanOf<std::int32_t>(attributes, attr::id);
        for (std::size_t i = 0; i < s.size(); ++i) {
            s[i] = static_cast<std::int32_t>(i);
        }
    });
    ensure(attr::seed, AttributeType::Int, [] {});
    ensure(attr::density, AttributeType::Float, [&] { fillRows(attributes, attr::density, 0, n, 1.0f); });
    ensure(attr::color, AttributeType::Color, [&] { fillRows(attributes, attr::color, 0, n, glm::vec4(1.0f)); });
    ensure(attr::emissive, AttributeType::Vec3, [&] { fillRows(attributes, attr::emissive, 0, n, glm::vec3(1.0f)); });
    ensure(attr::velocity, AttributeType::Vec3, [] {});
    ensure(attr::normal, AttributeType::Vec3, [&] { fillRows(attributes, attr::normal, 0, n, glm::vec3(0.0f, 1.0f, 0.0f)); });
    ensure(attr::bounds, AttributeType::Vec3, [&] { fillRows(attributes, attr::bounds, 0, n, glm::vec3(0.5f)); });
    ensure(attr::index, AttributeType::Float, [&] { renumberIndices(); });
}

void PointCloud::resize(std::size_t count) {
    const std::size_t old = attributes.count();
    attributes.resize(count);
    ensureCore();
    if (count > old) {
        fillRows(attributes, attr::rotation, old, count, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        fillRows(attributes, attr::scale, old, count, glm::vec3(1.0f));
        auto idSpan = ids();
        for (std::size_t i = old; i < count; ++i) {
            idSpan[i] = static_cast<std::int32_t>(i);
        }
        fillRows(attributes, attr::density, old, count, 1.0f);
        fillRows(attributes, attr::color, old, count, glm::vec4(1.0f));
        fillRows(attributes, attr::emissive, old, count, glm::vec3(1.0f));
        fillRows(attributes, attr::normal, old, count, glm::vec3(0.0f, 1.0f, 0.0f));
        fillRows(attributes, attr::bounds, old, count, glm::vec3(0.5f));
    }
    renumberIndices();
}

void PointCloud::clear() {
    attributes.clear();
}

void PointCloud::renumberIndices() {
    auto s = indices();
    const std::size_t n = s.size();
    const float inv = n > 1 ? 1.0f / static_cast<float>(n - 1) : 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        s[i] = static_cast<float>(i) * inv;
    }
}

void PointCloud::reseed(std::uint32_t generatorSeed) {
    auto idSpan = ids();
    auto seedSpan = seeds();
    for (std::size_t i = 0; i < seedSpan.size(); ++i) {
        const noise::U3 h = noise::pcg3d({generatorSeed, static_cast<std::uint32_t>(idSpan[i]), 0x9E3779B9u});
        seedSpan[i] = static_cast<std::int32_t>(h.x);
    }
}

// ---- accessors ------------------------------------------------------------------------------------

std::span<glm::vec3> PointCloud::positions() { return spanOf<glm::vec3>(attributes, attr::position); }
std::span<const glm::vec3> PointCloud::positions() const { return spanOf<glm::vec3>(attributes, attr::position); }
std::span<glm::vec4> PointCloud::rotations() { return spanOf<glm::vec4>(attributes, attr::rotation); }
std::span<const glm::vec4> PointCloud::rotations() const { return spanOf<glm::vec4>(attributes, attr::rotation); }
std::span<glm::vec3> PointCloud::scales() { return spanOf<glm::vec3>(attributes, attr::scale); }
std::span<const glm::vec3> PointCloud::scales() const { return spanOf<glm::vec3>(attributes, attr::scale); }
std::span<std::int32_t> PointCloud::ids() { return spanOf<std::int32_t>(attributes, attr::id); }
std::span<const std::int32_t> PointCloud::ids() const { return spanOf<std::int32_t>(attributes, attr::id); }
std::span<std::int32_t> PointCloud::seeds() { return spanOf<std::int32_t>(attributes, attr::seed); }
std::span<const std::int32_t> PointCloud::seeds() const { return spanOf<std::int32_t>(attributes, attr::seed); }
std::span<float> PointCloud::densities() { return spanOf<float>(attributes, attr::density); }
std::span<const float> PointCloud::densities() const { return spanOf<float>(attributes, attr::density); }
std::span<glm::vec4> PointCloud::colors() { return spanOf<glm::vec4>(attributes, attr::color); }
std::span<const glm::vec4> PointCloud::colors() const { return spanOf<glm::vec4>(attributes, attr::color); }
std::span<glm::vec3> PointCloud::emissives() { return spanOf<glm::vec3>(attributes, attr::emissive); }
std::span<const glm::vec3> PointCloud::emissives() const { return spanOf<glm::vec3>(attributes, attr::emissive); }
std::span<glm::vec3> PointCloud::velocities() { return spanOf<glm::vec3>(attributes, attr::velocity); }
std::span<const glm::vec3> PointCloud::velocities() const { return spanOf<glm::vec3>(attributes, attr::velocity); }
std::span<glm::vec3> PointCloud::normals() { return spanOf<glm::vec3>(attributes, attr::normal); }
std::span<const glm::vec3> PointCloud::normals() const { return spanOf<glm::vec3>(attributes, attr::normal); }
std::span<float> PointCloud::indices() { return spanOf<float>(attributes, attr::index); }
std::span<const float> PointCloud::indices() const { return spanOf<float>(attributes, attr::index); }

// ---- derived --------------------------------------------------------------------------------------

float PointCloud::random(std::size_t i, std::uint32_t channel) const {
    const auto seedSpan = seeds();
    const auto idSpan = ids();
    if (i >= seedSpan.size() || i >= idSpan.size()) {
        return 0.0f;
    }
    return noise::hashIndex(static_cast<std::uint32_t>(seedSpan[i]), static_cast<std::uint32_t>(idSpan[i]), channel);
}

void PointCloud::bounds(glm::vec3& outMin, glm::vec3& outMax, bool includeExtent) const {
    const auto pos = positions();
    if (pos.empty()) {
        outMin = outMax = glm::vec3(0.0f);
        return;
    }
    const auto ext = spanOf<glm::vec3>(attributes, attr::bounds);
    const auto sc = scales();
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (std::size_t i = 0; i < pos.size(); ++i) {
        glm::vec3 e(0.0f);
        if (includeExtent && i < ext.size() && i < sc.size()) {
            e = glm::abs(ext[i] * sc[i]);
        }
        lo = glm::min(lo, pos[i] - e);
        hi = glm::max(hi, pos[i] + e);
    }
    outMin = lo;
    outMax = hi;
}

glm::mat4 PointCloud::pointMatrix(std::size_t i) const {
    const glm::vec4 r = rotations()[i];
    const glm::quat q(r.w, r.x, r.y, r.z);
    return glm::translate(glm::mat4(1.0f), positions()[i]) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), scales()[i]);
}

nlohmann::json PointCloud::toJson() const {
    return attributes.toJson();
}

Result<PointCloud> PointCloud::fromJson(const nlohmann::json& j) {
    auto set = AttributeSet::fromJson(j);
    if (!set) {
        return std::unexpected(set.error());
    }
    PointCloud cloud;
    cloud.attributes = std::move(*set);
    cloud.ensureCore();
    return cloud;
}

// ---- projection -----------------------------------------------------------------------------------

void projectInstances(const PointCloud& cloud, std::vector<InstanceRecord>& out, std::string_view extraLane) {
    const std::size_t n = cloud.count();
    out.clear();
    out.reserve(n);
    const auto pos = cloud.positions();
    const auto rot = cloud.rotations();
    const auto sc = cloud.scales();
    const auto idSpan = cloud.ids();
    const auto den = cloud.densities();
    const auto col = cloud.colors();
    const auto emi = cloud.emissives();
    const auto idx = cloud.indices();
    const AttributeBuffer* extra = extraLane.empty() ? nullptr : cloud.attributes.find(extraLane);
    for (std::size_t i = 0; i < n; ++i) {
        InstanceRecord rec{};
        rec.position = glm::vec4(pos[i], den[i]);
        rec.rotation = rot[i];
        rec.scale = glm::vec4(sc[i], idx[i]);
        rec.random = glm::vec4(cloud.random(i, 0), cloud.random(i, 1), cloud.random(i, 2), cloud.random(i, 3));
        rec.color = glm::vec4(glm::vec3(col[i]), static_cast<float>(idSpan[i]));
        rec.emissive = glm::vec4(emi[i], extra != nullptr ? readAsVec4(*extra, i).x : 0.0f);
        out.push_back(rec);
    }
}

PointCloud cloudFromInstances(std::span<const InstanceRecord> records) {
    PointCloud cloud(records.size());
    auto pos = cloud.positions();
    auto rot = cloud.rotations();
    auto sc = cloud.scales();
    auto idSpan = cloud.ids();
    auto den = cloud.densities();
    auto col = cloud.colors();
    auto emi = cloud.emissives();
    auto idx = cloud.indices();
    for (std::size_t i = 0; i < records.size(); ++i) {
        const InstanceRecord& r = records[i];
        pos[i] = glm::vec3(r.position);
        den[i] = r.position.w;
        rot[i] = r.rotation;
        sc[i] = glm::vec3(r.scale);
        idx[i] = r.scale.w;
        col[i] = glm::vec4(glm::vec3(r.color), 1.0f);
        idSpan[i] = static_cast<std::int32_t>(r.color.a);
        emi[i] = glm::vec3(r.emissive);
    }
    return cloud;
}

} // namespace avgen::spatial
