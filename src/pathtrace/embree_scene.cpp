#include "pathtrace/embree_scene.hpp"

#include <embree4/rtcore.h>

#include "core/log.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace avgen::pathtrace {
namespace {

void embreeError(void* /*user*/, RTCError code, const char* message) {
    log::error("pathtrace: embree error {}: {}", static_cast<int>(code), message ? message : "(no message)");
}

} // namespace

float shadowEpsilon(float t) {
    // 1e-4 relative, with an absolute floor so a hit at the origin still gets pushed off.
    return std::max(1e-4f, std::abs(t) * 1e-4f);
}

struct EmbreeScene::Impl {
    RTCDevice device = nullptr;
    RTCScene scene = nullptr;
    ~Impl() {
        if (scene != nullptr) rtcReleaseScene(scene);
        if (device != nullptr) rtcReleaseDevice(device);
    }
};

EmbreeScene::EmbreeScene() = default;
EmbreeScene::~EmbreeScene() = default;

Result<void> EmbreeScene::build(const Snapshot& snapshot, unsigned buildThreads) {
    if (snapshot.meshes.empty()) {
        return fail("pathtrace: cannot build an Embree scene from a snapshot with no geometry");
    }

    const unsigned threads = std::max(1u, buildThreads);
    // `threads=N` bounds Embree's own pool; the commit below is joined by our threads regardless.
    const std::string config = fmt::format("threads={}", threads);
    impl_ = std::make_unique<Impl>();
    impl_->device = rtcNewDevice(config.c_str());
    if (impl_->device == nullptr) {
        return fail("pathtrace: rtcNewDevice failed (config '{}'), error {}", config,
                    static_cast<int>(rtcGetDeviceError(nullptr)));
    }
    rtcSetDeviceErrorFunction(impl_->device, &embreeError, nullptr);

    impl_->scene = rtcNewScene(impl_->device);
    if (impl_->scene == nullptr) return fail("pathtrace: rtcNewScene failed");
    rtcSetSceneBuildQuality(impl_->scene, RTC_BUILD_QUALITY_HIGH);

    for (std::size_t mi = 0; mi < snapshot.meshes.size(); ++mi) {
        const TriangleMesh& mesh = snapshot.meshes[mi];
        if (!mesh.valid()) {
            return fail("pathtrace: mesh {} ('{}') is not valid geometry", mi, mesh.entityName);
        }

        RTCGeometry geom = rtcNewGeometry(impl_->device, RTC_GEOMETRY_TYPE_TRIANGLE);
        if (geom == nullptr) return fail("pathtrace: rtcNewGeometry failed for mesh {}", mi);

        // Embree wants 16-byte-aligned vertex rows, so the buffer is float4 with a padded w. Passing
        // a tight float3 array works by accident until the last vertex sits at the end of a page.
        auto* verts = static_cast<float*>(rtcSetNewGeometryBuffer(
            geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 4 * sizeof(float), mesh.positions.size()));
        if (verts == nullptr) return fail("pathtrace: vertex buffer allocation failed for mesh {}", mi);
        for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
            verts[v * 4 + 0] = mesh.positions[v].x;
            verts[v * 4 + 1] = mesh.positions[v].y;
            verts[v * 4 + 2] = mesh.positions[v].z;
            verts[v * 4 + 3] = 0.0f;
        }

        auto* idx = static_cast<std::uint32_t*>(rtcSetNewGeometryBuffer(
            geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(std::uint32_t),
            mesh.indices.size() / 3));
        if (idx == nullptr) return fail("pathtrace: index buffer allocation failed for mesh {}", mi);
        std::copy(mesh.indices.begin(), mesh.indices.end(), idx);

        rtcCommitGeometry(geom);
        const unsigned id = rtcAttachGeometry(impl_->scene, geom);
        rtcReleaseGeometry(geom);
        // The tracer indexes Snapshot::meshes by Embree's geomID, so the two orders must agree.
        if (id != static_cast<unsigned>(mi)) {
            return fail("pathtrace: embree assigned geomID {} to mesh {}; the snapshot's mesh order and the "
                        "scene's geometry order must agree",
                        id, mi);
        }
    }
    geometryCount_ = snapshot.meshes.size();

    // Build the BVH on threads this process owns (ADR-344). One `rtcJoinCommitScene` per thread;
    // they cooperate and all return when the build is done.
    if (threads <= 1) {
        rtcJoinCommitScene(impl_->scene);
    } else {
        std::vector<std::thread> joiners;
        joiners.reserve(threads - 1);
        for (unsigned i = 1; i < threads; ++i) {
            joiners.emplace_back([this] { rtcJoinCommitScene(impl_->scene); });
        }
        rtcJoinCommitScene(impl_->scene);
        for (auto& t : joiners) t.join();
    }

    if (rtcGetDeviceError(impl_->device) != RTC_ERROR_NONE) {
        return fail("pathtrace: embree reported an error while building the BVH");
    }
    return {};
}

SurfaceHit EmbreeScene::intersect(const Snapshot& snapshot, const glm::vec3& origin,
                                  const glm::vec3& direction, float tnear, float tfar) const {
    SurfaceHit out;
    if (!impl_ || impl_->scene == nullptr) return out;

    RTCRayHit rh{};
    rh.ray.org_x = origin.x;
    rh.ray.org_y = origin.y;
    rh.ray.org_z = origin.z;
    rh.ray.dir_x = direction.x;
    rh.ray.dir_y = direction.y;
    rh.ray.dir_z = direction.z;
    rh.ray.tnear = tnear;
    rh.ray.tfar = tfar;
    rh.ray.mask = 0xFFFFFFFF;
    rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rh.hit.primID = RTC_INVALID_GEOMETRY_ID;

    rtcIntersect1(impl_->scene, &rh);
    if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID) return out;

    const auto mi = static_cast<std::size_t>(rh.hit.geomID);
    if (mi >= snapshot.meshes.size()) return out;
    const TriangleMesh& mesh = snapshot.meshes[mi];

    const std::size_t tri = static_cast<std::size_t>(rh.hit.primID) * 3;
    if (tri + 2 >= mesh.indices.size()) return out;
    const std::uint32_t i0 = mesh.indices[tri + 0];
    const std::uint32_t i1 = mesh.indices[tri + 1];
    const std::uint32_t i2 = mesh.indices[tri + 2];

    // Embree's (u, v) are the barycentric weights of v1 and v2; v0 gets the remainder.
    const float u = rh.hit.u;
    const float v = rh.hit.v;
    const float w = 1.0f - u - v;

    out.hit = true;
    out.t = rh.ray.tfar;
    out.meshIndex = static_cast<std::uint32_t>(mi);
    out.primIndex = rh.hit.primID;
    out.position = origin + direction * out.t;

    glm::vec3 ng{rh.hit.Ng_x, rh.hit.Ng_y, rh.hit.Ng_z};
    const float ngLen = glm::length(ng);
    ng = ngLen > 1e-20f ? ng / ngLen : glm::vec3(0.0f, 1.0f, 0.0f);

    glm::vec3 ns = w * mesh.normals[i0] + u * mesh.normals[i1] + v * mesh.normals[i2];
    const float nsLen = glm::length(ns);
    // An interpolated normal can cancel to zero across a fold. Fall back to the geometric normal
    // rather than normalising a zero vector into NaNs.
    ns = nsLen > 1e-9f ? ns / nsLen : ng;

    // Both normals are flipped to face the incoming ray, and `backface` records that it happened,
    // so a double-sided material shades correctly and a single-sided one can still tell.
    out.backface = glm::dot(ng, direction) > 0.0f;
    if (out.backface) {
        ng = -ng;
        ns = -ns;
    }
    // Keep the shading normal in the geometric hemisphere: a heavily-interpolated normal can point
    // below the surface, which makes a BSDF return light from behind the geometry.
    if (glm::dot(ns, ng) < 0.0f) ns = ng;

    out.geometricNormal = ng;
    out.shadingNormal = ns;

    if (!mesh.uvs.empty()) {
        out.uv = w * mesh.uvs[i0] + u * mesh.uvs[i1] + v * mesh.uvs[i2];
    }
    return out;
}

bool EmbreeScene::occluded(const glm::vec3& origin, const glm::vec3& direction, float tnear,
                           float tfar) const {
    if (!impl_ || impl_->scene == nullptr) return false;
    RTCRay r{};
    r.org_x = origin.x;
    r.org_y = origin.y;
    r.org_z = origin.z;
    r.dir_x = direction.x;
    r.dir_y = direction.y;
    r.dir_z = direction.z;
    r.tnear = tnear;
    r.tfar = tfar;
    r.mask = 0xFFFFFFFF;
    rtcOccluded1(impl_->scene, &r);
    // Embree signals "occluded" by setting tfar negative.
    return r.tfar < 0.0f;
}

} // namespace avgen::pathtrace
