// ADR-1879/1880/1882: the Composition's lunar world and its portal to another world (scene/composition.hpp).

#include "core/log.hpp"
#include "scene/composition.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace avgen::scene {

namespace {

std::string sanitisePrefix(std::string name) {
    for (auto& c : name) {
        if (c == '/' || c == ' ' || c == '.') c = '_';
    }
    return name;
}

std::mutex gLunarMutex;
std::shared_ptr<const LunarWorld> gLunarCache;

std::shared_ptr<const LunarWorld> acquireLunarWorld(const LunarGen& gen) {
    std::lock_guard lock(gLunarMutex);
    if (gLunarCache && gLunarCache->hash == gen.hash()) return gLunarCache;
    gLunarCache = generateLunarWorld(gen);
    log::info("lunarWorld: seed {} generated in {:.2f} s ({} triangles, {} boulders)", gen.seed,
              gLunarCache->generateSeconds, gLunarCache->triangles, gLunarCache->rockCount);
    return gLunarCache;
}

float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 hermite(glm::vec3 p0, glm::vec3 m0, glm::vec3 p1, glm::vec3 m1, float u) {
    const float u2 = u * u, u3 = u2 * u;
    return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1;
}

} // namespace

void Composition::setLunarWorld(LunarWorldBlock block) {
    lunar_ = std::move(block);
    if (lunar_.enabled) lunar_.world = acquireLunarWorld(lunar_.gen);
    dirty_ = true;
}

void Composition::flattenLunar() {
    if (!lunar_.enabled || !lunar_.world) return;
    const std::string program = sanitisePrefix(prefix_) + lunar_.program;
    for (std::size_t c = 0; c < lunar_.world->chunks.size(); ++c) {
        const LunarChunk& ch = lunar_.world->chunks[c];
        MeshData mesh;
        mesh.name = fmt::format("lunar.surface{}", c);
        mesh.vertices.resize(ch.positions.size());
        for (std::size_t v = 0; v < ch.positions.size(); ++v) mesh.vertices[v] = Vertex{ch.positions[v], ch.normals[v], ch.uvs[v]};
        mesh.indices = ch.indices;
        const auto id = static_cast<MeshId>(scene_.meshes.size());
        scene_.meshes.push_back(std::move(mesh));
        Entity& e = scene_.addEntity(fmt::format("lunar.surface{}", c), id);
        e.material.baseColor = glm::vec4(0.12f, 0.12f, 0.12f, 1.0f);
        e.material.roughness = 0.95f;
        e.material.metallic = 0.0f;
        e.material.program = program;
        e.castsShadow = true;
    }
}

Result<void> Composition::setPortal(PortalBlock block, std::unique_ptr<Composition> world) {
    if (!world) return fail("portal: no world");
    portal_ = std::move(block);
    portalWorld_ = std::move(world);
    portalCrossing_ = 1e30;
    // over a generated ground, the exit's height is above that ground (as the flight's are)
    if (const LunarWorldBlock& lw = portalWorld_->lunarWorld(); lw.world) {
        portal_.exit.y += lw.world->sample(portal_.exit.x, portal_.exit.z);
        log::info("portal: the exit stands at ({:.1f}, {:.1f}, {:.1f}) in '{}'", portal_.exit.x, portal_.exit.y, portal_.exit.z,
                  portal_.world);
    }
    return {};
}

void Composition::applyCameraOverride() {
    if (!cameraOverride_) return;
    const Camera& o = *cameraOverride_;
    scene_.camera.position = o.position;
    scene_.camera.target = o.target;
    scene_.camera.up = o.up;
    scene_.camera.fovYRadians = o.fovYRadians;
    scene_.camera.nearPlane = o.nearPlane;
    scene_.camera.farPlane = o.farPlane;
    scene_.camera.lens = o.lens;
}

// The destination: its camera (the source's camera carried through the portal's transform until the flight crosses
// the entry plane; then its own flight, which leaves from exactly where and how the source arrived), its update, and
// the view the renderer composites.
void Composition::updatePortal(const FrameTime& time) {
    if (!portalWorld_ || corruption_.targets.empty()) {
        scene_.portal = {};
        return;
    }
    const glm::vec3 entry = corruption_.focus;
    const glm::vec3 axis = glm::normalize(corruption_.axis);
    const float yaw = glm::radians(portal_.exitYaw);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const auto mapD = [&](glm::vec3 d) { return glm::vec3(cy * d.x - sy * d.z, d.y, sy * d.x + cy * d.z); };
    const auto mapP = [&](glm::vec3 p) { return mapD(p - entry) + portal_.exit; };
    const AttractorWorldBlock& a = scene_.attractorWorld;
    const bool flight = a.active() && a.world->flight.size() > 2 && a.cameraMode == static_cast<int>(AttractorCameraMode::Flight);
    // the crossing: the first flight sample past the entry plane, interpolated (a pure function of the flight)
    if (flight && portalCrossing_ > 1e29) {
        const auto& f = a.world->flight;
        for (std::size_t k = 1; k < f.size(); ++k) {
            const float s0 = glm::dot(glm::vec3(f[k - 1]) - entry, axis), s1 = glm::dot(glm::vec3(f[k]) - entry, axis);
            if (s0 < 0.0f && s1 >= 0.0f) {
                portalCrossing_ = (static_cast<double>(k - 1) + static_cast<double>(-s0 / (s1 - s0))) / AttractorWorld::kFlightRate;
                log::info("portal: the flight crosses the entry plane at {:.3f} s", portalCrossing_);
                break;
            }
        }
    }
    const double t = currentTime_;
    Camera cam = scene_.camera;
    cam.farPlane = std::max(cam.farPlane, 150000.0f);
    if (!flight || t < portalCrossing_) {
        cam.position = mapP(scene_.camera.position);
        cam.target = mapP(scene_.camera.target);
        cam.up = mapD(scene_.camera.up);
    } else {
        // the arrival, from the flight at the crossing
        const auto tc = static_cast<float>(portalCrossing_);
        const AttractorCameraPose arrive = attractorCamera(*a.world, AttractorCameraMode::Flight, tc);
        const AttractorCameraPose before = attractorCamera(*a.world, AttractorCameraMode::Flight, tc - 0.1);
        const glm::vec3 p0 = mapP(arrive.eye);
        const glm::vec3 v0 = mapD((arrive.eye - before.eye) / 0.1f);
        const glm::vec3 f0 = glm::normalize(mapD(arrive.target - arrive.eye));
        std::vector<glm::vec3> q{p0};
        std::vector<float> qt{tc};
        const LunarWorldBlock& lw = portalWorld_->lunarWorld();
        for (const glm::vec4& w : portal_.flight) {
            const float ground = lw.world ? lw.world->sample(w.x, w.z) : 0.0f;
            q.emplace_back(w.x, ground + w.y, w.z);
            qt.push_back(w.w);
        }
        std::vector<glm::vec3> m(q.size());
        m[0] = v0;
        for (std::size_t i = 1; i < q.size(); ++i) {
            m[i] = (i + 1 < q.size()) ? (q[i + 1] - q[i - 1]) / std::max(qt[i + 1] - qt[i - 1], 1e-3f)
                                      : (q[i] - q[i - 1]) / std::max(qt[i] - qt[i - 1], 1e-3f);
        }
        const auto at = [&](float s) {
            if (q.size() < 2) return p0 + v0 * (s - tc);
            if (s >= qt.back()) return q.back() + m.back() * (s - qt.back());
            std::size_t i = 0;
            while (i + 2 < q.size() && s > qt[i + 1]) ++i;
            const float h = qt[i + 1] - qt[i];
            return hermite(q[i], m[i] * h, q[i + 1], m[i + 1] * h, std::clamp((s - qt[i]) / h, 0.0f, 1.0f));
        };
        const auto s = static_cast<float>(t);
        const glm::vec3 p = at(s);
        const glm::vec3 look = glm::normalize(at(s + 1.2f) - p + glm::vec3(0.0f, -0.1f, 0.0f) * glm::length(at(s + 1.2f) - p));
        const float w = smooth(tc, tc + 1.6f, s);
        const glm::vec3 dir = glm::normalize(glm::mix(f0, look, w));
        cam.position = p;
        cam.target = p + dir * 100.0f;
        cam.up = glm::normalize(glm::mix(mapD(scene_.camera.up), glm::vec3(0.0f, 1.0f, 0.0f), w));
    }
    portalWorld_->setCameraOverride(cam);
    portalWorld_->update(time);
    scene_.portal.world = &portalWorld_->scene();
    scene_.portal.crossed = flight && t >= portalCrossing_;
}

} // namespace avgen::scene
