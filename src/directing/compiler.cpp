#include "directing/compiler.hpp"

#include "app/cinematic.hpp"
#include "seq/events.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/effect_timing.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <set>

namespace avgen::directing {

// ---- SceneFacts -----------------------------------------------------------------------------------

const Place* SceneFacts::place(std::string_view id) const {
    const auto it = std::find_if(places.begin(), places.end(), [&](const Place& p) { return p.id == id; });
    return it == places.end() ? nullptr : &*it;
}

const std::vector<float>* SceneFacts::base(std::string_view path) const {
    for (const auto& [p, v] : parameterBases) {
        if (p == path) {
            return &v;
        }
    }
    return nullptr;
}

bool SceneFacts::hasParameter(std::string_view path) const {
    const auto& all = subjects.parameters();
    return std::find(all.begin(), all.end(), path) != all.end();
}

const Plan* SceneFacts::plan(std::string_view id) const {
    const auto it = std::find_if(plans.begin(), plans.end(), [&](const Plan& p) { return p.id == id; });
    return it == plans.end() ? nullptr : &*it;
}

// ---- provenance ------------------------------------------------------------------------------------

std::string fingerprint(const nlohmann::json& content) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const char c : content.dump()) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ULL;
    }
    return fmt::format("fnv1a64:{:016x}", h);
}

namespace {

std::string markerId(const seq::Marker& m) { return fmt::format("{}@{:.3f}", m.name, m.timeSeconds); }

std::string cameraShotId(const scene::CameraShot& s, const scene::CameraDirection& d) {
    const scene::CameraRig* rig = d.find(s.camera);
    return fmt::format("{}@{:.3f}", rig != nullptr && !rig->slug.empty() ? rig->slug : std::string("main"), s.startSeconds);
}

nlohmann::json shotJson(const seq::Shot& shot) {
    seq::Sequence one;
    one.shots.push_back(shot);
    return one.toJson().at("shots").at(0);
}

nlohmann::json rigJson(const scene::CameraRig& rig) {
    scene::CameraDirection one;
    one.cameras.push_back(rig);
    for (const nlohmann::json& c : one.toJson().at("cameras")) {
        if (c.value("slug", std::string{}) == rig.slug) {
            return c;
        }
    }
    return nlohmann::json{};
}

// Removes a content ref's content from the staging copies. True when something was removed.
bool remove(const ContentRef& ref, Staging& staged) {
    seq::Sequence& sequence = staged.sequence;
    scene::CameraDirection& cameras = staged.cameras;
    switch (ref.domain) {
    case ContentDomain::SequenceShot:
        return std::erase_if(sequence.shots, [&](const seq::Shot& s) { return s.name == ref.id; }) > 0;
    case ContentDomain::SequenceMarker:
        return std::erase_if(sequence.markers, [&](const seq::Marker& m) { return markerId(m) == ref.id; }) > 0;
    case ContentDomain::SequenceEvent:
        return std::erase_if(sequence.events, [&](const seq::SequenceEvent& e) { return e.id == ref.id; }) > 0;
    case ContentDomain::SequenceActor:
        return std::erase_if(sequence.actors, [&](const seq::Actor& a) { return a.id == ref.id; }) > 0;
    case ContentDomain::CameraShot:
        return std::erase_if(cameras.shots, [&](const scene::CameraShot& s) { return cameraShotId(s, cameras) == ref.id; }) > 0;
    case ContentDomain::CameraRig: {
        const auto rig = std::find_if(cameras.cameras.begin(), cameras.cameras.end(),
                                      [&](const scene::CameraRig& r) { return r.slug == ref.id; });
        return rig != cameras.cameras.end() && cameras.removeCamera(rig->id);
    }
    case ContentDomain::EffectInstance: return world::removeEffect(staged.effects, ref.id);
    case ContentDomain::SequenceTrack:
    case ContentDomain::TimelineTrack: return false; // not produced yet
    }
    return false;
}

} // namespace

std::optional<nlohmann::json> contentOf(const ContentRef& ref, const Staging& staged) {
    const seq::Sequence& sequence = staged.sequence;
    const scene::CameraDirection& cameras = staged.cameras;
    switch (ref.domain) {
    case ContentDomain::SequenceShot:
        for (const seq::Shot& s : sequence.shots) {
            if (s.name == ref.id) {
                return shotJson(s);
            }
        }
        break;
    case ContentDomain::SequenceMarker:
        for (const seq::Marker& m : sequence.markers) {
            if (markerId(m) == ref.id) {
                return nlohmann::json{{"name", m.name}, {"time", m.timeSeconds}, {"kind", seq::markerKindName(m.kind)}};
            }
        }
        break;
    case ContentDomain::SequenceEvent:
        for (const seq::SequenceEvent& e : sequence.events) {
            if (e.id == ref.id) {
                return e.toJson();
            }
        }
        break;
    case ContentDomain::CameraShot:
        for (const scene::CameraShot& s : cameras.shots) {
            if (cameraShotId(s, cameras) == ref.id) {
                return s.toJson();
            }
        }
        break;
    case ContentDomain::CameraRig:
        for (const scene::CameraRig& r : cameras.cameras) {
            if (r.slug == ref.id) {
                return rigJson(r);
            }
        }
        break;
    case ContentDomain::EffectInstance:
        for (const world::EffectInstance& e : staged.effects) {
            if (e.id == ref.id) {
                return e.toJson();
            }
        }
        break;
    case ContentDomain::SequenceActor:
    case ContentDomain::SequenceTrack:
    case ContentDomain::TimelineTrack: break;
    }
    return std::nullopt;
}

// ---- compilation -----------------------------------------------------------------------------------

namespace {

std::string clock(double seconds) {
    const auto minutes = static_cast<int>(seconds / 60.0);
    return fmt::format("{:02d}:{:06.3f}", minutes, seconds - (minutes * 60.0));
}

// A framing move on a place, from the engine's own presets (ADR-062/089), adjusted by what the plan
// said. Stored as the move: the shot inspector edits it as that move afterwards.
seq::ShotCamera framing(CameraMove move, const Place& place, const CameraBeat& beat, bool lowAngle) {
    app::FocalTarget target;
    target.position = place.position;
    target.radius = std::max(place.radius, 0.5f);
    target.name = place.id;
    target.preferredDistance = place.preferredCameraDistance;
    seq::CameraPreset preset = seq::CameraPreset::Wide;
    switch (move) {
    case CameraMove::Close: preset = seq::CameraPreset::Close; break;
    case CameraMove::PushIn: preset = seq::CameraPreset::Close; break;
    case CameraMove::TopDown: preset = seq::CameraPreset::TopDown; break;
    case CameraMove::Reveal:
    case CameraMove::PullOut: preset = seq::CameraPreset::Reveal; break;
    default: preset = seq::CameraPreset::Wide; break;
    }
    seq::ShotCamera cam = seq::cameraFromPreset(preset, target);
    app::Shot& m = cam.move;
    if (move == CameraMove::Hold || move == CameraMove::Chase || move == CameraMove::Follow) {
        m.endDistance = m.startDistance; // holding: the frame does not travel
        m.endAzimuth = m.startAzimuth;
        cam.samples = 2;
    }
    if (move == CameraMove::PushIn) {
        m.startDistance = std::max(m.startDistance * 2.5f, m.endDistance + 2.0f); // a push travels in
    }
    if (move == CameraMove::Orbit) {
        const float degrees = beat.degrees.value_or(90.0f);
        m.endAzimuth = m.startAzimuth + (degrees * std::numbers::pi_v<float> / 180.0f);
        m.endDistance = m.startDistance;
    }
    if (beat.distanceMetres) {
        const float radii = *beat.distanceMetres / target.radius;
        const float scale = radii / std::max(m.startDistance, 1e-3f);
        m.startDistance *= scale;
        m.endDistance *= scale;
    }
    if (lowAngle) {
        // Elevation is a height in orbit radii, not an angle (`app::Shot`); near zero is the camera
        // at the subject's feet looking along the ground.
        m.startElevation = m.endElevation = 0.04f;
        m.composition.focalLength = std::min(m.composition.focalLength > 0.0f ? m.composition.focalLength : 28.0f, 28.0f);
    }
    if (beat.heightMetres) {
        const float h = *beat.heightMetres / (target.radius * std::max(m.startDistance, 1e-3f));
        m.startElevation = m.endElevation = h;
    }
    return cam;
}

// A camera that follows a character's node every frame: the evaluated architecture (ADR-245), since
// a character's position is its simulation's. `low_angle` lowers it; the plan's height and distance
// are metres in the character's own frame.
scene::CameraRig followRig(const std::string& name, const std::string& node, CameraMove move, const CameraBeat& beat,
                           bool lowAngle) {
    scene::CameraRig rig;
    rig.name = name;
    rig.placement = scene::CameraPlacement::Free;
    rig.followNode = node;
    rig.aimNode = node;
    rig.followLocal = true;
    const float height = beat.heightMetres.value_or(lowAngle ? 0.4f : 2.0f);
    const float distance = beat.distanceMetres.value_or(move == CameraMove::Chase ? 4.0f : 6.0f);
    rig.followOffset = glm::vec3(0.0f, height, -distance);
    rig.aimOffset = glm::vec3(0.0f, lowAngle ? 1.4f : 1.2f, 0.0f);
    rig.followLagSeconds = move == CameraMove::Chase ? 0.25 : 0.0;
    rig.followClearance = 0.2f;
    rig.focalLength = lowAngle ? 24.0f : 35.0f;
    return rig;
}

} // namespace

bool Compilation::changesAnything() const {
    return std::any_of(diff.begin(), diff.end(), [](const DiffLine& d) { return d.sign != '!'; });
}

std::string Compilation::diffText() const {
    std::string out;
    for (const DiffLine& d : diff) {
        out += fmt::format("{} {}\n", d.sign, d.text);
    }
    return out;
}

Compilation compilePlan(Plan plan, const SceneFacts& facts) {
    Compilation out;
    out.validation = validatePlan(plan, facts);
    out.staged = facts.staged;
    const Validation& v = out.validation;
    const auto line = [&](char sign, std::string item, std::string text) {
        out.diff.push_back(DiffLine{sign, std::move(item), std::move(text)});
    };

    // Only the compiler writes provenance: whatever a model or a file put in `produced` is discarded.
    plan.produced.clear();

    // ---- a revision: take the previous revision's content out, unless a person edited it ------------
    std::vector<std::string> replacedItems;
    std::map<std::string, std::string> reusableRigs; // item -> slug of the rig its last revision made
    const Plan* previous = facts.plan(plan.id);
    if (previous != nullptr) {
        plan.revision = previous->revision + 1;
        // An item is kept or replaced WHOLE. A shot is a seq::Shot and a camera cut (and maybe a rig);
        // if the person edited any piece, every piece stays -- removing the cut from under a shot
        // they trimmed would leave half of what they kept.
        std::set<std::string> edited;
        for (const ContentRef& ref : previous->produced) {
            const auto now = contentOf(ref, out.staged);
            if (now && !ref.fingerprint.empty() && fingerprint(*now) != ref.fingerprint) {
                edited.insert(ref.item);
            }
        }
        for (const ContentRef& ref : previous->produced) {
            // A rig this plan made is kept for the item to reuse in place: `CameraDirection` mints
            // ids monotonically, so removing and re-adding it would give the same rig a new id and
            // make a revision that changed nothing look like it changed the cameras. Rigs the
            // revision does not reuse are removed at the end.
            if (ref.domain == ContentDomain::CameraRig && !edited.contains(ref.item)) {
                reusableRigs.emplace(ref.item, ref.id);
                if (std::find(replacedItems.begin(), replacedItems.end(), ref.item) == replacedItems.end()) {
                    replacedItems.push_back(ref.item);
                }
                continue;
            }
            if (edited.contains(ref.item)) {
                if (contentOf(ref, out.staged)) {
                    plan.produced.push_back(ref); // still this plan's provenance, now the person's content
                }
                continue;
            }
            if (remove(ref, out.staged) &&
                std::find(replacedItems.begin(), replacedItems.end(), ref.item) == replacedItems.end()) {
                replacedItems.push_back(ref.item);
            }
        }
    }
    const auto removedLine = [&](const std::string& item) {
        return std::find(replacedItems.begin(), replacedItems.end(), item) != replacedItems.end() ? '~' : '+';
    };
    std::vector<ContentRef> produced;
    const auto record = [&](const std::string& item, ContentDomain domain, const std::string& id) {
        produced.push_back(ContentRef{item, domain, id, {}});
    };

    const auto idOf = [&](const std::string& alias) {
        const Subject* s = plan.subject(alias);
        return s == nullptr ? std::string() : s->id;
    };
    const auto kindOf = [&](const std::string& alias) {
        const Subject* s = plan.subject(alias);
        return s == nullptr ? SubjectKind::Unresolved : s->kind;
    };
    const auto nodeOf = [&](const std::string& alias) {
        for (const SubjectIdentity& i : facts.subjects.identities()) {
            if (i.id == idOf(alias) && i.kind == kindOf(alias)) {
                return i.node;
            }
        }
        return std::string();
    };

    // ---- shots ---------------------------------------------------------------------------------------
    for (std::size_t i = 0; i < plan.shots.size(); ++i) {
        const PlanShot& ps = plan.shots[i];
        if (v.isBlocked(ps.key)) {
            continue;
        }
        const double start = *v.times.at(fmt::format("/shots/{}/start", i));
        const double end = start + ps.durationSeconds;
        seq::Shot shot;
        shot.name = ps.name;
        shot.startSeconds = start;
        shot.durationSeconds = ps.durationSeconds;
        shot.in = seq::Transition{*seq::transitionKindFromName(ps.transition), 0.0};
        shot.camera.kind = seq::CameraKind::Inherit;

        std::optional<scene::CameraId> liveCamera;
        std::string cameraText = "inherits the camera";
        const bool lowAngle = std::any_of(ps.camera.begin(), ps.camera.end(),
                                          [](const CameraBeat& b) { return b.move == CameraMove::LowAngle; });
        if (!ps.rig.empty()) {
            for (const scene::CameraRig& r : out.staged.cameras.cameras) {
                if (r.name == ps.rig || r.slug == ps.rig) {
                    liveCamera = r.id;
                    cameraText = fmt::format("cut to camera '{}'", r.name);
                }
            }
        } else {
            for (const CameraBeat& beat : ps.camera) {
                const std::string subject = beat.subject.empty() ? ps.subject : beat.subject;
                const CameraSupport support = cameraSupport(beat.move, kindOf(subject));
                if (support == CameraSupport::Framing) {
                    const Place* place = facts.place(idOf(subject));
                    if (place == nullptr) {
                        break;
                    }
                    shot.camera = framing(beat.move, *place, beat, lowAngle);
                    liveCamera = scene::kMainCamera;
                    cameraText = fmt::format("{}{} on {}", lowAngle ? "low-angle " : "", cameraMoveName(beat.move), place->id);
                    break;
                }
                if (support == CameraSupport::FollowRig) {
                    scene::CameraRig rig = followRig(ps.name, nodeOf(subject), beat.move, beat, lowAngle);
                    scene::CameraId id = scene::kNoCamera;
                    if (const auto reuse = reusableRigs.find(ps.key); reuse != reusableRigs.end()) {
                        for (scene::CameraRig& existing : out.staged.cameras.cameras) {
                            if (existing.slug == reuse->second) {
                                rig.id = existing.id;     // the same camera, revised in place
                                rig.slug = existing.slug;
                                existing = rig;
                                id = existing.id;
                            }
                        }
                        reusableRigs.erase(reuse);
                    }
                    if (id == scene::kNoCamera) {
                        id = out.staged.cameras.addCamera(std::move(rig));
                    }
                    liveCamera = id;
                    const scene::CameraRig* added = out.staged.cameras.find(id);
                    record(ps.key, ContentDomain::CameraRig, added->slug);
                    line(removedLine(ps.key), ps.key,
                         fmt::format("Camera \"{}\": {}{} on {} (follows node '{}', {:.1f} m up, {:.1f} m behind)",
                                     added->name, lowAngle ? "low-angle " : "", cameraMoveName(beat.move), idOf(subject),
                                     added->followNode, added->followOffset.y, -added->followOffset.z));
                    cameraText = fmt::format("cut to camera '{}'", added->name);
                    break;
                }
            }
        }
        out.staged.sequence.shots.push_back(shot);
        record(ps.key, ContentDomain::SequenceShot, shot.name);
        line(removedLine(ps.key), ps.key,
             fmt::format("Shot \"{}\" {}-{}; {}", shot.name, clock(start), clock(end), cameraText));
        if (liveCamera) {
            scene::CameraShot cut;
            cut.camera = *liveCamera;
            cut.startSeconds = start;
            cut.endSeconds = end;
            cut.locked = ps.locked;
            cut.label = ps.name;
            out.staged.cameras.shots.push_back(cut);
            record(ps.key, ContentDomain::CameraShot, cameraShotId(cut, out.staged.cameras));
            line(removedLine(ps.key), ps.key,
                 fmt::format("Camera track: {} live {}-{}{}", out.staged.cameras.nameOf(cut.camera), clock(start), clock(end),
                             cut.locked ? ", locked" : ""));
        }
    }

    // ---- markers -------------------------------------------------------------------------------------
    for (std::size_t i = 0; i < plan.markers.size(); ++i) {
        const PlanMarker& pm = plan.markers[i];
        if (v.isBlocked(pm.key)) {
            continue;
        }
        const double t = *v.times.at(fmt::format("/markers/{}/at", i));
        seq::Marker marker{t, pm.name, seq::MarkerKind::Cue};
        out.staged.sequence.markers.push_back(marker);
        record(pm.key, ContentDomain::SequenceMarker, markerId(marker));
        line(removedLine(pm.key), pm.key, fmt::format("Marker {} {}", pm.name, clock(t)));
    }
    std::stable_sort(out.staged.sequence.markers.begin(), out.staged.sequence.markers.end(),
                     [](const seq::Marker& a, const seq::Marker& b) { return a.timeSeconds < b.timeSeconds; });

    // ---- parameter cues ------------------------------------------------------------------------------
    for (std::size_t i = 0; i < plan.cues.size(); ++i) {
        const PlanCue& pc = plan.cues[i];
        if (v.isBlocked(pc.key) || !pc.at) {
            continue;
        }
        const double t = *v.times.at(fmt::format("/cues/{}/at", i));
        std::string parameter = pc.parameter;
        if (pc.effect) {
            const ResolvedEffect effect = resolveEffect(*pc.effect, plan, facts);
            if (pc.field.empty()) {
                // ---- activation: a window of its own, on a copy of the owner's instance ---------
                // The owner's existing instance keeps its own activation (Umbra's pulse still fires
                // when the cut spotlights Umbra); the plan adds a second instance of the same look,
                // activated on the transport clock at exactly this time -- deterministic, and
                // editable in the Effects section like any other instance (ADR-702 allows several
                // of one type on one owner).
                world::EffectInstance instance;
                const world::EffectInstance* source = nullptr;
                for (const world::EffectInstance& e : out.staged.effects) {
                    if (e.id == effect.id) {
                        source = &e;
                    }
                }
                const world::EffectSchema* schema = world::effectSchema(effect.kind);
                const std::string display = schema != nullptr ? schema->displayName : pc.effect->type;
                instance = source != nullptr ? *source : world::makeEffect(effect.kind, display);
                instance.owner = effect.owner;
                instance.id = world::uniqueEffectId(out.staged.effects, plan.id + "-" + pc.key);
                instance.name = fmt::format("{} ({})", source != nullptr ? source->name : display,
                                            plan.title.empty() ? plan.id : plan.title);
                instance.enabled = true;
                instance.activation = world::Activation::Window;
                double window = pc.holdSeconds;
                if (pc.until) {
                    window = *v.times.at(fmt::format("/cues/{}/until", i)) - t;
                }
                if (window <= 0.0) {
                    window = instance.timing.windowSeconds; // the look's own length
                }
                // As the engine will hold them: an effect's timing is registered as float parameters,
                // so the compiled value is rounded the same way here. Compiled content then IS the
                // installed content, and a revision rebuilds it byte for byte.
                instance.timing.windowStart = static_cast<double>(static_cast<float>(t));
                instance.timing.windowSeconds = static_cast<double>(static_cast<float>(window));
                world::adaptEffectToOwner(instance);
                auto inserted = world::insertEffect(out.staged.effects, std::move(instance));
                if (!inserted) {
                    line('!', pc.key, fmt::format("{}: {}", issueCodeName(IssueCode::SchemaInvalid), inserted.error().message));
                    continue;
                }
                record(pc.key, ContentDomain::EffectInstance, *inserted);
                line(removedLine(pc.key), pc.key,
                     fmt::format("Effect \"{}\" on {} {}: active {}-{}", display,
                                 effect.owner.isWorld() ? std::string("the world") : effect.owner.name,
                                 source != nullptr ? fmt::format("(a copy of '{}')", source->id) : std::string("(new, the type's defaults)"),
                                 clock(t), clock(t + window)));
                continue;
            }
            parameter = fmt::format("fx/{}/{}", effect.id, pc.field);
        }
        if (parameter.empty()) {
            continue;
        }
        double hold = pc.holdSeconds;
        if (pc.until) {
            hold = std::max(0.0, *v.times.at(fmt::format("/cues/{}/until", i)) - t - pc.rampSeconds);
        }
        seq::SequenceEvent event;
        event.id = fmt::format("{}.{}", plan.id, pc.key);
        event.when.kind = seq::TriggerKind::Time;
        event.when.timeSeconds = t;
        event.what.kind = seq::EventActionKind::SetParameter;
        event.what.target = parameter;
        // "Set it to 2 for a moment" is an Add of (2 - base): an Add's identity is 0, so the bake
        // knows what the value is before, and what it returns to after the hold. An absolute
        // Replace has neither -- the bake warns, and the value holds backwards from t = 0.
        const std::vector<float>* base = facts.base(parameter);
        const float from = base != nullptr && !base->empty() ? base->front() : 0.0f;
        event.what.mode = params::TrackMode::Add;
        event.what.amount = glm::vec4(*pc.value - from, 0.0f, 0.0f, 0.0f);
        event.what.seconds = pc.rampSeconds;
        event.what.holdSeconds = hold;
        out.staged.sequence.events.push_back(event);
        record(pc.key, ContentDomain::SequenceEvent, event.id);
        line(removedLine(pc.key), pc.key,
             fmt::format("Cue {} -> {:.3g} (from {:.3g}) at {}{}{}", parameter, *pc.value,
                         facts.base(parameter) != nullptr ? facts.base(parameter)->front() : 0.0f, clock(t),
                         pc.rampSeconds > 0.0 ? fmt::format(", ramp {:.2f}s", pc.rampSeconds) : std::string(),
                         hold > 0.0 ? fmt::format(", back after {:.2f}s", hold) : std::string()));
    }

    // Rigs a previous revision made that this one no longer uses.
    for (const auto& [item, slug] : reusableRigs) {
        remove(ContentRef{item, ContentDomain::CameraRig, slug, {}}, out.staged);
    }

    // The effect list in the order the engine stores it (grouped by owner, stack order), so the staged
    // copy IS what an install leaves behind -- not merely equivalent to it.
    world::normaliseEffectOrder(out.staged.effects);

    // ---- content the previous revision made that this one does not ----------------------------------
    for (const std::string& item : replacedItems) {
        const bool again = std::any_of(produced.begin(), produced.end(), [&](const ContentRef& r) { return r.item == item; });
        if (!again && std::none_of(out.diff.begin(), out.diff.end(), [&](const DiffLine& d) { return d.item == item && d.sign == '-'; })) {
            line('-', item, fmt::format("'{}' from revision {}", item, previous->revision));
        }
    }

    // ---- findings, last: what the person must see before approving ----------------------------------
    for (const Issue& issue : v.issues) {
        if (issue.severity == Severity::Info) {
            continue;
        }
        line('!', issue.item, fmt::format("{}: {}", issueCodeName(issue.code), issue.message));
    }

    // ---- provenance: fingerprints of the content as compiled -----------------------------------------
    for (ContentRef& ref : produced) {
        if (const auto content = contentOf(ref, out.staged)) {
            ref.fingerprint = fingerprint(*content);
        }
        plan.produced.push_back(std::move(ref));
    }
    out.plan = std::move(plan);
    return out;
}

} // namespace avgen::directing
