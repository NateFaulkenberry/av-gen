#include "pathtrace/embree_scene.hpp"

#include <embree4/rtcore.h>

#include <glm/gtc/type_ptr.hpp>

#include "core/log.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

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

namespace {

using Clock = std::chrono::steady_clock;
[[nodiscard]] double since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

// A child large enough that joining the build from our own threads is worth spawning them.
// Below this the thread start-up costs more than the build (Glowmere has ~250 small children).
constexpr std::size_t kJoinedBuildTriangles = 100'000;

// Commits `scene` on threads this process owns (ADR-351): one `rtcJoinCommitScene` per thread;
// they cooperate and all return when the build is done.
void joinCommit(RTCScene scene, unsigned threads) {
    if (threads <= 1) {
        rtcJoinCommitScene(scene);
        return;
    }
    std::vector<std::thread> joiners;
    joiners.reserve(threads - 1);
    for (unsigned i = 1; i < threads; ++i) {
        joiners.emplace_back([scene] { rtcJoinCommitScene(scene); });
    }
    rtcJoinCommitScene(scene);
    for (auto& t : joiners) t.join();
}

// The identity of one member of a child scene across frames. Meshes and procedural sources are
// numbered from different lists, so the list is part of the key.
[[nodiscard]] std::uint64_t meshKey(const TriangleMesh& m) { return m.entityIndex; }
[[nodiscard]] std::uint64_t proceduralKey(const InstancedObject& o) {
    return (std::uint64_t{1} << 63) | o.source.entityIndex;
}

// The positions the BVH is built over, and the transform that places them. A mesh that carries no
// object-space copy is already in world space (see TriangleMesh::objectPositions).
[[nodiscard]] const std::vector<glm::vec3>& bvhPositions(const TriangleMesh& m) {
    return m.objectPositions.empty() ? m.positions : m.objectPositions;
}
[[nodiscard]] glm::mat4 bvhTransform(const TriangleMesh& m) {
    return m.objectPositions.empty() ? glm::mat4(1.0f) : m.objectToWorld;
}

// Bitwise, never `==`: -0.0 == 0.0 and NaN != NaN, and what Embree was handed is bits.
[[nodiscard]] bool sameBits(const glm::mat4& a, const glm::mat4& b) {
    return std::memcmp(glm::value_ptr(a), glm::value_ptr(b), sizeof(glm::mat4)) == 0;
}

struct TransformKey {
    std::array<std::uint32_t, 16> bits{};
    bool operator<(const TransformKey& o) const { return bits < o.bits; }
};
[[nodiscard]] TransformKey transformKey(const glm::mat4& m) {
    TransformKey k;
    std::memcpy(k.bits.data(), glm::value_ptr(m), sizeof(glm::mat4));
    return k;
}

} // namespace

// What a top-level geomID refers to. Embree hands back a flat id per top-level geometry, and every
// top-level geometry is now an instance, so the tracer needs its own table to get back to the
// snapshot.
struct TopLevelRef {
    bool instanced = false;          // a procedural copy; otherwise a mesh group
    std::uint32_t objectIndex = 0;   // Snapshot::instanced index, or the group index
    std::uint32_t instanceIndex = 0;
};

// One child scene: a group of meshes that share a transform, or one procedural source.
struct ChildScene {
    RTCScene scene = nullptr;
    // One per member, RETAINED past attachment so the next frame can compare its buffers against
    // Embree's own copy -- the exact bytes this BVH was built from.
    std::vector<RTCGeometry> geoms;
    std::vector<std::uint64_t> memberKeys;
    std::vector<std::size_t> vertexCounts;
    std::vector<std::size_t> indexCounts;
    std::size_t triangles = 0;
    std::uint64_t serial = 0;        // unique per build; a pointer can be reused after a release
    bool used = false;

    ChildScene() = default;
    ChildScene(const ChildScene&) = delete;
    ChildScene& operator=(const ChildScene&) = delete;
    ChildScene(ChildScene&& o) noexcept { *this = std::move(o); }
    ChildScene& operator=(ChildScene&& o) noexcept {
        if (this != &o) {
            release();
            scene = std::exchange(o.scene, nullptr);
            geoms = std::move(o.geoms);
            o.geoms.clear();
            memberKeys = std::move(o.memberKeys);
            vertexCounts = std::move(o.vertexCounts);
            indexCounts = std::move(o.indexCounts);
            triangles = o.triangles;
            serial = o.serial;
            used = o.used;
        }
        return *this;
    }
    ~ChildScene() { release(); }
    void release() {
        for (RTCGeometry g : geoms) rtcReleaseGeometry(g);
        geoms.clear();
        if (scene != nullptr) rtcReleaseScene(scene);
        scene = nullptr;
    }
};

// What one top-level instance was built from. The top level is reused only if every entry is
// identical: the same child BUILD (by serial) under a bit-identical transform.
struct TopLevelEntry {
    std::uint64_t childSerial = 0;
    glm::mat4 transform{1.0f};
};

// Embree reports every allocation (positive) and free (negative) here. `peak` is reset at the start
// of each update so it answers "what did this frame's update need at most".
struct MemoryCounter {
    std::atomic<std::int64_t> current{0};
    std::atomic<std::int64_t> peak{0};
};

static bool countEmbreeMemory(void* user, ssize_t bytes, bool /*post*/) {
    auto* c = static_cast<MemoryCounter*>(user);
    const std::int64_t now = c->current.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::int64_t seen = c->peak.load(std::memory_order_relaxed);
    while (now > seen && !c->peak.compare_exchange_weak(seen, now, std::memory_order_relaxed)) {
    }
    return true;   // never refuse an allocation; this only watches
}

struct EmbreeScene::Impl {
    MemoryCounter memory;   // declared first: the device reports its last frees into it on release
    RTCDevice device = nullptr;
    unsigned threads = 0;
    RTCScene top = nullptr;
    // Keyed by the first member's key; the full member list is verified before any reuse.
    std::unordered_map<std::uint64_t, ChildScene> children;
    std::vector<TopLevelEntry> topEntries;
    std::vector<TopLevelRef> refs;
    // groupMembers[g] = the Snapshot::meshes indices of group g, in child geomID order.
    std::vector<std::vector<std::uint32_t>> groupMembers;
    std::uint64_t nextSerial = 1;

    ~Impl() {
        children.clear();   // before the device
        if (top != nullptr) rtcReleaseScene(top);
        if (device != nullptr) rtcReleaseDevice(device);
    }
};

EmbreeScene::EmbreeScene() = default;
EmbreeScene::~EmbreeScene() = default;

void EmbreeScene::reset() {
    impl_.reset();
    geometryCount_ = 0;
}

Result<void> EmbreeScene::build(const Snapshot& snapshot, unsigned buildThreads) {
    return update(snapshot, buildThreads, BvhReuse::Rebuild);
}

namespace {

// One member of a child scene as this frame presents it.
struct MemberView {
    std::uint64_t key = 0;
    const std::vector<glm::vec3>* positions = nullptr;
    const std::vector<std::uint32_t>* indices = nullptr;
};

// Every x, y, z of `rows` (float4, w ignored) bit-identical to `p`. Compared as integers and OR-ed
// over a block before testing, so the loop vectorises: a per-vertex memcmp was measured at 65-180 ms
// on the Tree of Life's vertices, which would have eaten most of what reuse saves.
[[nodiscard]] bool sameVertexBitsRange(const float* rows, const glm::vec3* p, std::size_t first,
                                       std::size_t n) {
    static_assert(sizeof(glm::vec3) == 3 * sizeof(float));
    const auto* a = reinterpret_cast<const std::uint32_t*>(rows);
    const auto* b = reinterpret_cast<const std::uint32_t*>(p);
    constexpr std::size_t kBlock = 4096;
    for (std::size_t start = first; start < n; start += kBlock) {
        const std::size_t end = std::min(n, start + kBlock);
        std::uint32_t diff = 0;
        for (std::size_t v = start; v < end; ++v) {
            diff |= (a[v * 4 + 0] ^ b[v * 3 + 0]) | (a[v * 4 + 1] ^ b[v * 3 + 1]) |
                    (a[v * 4 + 2] ^ b[v * 3 + 2]);
        }
        if (diff != 0) return false;
    }
    return true;
}

// The same, split across our threads when the mesh is big enough to be worth it. The Tree of Life
// compares several million vertices every frame, and one core cannot read them fast enough.
[[nodiscard]] bool sameVertexBits(const float* rows, const glm::vec3* p, std::size_t n,
                                  unsigned threads) {
    constexpr std::size_t kParallelVertices = 262'144;
    if (threads <= 1 || n < kParallelVertices) return sameVertexBitsRange(rows, p, 0, n);
    std::atomic<bool> same{true};
    const std::size_t chunk = (n + threads - 1) / threads;
    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    for (unsigned t = 1; t < threads; ++t) {
        const std::size_t a = std::min(n, t * chunk);
        const std::size_t b = std::min(n, a + chunk);
        if (a >= b) break;
        workers.emplace_back([&, a, b] {
            if (!sameVertexBitsRange(rows, p, a, b)) same.store(false, std::memory_order_relaxed);
        });
    }
    if (!sameVertexBitsRange(rows, p, 0, std::min(n, chunk))) same.store(false, std::memory_order_relaxed);
    for (auto& w : workers) w.join();
    return same.load();
}

// True when `child` was built from exactly `members`. With `compareData` false only the structure
// is checked -- that is BvhReuse::TrustStructure, change detection switched off, for tests.
[[nodiscard]] bool childMatches(const ChildScene& child, const std::vector<MemberView>& members,
                                bool compareData, unsigned threads) {
    if (child.memberKeys.size() != members.size()) return false;
    for (std::size_t k = 0; k < members.size(); ++k) {
        const MemberView& m = members[k];
        if (child.memberKeys[k] != m.key) return false;
        if (child.vertexCounts[k] != m.positions->size()) return false;
        if (child.indexCounts[k] != m.indices->size()) return false;
    }
    if (!compareData) return true;
    for (std::size_t k = 0; k < members.size(); ++k) {
        const MemberView& m = members[k];
        // Embree's vertex rows are float4 with a padded w (see buildChild); compare the xyz.
        const auto* verts = static_cast<const float*>(
            rtcGetGeometryBufferData(child.geoms[k], RTC_BUFFER_TYPE_VERTEX, 0));
        const auto* idx = static_cast<const std::uint32_t*>(
            rtcGetGeometryBufferData(child.geoms[k], RTC_BUFFER_TYPE_INDEX, 0));
        if (verts == nullptr || idx == nullptr) return false;
        if (!sameVertexBits(verts, m.positions->data(), m.positions->size(), threads)) return false;
        if (std::memcmp(idx, m.indices->data(), m.indices->size() * sizeof(std::uint32_t)) != 0) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Result<ChildScene> buildChild(RTCDevice device, const std::vector<MemberView>& members,
                                            bool highQuality, unsigned threads,
                                            std::uint64_t serial) {
    ChildScene child;
    child.serial = serial;
    child.scene = rtcNewScene(device);
    if (child.scene == nullptr) return fail("pathtrace: rtcNewScene failed for a child scene");
    // Meshes were built at HIGH when they lived in the top-level scene, and keep it; procedural
    // sources took Embree's default and keep that. The build quality is not what ADR-582 changes.
    if (highQuality) rtcSetSceneBuildQuality(child.scene, RTC_BUILD_QUALITY_HIGH);

    for (std::size_t k = 0; k < members.size(); ++k) {
        const MemberView& m = members[k];
        RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);
        if (geom == nullptr) return fail("pathtrace: rtcNewGeometry failed");
        child.geoms.push_back(geom);   // owned (and released) by the child from here on

        // Embree wants 16-byte-aligned vertex rows, so the buffer is float4 with a padded w. Passing
        // a tight float3 array works by accident until the last vertex sits at the end of a page.
        auto* verts = static_cast<float*>(rtcSetNewGeometryBuffer(
            geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 4 * sizeof(float), m.positions->size()));
        if (verts == nullptr) return fail("pathtrace: vertex buffer allocation failed");
        const glm::vec3* p = m.positions->data();
        for (std::size_t v = 0; v < m.positions->size(); ++v) {
            verts[v * 4 + 0] = p[v].x;
            verts[v * 4 + 1] = p[v].y;
            verts[v * 4 + 2] = p[v].z;
            verts[v * 4 + 3] = 0.0f;
        }
        auto* idx = static_cast<std::uint32_t*>(rtcSetNewGeometryBuffer(
            geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(std::uint32_t),
            m.indices->size() / 3));
        if (idx == nullptr) return fail("pathtrace: index buffer allocation failed");
        std::copy(m.indices->begin(), m.indices->end(), idx);

        rtcCommitGeometry(geom);
        const unsigned id = rtcAttachGeometry(child.scene, geom);
        if (id != static_cast<unsigned>(k)) {
            return fail("pathtrace: embree assigned child geomID {} where {} was expected", id, k);
        }
        child.memberKeys.push_back(m.key);
        child.vertexCounts.push_back(m.positions->size());
        child.indexCounts.push_back(m.indices->size());
        child.triangles += m.indices->size() / 3;
    }
    // Joined, never `rtcCommitScene`: with Embree's internal tasking a join uses exactly the
    // threads that call it and no pool (ADR-351), which is what lets several small children build
    // side by side on one thread each.
    joinCommit(child.scene, threads);
    return child;
}

} // namespace

Result<void> EmbreeScene::update(const Snapshot& snapshot, unsigned buildThreads, BvhReuse mode) {
    auto ok = updateInner(snapshot, buildThreads, mode);
    // A failed update can leave children and top level disagreeing. Nothing half-built is ever
    // traced or reused: the next call starts from nothing.
    if (!ok) reset();
    return ok;
}

Result<void> EmbreeScene::updateInner(const Snapshot& snapshot, unsigned buildThreads, BvhReuse mode) {
    lastUpdate_ = BvhUpdateStats{};
    if (snapshot.meshes.empty() && snapshot.instanced.empty()) {
        return fail("pathtrace: cannot build an Embree scene from a snapshot with no geometry");
    }
    for (std::size_t mi = 0; mi < snapshot.meshes.size(); ++mi) {
        if (!snapshot.meshes[mi].valid()) {
            return fail("pathtrace: mesh {} ('{}') is not valid geometry", mi,
                        snapshot.meshes[mi].entityName);
        }
    }
    for (std::size_t oi = 0; oi < snapshot.instanced.size(); ++oi) {
        if (!snapshot.instanced[oi].valid()) {
            return fail("pathtrace: instanced object {} ('{}') is not valid geometry", oi,
                        snapshot.instanced[oi].name);
        }
    }

    const unsigned threads = std::max(1u, buildThreads);
    if (mode == BvhReuse::Rebuild || !impl_ || impl_->threads != threads) {
        reset();
    }
    if (!impl_) {
        // `threads=N` bounds Embree's own pool; joined commits use our threads regardless.
        const std::string config = fmt::format("threads={}", threads);
        auto impl = std::make_unique<Impl>();
        impl->device = rtcNewDevice(config.c_str());
        if (impl->device == nullptr) {
            return fail("pathtrace: rtcNewDevice failed (config '{}'), error {}", config,
                        static_cast<int>(rtcGetDeviceError(nullptr)));
        }
        rtcSetDeviceErrorFunction(impl->device, &embreeError, nullptr);
        rtcSetDeviceMemoryMonitorFunction(impl->device, &countEmbreeMemory, &impl->memory);
        impl->threads = threads;
        impl_ = std::move(impl);
        lastUpdate_.deviceCreated = true;
    }
    Impl& im = *impl_;
    const bool compareData = mode != BvhReuse::TrustStructure;
    im.memory.peak.store(im.memory.current.load());
    struct RecordMemory {   // on every return path, including the early "nothing changed" one
        BvhUpdateStats& stats;
        MemoryCounter& memory;
        ~RecordMemory() {
            stats.heldBytes = memory.current.load();
            stats.peakBytes = memory.peak.load();
        }
    } recordMemory{lastUpdate_, im.memory};

    // ---- the structure: a pure function of the snapshot -------------------------------------
    //
    // Meshes sharing a bit-identical transform share a child, so a rigid assembly (the Tree of
    // Life is 33 meshes under three drifting transforms) keeps the single-level BVH it traced with
    // before -- instancing each mesh separately would make every canopy ray descend a dozen
    // overlapping BVHs. A rigged mesh is always alone: its vertices change every frame and must
    // not drag the scenery into its rebuild.
    std::vector<std::vector<std::uint32_t>> groups;
    std::vector<glm::mat4> groupTransforms;
    {
        std::map<TransformKey, std::size_t> byTransform;
        for (std::uint32_t mi = 0; mi < snapshot.meshes.size(); ++mi) {
            const TriangleMesh& m = snapshot.meshes[mi];
            const glm::mat4 xf = bvhTransform(m);
            if (m.deforming) {
                groups.push_back({mi});
                groupTransforms.push_back(xf);
                continue;
            }
            const auto [it, inserted] = byTransform.try_emplace(transformKey(xf), groups.size());
            if (inserted) {
                groups.emplace_back();
                groupTransforms.push_back(xf);
            }
            groups[it->second].push_back(mi);
        }
    }

    for (auto& [key, child] : im.children) child.used = false;

    // Every child the frame needs, in top-level order: the mesh groups, then the procedurals.
    struct Need {
        std::vector<MemberView> members;
        bool highQuality = true;
        ChildScene* child = nullptr;          // resolved: reused, or freshly built
        std::optional<ChildScene> built;      // filled by the build phase
    };
    std::vector<Need> needs;
    needs.reserve(groups.size() + snapshot.instanced.size());
    for (const auto& g : groups) {
        Need n;
        for (std::uint32_t mi : g) {
            const TriangleMesh& m = snapshot.meshes[mi];
            n.members.push_back(MemberView{meshKey(m), &bvhPositions(m), &m.indices});
        }
        needs.push_back(std::move(n));
    }
    for (const InstancedObject& obj : snapshot.instanced) {
        Need n;
        n.members.push_back(MemberView{proceduralKey(obj), &obj.source.positions, &obj.source.indices});
        // Procedural sources took Embree's default quality before ADR-582 and keep it.
        n.highQuality = false;
        needs.push_back(std::move(n));
    }

    // ---- phase 1: what can be kept ----------------------------------------------------------
    std::vector<std::size_t> toBuild;
    {
        const auto t0 = Clock::now();
        for (std::size_t i = 0; i < needs.size(); ++i) {
            Need& n = needs[i];
            const std::uint64_t key = n.members.front().key;
            auto it = im.children.find(key);
            if (it != im.children.end() && it->second.used) {
                // Two children of one frame claiming one identity would let a rebuild free a
                // scene this frame already handed to the top level. Keys are unique by
                // construction; this says so loudly if that ever stops being true.
                return fail("pathtrace: two BVH children share the key {:#x}", key);
            }
            if (it != im.children.end() && childMatches(it->second, n.members, compareData, threads)) {
                it->second.used = true;
                n.child = &it->second;
                lastUpdate_.trianglesReused += it->second.triangles;
            } else {
                if (it != im.children.end()) {
                    // Released BEFORE the replacement is built, so peak memory never holds both.
                    // The old top level may still reference it; Embree's reference count keeps
                    // it alive until that top level is released, and the top level is always
                    // rebuilt when any child is.
                    im.children.erase(it);
                }
                toBuild.push_back(i);
            }
        }
        lastUpdate_.objects = needs.size();
        lastUpdate_.compareSeconds += since(t0);
    }
    // Children nothing referenced this frame are released now, so memory tracks the frame.
    for (auto it = im.children.begin(); it != im.children.end();) {
        it = it->second.used ? std::next(it) : im.children.erase(it);
    }

    // ---- phase 2: build what changed ----------------------------------------------------------
    //
    // A big child is built by all our threads together. Small children are built whole, one per
    // thread, several at once: Glowmere rebuilds 16-28 characters of ~2,000 triangles a frame, and
    // committing each through Embree's pool cost ~3 ms apiece in wake-ups, not in building. Each
    // child's BVH is built the same way whatever else is being built beside it, so a reused frame
    // and a from-scratch frame still hold identical structures.
    if (!toBuild.empty()) {
        const auto t0 = Clock::now();
        std::vector<std::size_t> small;
        for (std::size_t i : toBuild) {
            std::size_t tris = 0;
            for (const auto& m : needs[i].members) tris += m.indices->size() / 3;
            if (tris >= kJoinedBuildTriangles) {
                const std::uint64_t serial = im.nextSerial++;
                auto built = buildChild(im.device, needs[i].members, needs[i].highQuality, threads, serial);
                if (!built) return std::unexpected(built.error());
                needs[i].built.emplace(std::move(*built));
            } else {
                small.push_back(i);
            }
        }
        // Serials are assigned in need order before any thread starts, so they do not depend on
        // which thread finishes first.
        std::vector<std::uint64_t> serials(small.size());
        for (auto& sn : serials) sn = im.nextSerial++;
        std::vector<std::optional<Error>> errors(small.size());
        std::atomic<std::size_t> next{0};
        const auto worker = [&] {
            for (std::size_t k = next.fetch_add(1); k < small.size(); k = next.fetch_add(1)) {
                Need& n = needs[small[k]];
                auto built = buildChild(im.device, n.members, n.highQuality, 1, serials[k]);
                if (built) {
                    n.built.emplace(std::move(*built));
                } else {
                    errors[k] = built.error();
                }
            }
        };
        const unsigned workers = static_cast<unsigned>(std::min<std::size_t>(threads, small.size()));
        std::vector<std::thread> pool;
        for (unsigned t = 1; t < workers; ++t) pool.emplace_back(worker);
        worker();
        for (auto& t : pool) t.join();
        for (auto& e : errors) {
            if (e) return std::unexpected(std::move(*e));
        }
        for (std::size_t i : toBuild) {
            Need& n = needs[i];
            ++lastUpdate_.objectsBuilt;
            lastUpdate_.trianglesBuilt += n.built->triangles;
            n.built->used = true;
            const std::uint64_t key = n.members.front().key;
            n.child = &im.children.emplace(key, std::move(*n.built)).first->second;
            n.built.reset();
        }
        lastUpdate_.objectSeconds = since(t0);
    }

    std::vector<std::uint64_t> childSerials;   // per group, then per procedural object
    std::vector<RTCScene> childScenes;
    childSerials.reserve(needs.size());
    childScenes.reserve(needs.size());
    for (const Need& n : needs) {
        childSerials.push_back(n.child->serial);
        childScenes.push_back(n.child->scene);
    }

    // ---- the top level ------------------------------------------------------------------------
    std::vector<TopLevelEntry> entries;
    std::vector<TopLevelRef> refs;
    std::size_t instanceTotal = groups.size();
    for (const auto& obj : snapshot.instanced) instanceTotal += obj.transforms.size();
    entries.reserve(instanceTotal);
    refs.reserve(instanceTotal);
    for (std::size_t g = 0; g < groups.size(); ++g) {
        entries.push_back(TopLevelEntry{childSerials[g], groupTransforms[g]});
        refs.push_back(TopLevelRef{false, static_cast<std::uint32_t>(g), 0});
    }
    for (std::size_t oi = 0; oi < snapshot.instanced.size(); ++oi) {
        const InstancedObject& obj = snapshot.instanced[oi];
        for (std::size_t ii = 0; ii < obj.transforms.size(); ++ii) {
            entries.push_back(TopLevelEntry{childSerials[groups.size() + oi], obj.transforms[ii]});
            refs.push_back(TopLevelRef{true, static_cast<std::uint32_t>(oi),
                                       static_cast<std::uint32_t>(ii)});
        }
    }
    lastUpdate_.topLevelInstances = entries.size();

    bool topSame = im.top != nullptr && entries.size() == im.topEntries.size();
    {
        const auto t0 = Clock::now();
        if (entries.size() == im.topEntries.size()) {
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].childSerial != im.topEntries[i].childSerial) topSame = false;
                if (!sameBits(entries[i].transform, im.topEntries[i].transform)) {
                    ++lastUpdate_.transformsChanged;
                }
            }
            if (compareData && lastUpdate_.transformsChanged > 0) topSame = false;
        }
        lastUpdate_.compareSeconds += since(t0);
    }

    // The per-frame tables always follow THIS snapshot: its indices are what a hit resolves into.
    im.refs = std::move(refs);
    im.groupMembers = std::move(groups);
    geometryCount_ = im.refs.size();
    if (topSame) {
        return {};
    }

    const auto topStart = Clock::now();
    if (im.top != nullptr) {
        rtcReleaseScene(im.top);
        im.top = nullptr;
    }
    im.topEntries.clear();
    im.top = rtcNewScene(im.device);
    if (im.top == nullptr) return fail("pathtrace: rtcNewScene failed");
    rtcSetSceneBuildQuality(im.top, RTC_BUILD_QUALITY_HIGH);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const TopLevelRef& ref = im.refs[i];
        const std::size_t childIndex = ref.instanced ? im.groupMembers.size() + ref.objectIndex
                                                     : ref.objectIndex;
        RTCGeometry inst = rtcNewGeometry(im.device, RTC_GEOMETRY_TYPE_INSTANCE);
        if (inst == nullptr) return fail("pathtrace: rtcNewGeometry(INSTANCE) failed");
        rtcSetGeometryInstancedScene(inst, childScenes[childIndex]);
        // FLOAT4X4, not FLOAT3X4. `glm::mat4` is four columns of FOUR floats; a 3x4 layout
        // expects three floats per column, so handing it `value_ptr` makes it read each column's
        // w as the next column's x. Every instance then lands somewhere arbitrary -- measured as
        // rays missing all three test cubes entirely. COLUMN_MAJOR because that is glm's order.
        rtcSetGeometryTransform(inst, 0, RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR,
                                glm::value_ptr(entries[i].transform));
        rtcCommitGeometry(inst);
        const unsigned id = rtcAttachGeometry(im.top, inst);
        rtcReleaseGeometry(inst);
        if (id != static_cast<unsigned>(i)) {
            return fail("pathtrace: embree assigned instance geomID {} where {} was expected", id, i);
        }
    }
    joinCommit(im.top, threads);
    im.topEntries = std::move(entries);
    lastUpdate_.topLevelBuilt = true;
    lastUpdate_.topLevelSeconds = since(topStart);

    if (rtcGetDeviceError(im.device) != RTC_ERROR_NONE) {
        return fail("pathtrace: embree reported an error while building the BVH");
    }
    return {};
}

SurfaceHit EmbreeScene::intersect(const Snapshot& snapshot, const glm::vec3& origin,
                                  const glm::vec3& direction, float tnear, float tfar) const {
    SurfaceHit out;
    if (!impl_ || impl_->top == nullptr) return out;

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

    rtcIntersect1(impl_->top, &rh);
    if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID) return out;

    // Every top-level geometry is an instance, so instID[0] is the top-level id and geomID is the
    // geometry within the child. Reading geomID as a top-level id is the classic instancing bug:
    // every hit resolves to whatever is first.
    if (rh.hit.instID[0] == RTC_INVALID_GEOMETRY_ID) return out;
    const auto topId = static_cast<std::size_t>(rh.hit.instID[0]);
    if (topId >= impl_->refs.size()) return out;
    const TopLevelRef& ref = impl_->refs[topId];

    const TriangleMesh* meshPtr = nullptr;
    std::size_t mi = 0;
    if (ref.instanced) {
        if (ref.objectIndex >= snapshot.instanced.size()) return out;
        mi = ref.objectIndex;
        meshPtr = &snapshot.instanced[mi].source;
    } else {
        // A mesh group: the child's geomID is the member's position in the group.
        if (ref.objectIndex >= impl_->groupMembers.size()) return out;
        const auto& members = impl_->groupMembers[ref.objectIndex];
        if (rh.hit.geomID >= members.size()) return out;
        mi = members[rh.hit.geomID];
        if (mi >= snapshot.meshes.size()) return out;
        meshPtr = &snapshot.meshes[mi];
    }
    const TriangleMesh& mesh = *meshPtr;

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
    if (!impl_ || impl_->top == nullptr) return false;
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
    rtcOccluded1(impl_->top, &r);
    // Embree signals "occluded" by setting tfar negative.
    return r.tfar < 0.0f;
}

const scene::Material& EmbreeScene::materialOf(const Snapshot& snap, const SurfaceHit& hit) {
    return hit.instanced ? snap.instanced[hit.meshIndex].source.material
                         : snap.meshes[hit.meshIndex].material;
}

} // namespace avgen::pathtrace
