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

// The camera's true up: `up` made perpendicular to the view (what a look-at would use), so a rotation through a
// portal carries it exactly even when the view is nearly vertical.
glm::vec3 orthoUp(glm::vec3 eye, glm::vec3 target, glm::vec3 up) {
    const glm::vec3 f = target - eye;
    if (glm::dot(f, f) < 1e-12f) return up;
    const glm::vec3 d = glm::normalize(f);
    const glm::vec3 u = up - d * glm::dot(up, d);
    return glm::dot(u, u) > 1e-10f ? glm::normalize(u) : up;
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
    if (activeLeg_ != 0) activateLeg(0);
    if (legs_.empty()) legs_.resize(1);
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

void Composition::setPortalToRoot(PortalBlock block) {
    if (legs_.empty()) legs_.resize(1);
    portal_ = std::move(block);
    portalToRoot_ = true;
    portalCrossing_ = 1e30;
}

// ADR-1895: the journey's legs. The active leg's state is in the portal members; a leg not active keeps its own in
// legs_. Switching swaps them, so all the single-portal code reads the active leg unchanged.
Result<void> Composition::addPortalLeg(PortalBlock block, std::unique_ptr<Composition> world) {
    if (!world) return fail("portal leg: no world");
    if (legs_.empty()) return fail("portal leg: the first leg is the scene's 'portal'");
    LegState leg;
    leg.portal = std::move(block);
    leg.world = std::move(world);
    if (const LunarWorldBlock& lw = leg.world->lunarWorld(); lw.world) {
        leg.portal.exit.y += lw.world->sample(leg.portal.exit.x, leg.portal.exit.z);
    }
    legs_.push_back(std::move(leg));
    return {};
}

void Composition::swapActiveLeg(LegState& with) {
    std::swap(portal_, with.portal);
    std::swap(portalWorld_, with.world);
    std::swap(portalToRoot_, with.toRoot);
    std::swap(portalTarget_, with.target);
    std::swap(portalCrossing_, with.crossing);
    std::swap(portalKnots_, with.knots);
    std::swap(portalTangents_, with.tangents);
    std::swap(portalKnotTimes_, with.knotTimes);
    std::swap(portalArriveAim_, with.arriveAim);
    std::swap(portalArriveUp_, with.arriveUp);
}

void Composition::activateLeg(int k) {
    if (k == activeLeg_ || k < 0 || k >= static_cast<int>(legs_.size())) return;
    swapActiveLeg(legs_[static_cast<std::size_t>(activeLeg_)]); // the active leg's state back into its slot
    swapActiveLeg(legs_[static_cast<std::size_t>(k)]);          // and leg k's out of its slot
    activeLeg_ = k;
}

const Composition* Composition::legWorld(int k) const {
    if (k == activeLeg_) return destWorld();
    const LegState& l = legs_[static_cast<std::size_t>(k)];
    return l.target != nullptr ? l.target : l.world.get();
}

double Composition::legCrossing(int k) const {
    return k == activeLeg_ ? portalCrossing_ : legs_[static_cast<std::size_t>(k)].crossing;
}

bool Composition::legReturns(int k) const {
    const Composition* w = legWorld(k);
    return w != nullptr && w->portalToRoot_ && w->portalTarget_ == this;
}

double Composition::legReturnCrossing(int k) const {
    return legReturns(k) ? legWorld(k)->portalCrossing_ : 1e30;
}

int Composition::legForTime(double t) const {
    const int n = static_cast<int>(legs_.size());
    int k = 0;
    while (k < n && legReturns(k)) {
        const double back = legReturnCrossing(k);
        if (back > 1e28 || t < back) break;
        ++k;
    }
    return k; // n: every leg's world has been and gone
}

// ADR-1885: find the root (up the chain of portals that led here) and, for a portal back to it, link both ends.
void Composition::linkPortalRoot() {
    if (portalWorld_) portalWorld_->parentPortal_ = this;
    for (LegState& l : legs_) {
        if (l.world) l.world->parentPortal_ = this;
    }
    if (!portalToRoot_) return;
    Composition* root = this;
    while (root->parentPortal_ != nullptr) root = root->parentPortal_;
    if (root == this) return;
    portalTarget_ = root;
}

std::vector<double> Composition::journeyCrossings() const {
    std::vector<double> out;
    // ADR-1895: a root with legs out and back: each leg's crossing, then its world's crossing home
    if (legs_.size() > 1 || (portalWorld_ && portalWorld_->portalToRoot_)) {
        for (int k = 0; k < static_cast<int>(legs_.size()); ++k) {
            out.push_back(legCrossing(k));
            if (!legReturns(k)) break;
            out.push_back(legReturnCrossing(k));
        }
        return out;
    }
    const Composition* c = this;
    for (int guard = 0; c != nullptr && guard < 16; ++guard) {
        out.push_back(c->portalCrossing_);
        if (c->portalToRoot_ || !c->portalWorld_) break;
        c = c->portalWorld_.get();
    }
    return out;
}

int Composition::lookIndexAt(double t) const {
    const std::vector<double> crossings = journeyCrossings();
    // how many crossings the journey has passed at t; undecidable while a crossing a look depends on is unknown
    int passed = 0;
    for (const double c : crossings) {
        if (c > 1e29) {
            for (const Look& l : looks_) {
                if (l.fromCrossing > passed) return -2;
            }
            break;
        }
        if (t >= c) ++passed;
        else break;
    }
    int best = -1;
    for (std::size_t i = 0; i < looks_.size(); ++i) {
        if (looks_[i].fromCrossing <= passed && (best < 0 || looks_[i].fromCrossing >= looks_[static_cast<std::size_t>(best)].fromCrossing)) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

void Composition::applyLook(int index) {
    if (params_ == nullptr) return;
    // the authored values of everything any look touches, captured once: the look "-1"
    if (lookOriginals_.empty()) {
        for (const Look& l : looks_) {
            for (const auto& [path, v] : l.values) {
                params::IParameter* p = params_->find(prefix_ + path);
                if (p == nullptr) {
                    log::warn("look '{}': no parameter '{}'", l.name, prefix_ + path);
                    continue;
                }
                if (std::any_of(lookOriginals_.begin(), lookOriginals_.end(), [&](const auto& o) { return o.first == p; })) continue;
                std::vector<float> orig(p->componentCount());
                for (std::size_t c = 0; c < orig.size(); ++c) orig[c] = p->baseComponent(c);
                lookOriginals_.emplace_back(p, std::move(orig));
            }
        }
    }
    // a look is an authored value (the base, as a preset sets it), not a modulation: the final follows the base as
    // the frame's reset made it, and the routes that run after may still modulate it
    const auto set = [](params::IParameter* p, const std::vector<float>& v) {
        for (std::size_t c = 0; c < p->componentCount() && c < v.size(); ++c) p->setBaseComponent(c, v[c]);
        p->resetFinal();
    };
    for (const auto& [p, v] : lookOriginals_) set(p, v);
    if (index >= 0) {
        for (const auto& [path, v] : looks_[static_cast<std::size_t>(index)].values) {
            if (params::IParameter* p = params_->find(prefix_ + path)) set(p, v);
        }
    }
    appliedLook_ = index;
    log::info("look: '{}' from {:.3f} s", index >= 0 ? looks_[static_cast<std::size_t>(index)].name : std::string("authored"), currentTime_);
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
    // ADR-1885: on the root, after its own crossing the camera is the journey's path carried back by the return (once
    // the arrival is built: building it reads this world's own camera at the crossing)
    // ADR-1895: the latest leg the camera has crossed into whose world returns here (its knots built)
    for (int k = static_cast<int>(legs_.size()) - 1; k >= 0; --k) {
        const double c = legCrossing(k);
        if (c > 1e28 || seconds < c || !legReturns(k)) continue;
        const bool built = k == activeLeg_ ? !portalKnots_.empty() : !legs_[static_cast<std::size_t>(k)].knots.empty();
        if (built) return legWorld(k)->portalCameraAt(seconds);
    }
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
    const Composition* dest = portalTarget_ != nullptr ? portalTarget_ : portalWorld_.get();
    if (dest == nullptr) return 0.0f;
    if (const LunarWorldBlock& lw = dest->lunarWorld(); lw.world) return lw.world->sample(x, z);
    if (const AttractorWorldBlock& aw = dest->attractorWorld(); aw.world) return aw.world->sample(glm::vec2(x, z));
    return 0.0f;
}

namespace {
struct PortalFrame {
    glm::vec3 entry, exit;
    glm::mat3 r;
    [[nodiscard]] glm::vec3 d(glm::vec3 v) const { return r * v; }
    [[nodiscard]] glm::vec3 p(glm::vec3 v) const { return d(v - entry) + exit; }
};

// An orthonormal frame from a forward and an up (up made perpendicular to forward).
glm::mat3 frameOf(glm::vec3 forward, glm::vec3 up) {
    const glm::vec3 f = glm::normalize(forward);
    const glm::vec3 u = glm::normalize(up - f * glm::dot(up, f));
    return glm::mat3(f, u, glm::cross(f, u));
}
} // namespace

// The portal's rotation from this world to the destination. A yaw (exitYaw) unless the block names the exit's own
// crossing direction (exitAxis): then the rotation takes the entry's (the corruption's axis, entryUp) onto the exit's
// (exitAxis, exitUp) -- Gate C 2: the Moon's floor (axis down) onto a canyon wall (axis level), so that a camera
// dropping into the floor with its up pointing ahead leaves the wall level, its up the world's.
glm::mat3 Composition::portalRotation() const { return legRotation(portal_, scene_.corruption.axis); }

glm::mat3 Composition::legRotation(const PortalBlock& b, glm::vec3 entryAxis) {
    if (glm::length(b.exitAxis) > 1e-6f) {
        const glm::vec3 axis = glm::normalize(entryAxis);
        return frameOf(b.exitAxis, b.exitUp) * glm::transpose(frameOf(axis, b.entryUp));
    }
    const float yaw = glm::radians(b.exitYaw);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    // columns: the images of x, y, z (x' = cy x - sy z, z' = sy x + cy z)
    return glm::mat3(glm::vec3(cy, 0.0f, sy), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(-sy, 0.0f, cy));
}

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
    const PortalFrame f{c.focus, portal_.exit, portalRotation()};
    const glm::vec3 p0 = f.p(arrive->eye);
    const glm::vec3 v0 = f.d((arrive->eye - before->eye) / 0.05f);
    portalArriveAim_ = glm::normalize(f.d(arrive->target - arrive->eye));
    portalArriveUp_ = f.d(orthoUp(arrive->eye, arrive->target, arrive->up));
    portalKnots_.push_back(p0);
    portalKnotTimes_.push_back(tc);
    for (const glm::vec4& w : portal_.flight) {
        portalKnots_.emplace_back(w.x, portalGround(w.x, w.z) + w.y, w.z);
        portalKnotTimes_.push_back(w.w);
    }
    glm::vec3 joinVelocity{0.0f};
    bool joined = false;
    if (portal_.join.z > 0.0f && destWorld() != nullptr) {
        if (const auto j0 = destWorld()->flythroughAt(static_cast<double>(portal_.join.x)),
                       j1 = destWorld()->flythroughAt(static_cast<double>(portal_.join.x) + 0.05);
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

std::optional<Composition::PortalPose> Composition::portalCameraAt(double seconds) const { return legCameraAt(activeLeg_, seconds); }

// ADR-1895: the camera leg k hands its world at `seconds` (any leg: a world reached by an earlier leg keeps asking for
// its own leg's path, whichever leg is active now)
std::optional<Composition::PortalPose> Composition::legCameraAt(int k, double seconds) const {
    const bool active = k == activeLeg_ || legs_.empty();
    const LegState* stored = active ? nullptr : &legs_[static_cast<std::size_t>(k)];
    const PortalBlock& block = active ? portal_ : stored->portal;
    const Composition* dest = active ? destWorld() : (stored->target != nullptr ? stored->target : stored->world.get());
    if (dest == nullptr) return std::nullopt;
    const double crossing = active ? portalCrossing_ : stored->crossing;
    const auto& q = active ? portalKnots_ : stored->knots;
    const auto& m = active ? portalTangents_ : stored->tangents;
    const auto& qt = active ? portalKnotTimes_ : stored->knotTimes;
    const glm::vec3 arriveAim = active ? portalArriveAim_ : stored->arriveAim;
    const glm::vec3 arriveUp = active ? portalArriveUp_ : stored->arriveUp;
    // the leg's site: the live corruption for the active leg; a leg's own entry, or the authored corruption, otherwise
    glm::vec3 entry = scene_.corruption.focus, entryAxis = scene_.corruption.axis;
    if (!active) {
        entry = block.hasEntry ? block.entry : corruption_.focus;
        entryAxis = block.hasEntry ? block.entryAxis : corruption_.axis;
        if (lunar_.world) entry.y += lunar_.world->sample(entry.x, entry.z);
    }
    const PortalFrame f{entry, block.exit, legRotation(block, entryAxis)};
    if (seconds < crossing || q.empty()) {
        const auto own = ownCameraAt(seconds);
        if (!own) return std::nullopt;
        return PortalPose{f.p(own->eye), f.p(own->target), f.d(orthoUp(own->eye, own->target, own->up))};
    }
    const auto tc = static_cast<float>(crossing);
    const auto s = static_cast<float>(seconds);
    const bool joined = block.join.z > 0.0f;
    const auto destAt = [&](float t) { return dest->flythroughAt(static_cast<double>(block.join.x + (t - block.join.y) * block.join.z)); };
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
    glm::vec3 ahead = at(s + 1.2f) - p;
    // a climb is not looked up at (a quarter of it): rising over a crater rim to hover above a portal, the view stays
    // on the ground ahead instead of tipping into the black sky; a descent is looked down at in full
    if (ahead.y > 0.0f) ahead.y *= 0.25f;
    glm::vec3 look = glm::length(ahead) > 1e-4f ? glm::normalize(ahead + glm::vec3(0.0f, -0.1f, 0.0f) * glm::length(ahead)) : arriveAim;
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    if (joined) {
        // into the destination's own view (its aim and bank) over the 3 s before the join, eased at both ends
        const float tj = block.join.y;
        const float u = std::clamp((s - (tj - 3.0f)) / 3.0f, 0.0f, 1.0f);
        const float wj = u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);
        if (const auto d = destAt(std::max(s, tj)); d && wj > 0.0f) {
            look = glm::normalize(glm::mix(look, glm::normalize(d->target - d->eye), wj));
            up = glm::normalize(glm::mix(up, d->up, wj));
        }
    }
    for (const auto& a : block.aims) { // a look toward something passing (eased in and out)
        const float wa = smooth(a[3], a[3] + 1.2f, s) * (1.0f - smooth(a[4] - 1.2f, a[4], s)) * 0.75f;
        const glm::vec3 to = glm::vec3(a[0], a[1], a[2]) - p;
        if (wa > 0.0f && glm::length(to) > 1e-3f) look = glm::normalize(glm::mix(look, glm::normalize(to), wa));
    }
    const float w = smooth(tc, tc + 1.6f, s);
    const glm::vec3 dir = glm::normalize(glm::mix(arriveAim, look, w));
    return PortalPose{p, p + dir * 100.0f, orthoUp(p, p + dir, glm::normalize(glm::mix(arriveUp, up, w)))};
}

// The destination: its camera (the source's camera carried through the portal's transform until the camera crosses
// the entry plane inside the opening; then its own flight, which leaves from exactly where and how the source
// arrived), its update, and the view the renderer composites.
void Composition::updatePortal(const FrameTime& time) {
    if (destWorld() == nullptr || corruption_.targets.empty()) {
        scene_.portal = {};
        return;
    }
    const Corruption& c = scene_.corruption;
    const glm::vec3 entry = c.focus;
    const glm::vec3 axis = glm::normalize(c.axis);
    // the crossing: the first time the camera's path passes the entry plane inside the opening's reach (a pure
    // function of the path, found once)
    // ADR-1895: a later leg is searched from its previous leg's return home
    const double from = activeLeg_ > 0 ? legReturnCrossing(activeLeg_ - 1) : 0.0;
    if (portalCrossing_ > 1e29 && from < 1e28 && ownCameraAt(from)) {
        constexpr double kStep = 1.0 / 60.0, kEnd = 240.0;
        const float reach = 2.0f * std::max(c.width, c.height);
        auto prev = ownCameraAt(from);
        for (double t = from + kStep; t <= kEnd; t += kStep) {
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
    // (ADR-1895: on a later leg the root's scene camera is still last leg's until the end of the frame: carry the
    // journey's camera, as portalCameraAt does, never the stale one)
    if (const auto pose = portalCameraAt(t); pose && (crossed || cameraPath_ || !scene_.attractorWorld.active() || activeLeg_ > 0)) {
        cam.position = pose->eye;
        cam.target = pose->target;
        cam.up = pose->up;
    } else {
        // not crossed: this frame's own camera (with everything composed on it) carried through the portal
        const PortalFrame f{entry, portal_.exit, portalRotation()};
        cam.position = f.p(scene_.camera.position);
        cam.target = f.p(scene_.camera.target);
        cam.up = f.d(orthoUp(scene_.camera.position, scene_.camera.target, scene_.camera.up));
    }
    Composition* dest = destWorld();
    if (portalTarget_ == nullptr) {
        const int leg = activeLeg_;
        dest->setCameraPath([this, leg](double s) { return legCameraAt(leg, s); }); // ADR-1895: bound to its leg
        dest->setCameraOverride(cam);
        dest->update(time);
    }
    // (a return to the root does not update it: the root is updating, and places its own camera on this path)
    // an exit cut into the destination's own geometry (exitAxis given): its rock behind the exit plane is not drawn,
    // so the camera carried behind the wall sees out through the opening; on the far side the camera soon flies away
    // from the wall, and the clip ends a second after the crossing (an infinite plane must not cut the world later)
    // ADR-1885: a return to the root clips it only once the root has handed the frame on (its own crossing): before
    // that the root is the world the camera is in, and the plane would cut its own foreground away
    const bool destElsewhere = portalTarget_ == nullptr || (dest->portalCrossing_ < 1e28 && t >= dest->portalCrossing_);
    if (glm::length(portal_.exitAxis) > 1e-6f && t < portalCrossing_ + 1.0 && destElsewhere) {
        const glm::vec3 n = glm::normalize(portal_.exitAxis);
        dest->scene_.portalClip = glm::vec4(n, -glm::dot(n, portal_.exit));
    } else {
        dest->scene_.portalClip = glm::vec4(0.0f);
    }
    scene_.portal.world = &dest->scene();
    scene_.portal.crossed = crossed;
}

} // namespace avgen::scene
