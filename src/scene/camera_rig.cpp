#include "scene/camera_rig.hpp"

#include <array>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace avgen::scene {
namespace {

using nlohmann::json;

constexpr const char* kPlacementNames[] = {"free", "spline"};
constexpr const char* kTransitionNames[] = {"cut", "blend"};

[[nodiscard]] glm::vec3 readVec3(const json& j, const char* key, glm::vec3 fallback) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 3 || !(*it)[0].is_number()) {
        return fallback;
    }
    return glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
}

[[nodiscard]] json writeVec3(glm::vec3 v) { return json::array({v.x, v.y, v.z}); }

} // namespace

const char* cameraPlacementName(CameraPlacement placement) {
    return kPlacementNames[static_cast<std::size_t>(placement)];
}

std::optional<CameraPlacement> cameraPlacementFromName(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kPlacementNames); ++i) {
        if (name == kPlacementNames[i]) {
            return static_cast<CameraPlacement>(i);
        }
    }
    return std::nullopt;
}

const char* shotTransitionName(ShotTransition transition) {
    return kTransitionNames[static_cast<std::size_t>(transition)];
}

std::span<const ShotTransition> allShotTransitions() {
    static const auto all = [] {
        std::array<ShotTransition, std::size(kTransitionNames)> out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<ShotTransition>(i);
        }
        return out;
    }();
    return all;
}

std::optional<ShotTransition> shotTransitionFromName(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kTransitionNames); ++i) {
        if (name == kTransitionNames[i]) {
            return static_cast<ShotTransition>(i);
        }
    }
    return std::nullopt;
}

const char* activeCameraReasonName(ActiveCameraReason reason) {
    switch (reason) {
    case ActiveCameraReason::Shot: return "shot";
    case ActiveCameraReason::Event: return "event";
    case ActiveCameraReason::Default: break;
    }
    return "default";
}

const char* viewportCameraName(ViewportCamera mode) {
    switch (mode) {
    case ViewportCamera::Editor: return "editor";
    case ViewportCamera::Through: return "through";
    case ViewportCamera::Film: break;
    }
    return "film";
}

std::string cameraSlug(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0) {
            out.push_back(static_cast<char>(std::tolower(u)));
        }
    }
    if (out.empty()) {
        return "camera";
    }
    // A path segment that starts with a digit is legal but reads as an index in every place a
    // parameter path is shown, so it is prefixed rather than left to be misread.
    if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), 'c');
    }
    return out;
}

std::string CameraRig::channelPrefix() const {
    // The main camera's channels are the ones every project already has. This single line is what
    // makes a pre-ADR-245 project render byte for byte as it did: nothing re-homes, nothing renames.
    if (id == kMainCamera) {
        return "camera/";
    }
    return "cameras/" + slug + "/";
}

// ---- the collection -----------------------------------------------------------------------------

void CameraDirection::ensureMainCamera() {
    if (find(kMainCamera) != nullptr) {
        return;
    }
    CameraRig main;
    main.id = kMainCamera;
    main.name = "Main";
    main.slug.clear(); // the legacy prefix; see CameraRig::channelPrefix
    // The Auto-director's camera, so it is the one the Auto-director may use. Every other camera
    // has to be opted in.
    main.autoDirectorEligible = true;
    cameras.insert(cameras.begin(), std::move(main));
    nextId = std::max<CameraId>(nextId, kMainCamera + 1);
}

CameraId CameraDirection::addCamera(CameraRig rig) {
    ensureMainCamera();
    rig.id = nextId++;
    if (rig.name.empty()) {
        rig.name = "Camera " + std::to_string(rig.id);
    }
    std::string base = rig.slug.empty() ? cameraSlug(rig.name) : cameraSlug(rig.slug);
    std::string slug = base;
    // A slug is a parameter path, so two cameras sharing one would silently share their animation.
    for (int suffix = 2; true; ++suffix) {
        const bool taken = std::ranges::any_of(cameras, [&](const CameraRig& c) { return c.slug == slug; });
        if (!taken) {
            break;
        }
        slug = base + std::to_string(suffix);
    }
    rig.slug = std::move(slug);
    cameras.push_back(std::move(rig));
    return cameras.back().id;
}

bool CameraDirection::removeCamera(CameraId id) {
    if (id == kMainCamera || id == kNoCamera) {
        return false;
    }
    const auto it = std::ranges::find(cameras, id, &CameraRig::id);
    if (it == cameras.end()) {
        return false;
    }
    cameras.erase(it);
    // A shot pointing at a camera that is gone would resolve to nothing and fall through to the
    // default, which looks exactly like the shot list being ignored. It goes with the camera.
    std::erase_if(shots, [id](const CameraShot& s) { return s.camera == id; });
    if (defaultCamera == id) {
        defaultCamera = kMainCamera;
    }
    return true;
}

CameraRig* CameraDirection::find(CameraId id) {
    const auto it = std::ranges::find(cameras, id, &CameraRig::id);
    return it == cameras.end() ? nullptr : &*it;
}

const CameraRig* CameraDirection::find(CameraId id) const {
    const auto it = std::ranges::find(cameras, id, &CameraRig::id);
    return it == cameras.end() ? nullptr : &*it;
}

const CameraRig* CameraDirection::findByName(std::string_view name) const {
    const auto it = std::ranges::find(cameras, name, &CameraRig::name);
    return it == cameras.end() ? nullptr : &*it;
}

std::string CameraDirection::nameOf(CameraId id) const {
    const CameraRig* rig = find(id);
    return rig != nullptr ? rig->name : std::string();
}

Result<void> CameraDirection::validate() const {
    std::unordered_set<CameraId> ids;
    std::unordered_set<std::string> slugs;
    for (const CameraRig& rig : cameras) {
        if (rig.id == kNoCamera) {
            return fail("camera '{}' has no id", rig.name);
        }
        if (!ids.insert(rig.id).second) {
            return fail("two cameras share the id {}", rig.id);
        }
        if (rig.id != kMainCamera && rig.slug.empty()) {
            return fail("camera '{}' has no slug: its parameters would have no path", rig.name);
        }
        if (!slugs.insert(rig.slug).second) {
            return fail("two cameras share the parameter slug '{}'", rig.slug);
        }
        if (rig.id >= nextId) {
            return fail("camera '{}' has id {}, which the next-id counter ({}) would hand out again",
                        rig.name, rig.id, nextId);
        }
        if (rig.fovDegrees <= 0.0f || rig.fovDegrees >= 180.0f) {
            return fail("camera '{}' has a field of view of {} degrees", rig.name, rig.fovDegrees);
        }
        if (rig.focalLength < 0.0f) {
            return fail("camera '{}' has a negative focal length", rig.name);
        }
        if (rig.eventLeadSeconds < 0.0 || rig.eventTailSeconds < 0.0 || rig.eventBlendSeconds < 0.0) {
            return fail("camera '{}' has a negative event lead, tail or blend", rig.name);
        }
        if (rig.placement == CameraPlacement::Spline && rig.spline.empty() && rig.id != kMainCamera) {
            return fail("camera '{}' rides a spline but names none", rig.name);
        }
    }
    if (!cameras.empty() && ids.find(kMainCamera) == ids.end()) {
        return fail("the camera list has no main camera");
    }
    if (defaultCamera != kNoCamera && !cameras.empty() && ids.find(defaultCamera) == ids.end()) {
        return fail("the default camera {} is not in the camera list", defaultCamera);
    }
    for (const CameraShot& shot : shots) {
        if (ids.find(shot.camera) == ids.end()) {
            return fail("a shot at {:.3f} s names camera {}, which does not exist", shot.startSeconds,
                        shot.camera);
        }
        if (!(shot.endSeconds > shot.startSeconds)) {
            return fail("a shot on camera {} ends at {:.3f} s, at or before its start {:.3f} s",
                        shot.camera, shot.endSeconds, shot.startSeconds);
        }
        if (shot.blendSeconds < 0.0) {
            return fail("a shot on camera {} has a negative blend", shot.camera);
        }
    }
    return {};
}

json CameraShot::toJson() const {
    json s = json::object();
    s["camera"] = camera;
    s["start"] = startSeconds;
    s["end"] = endSeconds;
    s["transition"] = shotTransitionName(transition);
    if (transition == ShotTransition::Blend) {
        s["blend"] = blendSeconds;
    }
    if (locked) {
        s["locked"] = true;
    }
    if (!label.empty()) {
        s["label"] = label;
    }
    // Written only when it is not the default, so an untouched project is byte-identical.
    if (origin == Origin::Directed) {
        s["origin"] = "directed";
    }
    return s;
}

Result<CameraShot> CameraShot::fromJson(const json& sh) {
    if (!sh.is_object()) {
        return fail("a camera shot must be a JSON object");
    }
    CameraShot shot;
    shot.camera = sh.value("camera", kNoCamera);
    shot.startSeconds = sh.value("start", 0.0);
    shot.endSeconds = sh.value("end", 0.0);
    if (const auto t = sh.find("transition"); t != sh.end() && t->is_string()) {
        const auto parsed = shotTransitionFromName(t->get<std::string>());
        if (!parsed) {
            return fail("shot transition '{}' is not one of cut, blend", t->get<std::string>());
        }
        shot.transition = *parsed;
    }
    shot.blendSeconds = sh.value("blend", 0.0);
    shot.locked = sh.value("locked", false);
    shot.label = sh.value("label", std::string());
    if (const auto o = sh.find("origin"); o != sh.end() && o->is_string()) {
        const std::string origin = o->get<std::string>();
        if (origin == "directed") {
            shot.origin = Origin::Directed;
        } else if (origin != "authored") {
            return fail("shot origin '{}' is not one of authored, directed", origin);
        }
    }
    return shot;
}

json CameraDirection::toJson() const {
    json cams = json::array();
    for (const CameraRig& rig : cameras) {
        json c = json::object();
        c["id"] = rig.id;
        c["name"] = rig.name;
        if (!rig.slug.empty()) {
            c["slug"] = rig.slug;
        }
        // The main camera's placement and transform live in the composition's own `camera` block;
        // writing them here too would be the second source of truth this design exists to avoid.
        if (rig.id != kMainCamera) {
            c["placement"] = cameraPlacementName(rig.placement);
            c["position"] = writeVec3(rig.position);
            c["target"] = writeVec3(rig.target);
            c["fov"] = rig.fovDegrees;
            if (!rig.aimNode.empty()) {
                c["aimNode"] = rig.aimNode;
                c["aimOffset"] = writeVec3(rig.aimOffset);
            }
            if (!rig.followNode.empty()) {
                c["followNode"] = rig.followNode;
                c["followOffset"] = writeVec3(rig.followOffset);
                // Written only when set, so a rig that follows the way every existing rig follows
                // serialises byte-identically to before.
                if (rig.followLocal) {
                    c["followLocal"] = true;
                }
                if (rig.followLagSeconds > 0.0) {
                    c["followLagSeconds"] = rig.followLagSeconds;
                }
                if (rig.followClearance > 0.0f) {
                    c["followClearance"] = rig.followClearance;
                }
            }
            if (rig.focalLength > 0.0f) {
                c["focalLength"] = rig.focalLength;
            }
            if (rig.placement == CameraPlacement::Spline) {
                c["spline"] = rig.spline;
                c["splineT"] = rig.splineT;
                c["lookAhead"] = rig.lookAhead;
                c["splineOffset"] = writeVec3(rig.splineOffset);
            }
        }
        c["autoDirector"] = rig.autoDirectorEligible;
        if (rig.priority != 0) {
            c["priority"] = rig.priority;
        }
        if (!rig.eventScenario.empty()) {
            c["eventScenario"] = rig.eventScenario;
            c["eventLead"] = rig.eventLeadSeconds;
            c["eventTail"] = rig.eventTailSeconds;
            c["eventBlend"] = rig.eventBlendSeconds;
            if (!rig.eventBeats.empty()) {
                c["eventBeats"] = rig.eventBeats;
            }
        }
        cams.push_back(std::move(c));
    }
    json shotArray = json::array();
    for (const CameraShot& shot : shots) {
        shotArray.push_back(shot.toJson());
    }
    return json{{"cameras", std::move(cams)},
                {"shots", std::move(shotArray)},
                {"default", defaultCamera},
                {"nextId", nextId}};
}

Result<CameraDirection> CameraDirection::fromJson(const json& doc) {
    if (!doc.is_object()) {
        return fail("'cameraDirection' must be a JSON object");
    }
    CameraDirection out;
    if (const auto cams = doc.find("cameras"); cams != doc.end()) {
        if (!cams->is_array()) {
            return fail("cameraDirection.cameras must be an array");
        }
        for (const json& c : *cams) {
            if (!c.is_object()) {
                return fail("every entry of cameraDirection.cameras must be an object");
            }
            CameraRig rig;
            rig.id = c.value("id", kNoCamera);
            rig.name = c.value("name", std::string());
            rig.slug = c.value("slug", std::string());
            if (rig.id != kMainCamera) {
                if (const auto p = c.find("placement"); p != c.end() && p->is_string()) {
                    const auto parsed = cameraPlacementFromName(p->get<std::string>());
                    if (!parsed) {
                        return fail("camera placement '{}' is not one of free, spline",
                                    p->get<std::string>());
                    }
                    rig.placement = *parsed;
                }
                rig.position = readVec3(c, "position", rig.position);
                rig.target = readVec3(c, "target", rig.target);
                rig.fovDegrees = c.value("fov", rig.fovDegrees);
                rig.aimNode = c.value("aimNode", std::string());
                rig.aimOffset = readVec3(c, "aimOffset", rig.aimOffset);
                rig.followNode = c.value("followNode", std::string());
                rig.followOffset = readVec3(c, "followOffset", rig.followOffset);
                rig.followLocal = c.value("followLocal", rig.followLocal);
                rig.followLagSeconds = c.value("followLagSeconds", rig.followLagSeconds);
                rig.followClearance = c.value("followClearance", rig.followClearance);
                rig.focalLength = c.value("focalLength", rig.focalLength);
                rig.spline = c.value("spline", rig.spline);
                rig.splineT = c.value("splineT", rig.splineT);
                rig.lookAhead = c.value("lookAhead", rig.lookAhead);
                rig.splineOffset = readVec3(c, "splineOffset", rig.splineOffset);
            }
            rig.autoDirectorEligible = c.value("autoDirector", rig.id == kMainCamera);
            rig.priority = c.value("priority", 0);
            rig.eventScenario = c.value("eventScenario", std::string());
            rig.eventLeadSeconds = c.value("eventLead", rig.eventLeadSeconds);
            rig.eventTailSeconds = c.value("eventTail", rig.eventTailSeconds);
            rig.eventBlendSeconds = c.value("eventBlend", rig.eventBlendSeconds);
            if (const auto b = c.find("eventBeats"); b != c.end() && b->is_array()) {
                for (const json& name : *b) {
                    if (name.is_string()) {
                        rig.eventBeats.push_back(name.get<std::string>());
                    }
                }
            }
            out.cameras.push_back(std::move(rig));
        }
    }
    if (const auto s = doc.find("shots"); s != doc.end()) {
        if (!s->is_array()) {
            return fail("cameraDirection.shots must be an array");
        }
        for (const json& sh : *s) {
            if (!sh.is_object()) {
                return fail("every entry of cameraDirection.shots must be an object");
            }
            auto shot = CameraShot::fromJson(sh);
            if (!shot) {
                return std::unexpected(shot.error());
            }
            out.shots.push_back(std::move(*shot));
        }
    }
    out.defaultCamera = doc.value("default", kMainCamera);
    // The counter is restored rather than recomputed, because an id freed by a deletion must not
    // come back: a shot list saved with the camera deleted would then point at a stranger.
    CameraId highest = kMainCamera;
    for (const CameraRig& rig : out.cameras) {
        highest = std::max(highest, rig.id);
    }
    out.nextId = std::max<CameraId>(doc.value("nextId", highest + 1), highest + 1);
    out.ensureMainCamera();
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

// ---- the director -------------------------------------------------------------------------------

namespace {

// What claims the frame at an instant, before anything is blended. The whole of the priority rule.
struct Claim {
    CameraId camera = kMainCamera;
    ActiveCameraReason reason = ActiveCameraReason::Default;
    double since = 0.0;
    double until = 0.0; // <= since means unbounded
    std::string eventName;
    double blendSeconds = 0.0;
};

[[nodiscard]] const CameraShot* shotAt(const CameraDirection& direction, double seconds) {
    // Last match wins (see the header): a shot dragged over another is the one the author is
    // looking at.
    const CameraShot* chosen = nullptr;
    for (const CameraShot& shot : direction.shots) {
        if (shot.contains(seconds) && direction.find(shot.camera) != nullptr) {
            chosen = &shot;
        }
    }
    return chosen;
}

[[nodiscard]] Claim shotClaim(const CameraShot& shot) {
    Claim claim;
    claim.camera = shot.camera;
    claim.reason = ActiveCameraReason::Shot;
    claim.since = shot.startSeconds;
    claim.until = shot.endSeconds;
    claim.blendSeconds = shot.transition == ShotTransition::Blend ? shot.blendSeconds : 0.0;
    return claim;
}

[[nodiscard]] Claim claimAt(const CameraDirection& direction, std::span<const CameraEventSpan> events,
                            double seconds) {
    // 0. A locked shot is the author saying "not now". Checked before the events rather than after,
    //    because "an event wins unless the author locked the shot" is the rule, and expressing it
    //    the other way round would need the event resolution to be undone.
    const CameraShot* authored = shotAt(direction, seconds);
    if (authored != nullptr && authored->locked) {
        return shotClaim(*authored);
    }

    // 1. Events. A camera whose scenario is running outranks anything authored, because an event is
    //    the thing the piece could not know about when it was cut.
    const CameraRig* best = nullptr;
    const CameraEventSpan* bestSpan = nullptr;
    for (const CameraRig& rig : direction.cameras) {
        if (rig.eventScenario.empty()) {
            continue;
        }
        for (const CameraEventSpan& span : events) {
            if (span.name != rig.eventScenario) {
                continue;
            }
            const double from = span.startSeconds - rig.eventLeadSeconds;
            // An event with no end yet (endSeconds <= startSeconds) is one that is still running:
            // the staging system knows what has started, not what is going to stop.
            const bool open = span.endSeconds <= span.startSeconds;
            const double to = open ? std::numeric_limits<double>::infinity()
                                   : span.endSeconds + rig.eventTailSeconds;
            if (seconds < from || seconds >= to) {
                continue;
            }
            if (best == nullptr || rig.priority > best->priority ||
                (rig.priority == best->priority && rig.id < best->id)) {
                best = &rig;
                bestSpan = &span;
            }
        }
    }
    if (best != nullptr) {
        Claim claim;
        claim.camera = best->id;
        claim.reason = ActiveCameraReason::Event;
        claim.since = bestSpan->startSeconds - best->eventLeadSeconds;
        claim.until = bestSpan->endSeconds <= bestSpan->startSeconds
                          ? claim.since
                          : bestSpan->endSeconds + best->eventTailSeconds;
        claim.eventName = best->eventScenario;
        claim.blendSeconds = best->eventBlendSeconds;
        return claim;
    }

    // 2. Authored shots.
    if (authored != nullptr) {
        return shotClaim(*authored);
    }

    // 3. Nobody asked. The default camera has it, which is what makes a project with no shots and no
    //    events render exactly as it did before any of this existed.
    Claim claim;
    claim.camera = direction.defaultCamera != kNoCamera && direction.find(direction.defaultCamera) != nullptr
                       ? direction.defaultCamera
                       : kMainCamera;
    claim.reason = ActiveCameraReason::Default;
    return claim;
}

} // namespace

ActiveCameraState resolveActiveCamera(const CameraDirection& direction,
                                      std::span<const CameraEventSpan> events, double seconds) {
    const Claim claim = claimAt(direction, events, seconds);
    ActiveCameraState state;
    state.camera = claim.camera;
    state.previous = claim.camera;
    state.reason = claim.reason;
    state.sinceSeconds = claim.since;
    state.untilSeconds = claim.until;
    state.eventName = claim.eventName;
    state.name = direction.nameOf(claim.camera);
    if (const CameraRig* rig = direction.find(claim.camera); rig != nullptr) {
        state.focalLength = rig->focalLength;
    }

    const double elapsed = seconds - claim.since;
    if (claim.blendSeconds > 0.0 && elapsed >= 0.0 && elapsed < claim.blendSeconds) {
        // One step back, never two. Resolving the instant before this claim began is what makes a
        // blend a function of the clock rather than of a remembered previous frame -- and it is the
        // whole reason a blend survives a scrub. A blend that was itself running when this claim
        // landed is superseded rather than stacked: a cut does not cross-fade three pictures.
        const Claim before = claimAt(direction, events, claim.since - 1e-6);
        if (before.camera != claim.camera) {
            state.previous = before.camera;
            state.blend = static_cast<float>(elapsed / claim.blendSeconds);
            state.blend = std::clamp(state.blend, 0.0f, 1.0f);
        }
    }
    return state;
}

// ---- pose maths ---------------------------------------------------------------------------------

CameraPose blendPoses(const CameraPose& from, const CameraPose& to, float t) {
    const float k = std::clamp(t, 0.0f, 1.0f);
    CameraPose out;
    out.position = glm::mix(from.position, to.position, k);
    out.target = glm::mix(from.target, to.target, k);
    out.fovDegrees = glm::mix(from.fovDegrees, to.fovDegrees, k);
    // Only when both ends have an opinion. Mixing a stated 24 mm with an unstated 0 would sweep the
    // lens towards a pinhole, which is not a blend of two cameras -- it is one of them being wrong.
    if (from.focalLength > 0.0f && to.focalLength > 0.0f) {
        out.focalLength = glm::mix(from.focalLength, to.focalLength, k);
    } else {
        out.focalLength = k >= 0.5f ? to.focalLength : from.focalLength;
    }
    return out;
}

void ensureDistinctAim(CameraPose& pose, glm::vec3 fallbackForward) {
    if (glm::length(pose.target - pose.position) < 1e-4f) {
        pose.target = pose.position + fallbackForward;
    }
}

} // namespace avgen::scene
