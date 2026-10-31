// ADR-1879/1880/1882: the Composition's lunar world and its portal to another world (scene/composition.hpp).

#include "core/log.hpp"
#include "scene/composition.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <mutex>

#include <glm/gtc/quaternion.hpp>

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
    if (cameraOverride_) {
        const Camera& o = *cameraOverride_;
        scene_.camera.position = o.position;
        scene_.camera.target = o.target;
        scene_.camera.up = o.up;
        scene_.camera.fovYRadians = o.fovYRadians;
        scene_.camera.nearPlane = o.nearPlane;
        scene_.camera.farPlane = o.farPlane;
        scene_.camera.lens = o.lens;
    } else if (cameraPath_) {
        if (const auto pose = cameraPath_(currentTime_)) {
            scene_.camera.position = pose->eye;
            scene_.camera.target = pose->target;
            scene_.camera.up = pose->up;
        }
    }
}

std::optional<Composition::PortalPose> Composition::ownCameraAt(double seconds) const {
    if (cameraPath_) return cameraPath_(seconds);
    return flythroughAt(seconds);
}

std::optional<Composition::PortalPose> Composition::flythroughAt(double seconds) const {
    const AttractorWorldBlock& a = scene_.attractorWorld;
    if (a.active() && a.world->flight.size() > 2 && a.cameraMode == static_cast<int>(AttractorCameraMode::Flight)) {
        const AttractorCameraPose pose = attractorCamera(*a.world, AttractorCameraMode::Flight, seconds);
        PortalPose out{pose.eye, pose.target, glm::vec3(0.0f, 1.0f, 0.0f)};
        const glm::vec3 forward = pose.target - pose.eye;
        if (pose.rollDegrees != 0.0f && glm::dot(forward, forward) > 1e-12f) {
            out.up = glm::angleAxis(glm::radians(pose.rollDegrees), glm::normalize(forward)) * out.up;
        }
        return out;
    }
    return std::nullopt;
}

float Composition::portalGround(float x, float z) const {
    if (!portalWorld_) return 0.0f;
    if (const LunarWorldBlock& lw = portalWorld_->lunarWorld(); lw.world) return lw.world->sample(x, z);
    if (const AttractorWorldBlock& aw = portalWorld_->attractorWorld(); aw.world) return aw.world->sample(glm::vec2(x, z));
    return 0.0f;
}

namespace {
struct PortalFrame {
    glm::vec3 entry, exit;
    float cy, sy;
    [[nodiscard]] glm::vec3 d(glm::vec3 v) const { return glm::vec3(cy * v.x - sy * v.z, v.y, sy * v.x + cy * v.z); }
    [[nodiscard]] glm::vec3 p(glm::vec3 v) const { return d(v - entry) + exit; }
};
} // namespace

// The arrival: from where and how the camera crosses (C1), through the authored knots (heights above the
// destination's ground), and, with a join, into the destination's own flythrough.
void Composition::buildPortalArrival() {
    portalKnots_.clear();
    portalTangents_.clear();
    portalKnotTimes_.clear();
    const auto tc = static_cast<float>(portalCrossing_);
    const auto arrive = ownCameraAt(portalCrossing_), before = ownCameraAt(portalCrossing_ - 0.05);
    if (!arrive || !before) return;
    const Corruption& c = scene_.corruption;
    const float yaw = glm::radians(portal_.exitYaw);
    const PortalFrame f{c.focus, portal_.exit, std::cos(yaw), std::sin(yaw)};
    const glm::vec3 p0 = f.p(arrive->eye);
    const glm::vec3 v0 = f.d((arrive->eye - before->eye) / 0.05f);
    portalArriveAim_ = glm::normalize(f.d(arrive->target - arrive->eye));
    portalArriveUp_ = f.d(arrive->up);
    portalKnots_.push_back(p0);
    portalKnotTimes_.push_back(tc);
    for (const glm::vec4& w : portal_.flight) {
        portalKnots_.emplace_back(w.x, portalGround(w.x, w.z) + w.y, w.z);
        portalKnotTimes_.push_back(w.w);
    }
    glm::vec3 joinVelocity{0.0f};
    bool joined = false;
    if (portal_.join.z > 0.0f && portalWorld_) {
        if (const auto j0 = portalWorld_->flythroughAt(static_cast<double>(portal_.join.x)),
                       j1 = portalWorld_->flythroughAt(static_cast<double>(portal_.join.x) + 0.05);
            j0 && j1) {
            portalKnots_.push_back(j0->eye);
            portalKnotTimes_.push_back(portal_.join.y);
            joinVelocity = (j1->eye - j0->eye) / 0.05f * portal_.join.z;
            joined = true;
        }
        if (!joined) { // the destination's flythrough is not up yet: build again next frame
            portalKnots_.clear();
            portalKnotTimes_.clear();
            return;
        }
    }
    const std::size_t n = portalKnots_.size();
    portalTangents_.resize(n);
    portalTangents_[0] = v0;
    for (std::size_t i = 1; i < n; ++i) {
        portalTangents_[i] = (i + 1 < n) ? (portalKnots_[i + 1] - portalKnots_[i - 1]) / std::max(portalKnotTimes_[i + 1] - portalKnotTimes_[i - 1], 1e-3f)
                                         : (portalKnots_[i] - portalKnots_[i - 1]) / std::max(portalKnotTimes_[i] - portalKnotTimes_[i - 1], 1e-3f);
    }
    if (joined) portalTangents_[n - 1] = joinVelocity;
}

std::optional<Composition::PortalPose> Composition::portalCameraAt(double seconds) const {
    if (!portalWorld_) return std::nullopt;
    const Corruption& c = scene_.corruption;
    const float yaw = glm::radians(portal_.exitYaw);
    const PortalFrame f{c.focus, portal_.exit, std::cos(yaw), std::sin(yaw)};
    if (seconds < portalCrossing_ || portalKnots_.empty()) {
        const auto own = ownCameraAt(seconds);
        if (!own) return std::nullopt;
        return PortalPose{f.p(own->eye), f.p(own->target), f.d(own->up)};
    }
    const auto tc = static_cast<float>(portalCrossing_);
    const auto s = static_cast<float>(seconds);
    const auto& q = portalKnots_;
    const auto& m = portalTangents_;
    const auto& qt = portalKnotTimes_;
    const bool joined = portal_.join.z > 0.0f;
    const auto destAt = [&](float t) { return portalWorld_->flythroughAt(static_cast<double>(portal_.join.x + (t - portal_.join.y) * portal_.join.z)); };
    const auto at = [&](float t) -> glm::vec3 {
        if (q.size() < 2) return q[0] + m[0] * (t - tc);
        if (t >= qt.back()) {
            if (joined) {
                if (const auto d = destAt(t)) return d->eye;
            }
            return q.back() + m.back() * (t - qt.back());
        }
        std::size_t i = 0;
        while (i + 2 < q.size() && t > qt[i + 1]) ++i;
        const float h = qt[i + 1] - qt[i];
        return hermite(q[i], m[i] * h, q[i + 1], m[i + 1] * h, std::clamp((t - qt[i]) / h, 0.0f, 1.0f));
    };
    const glm::vec3 p = at(s);
    const glm::vec3 ahead = at(s + 1.2f) - p;
    glm::vec3 look = glm::length(ahead) > 1e-4f ? glm::normalize(ahead + glm::vec3(0.0f, -0.1f, 0.0f) * glm::length(ahead)) : portalArriveAim_;
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    if (joined) {
        // into the destination's own view (its aim and bank) over the last 1.5 s before the join
        const float tj = portal_.join.y;
        const float wj = smooth(tj - 1.5f, tj, s);
        if (const auto d = destAt(std::max(s, tj)); d && wj > 0.0f) {
            look = glm::normalize(glm::mix(look, glm::normalize(d->target - d->eye), wj));
            up = glm::normalize(glm::mix(up, d->up, wj));
        }
    }
    const float w = smooth(tc, tc + 1.6f, s);
    const glm::vec3 dir = glm::normalize(glm::mix(portalArriveAim_, look, w));
    return PortalPose{p, p + dir * 100.0f, glm::normalize(glm::mix(portalArriveUp_, up, w))};
}

// The destination: its camera (the source's camera carried through the portal's transform until the camera crosses
// the entry plane inside the opening; then its own flight, which leaves from exactly where and how the source
// arrived), its update, and the view the renderer composites.
void Composition::updatePortal(const FrameTime& time) {
    if (!portalWorld_ || corruption_.targets.empty()) {
        scene_.portal = {};
        return;
    }
    const Corruption& c = scene_.corruption;
    const glm::vec3 entry = c.focus;
    const glm::vec3 axis = glm::normalize(c.axis);
    // the crossing: the first time the camera's path passes the entry plane inside the opening's reach (a pure
    // function of the path, found once)
    if (portalCrossing_ > 1e29 && ownCameraAt(0.0)) {
        constexpr double kStep = 1.0 / 60.0, kEnd = 240.0;
        const float reach = 2.0f * std::max(c.width, c.height);
        auto prev = ownCameraAt(0.0);
        for (double t = kStep; t <= kEnd; t += kStep) {
            const auto cur = ownCameraAt(t);
            if (!cur) break;
            const float s0 = glm::dot(prev->eye - entry, axis), s1 = glm::dot(cur->eye - entry, axis);
            if (s0 < 0.0f && s1 >= 0.0f) {
                const float u = -s0 / (s1 - s0);
                const glm::vec3 hit = glm::mix(prev->eye, cur->eye, u);
                const glm::vec3 lateral = (hit - entry) - axis * glm::dot(hit - entry, axis);
                if (glm::length(lateral) < reach) {
                    portalCrossing_ = t - kStep + kStep * static_cast<double>(u);
                    log::info("portal: the camera crosses the entry plane at {:.3f} s ({} m from the centre)", portalCrossing_,
                              static_cast<int>(glm::length(lateral)));
                    buildPortalArrival();
                    break;
                }
            }
            prev = cur;
        }
        if (portalCrossing_ > 1e29) portalCrossing_ = 1e28; // never: do not search again
    }
    if (portalCrossing_ < 1e28 && portalKnots_.empty()) buildPortalArrival();
    Camera cam = scene_.camera;
    cam.farPlane = std::max(cam.farPlane, 150000.0f);
    const double t = currentTime_;
    const bool crossed = t >= portalCrossing_ && !portalKnots_.empty();
    if (const auto pose = portalCameraAt(t); pose && (crossed || cameraPath_ || !scene_.attractorWorld.active())) {
        cam.position = pose->eye;
        cam.target = pose->target;
        cam.up = pose->up;
    } else {
        // not crossed: this frame's own camera (with everything composed on it) carried through the portal
        const float yaw = glm::radians(portal_.exitYaw);
        const PortalFrame f{entry, portal_.exit, std::cos(yaw), std::sin(yaw)};
        cam.position = f.p(scene_.camera.position);
        cam.target = f.p(scene_.camera.target);
        cam.up = f.d(scene_.camera.up);
    }
    portalWorld_->setCameraPath([this](double s) { return portalCameraAt(s); });
    portalWorld_->setCameraOverride(cam);
    portalWorld_->update(time);
    scene_.portal.world = &portalWorld_->scene();
    scene_.portal.crossed = crossed;
}

} // namespace avgen::scene
