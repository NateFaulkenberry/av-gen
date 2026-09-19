#include "pathtrace/embree_scene.hpp"

#include <embree4/rtcore.h>

#include <glm/gtc/type_ptr.hpp>

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

// What a top-level geomID refers to. Embree hands back a flat id per top-level geometry, and an
// instance is one of those, so the tracer needs its own table to get back to the snapshot.
struct TopLevelRef {
    bool instanced = false;
    std::uint32_t objectIndex = 0;   // Snapshot::meshes or Snapshot::instanced
    std::uint32_t instanceIndex = 0;
};

struct EmbreeScene::Impl {
    RTCDevice device = nullptr;
    RTCScene scene = nullptr;
    std::vector<RTCScene> childScenes;   // one per InstancedObject, released with the device
    std::vector<TopLevelRef> refs;
    ~Impl() {
        for (RTCScene c : childScenes) {
            if (c != nullptr) rtcReleaseScene(c);
        }
        if (scene != nullptr) rtcReleaseScene(scene);
        if (device != nullptr) rtcReleaseDevice(device);
    }
};

EmbreeScene::EmbreeScene() = default;
EmbreeScene::~EmbreeScene() = default;

Result<void> EmbreeScene::build(const Snapshot& snapshot, unsigned buildThreads) {
    if (snapshot.meshes.empty() && snapshot.instanced.empty()) {
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
        if (id != static_cast<unsigned>(impl_->refs.size())) {
            return fail("pathtrace: embree assigned geomID {} where {} was expected", id,
                        impl_->refs.size());
        }
        impl_->refs.push_back(TopLevelRef{false, static_cast<std::uint32_t>(mi), 0});
    }

    // ---- instanced geometry --------------------------------------------------------------------
    //
    // Each InstancedObject becomes a child RTCScene holding its triangles ONCE, and one
    // RTC_GEOMETRY_TYPE_INSTANCE per copy in the top-level scene. Embree transforms the ray into
    // the child's space, so the triangles are never duplicated.
    for (std::size_t oi = 0; oi < snapshot.instanced.size(); ++oi) {
        const InstancedObject& obj = snapshot.instanced[oi];
        if (!obj.valid()) {
            return fail("pathtrace: instanced object {} ('{}') is not valid geometry", oi, obj.name);
        }

        RTCScene child = rtcNewScene(impl_->device);
        if (child == nullptr) return fail("pathtrace: rtcNewScene failed for instanced object {}", oi);
        impl_->childScenes.push_back(child);

        RTCGeometry geom = rtcNewGeometry(impl_->device, RTC_GEOMETRY_TYPE_TRIANGLE);
        auto* verts = static_cast<float*>(rtcSetNewGeometryBuffer(
            geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 4 * sizeof(float),
            obj.source.positions.size()));
        if (verts == nullptr) return fail("pathtrace: vertex buffer allocation failed for object {}", oi);
        for (std::size_t v = 0; v < obj.source.positions.size(); ++v) {
            verts[v * 4 + 0] = obj.source.positions[v].x;
            verts[v * 4 + 1] = obj.source.positions[v].y;
            verts[v * 4 + 2] = obj.source.positions[v].z;
            verts[v * 4 + 3] = 0.0f;
        }
        auto* idx = static_cast<std::uint32_t*>(rtcSetNewGeometryBuffer(
            geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(std::uint32_t),
            obj.source.indices.size() / 3));
        if (idx == nullptr) return fail("pathtrace: index buffer allocation failed for object {}", oi);
        std::copy(obj.source.indices.begin(), obj.source.indices.end(), idx);
        rtcCommitGeometry(geom);
        rtcAttachGeometry(child, geom);
        rtcReleaseGeometry(geom);
        rtcCommitScene(child);

        for (std::size_t ii = 0; ii < obj.transforms.size(); ++ii) {
            RTCGeometry inst = rtcNewGeometry(impl_->device, RTC_GEOMETRY_TYPE_INSTANCE);
            if (inst == nullptr) return fail("pathtrace: rtcNewGeometry(INSTANCE) failed");
            rtcSetGeometryInstancedScene(inst, child);
            // FLOAT4X4, not FLOAT3X4. `glm::mat4` is four columns of FOUR floats; a 3x4 layout
            // expects three floats per column, so handing it `value_ptr` makes it read each
            // column's w as the next column's x. Every instance then lands somewhere arbitrary --
            // measured as rays missing all three test cubes entirely, which is at least a loud
            // failure rather than a subtle one. COLUMN_MAJOR because that is glm's storage order.
            rtcSetGeometryTransform(inst, 0, RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR,
                                    glm::value_ptr(obj.transforms[ii]));
            rtcCommitGeometry(inst);
            const unsigned id = rtcAttachGeometry(impl_->scene, inst);
            rtcReleaseGeometry(inst);
            if (id != static_cast<unsigned>(impl_->refs.size())) {
                return fail("pathtrace: embree assigned instance geomID {} where {} was expected", id,
                            impl_->refs.size());
            }
            impl_->refs.push_back(TopLevelRef{true, static_cast<std::uint32_t>(oi),
                                              static_cast<std::uint32_t>(ii)});
        }
    }
    geometryCount_ = impl_->refs.size();

    // Build the BVH on threads this process owns (ADR-348). One `rtcJoinCommitScene` per thread;
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

    // Embree reports the TOP-LEVEL geometry in instID[0] when the hit is inside an instance, and
    // geomID is then the geometry within the child scene. Reading geomID as a top-level id is the
    // classic instancing bug: every instanced hit resolves to mesh 0.
    const bool viaInstance = rh.hit.instID[0] != RTC_INVALID_GEOMETRY_ID;
    const auto topId = static_cast<std::size_t>(viaInstance ? rh.hit.instID[0] : rh.hit.geomID);
    if (topId >= impl_->refs.size()) return out;
    const TopLevelRef& ref = impl_->refs[topId];

    const TriangleMesh* meshPtr = nullptr;
    if (ref.instanced) {
        if (ref.objectIndex >= snapshot.instanced.size()) return out;
        meshPtr = &snapshot.instanced[ref.objectIndex].source;
    } else {
        if (ref.objectIndex >= snapshot.meshes.size()) return out;
        meshPtr = &snapshot.meshes[ref.objectIndex];
    }
    const TriangleMesh& mesh = *meshPtr;
    const auto mi = static_cast<std::size_t>(ref.objectIndex);

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
    out.instanced = ref.instanced;
    out.instanceIndex = ref.instanceIndex;
    out.meshIndex = static_cast<std::uint32_t>(mi);
    out.primIndex = rh.hit.primID;
    out.position = origin + direction * out.t;
    out.baryW = w;
    out.baryU = u;
    out.baryV = v;

    // Normals are computed from our OWN data rather than read from `rh.hit.Ng`, because for an
    // instanced hit Embree reports Ng in the child scene's space and it would need transforming --
    // a convention that is easy to get wrong in one of the two branches and produces lighting that
    // is subtly incorrect only on scattered objects.
    glm::vec3 ng = glm::cross(mesh.positions[i1] - mesh.positions[i0],
                              mesh.positions[i2] - mesh.positions[i0]);
    glm::vec3 ns = w * mesh.normals[i0] + u * mesh.normals[i1] + v * mesh.normals[i2];
    if (ref.instanced) {
        const glm::mat3& nm = snapshot.instanced[ref.objectIndex].normalMatrices[ref.instanceIndex];
        ng = nm * ng;
        ns = nm * ns;
    }
    const float ngLen = glm::length(ng);
    ng = ngLen > 1e-20f ? ng / ngLen : glm::vec3(0.0f, 1.0f, 0.0f);
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

const scene::Material& EmbreeScene::materialOf(const Snapshot& snap, const SurfaceHit& hit) {
    return hit.instanced ? snap.instanced[hit.meshIndex].source.material
                         : snap.meshes[hit.meshIndex].material;
}

} // namespace avgen::pathtrace
