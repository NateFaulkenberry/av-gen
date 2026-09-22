#include "entity/entity.hpp"
#include "world/world_map.hpp"
#include "core/phase2_probe.hpp" // TEMPORARY: ui-responsiveness phase 2

#include "entity/nav_grid.hpp"

#include "core/hash.hpp"
#include "core/log.hpp"
#include "params/serialization.hpp"
#include "scene/procedural_detail.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <optional>
#include <cmath>
#include <fstream>
#include <span>

namespace avgen::entity {

namespace {

// ADR-620. The authored acceleration limit, applied where the entity's own gait governs the entity.
//
// `GaitSettings::accel`/`decel` were authored, parsed and serialised, and read by nobody for a
// behaviour-driven body: `Gait::approach`'s only caller was the action tier's `Move` verb, and
// every behaviour assigns `state.speed` outright. So a character went from standing to full walking
// speed in one frame and back to zero in one frame -- the sprite behaviour `gait.hpp` says the
// class exists to prevent, with the two numbers that would prevent it sitting dead in the scene.
//
// Applied centrally rather than in each behaviour, because there are twenty-odd `state.speed`
// writes across `behaviors.cpp` and a limit honoured by some of them is a limit an author cannot
// reason about. **It is a no-op for an action-driven body**, whose speed the action tier has
// already limited with the same numbers: the delta it sees is within budget and `approach` returns
// it unchanged.
//
// `dt <= 0` is the first frame and a seek's zero-length step; there is no rate to limit over one.
void limitSpeedToGait(const GaitSettings& gait, float previous, double dt, float& speed) {
    // **Only where the scene authored a ramp.** The defaults exist for the action tier, which has
    // always applied them; switching the behaviour tier on for every body would cost every shallow
    // body a forty-step replay and change three behaviours that no author asked to change. See
    // `GaitSettings::accelAuthored`.
    if (!gait.accelAuthored || dt <= 0.0 || (gait.accel <= 0.0f && gait.decel <= 0.0f)) {
        return;
    }
    speed = Gait::approach(previous, speed, gait.accel, gait.decel, dt);
}

} // namespace

namespace {

constexpr float kDegrees = 180.0f / 3.14159265358979323846f;

// An angle folded into (-half, half]. Used on two things that are angles rather than odometers.
//
// `EntityState::yaw` is written as `yaw += turned` by every behaviour and action that steers, so
// over a long walk it is the *total* the body has turned rather than the direction it faces. That
// was invisible until it reached the node's rotation parameter, whose hard range is +/-360 degrees
// (composition.cpp, registerNodeParameters): past a net revolution the parameter clamped, the drawn
// facing stopped following the body, and a character walked while pointing wherever the clamp left
// it. Measured on Glowmere's wanderer at 31.8% of moving frames drawn travelling backwards.
//
// Safe everywhere because nothing reads yaw as an accumulation: every comparison goes through
// `angleDelta`, which normalises, and everything else takes its sine and cosine. `spin` already
// does the same thing to its own angle, for the same reason.
float wrapAngle(float radians, float half) {
    const float full = half * 2.0f;
    float wrapped = std::fmod(radians + half, full);
    if (wrapped < 0.0f) {
        wrapped += full;
    }
    return wrapped - half;
}

std::uint32_t nameSeed(std::string_view name, std::uint32_t sceneSeed) {
    // FNV-1a over the name, mixed with the scene seed. Deterministic across runs and platforms,
    // and different for two entities that differ only in their name -- which is what stops a row
    // of identical props from moving in lockstep.
    std::uint32_t h = 2166136261u ^ sceneSeed;
    for (const char c : name) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    return h == 0u ? 1u : h;
}

bool isAllDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

glm::vec3 readVec3(const nlohmann::json& j, const char* key, glm::vec3 fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) {
        return fallback;
    }
    glm::vec3 out = fallback;
    for (int i = 0; i < 3; ++i) {
        if (j[key][static_cast<std::size_t>(i)].is_number()) {
            out[i] = j[key][static_cast<std::size_t>(i)].get<float>();
        }
    }
    return out;
}

glm::quat quatFromEulerDegrees(glm::vec3 degrees) {
    return glm::quat(glm::radians(degrees));
}

// Inverse of glm::quat(vec3), which builds Rz * Ry * Rx. glm::eulerAngles recovers the middle
// angle with asin and loses precision near +-90 degrees; atan2 does not. The same recovery the
// composition uses, so a transform written here reads back identically there.
glm::vec3 eulerDegrees(const glm::quat& q) {
    const glm::mat3 m = glm::mat3_cast(q);
    const float cy = std::sqrt(m[0][0] * m[0][0] + m[0][1] * m[0][1]);
    const float y = std::atan2(-m[0][2], cy);
    float x = 0.0f;
    float z = 0.0f;
    if (cy > 1e-6f) {
        x = std::atan2(m[1][2], m[2][2]);
        z = std::atan2(m[0][1], m[0][0]);
    } else {
        x = std::atan2(-m[2][1], m[1][1]);
    }
    return glm::degrees(glm::vec3(x, y, z));
}

} // namespace

// ---- Entity ----------------------------------------------------------------------------------

Entity::Entity(EntityDesc desc, std::uint32_t sceneSeed)
    : desc_(std::move(desc)),
      seed_(desc_.seed != 0 ? desc_.seed : nameSeed(desc_.name, sceneSeed)),
      rng_(seed_) {
    // The runtime copies of everything an action may change. Keeping them beside the description
    // rather than in it is what lets a reset put the entity back exactly as the scene file wrote
    // it: `equip` adds to `attachments_`, and a seek throws that away without touching the author's
    // own attachments.
    propertyValues_.reserve(desc_.properties.size());
    for (const PropertyDesc& property : desc_.properties) {
        propertyValues_.emplace_back(property.name, std::clamp(property.value, property.min, property.max));
    }
    attachments_ = desc_.attachments;
    schedule_.setDesc(desc_.schedule);
    if (!desc_.actions.empty()) {
        actions_.push(desc_.actions, Authority::Routine);
    }
}

const std::string& Entity::clipFor(std::string_view activity) const {
    static const std::string kNone;
    const auto find = [&](std::string_view name) -> const std::string* {
        const auto it = std::find_if(desc_.clips.begin(), desc_.clips.end(),
                                     [&](const std::pair<std::string, std::string>& c) {
                                         return c.first == name;
                                     });
        return it == desc_.clips.end() ? nullptr : &it->second;
    };
    if (const std::string* exact = find(activity)) {
        return *exact;
    }
    // No clip for the activity an action named. Falling back to `idle` rather than to nothing is
    // deliberate: a character told to sit whose asset has no sit clip should stand there, not
    // freeze in its bind pose.
    if (const std::string* idle = find("idle")) {
        return *idle;
    }
    return kNone;
}

bool Entity::hasProperty(std::string_view property) const {
    return std::any_of(propertyValues_.begin(), propertyValues_.end(),
                       [&](const std::pair<std::string, float>& p) { return p.first == property; });
}

float Entity::property(std::string_view property) const {
    const auto it = std::find_if(propertyValues_.begin(), propertyValues_.end(),
                                 [&](const std::pair<std::string, float>& p) { return p.first == property; });
    return it == propertyValues_.end() ? 0.0f : it->second;
}

bool Entity::setProperty(std::string_view property, float value) {
    for (std::size_t i = 0; i < propertyValues_.size(); ++i) {
        if (propertyValues_[i].first != property) {
            continue;
        }
        const PropertyDesc& declared = desc_.properties[i];
        propertyValues_[i].second = std::clamp(value, declared.min, declared.max);
        if (i < propertyParams_.size() && propertyParams_[i] != nullptr) {
            // The base, not the final: the modulation pass resets finals to bases every frame, so a
            // state written to a final would be forgotten by the next one. Writing the base is also
            // what makes an equipped prop survive a save.
            propertyParams_[i]->setBase(propertyValues_[i].second);
        }
        return true;
    }
    return false;
}

const InteractionDesc* Entity::interaction(std::string_view verb) const {
    const auto it = std::find_if(desc_.interactions.begin(), desc_.interactions.end(),
                                 [&](const InteractionDesc& i) { return i.name == verb; });
    return it == desc_.interactions.end() ? nullptr : &*it;
}

std::string_view Entity::occupant(std::string_view verb) const {
    const auto it = std::find_if(claims_.begin(), claims_.end(),
                                 [&](const std::pair<std::string, std::string>& c) { return c.first == verb; });
    return it == claims_.end() ? std::string_view{} : std::string_view(it->second);
}

bool Entity::claim(std::string_view verb, std::string_view who) {
    for (auto& claim : claims_) {
        if (claim.first == verb) {
            return claim.second == who;
        }
    }
    claims_.emplace_back(std::string(verb), std::string(who));
    return true;
}

void Entity::release(std::string_view verb, std::string_view who) {
    claims_.erase(std::remove_if(claims_.begin(), claims_.end(),
                                 [&](const std::pair<std::string, std::string>& c) {
                                     return c.first == verb && c.second == who;
                                 }),
                  claims_.end());
}

bool Entity::attach(const std::string& node, const std::string& socket) {
    if (std::find_if(desc_.sockets.begin(), desc_.sockets.end(),
                     [&](const SocketDesc& s) { return s.name == socket; }) == desc_.sockets.end()) {
        // An attachment to a socket that does not exist is the silent-nothing failure this layer
        // exists to prevent. Refuse it and let the action report it by name.
        return false;
    }
    for (AttachmentDesc& attachment : attachments_) {
        if (attachment.node == node) {
            attachment.socket = socket;
            return true;
        }
    }
    attachments_.push_back(AttachmentDesc{node, socket});
    return true;
}

bool Entity::detach(const std::string& node) {
    const auto it = std::find_if(attachments_.begin(), attachments_.end(),
                                 [&](const AttachmentDesc& a) { return a.node == node; });
    if (it == attachments_.end()) {
        return false;
    }
    attachments_.erase(it);
    return true;
}

const std::string& Entity::clipFor(Activity activity) const {
    static const std::string kNone;
    const auto find = [&](std::string_view name) -> const std::string* {
        const auto it = std::find_if(desc_.clips.begin(), desc_.clips.end(),
                                     [&](const std::pair<std::string, std::string>& c) {
                                         return c.first == name;
                                     });
        return it == desc_.clips.end() ? nullptr : &it->second;
    };
    if (const std::string* exact = find(activityName(activity))) {
        return *exact;
    }
    if (const std::string* idle = find("idle")) {
        return *idle;
    }
    return kNone;
}

SocketResolution Entity::socketTransform(std::string_view socket, scene::Transform& out) const {
    const auto it = std::find_if(desc_.sockets.begin(), desc_.sockets.end(),
                                 [&](const SocketDesc& s) { return s.name == socket; });
    if (it == desc_.sockets.end()) {
        return SocketResolution::None;
    }
    // ADR-260's *drawn* position -- `visualPosition()`, the simulation plus the behaviours' offsets
    // -- because a socket exists to put something where the body is on screen. A prop hung on
    // `state().position()` would sit 2.4 m from Glowmere's saucer, which is exactly the drift the
    // saucer's hover writes.
    scene::Transform base;
    base.position = state_.position() + motion_.position;
    base.rotation = quatFromEulerDegrees(glm::vec3(motion_.rotation.x, motion_.rotation.y + state_.yaw * kDegrees,
                                                   motion_.rotation.z));
    // ADR-274. The node's scale, which this read for as long as sockets existed and never included,
    // and which was invisible for exactly as long as no socket resolved through a joint: a joint
    // offset is in the *asset's* units, and the four aliens in `glowmere-valley-2` are drawn at
    // 3.344x to 3.610x. Ignoring it put a hand-mounted prop at 28% of the distance out from the
    // body that the hand actually is. The entity's own offset (`SocketDesc::offset`) is authored in
    // the same asset units for the same reason, so it is scaled by the same number.
    //
    // Read from the node parameter rather than remembered, because a scale is keyframable and
    // `applyOffsets` -- which ran earlier in this same update -- has already folded `motion_.scale`
    // into it. This is the number the renderer will draw with.
    if (scaleParam_ != nullptr) {
        for (std::size_t c = 0; c < 3; ++c) {
            base.scale[static_cast<int>(c)] = scaleParam_->finalComponent(c);
        }
    }
    // A joint, when there is a skeleton to ask and it has the joint. Until the animation layer
    // installs one, a socket rides the entity's own frame: an approximate place is the right
    // failure for a prop that has to be somewhere, and it means a scene can be authored before the
    // skeleton exists. What changed in ADR-274 is that the caller is now told which of the two
    // happened -- the approximation is still offered, and it no longer passes for the answer.
    SocketResolution how = SocketResolution::EntityFrame;
    if (skeleton_ != nullptr && !it->joint.empty()) {
        scene::Transform joint;
        // Entity-local, not world (locomotion.hpp): the entity frame is the only thing that knows
        // where this rig is standing, because one posed rig may carry several bodies.
        if (skeleton_->jointTransform(it->joint, joint)) {
            base = scene::detail::composeTransforms(base, joint);
            how = SocketResolution::Joint;
        }
    }
    out = scene::detail::composeTransforms(base, it->offset);
    return how;
}

// ---- EntityWorld -----------------------------------------------------------------------------

void EntityWorld::setEntities(std::vector<EntityDesc> descs, std::uint32_t sceneSeed) {
    entities_.clear();
    problems_.clear();
    sceneSeed_ = sceneSeed;
    for (EntityDesc& desc : descs) {
        auto entity = std::make_unique<Entity>(std::move(desc), sceneSeed);
        for (const BehaviorDesc& behavior : entity->desc_.behaviors) {
            auto made = makeBehavior(behavior.kind, &behavior.settings);
            if (!made) {
                std::string known;
                for (const std::string_view kind : behaviorKinds()) {
                    if (!known.empty()) {
                        known += ", ";
                    }
                    known += kind;
                }
                problems_.push_back(fmt::format("entity '{}': unknown behaviour '{}' (known: {})",
                                                entity->name(), behavior.kind, known));
                continue;
            }
            made->reset(entity->rng_);
            entity->behaviors_.push_back(std::move(made));
        }
        entity->actions_.setSink(&pendingEvents_, entity->name());
        entities_.push_back(std::move(entity));
    }
    // The per-entity field state is sized per field, so a new entity set needs a new binding pass.
    fieldsBound_ = false;
    // Whether the sense stage runs at all in this world. False is the ordinary answer -- Glowmere's
    // nine farm animals, every craft and every spinning rock declare no `perception` block -- and it
    // is what makes the stage cost exactly nothing rather than nearly nothing: no grid is built, no
    // loop is entered, and a scene that had no senses is bit-for-bit what it was before they existed.
    perceiving_ = false;
    for (const auto& entity : entities_) {
        perceiving_ = perceiving_ || entity->desc_.perceives;
    }
    for (const std::string& problem : problems_) {
        log::warn("{}", problem);
    }
}

void EntityWorld::clear() {
    entities_.clear();
    bindings_.clear();
    landmarks_.clear();
    problems_.clear();
    registered_.clear();
    actionEvents_.clear();
    pendingEvents_.clear();
    counts_ = {};
    fields_.clear();
    fieldRuntime_.clear();
    fieldReport_.clear();
    triggerEvents_.clear();
    gridPoints_.clear();
    gridEntity_.clear();
    grid_.clear();
    fieldCounts_ = FieldCounts{};
    fieldsBound_ = false;
}

Entity* EntityWorld::find(std::string_view name) {
    const auto it = std::find_if(entities_.begin(), entities_.end(),
                                 [&](const std::unique_ptr<Entity>& e) { return e->name() == name; });
    return it == entities_.end() ? nullptr : it->get();
}

const Entity* EntityWorld::find(std::string_view name) const {
    const auto it = std::find_if(entities_.begin(), entities_.end(),
                                 [&](const std::unique_ptr<Entity>& e) { return e->name() == name; });
    return it == entities_.end() ? nullptr : it->get();
}

void EntityWorld::setBindings(std::vector<NodeBinding> bindings) {
    bindings_ = std::move(bindings);
    for (auto& entity : entities_) {
        if (const NodeBinding* b = binding(entity->desc_.driven())) {
            entity->state_.anchor = b->anchor;
            // The facing half of the same statement (ADR-240): where the author put the body is
            // where it starts, in rotation as in position.
            entity->facing_ = b->facing;
            entity->state_.yaw = b->facing;
        }
    }
}

const NodeBinding* EntityWorld::binding(const std::string& node) const {
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                 [&](const NodeBinding& b) { return b.node == node; });
    return it == bindings_.end() ? nullptr : &*it;
}

bool EntityWorld::pointOfInterest(std::string_view name, glm::vec3& out) const {
    if (const Entity* e = find(name)) {
        out = e->state_.position() + e->motion_.position;
        return true;
    }
    const auto it = std::find_if(landmarks_.begin(), landmarks_.end(),
                                 [&](const std::pair<std::string, glm::vec3>& l) { return l.first == name; });
    if (it != landmarks_.end()) {
        out = it->second;
        return true;
    }
    return false;
}

const char* interestKindName(InterestKind kind) {
    switch (kind) {
    case InterestKind::Landmark: return "landmark";
    case InterestKind::Character: return "character";
    case InterestKind::Glow: return "glow";
    case InterestKind::Water: return "water";
    case InterestKind::Vista: return "vista";
    }
    return "unknown";
}

void EntityWorld::setExtraInterestPoints(std::vector<InterestPoint> extras) {
    extraInterests_ = std::move(extras);
    refreshInterestPoints();
}

void EntityWorld::refreshInterestPoints() {
    interests_.clear();
    // Order matters only for reproducibility, not for preference -- a behaviour weights by kind,
    // not by position in the list -- but it has to be *an* order, and this one is the order the
    // host declared things in, followed by what the terrain offered.
    for (const auto& [name, position] : landmarks_) {
        InterestPoint point;
        point.position = position;
        point.name = name;
        // A landmark that is also an entity moves, and a behaviour that cached its position would
        // walk to where the craft used to be. Naming the kind here lets the behaviour re-resolve.
        point.kind = find(name) != nullptr ? InterestKind::Character : InterestKind::Landmark;
        point.weight = 1.0f;
        interests_.push_back(std::move(point));
    }
    for (const InterestPoint& extra : extraInterests_) {
        interests_.push_back(extra);
    }
    if (const NavGrid* grid = nav_.grid()) {
        for (const glm::vec3& p : grid->shorePoints()) {
            interests_.push_back(InterestPoint{p, {}, InterestKind::Water, 1.0f});
        }
        for (const glm::vec3& p : grid->vistaPoints()) {
            interests_.push_back(InterestPoint{p, {}, InterestKind::Vista, 1.0f});
        }
    }

    // ---- and the index a sense stage scans it through (ADR-270) -------------------------------
    //
    // The entries of kind `Character` are deliberately left out. They are bodies, their positions
    // in this list are wherever the host last recorded the landmark, and a percept built from one
    // would report a walking character at where it was when the scene was loaded. They are scanned
    // through the body grid instead, where they are where they are now.
    //
    // 32 m cells: `spatial::PointGrid` is tuned for a query radius no larger than a cell, and 32 m
    // is a little under the 60 m default range and a little over the ranges an author is likely to
    // author. A 60 m query over this costs 0.098 us against the real 505-entry list -- four hundred
    // times cheaper than one `Navigator::sample`, which is the finding that decided the whole shape
    // of the sense stage (ADR-270 §2).
    interestGridPoints_.clear();
    interestGridSource_.clear();
    for (std::size_t i = 0; i < interests_.size(); ++i) {
        if (interests_[i].kind == InterestKind::Character) {
            continue;
        }
        interestGridPoints_.push_back(interests_[i].position);
        interestGridSource_.push_back(static_cast<std::uint32_t>(i));
    }
    interestGrid_.build(interestGridPoints_, 32.0f);
}

PerceptionIndex EntityWorld::perceptionIndex() const {
    PerceptionIndex index;
    index.interests = &interestGrid_;
    index.interestSource = interestGridSource_;
    index.bodies = &bodyGrid_;
    index.bodyPositions = bodyPoints_;
    return index;
}

void EntityWorld::buildBodyIndex() {
    // Every entity, in entity order, so a grid point index *is* an index into `entities()` and a
    // `Percept::source` needs no translation. Bodies with no radius are in it too: a saucer is not
    // a body the crowd separates against and it is very much a thing a character notices.
    bodyPoints_.clear();
    bodyPoints_.reserve(entities_.size());
    for (const auto& entity : entities_) {
        // R1 (ADR-260): the simulation's position. `visualPosition()` would fold in the behaviours'
        // offsets, and Glowmere's saucer carries a 2.4 m drift -- perceived from there, a character
        // sent to meet it walks to the wrong place by up to 2.4 m for reasons nothing explains.
        bodyPoints_.push_back(entity->state_.position());
    }
    bodyGrid_.build(bodyPoints_, 32.0f);
}

void EntityWorld::perceiveOne(std::size_t entityIndex, double time) {
    Entity& entity = *entities_[entityIndex];
    if (!entity.desc_.perceives) {
        return;
    }
    ++perceptionCounts_.perceivers;

    // ADR-225: a setting the application does not keep is not a setting. The authored value is the
    // default and the parameter is the answer, read here every tick, so a keyframed `range` is a
    // character whose senses narrow on cue rather than a knob that draws in a panel and does nothing.
    PerceptionSettings live = entity.desc_.perception;
    const Entity::PerceptionParams& p = entity.perceptionParams_;
    if (p.range != nullptr) {
        live.range = p.range->value();
        live.fieldOfView = p.fieldOfView->value();
        live.proximityRange = p.proximityRange->value();
        live.capacity = static_cast<std::uint16_t>(std::lround(p.capacity->value()));
        live.hertz = p.hertz->value();
        live.occlusionTestsPerSecond = p.occlusionTestsPerSecond->value();
        for (std::size_t i = 0; i < 5; ++i) {
            live.weight[i] = p.weight[i]->value();
        }
    }
    live.capacity = static_cast<std::uint16_t>(
        std::clamp<int>(static_cast<int>(live.capacity), 1, static_cast<int>(kPerceptCapacityMax)));
    entity.perceptionLive_ = live;

    // The cadence, as a tick index rather than an accumulator. `senseTick` is a pure function of
    // (time, hertz, seed), so the instants a replay senses at are the instants a play senses at --
    // which is what lets a character's working set be reconstructed by `seek` rather than persisted
    // (D4), and it is why nothing here reads a frame index or a wall clock (D1).
    const std::uint64_t tick = senseTick(time, live.hertz, entity.seed_);
    if (tick == entity.senseTick_) {
        return; // still inside the tick the working set already belongs to
    }
    ++perceptionCounts_.sensed;
    if (entity.percepts_.size() < kPerceptCapacityMax) {
        entity.percepts_.resize(kPerceptCapacityMax);
    }
    entity.perceptCount_ =
        perception().perceive(*this, entityIndex, live, entity.seed_, time,
                              std::span<Percept>(entity.percepts_.data(), entity.percepts_.size()));
    // *After* the call, not before: the occlusion budget prices this tick against the tick the body
    // last sensed at, and sense ticks are not consecutive (Entity::lastSenseTick).
    entity.senseTick_ = tick;
}

void EntityWorld::registerParameters(params::ParameterSet& params, const std::string& prefix) {
    prefix_ = prefix;
    registered_.clear();
    fieldParams_.assign(fields_.size(), FieldParams{});
    for (std::size_t f = 0; f < fields_.size(); ++f) {
        const FieldDesc& field = fields_[f];
        const std::string base = prefix + "fields/" + field.name + "/";
        const auto add = [&](const char* name, float value, float lo, float hi) {
            registered_.push_back(base + name);
            return &params.add(params::ParamDesc<float>{
                .path = base + name, .defaultValue = value, .hardMin = lo, .hardMax = hi});
        };
        FieldParams& p = fieldParams_[f];
        p.strength = add("strength", field.strength, 0.0f, 16.0f);
        p.scale = add("scale", 1.0f, 0.0f, 64.0f);
        p.inner = add("inner", field.volume.inner, 0.0f, 0.999f);
        p.floorGain = add("floorGain", field.floorGain, 0.0f, 16.0f);
        registered_.push_back(base + "center");
        p.center = &params.add(params::ParamDesc<glm::vec3>{.path = base + "center",
                                                            .defaultValue = field.volume.center,
                                                            .hardMin = glm::vec3(-1e5f),
                                                            .hardMax = glm::vec3(1e5f)});
    }
    for (auto& entity : entities_) {
        for (std::size_t i = 0; i < entity->behaviors_.size(); ++i) {
            const BehaviorDesc& desc = entity->desc_.behaviors[i];
            const std::string name = desc.name.empty() ? desc.kind : desc.name;
            const std::string base = prefix + entity->name() + "/" + name + "/";
            entity->behaviors_[i]->registerParameters(params, base);
            entity->behaviors_[i]->collectParameterPaths(registered_);
        }
        // The senses, as ordinary parameters (ADR-225, ADR-290). Registered for every entity that
        // declared a `perception` block and for no other, so the path set says which bodies have
        // senses -- and read back in `perceiveOne` every tick, so a keyframed `range` is a
        // character whose senses narrow on cue. A knob that draws and does nothing is the defect
        // this repository named ADR-225 after.
        if (entity->desc_.perceives) {
            const std::string base = prefix + entity->name() + "/perception/";
            const auto add = [&](const std::string& name, float value, float lo, float hi) {
                registered_.push_back(base + name);
                return &params.add(params::ParamDesc<float>{
                    .path = base + name, .defaultValue = value, .hardMin = lo, .hardMax = hi});
            };
            const PerceptionSettings& settings = entity->desc_.perception;
            Entity::PerceptionParams& pp = entity->perceptionParams_;
            pp.range = add("range", settings.range, 0.0f, 500.0f);
            pp.fieldOfView = add("fieldOfView", settings.fieldOfView, 0.0f, 360.0f);
            pp.proximityRange = add("proximityRange", settings.proximityRange, 0.0f, 100.0f);
            pp.capacity = add("capacity", static_cast<float>(settings.capacity), 1.0f,
                              static_cast<float>(kPerceptCapacityMax));
            pp.hertz = add("hertz", settings.hertz, 0.0f, 60.0f);
            pp.occlusionTestsPerSecond =
                add("occlusionTestsPerSecond", settings.occlusionTestsPerSecond, 0.0f, 120.0f);
            const std::span<const std::string_view> names = perceptionWeightNames();
            for (std::size_t w = 0; w < names.size(); ++w) {
                pp.weight[w] = add("weight/" + std::string(names[w]), settings.weight[w], 0.0f, 8.0f);
            }
            entity->percepts_.assign(kPerceptCapacityMax, Percept{});
        }
        // Declared state, as ordinary parameters. This is the whole of what makes "the headphones
        // are on" readable by anything else: a reaction targets "state/headphones", a track
        // keyframes it, a preset saves it, and none of them has to know an action wrote it.
        entity->propertyParams_.assign(entity->desc_.properties.size(), nullptr);
        for (std::size_t i = 0; i < entity->desc_.properties.size(); ++i) {
            const PropertyDesc& property = entity->desc_.properties[i];
            const std::string path = prefix + entity->name() + "/state/" + property.name;
            auto& param = params.add(params::ParamDesc<float>{.path = path,
                                                              .defaultValue = property.value,
                                                              .hardMin = property.min,
                                                              .hardMax = property.max});
            param.setBase(entity->propertyValues_[i].second);
            entity->propertyParams_[i] = &param;
            registered_.push_back(path);
        }
    }
    parametersLive_ = true;
}

void EntityWorld::unregisterParameters(params::ParameterSet& params) {
    for (const std::string& path : registered_) {
        params.remove(path);
    }
    registered_.clear();
    parametersLive_ = false;
    fieldParams_.assign(fields_.size(), FieldParams{});
    for (auto& entity : entities_) {
        entity->positionParam_ = nullptr;
        entity->rotationParam_ = nullptr;
        entity->scaleParam_ = nullptr;
        // Stale pointers into a set that has just been emptied. The state itself lives on in
        // propertyValues_, which is why it survives a scene swap and a rebind.
        std::fill(entity->propertyParams_.begin(), entity->propertyParams_.end(), nullptr);
        // And the senses'. `desc_.perception` is the authored value and survives; what is dropped
        // is the live reading of it, which `perceiveOne` falls back off when the pointers are null.
        entity->perceptionParams_ = Entity::PerceptionParams{};
    }
}

std::string EntityWorld::resolveTarget(const Entity& entity, const std::string& target,
                                       const std::string& prefix, std::vector<std::string>* tried) const {
    if (target.empty()) {
        return {};
    }
    // An explicit escape hatch for the case the addressing rules do not cover: '@' means "this is
    // already a parameter path, leave it alone". Without one, the only way out of a naming scheme
    // is to widen the scheme until it means nothing.
    if (target.front() == '@') {
        return target.substr(1);
    }
    std::vector<std::string> candidates;
    candidates.push_back(prefix + entity.name() + "/" + target);
    if (const NodeBinding* b = binding(entity.desc().driven()); b != nullptr && b->exists) {
        if (!b->transformPrefix.empty()) {
            candidates.push_back(b->transformPrefix + target);
        }
        if (!b->geometryPrefix.empty()) {
            // "parts/<material>/..." is the readable spelling of "parts/<index>/...". An asset's
            // material names are what an artist sees in the file they exported; the index is an
            // internal ordering by surface area that nobody can predict and that changes the day
            // someone edits the model.
            constexpr std::string_view kParts = "parts/";
            if (target.rfind(kParts, 0) == 0) {
                const std::size_t slash = target.find('/', kParts.size());
                if (slash != std::string::npos) {
                    const std::string part = target.substr(kParts.size(), slash - kParts.size());
                    if (!isAllDigits(part)) {
                        for (std::size_t i = 0; i < b->partNames.size(); ++i) {
                            if (b->partNames[i] == part) {
                                candidates.push_back(b->geometryPrefix + "parts/" + std::to_string(i) +
                                                     target.substr(slash));
                                break;
                            }
                        }
                    }
                }
            }
            candidates.push_back(b->geometryPrefix + target);
        }
    }
    for (const std::string& candidate : candidates) {
        if (params_ != nullptr && params_->find(candidate) != nullptr) {
            return candidate;
        }
    }
    if (tried != nullptr) {
        *tried = std::move(candidates);
    }
    return {};
}

std::vector<params::ModRoute> EntityWorld::compileReactions(const params::ParameterSet& params,
                                                            std::vector<std::string>& problems) const {
    params_ = &params;
    std::vector<params::ModRoute> routes;
    for (std::size_t index = 0; index < entities_.size(); ++index) {
        const auto& entity = entities_[index];
        for (const ReactionDesc& reaction : entity->desc().reactions) {
            std::vector<std::string> tried;
            const std::string target = resolveTarget(*entity, reaction.target, prefix_, &tried);
            if (target.empty()) {
                std::string names;
                for (const std::string& candidate : tried) {
                    if (!names.empty()) {
                        names += ", ";
                    }
                    names += candidate;
                }
                std::string parts;
                if (const NodeBinding* b = binding(entity->desc().driven())) {
                    for (std::size_t i = 0; i < b->partNames.size(); ++i) {
                        if (b->partNames[i].empty()) {
                            continue;
                        }
                        if (!parts.empty()) {
                            parts += ", ";
                        }
                        parts += b->partNames[i];
                    }
                }
                std::string message =
                    fmt::format("entity '{}': reaction '{}' <- '{}' resolves to no parameter (tried: {})",
                                entity->name(), reaction.target, reaction.signal,
                                names.empty() ? "nothing" : names);
                if (!parts.empty()) {
                    message += fmt::format("; this node's material parts are: {}", parts);
                }
                problems.push_back(std::move(message));
                continue;
            }
            params::ModRoute route;
            route.source = reaction.signal;
            route.target = target;
            route.component = reaction.component;
            route.amount = reaction.depth;
            route.op = reaction.op;
            route.polarity = reaction.polarity;
            route.chain = reaction.chain;
            route.enabled = reaction.enabled;
            // ADR-097. Which entity owns this route is what lets a field scale *its* reactions
            // rather than every reaction in the scene, and it is recorded here because this is the
            // only place that knows. A reaction the author marked `"spatial": false` keeps
            // kNoOwner and is therefore never touched by a field -- the escape hatch a Multiply
            // route whose neutral output is 1 has to have.
            route.ownerEntity = reaction.spatial ? static_cast<std::uint32_t>(index)
                                                 : params::ModRoute::kNoOwner;
            routes.push_back(std::move(route));
        }
    }
    return routes;
}

// Whether this entity has anything that needs the node it names. Behaviours move a transform,
// sockets and attachments hang off one, and a relative reaction target is resolved against one; a
// reaction written as an absolute path needs none of it.
bool EntityWorld::needsNode(const Entity& entity) {
    const EntityDesc& desc = entity.desc();
    if (!desc.behaviors.empty() || !desc.sockets.empty() || !desc.attachments.empty()) {
        return true;
    }
    if (desc.reactions.empty()) {
        return true; // an entity that does nothing at all: the missing node is still the reason
    }
    return std::any_of(desc.reactions.begin(), desc.reactions.end(), [](const ReactionDesc& r) {
        return r.target.empty() || r.target.front() != '@';
    });
}

void EntityWorld::bind(params::ParameterSet& params, const std::string& prefix) {
    prefix_ = prefix;
    params_ = &params;
    for (auto& entity : entities_) {
        const NodeBinding* b = binding(entity->desc_.driven());
        if (b == nullptr || !b->exists) {
            // An entity that drives a node the scene does not have is the exact failure this
            // project keeps shipping in silence. Say it once, here, with the name -- *unless* this
            // entity has no use for a node at all. An entity whose reactions are every one of them
            // absolute (`@lightrig/key/fill/intensity`, `@post/bloom/amount`) and which has no
            // behaviours, sockets or attachments is how a light, the grade or a particle system
            // gains reactions (§39-§43), and warning about the node it does not drive would train
            // the reader to ignore the warning that matters.
            if (needsNode(*entity)) {
                const std::string message =
                    fmt::format("entity '{}' drives node '{}', which this scene has no node for",
                                entity->name(), entity->desc_.driven());
                if (std::find(problems_.begin(), problems_.end(), message) == problems_.end()) {
                    problems_.push_back(message);
                    log::warn("{}", message);
                }
            }
            entity->positionParam_ = nullptr;
            entity->rotationParam_ = nullptr;
            entity->scaleParam_ = nullptr;
            continue;
        }
        entity->positionParam_ = params.findAs<glm::vec3>(b->transformPrefix + "position");
        entity->rotationParam_ = params.findAs<glm::vec3>(b->transformPrefix + "rotation");
        entity->scaleParam_ = params.findAs<glm::vec3>(b->transformPrefix + "scale");
        entity->state_.anchor = b->anchor;
        entity->actions_.setSink(&pendingEvents_, entity->name());
        entity->propertyParams_.assign(entity->desc_.properties.size(), nullptr);
        for (std::size_t i = 0; i < entity->desc_.properties.size(); ++i) {
            const std::string path = prefix + entity->name() + "/state/" + entity->desc_.properties[i].name;
            entity->propertyParams_[i] = params.findAs<float>(path);
            if (entity->propertyParams_[i] != nullptr) {
                entity->propertyParams_[i]->setBase(entity->propertyValues_[i].second);
            }
        }
        if (entity->desc_.perceives) {
            const std::string base = prefix + entity->name() + "/perception/";
            Entity::PerceptionParams& pp = entity->perceptionParams_;
            pp.range = params.findAs<float>(base + "range");
            pp.fieldOfView = params.findAs<float>(base + "fieldOfView");
            pp.proximityRange = params.findAs<float>(base + "proximityRange");
            pp.capacity = params.findAs<float>(base + "capacity");
            pp.hertz = params.findAs<float>(base + "hertz");
            pp.occlusionTestsPerSecond = params.findAs<float>(base + "occlusionTestsPerSecond");
            const std::span<const std::string_view> names = perceptionWeightNames();
            bool complete = pp.range != nullptr && pp.fieldOfView != nullptr &&
                            pp.proximityRange != nullptr && pp.capacity != nullptr &&
                            pp.hertz != nullptr && pp.occlusionTestsPerSecond != nullptr;
            for (std::size_t w = 0; w < names.size() && w < 5; ++w) {
                pp.weight[w] = params.findAs<float>(base + "weight/" + std::string(names[w]));
                complete = complete && pp.weight[w] != nullptr;
            }
            // All of them or none. `perceiveOne` tests one pointer and then reads the rest, which
            // is only safe because a partial resolution is turned into no resolution here: a set
            // that has half the knobs is a scene mid-swap, and falling back to the authored values
            // is the answer that cannot crash.
            if (!complete) {
                pp = Entity::PerceptionParams{};
            }
            if (entity->percepts_.size() < kPerceptCapacityMax) {
                entity->percepts_.assign(kPerceptCapacityMax, Percept{});
            }
        }
    }
    navPath_.setNavigator(&nav_);
    // After the anchors, because a field's source may be an entity and an entity's position is its
    // anchor until something moves it.
    bindFields();
}

// Phase B. One step of the provider memory, from the state the body is already in.
//
// **Deliberately reads `state_` rather than taking arguments.** The request is a view of what the
// body is doing and wants, and both halves already live on the entity (ADR-545); building it from
// parameters would give a caller a way to advance the memory with something other than the truth,
// and a seek would then replay a different request from the one a play used.
void Entity::publishLookSchedule() {
    if (state_.hasLookTarget != locomotion_.hasLookTarget) {
        locomotion_.lookTargetBefore = locomotion_.hasLookTarget;
        locomotion_.lookTargetSince = locomotion_.time;
    }
    locomotion_.hasLookTarget = state_.hasLookTarget;
}

void Entity::advanceMotion(double time, float dt) {
    if (motionChain_ == nullptr || !desc_.proceduralMotion) {
        return;
    }
    // Phase D §2.5/§15. The request is built from the character's INTENT, and intent comes in two
    // forms because the engine grew the second one late:
    //
    //   * the **vector** form, `EntityState::intent`, which can express a strafe -- a body moving
    //     one way while facing another;
    //   * the **polar** form, `speed` along `yaw`, which every behaviour that exists writes and
    //     which cannot.
    //
    // The vector form wins when a behaviour published one. When none did, the polar pair is
    // reconstructed into a vector here -- which is exactly what the whole cast gets today, and is
    // why adding this seam changed no existing character's motion.
    const glm::vec3 heading(std::sin(state_.yaw), 0.0f, std::cos(state_.yaw));
    MotionRequest request;
    if (state_.intent.valid) {
        request.desiredVelocity = state_.intent.desiredVelocity;
        request.desiredFacing =
            state_.intent.hasFacing ? state_.intent.facing : heading;
        request.steering = state_.intent.steering;
    } else {
        request.desiredVelocity = heading * state_.speed;
        request.desiredFacing = heading;
    }
    request.desiredTurnRate = state_.turnRate;
    request.bodyFacing = state_.facing();
    // How the body is moving now: its rate-limited speed along its heading (ADR-620), not the
    // measured `state_.velocity`. The measured one is a backward difference that reads zero on a
    // body's first steps and across a seek, so a prediction built on it made a scrub disagree with
    // the play it is supposed to reproduce (ADR-360). The limited speed is replayed exactly.
    request.bodyVelocity = heading * state_.speed;
    request.bodyVelocityKnown = true;
    request.mode = state_.airborne ? MovementMode::Airborne : MovementMode::Ground;
    MotionMemory next;
    motionChainResult_ = motionChain_->advance(request, motionMemory_, time, dt, next);
    motionMemory_ = next;
}

void EntityWorld::reset() {
    actionEvents_.clear();
    pendingEvents_.clear();
    for (auto& entity : entities_) {
        entity->rng_ = Rng(entity->seed_);
        entity->state_ = EntityState{};
        entity->motion_ = MotionOffset{};
        // The Director tier resets with everything else (ADR-210): a seek that left a cow half way
        // up a beam would make a scrubbed frame depend on how the playhead got there. `stage::Staging`
        // resets alongside this and re-issues whatever the scenario is doing at the new second.
        entity->director_ = DirectorMotion{};
        entity->locomotion_ = LocomotionState{};
        // ADR-337 / ADR-267 D4: the previous root-motion sample is recoverable by replaying the
        // steps, so a reset must forget it. Keeping it would make the first step of a seek a
        // difference between two unrelated clips -- the whole of `Landing` backwards, in one
        // step, as a teleport, which is exactly the class of thing `reset` exists to remove.
        // ADR-545: the measured velocity is a backward difference, so a reset must forget where
        // the body was. Keeping it would make the first step of a seek a difference between two
        // unrelated places -- the same class of mistake the root-motion sample below it makes, and
        // for the same reason.
        entity->lastPosition_ = glm::vec3(0.0f);
        entity->hasLastVelocity_ = false;
        entity->hasLastPosition_ = false;
        entity->rootMotionLast_ = glm::vec3(0.0f);
        entity->rootMotionGeneration_ = 0;
        entity->rootMotionHeld_ = false;
        entity->rootMotionStep_ = glm::vec3(0.0f);
        entity->coarseAccum_ = 0.0;
        entity->everUpdated_ = false;
        // The working set, and the tick it belonged to. D4: what a character knows is reconstructed
        // by the replay rather than persisted, so a reset must leave it knowing nothing -- a seek
        // that inherited the last played frame's percepts would be state surviving the one call
        // whose whole job is to remove state.
        entity->perceptCount_ = 0;
        entity->senseTick_ = Entity::kNoSenseTick;
        // And back into the crowd. `update` clears this for a body it culled by distance, and
        // nothing put it back -- so a reset inherited "entity 7 is not a body" from whatever frame
        // last played, and a seek that rebuilds the crowd field (which `seek` now does) would build
        // its first step's field out of that. Reset means reset.
        entity->active_ = true;
        // Field state is live-tier state and it resets with the rest of it (ADR-091). An arc that
        // was half-way through when the playhead jumped is gone, and the entity is outside every
        // field until the next pass says otherwise -- which is the same honest answer a behaviour
        // gives, said out loud rather than approximated.
        entity->fieldAccum_ = 0.0;
        entity->arcPhase_ = Entity::ArcPhase::None;
        entity->arcField_ = Entity::kNoField;
        entity->arcLevel_ = 0.0f;
        entity->arcTimer_ = 0.0;
        entity->arcCooldownUntil_ = -1.0e30;
        entity->influence_ = entity->governed_ ? entity->fieldFloor_ : 1.0f;
        std::fill(entity->insideField_.begin(), entity->insideField_.end(), 0);
        std::fill(entity->insideNow_.begin(), entity->insideNow_.end(), 0);
        std::fill(entity->entryCount_.begin(), entity->entryCount_.end(), 0u);
        if (const NodeBinding* b = binding(entity->desc_.driven())) {
            entity->state_.anchor = b->anchor;
            entity->facing_ = b->facing;
            entity->state_.yaw = b->facing;
        }
        for (auto& behavior : entity->behaviors_) {
            behavior->reset(entity->rng_);
        }
        // Intent goes back to what the scene file said, not to nothing: a seek must reproduce the
        // entity the author described, including the routine it was given and the props it was
        // placed holding. Everything an action added since -- an equipped prop, a set property, a
        // claimed chair -- is exactly what a seek should discard.
        entity->actions_.reset();
        entity->schedule_.reset();
        entity->gait_.reset();
        // ADR-541: the provider memory is reset with everything else, and rebuilt by the replay.
        // A seek that kept it would carry a clip clock from a timeline the scrub has abolished.
        entity->motionMemory_.reset();
        entity->locomotionPlan_.reset();
        entity->motionState_.reset();
        entity->motionChainResult_ = MotionChainResult{};
        entity->attachments_ = entity->desc_.attachments;
        entity->claims_.clear();
        for (std::size_t i = 0; i < entity->desc_.properties.size() && i < entity->propertyValues_.size(); ++i) {
            const PropertyDesc& property = entity->desc_.properties[i];
            entity->propertyValues_[i].second = std::clamp(property.value, property.min, property.max);
            if (i < entity->propertyParams_.size() && entity->propertyParams_[i] != nullptr) {
                entity->propertyParams_[i]->setBase(entity->propertyValues_[i].second);
            }
        }
        if (!entity->desc_.actions.empty()) {
            entity->actions_.push(entity->desc_.actions, Authority::Routine);
        }
    }
    triggerEvents_.clear();
    fieldCounts_ = FieldCounts{};
}

void EntityWorld::seek(double time, params::ParameterSet* params, const signals::SignalBus* bus,
                       double step, SeekBudget budget) {
    if (params != nullptr) {
        params->resetFinals();
    }
    reset();
    navPath_.setNavigator(&nav_);
    const double target = std::max(time, 0.0);
    const double dt = std::max(step, 1e-3);

    // ---- what each body actually needs (ADR-273) --------------------------------------------
    //
    // A body's requirement is the deepest of its behaviours', and the tiers above them are all
    // accumulations: an action queue holds a part-finished walk, a schedule holds which entry it
    // has reached. So an entity carrying either is replayed in full whatever its behaviours say.
    // `IBehavior::historySteps` defaults to "all of it", so this only ever shortens a replay for a
    // behaviour that has been looked at and can defend the shorter answer.
    seekFirstStep_.assign(entities_.size(), 0);
    std::uint64_t deepBodies = 0;
    std::vector<std::uint64_t> needSteps(entities_.size(), 0);
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        const Entity& entity = *entities_[i];
        bool deep = !entity.desc_.actions.empty() || !entity.schedule_.desc().entries.empty();
        // ADR-623: **a provider's memory is an accumulation too.** Its clip time has advanced on
        // every step since the body began, and a matcher's selection depends on every search before
        // it, so neither has a bounded history. A body on the provider chain is replayed in full;
        // replayed shallow, its scrubbed pose landed on a different sample from the played one.
        deep = deep || (entity.desc_.proceduralMotion && entity.motionChain_ != nullptr &&
                         entity.motionChain_->size() > 0);
        std::uint64_t need = 1; // every body integrates the step it lands on
        if (!deep) {
            for (const auto& behavior : entity.behaviors_) {
                const int steps = behavior->historySteps();
                if (steps < 0) {
                    deep = true;
                    break;
                }
                need = std::max(need, static_cast<std::uint64_t>(steps));
            }
        }
        // **ADR-620: a rate-limited speed is an integrator, so the ENTITY has a history
        // requirement of its own** -- one that belongs to no behaviour and is therefore invisible
        // to the loop above.
        //
        // Before the limiter, `state.speed` was assigned outright, so the step a body landed on
        // determined it and a shallow body needed one or two steps. With `accel`/`decel` applied
        // the speed ramps, and a replay too short to finish ramping lands on a different speed
        // than the played frame did -- which is ADR-360's contract broken, and it is how the
        // `drift` seek test found this change: seek read 0.10 m/s where play read 0.27.
        //
        // **Bounded, and that is the point.** The ramp converges in `maxSpeed / rate` seconds, so
        // the requirement is a few tens of steps rather than the whole window: for the shipping
        // `rook` (walk 3.07 m/s, accel 4.82) it is 39 steps against a 3,600-step window. A body
        // with no authored acceleration is unchanged.
        if (!deep && entity.desc_.gait.accelAuthored) {
            const GaitSettings& gait = entity.desc_.gait;
            const float rate = std::min(gait.accel > 0.0f ? gait.accel : 1e30f,
                                        gait.decel > 0.0f ? gait.decel : 1e30f);
            const float top = std::max(gait.runSpeed, gait.walkSpeed);
            if (rate > 0.0f && rate < 1e30f && top > 0.0f) {
                const double rampSteps = std::ceil((static_cast<double>(top) / rate) / dt) + 1.0;
                need = std::max(need, static_cast<std::uint64_t>(rampSteps));
            }
        }
        needSteps[i] = deep ? 0 : need; // 0 means "the whole window"
        deepBodies += deep ? 1u : 0u;
    }

    // ---- how far back to start ----------------------------------------------------------------
    //
    // The policy ceiling first, then the price cap, and the smaller wins. The cap is spent on the
    // bodies that need the window; the rest cost a step or two each and are not charged against it.
    double span = std::min(target, std::max(budget.maxSeconds, 0.0));
    bool budgetBound = false;
    if (budget.maxBodySteps > 0 && deepBodies > 0) {
        const auto affordable = static_cast<double>(budget.maxBodySteps / deepBodies) * dt;
        if (affordable < span) {
            span = affordable;
            budgetBound = true;
        }
    }
    const auto steps = static_cast<std::uint64_t>(span / dt);
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        seekFirstStep_[i] = needSteps[i] == 0 || needSteps[i] >= steps ? 0 : steps - needSteps[i];
    }

    perceptionCounts_ = PerceptionCounts{};
    gridPerception_.resetCounts();
    seekWork_ = SeekWork{};
    seekWork_.spanSeconds = static_cast<double>(steps) * dt;
    seekWork_.steps = steps;
    seekWork_.fullBodySteps = steps * static_cast<std::uint64_t>(entities_.size());
    seekWork_.deepBodies = deepBodies;
    seekWork_.shallowBodies = entities_.size() - deepBodies;
    seekWork_.budgetBound = budgetBound;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        seekWork_.bodySteps += steps - seekFirstStep_[i];
    }
    // TEMPORARY (ui-responsiveness phase 2): what the re-simulation actually integrated.
    probe2::frame().entitySimSteps += steps;
    probe2::frame().entitySimBodies += seekWork_.bodySteps;
    const probe2::Add probeEntitySeek(probe2::frame().entitySeekMs);
    // ---- the replay promises not to edit the world (ADR-483) ----------------------------------
    //
    // This is the caller the height cache exists for, and the only one in the engine that opts in.
    // A replay integrates entities against a fixed world -- it reads the terrain and never writes
    // it -- and the census measured 2.83M height calls at a 26.5% hit rate from a 4,096-entry
    // table across this one call. Opening the scope here rather than making the cache unconditional
    // is what keeps the world *build* (0.2% hit, twelve threads) out of the table entirely, and it
    // is what makes the promise auditable: one scope, one function, and the map named in it.
    std::optional<world::WorldMap::HeightCacheScope> heightCache;
    if (nav_.map() != nullptr) {
        heightCache.emplace(*nav_.map());
    }

    // A fixed step, not the frame's. That is what makes the answer a function of `time` alone: a
    // seek that integrated whatever dt the last frame happened to take would land somewhere that
    // depended on the machine it ran on.
    //
    // And the *last* step lands exactly on `target`, which it did not before this. The old loop ran
    // `now = target - span + i * dt`, so its final integrated instant was `target - dt` -- one step
    // short of the second being asked for, against a play whose final step is `target` itself. That
    // one step is where ADR-267's residual 0.000022 m came from. Counting backwards from the target
    // makes the replayed sequence the same sequence of instants a play from zero produces, and the
    // residual goes to zero; tests/unit/test_entity_seek.cpp holds it there with a 30 Hz arm beside
    // it that must disagree.
    // ADR-623. A play's first frame advances every provider memory at time 0 with no elapsed time
    // (its first search). A replay that starts from the very beginning has no step at 0, so it is
    // given that one here; without it the matcher's first search lands a frame late and every
    // search after it is shifted by one step.
    if (steps > 0 && target - static_cast<double>(steps) * dt < 0.5 * dt) {
        for (std::size_t e = 0; e < entities_.size(); ++e) {
            if (seekFirstStep_[e] == 0) {
                entities_[e]->advanceMotion(0.0, 0.0f);
            }
        }
    }
    for (std::uint64_t i = 0; i < steps; ++i) {
        const double now = target - static_cast<double>(steps - 1 - i) * dt;

        // The crowd, as it was at the end of the previous step, built before anything moves so
        // every body separates against the same snapshot. `update` has done this since crowd
        // separation existed and `seek` never did, so a scrub separated against whatever the last
        // *played* frame happened to leave behind -- state surviving across the one call whose
        // whole job is to remove state (ADR-267 defect 2).
        //
        // Skipped entirely when nothing needs the window. A body is classified shallow because its
        // answer at the target is a function of the target, and a crowd query is not: `state_.radius`
        // is written by `explore`, the crowd is read by `explore`, and `explore` is replayed in full.
        // Without this an all-craft scene pays 5,400 sweeps over its entities to build a field that
        // is empty every time and that nothing asks.
        if (deepBodies > 0) {
            crowd_.clear();
            crowdOwner_.clear();
            for (std::size_t e = 0; e < entities_.size(); ++e) {
                const Entity& entity = *entities_[e];
                if (!entity.active_ || entity.state_.radius <= 0.0f) {
                    continue;
                }
                const glm::vec3 at = entity.state_.position();
                spatial::NavigationObstacle body;
                body.center = glm::vec2(at.x, at.z);
                body.radius = entity.state_.radius;
                body.base = at.y;
                body.height = std::max(entity.state_.radius * 2.0f, 1.0f);
                body.type = spatial::ObstacleType::Creature;
                crowd_.add(body);
                crowdOwner_.push_back(e);
            }
            crowd_.build();
        }
        // And the sense stage's snapshot, on the same terms. A replay that skipped it would leave
        // every character knowing nothing at the second the scrub landed on, which is a working set
        // reconstructed wrongly rather than not at all -- and D4 is the promise that the replay
        // *is* the memory. `senseTick` is a pure function of the instant, so a replayed body senses
        // at the same instants a played one does and arrives at the same working set.
        if (perceiving_) {
            buildBodyIndex();
            gridPerception_.setIndex(perceptionIndex());
            gridPerception_.setClearance(nav_.valid() ? &nav_.clearance() : nullptr);
        }

        for (std::size_t entityIndex = 0; entityIndex < entities_.size(); ++entityIndex) {
            if (i < seekFirstStep_[entityIndex]) {
                continue; // its answer at `target` cannot depend on this step
            }
            Entity& entity = *entities_[entityIndex];
            entity.motion_ = MotionOffset{};
            entity.state_.hasLookTarget = false;
            entity.state_.reaction = 0.0f;
            // **`turnRate` is a RATE, so it is cleared with `activity` and for the same reason**
            // (ADR-619). Every turner writes it while it turns and none wrote zero when it
            // stopped, and `EntityState` persists -- so the last rate a body turned at survived
            // until something happened to write another. `Gait::select` reads it as this frame's
            // rate, which is the only reading under which its `turnEnter` band means anything, and
            // a standing body therefore kept classifying as `Activity::Turn`: measured at 509 of
            // 600 stationary frames, at a stale 2.094 rad/s against a 0.35 threshold.
            //
            // Clearing it here makes the contract the same one `activity` already has -- a
            // behaviour publishes what is true this frame, and silence means nothing is happening.
            // Every existing writer already computes an instantaneous `turned / dt`, so none of
            // them relied on the latch.
            entity.state_.activity = Activity::Idle;
            entity.state_.turnRate = 0.0f;
            entity.state_.detail = 1.0f;
            entity.state_.driven = false;
            entity.state_.airborne = false;
            entity.active_ = true;
            entity.everUpdated_ = true;

            // ---- intent, before behaviour (ADR-091) ----
            // Not replayed at all before this: `reset()` put the authored action list back and then
            // nothing integrated it, so ADR-091's Cinematic Action tier was the one tier a scrub
            // could not reproduce (ADR-267 defect 3). The events it raises are collected and thrown
            // away -- they belong to the moment they happened, and the moment is eighty seconds ago.
            entity.schedule_.update(now, entity.actions_);
            // The activity this action wants played, kept across the loop so the publish below can
            // carry it. It has to survive the iteration rather than be read at the end, because on
            // the final step the queue may have nothing pending and the answer is still whatever
            // the last step decided.
            entity.locomotion_.action.clear();
            if (entity.actions_.pending() > 0) {
                seekEvents_.clear();
                ActionContext ac;
                ac.time = now;
                ac.dt = dt;
                ac.rng = &entity.rng_;
                ac.self = &entity;
                ac.world = this;
                ac.path = &pathProvider();
                ac.gait = &entity.desc_.gait;
                ac.events = &seekEvents_;
                // **The result was discarded here, with a literal `(void)`.** `update` captures it
                // and publishes `locomotion_.action`, which is what picks the clip -- an action's
                // activity first, the gait's second. Throwing it away meant a body that sits while
                // the timeline runs stood up and walked the moment anyone scrubbed to the same
                // frame: an ADR-360 violation in the one tier ADR-267 defect 3 had already had to
                // teach this function to replay.
                const ActionOutput out = entity.actions_.update(ac, entity.state_);
                if (entity.locomotion_.action != out.activity) {
                    entity.locomotion_.action.assign(out.activity);
                }
            }

            if (perceiving_) {
                perceiveOne(entityIndex, now);
            }

            BehaviorContext bc;
            bc.time = now;
            bc.dt = dt;
            bc.bus = bus;
            bc.nav = &nav_;
            bc.world = this;
            // Which body this is. Omitted here and present in `update`, so every character
            // separated from entity 0 instead of from itself (ADR-267 defect 1) -- which for the
            // body that *is* entity 0 meant it pushed itself, and for everything else meant one
            // neighbour was invisible and one phantom was not.
            bc.self = entityIndex;
            bc.rng = &entity.rng_;
            // ADR-333. The same seam the played frame has, and it has to be here or a decider
            // would be the one tier a scrub could not reproduce -- which is ADR-267 defect 3 again,
            // one layer up. A decision taken during a replay pushes onto the queue the replay is
            // already integrating, so the replayed second contains the errand the played one did.
            bc.actions = &entity.actions_;
            // ADR-620, and it is applied on BOTH paths for the reason testing.md #31 gives: a
            // limit honoured by `update` and not by `seek` is a body that accelerates differently
            // when scrubbed than when played, which is exactly the class ADR-360 exists to stop.
            const float speedBefore = entity.state_.speed;
            for (auto& behavior : entity.behaviors_) {
                behavior->update(bc, entity.state_, entity.motion_);
            }
            limitSpeedToGait(entity.desc_.gait, speedBefore, step, entity.state_.speed);
            // The facing, canonicalised once after everything that steers has had its turn --
            // exactly where `update` does it. Without it a replayed yaw is the total a body has
            // turned rather than the direction it faces, and `angleDelta` against it is a different
            // number after 5,400 steps than after 1.
            entity.state_.yaw = wrapAngle(entity.state_.yaw, 3.14159265358979323846f);
            if (entity.state_.activity == Activity::Idle && entity.state_.reaction > 0.4f) {
                entity.state_.activity = Activity::React;
            }
            // The gait, with its hysteresis, because the hysteresis is state: a seek that published
            // the raw activity landed on a different clip from the play it is supposed to match,
            // on exactly the frames where the speed is sitting on a threshold.
            entity.locomotion_.activity =
                entity.gait_.select(entity.desc_.gait, entity.state_.activity, entity.state_.speed,
                                    entity.state_.turnRate, dt);
            // ADR-623: **the provider memory, on every replayed step, at that step's own time.**
            // This used to run once, after the replay, at the target time. A clip provider's time
            // and a matcher's selection therefore came out of a seek as the answer to one step from
            // a reset memory, and every scrubbed provider pose disagreed with the played one. The
            // matcher's scrub test found it; the clip provider had the same defect with nothing
            // looking.
            entity.advanceMotion(now, static_cast<float>(dt));
        }
    }
    // Publish the state the next frame will build on, without touching the parameter set.
    for (auto& entityPtr : entities_) {
        Entity& entity = *entityPtr;
        if (steps == 0) {
            entity.locomotion_.activity = entity.state_.activity;
        }
        entity.locomotion_.playbackRate =
            Gait::playbackRate(entity.desc_.gait, entity.locomotion_.activity, entity.state_.speed);
        entity.locomotion_.blend = entity.desc_.gait.blend;
        entity.locomotion_.time = target;
        entity.locomotion_.position = entity.state_.position() + entity.motion_.position;
        entity.locomotion_.yaw = entity.state_.yaw;
        entity.locomotion_.speed = entity.state_.speed;
        entity.locomotion_.turnRate = entity.state_.turnRate;
        // ADR-545. Both halves cross the seam: `speed`/`yaw` are what the mover intended, and these
        // two are what the body did. An animation layer that has to tell a strafe from a walk needs
        // the pair, and until now the seam could only carry one of them.
        entity.locomotion_.velocity = entity.state_.velocity;
        entity.locomotion_.facing = entity.state_.facing();
        // Published on BOTH paths, deliberately. The sweep that added these asked, for every
        // field of the seam, which of `seek` and `update` writes it -- and two of the answers
        // were wrong: `velocity`/`facing` were written only by `seek`, and `action` only by
        // `update`. `grounded` was written by neither -- it is written by both now, and still
        // read by nobody, which is the half of the fix that did not happen (ADR-615).
        entity.locomotion_.acceleration = entity.state_.acceleration;
        // Phase B §8-§11, on both paths (ADR-554). The plan reads the gait's answer and adds
        // the transitional phase to it -- a body does not go from standing to walking, it
        // goes from standing to STARTING to walking, and `Gait` has no notion of that.
        {
            LocomotionPlanState nextPlan;
            const LocomotionPlan plan = planLocomotion(
                entity.desc_.locomotion, entity.locomotionPlan_, entity.locomotion_.activity,
                entity.state_.groundSpeed(), entity.state_.speed, entity.state_.turnRate,
                entity.state_.strafeAngle(), entity.locomotion_.time, nextPlan);
            entity.locomotionPlan_ = nextPlan;
            entity.locomotion_.phase = plan.phase;
            entity.locomotion_.phaseStride = plan.strideScale;
            entity.locomotion_.strafeAngle = plan.strafeAngle;
        }
        entity.locomotion_.grounded = !entity.state_.airborne;
        entity.locomotion_.dt = static_cast<float>(dt);
        // Phase B: the provider memory. Advanced inside the replay above, once per step (ADR-623).
        // Only a seek with nothing to replay advances it here, once, at the target, as the played
        // frame at that instant did.
        if (steps == 0) {
            entity.advanceMotion(entity.locomotion_.time, 0.0f);
        }
        entity.locomotion_.reaction = entity.state_.reaction;
        entity.locomotion_.lookTarget = entity.state_.lookTarget;
        entity.publishLookSchedule();
        // ADR-359: the ground under this body, for a foot IK layer. Published here beside the look
        // target, and by the same rule -- the entity owns the smoothing, the layer owns the solve.
        entity.locomotion_.groundPoint = entity.state_.groundPoint;
        entity.locomotion_.groundNormal = entity.state_.groundNormal;
        entity.locomotion_.hasGroundPlane = entity.state_.hasGroundPlane;
    }
    // What the replay's sense stage cost, in the same structural terms an update reports it in.
    // A seek at 60 Hz over 90 s runs 5,400 steps; at 4 Hz a body senses on 360 of them, and the
    // difference between those two numbers is the whole of what the cadence buys the replay.
    if (perceiving_ && perception_ == nullptr) {
        const GridPerception::Counts counts = gridPerception_.counts();
        perceptionCounts_.candidates = counts.candidates;
        perceptionCounts_.percepts = counts.percepts;
        perceptionCounts_.dropped = counts.dropped;
        perceptionCounts_.occlusionTests = counts.occlusionTests;
        perceptionCounts_.occlusionDeferred = counts.occlusionDeferred;
    }
}

glm::vec2 EntityWorld::crowdSeparation(std::size_t self, glm::vec2 p, float radius) const {
    if (crowd_.empty() || radius <= 0.0f) {
        return glm::vec2(0.0f);
    }
    glm::vec2 push(0.0f);
    // A disc query into the grid, not a pass over every body. `query` allocates into a caller's
    // vector, so this keeps its own and pays one allocation the first time rather than one per call.
    static thread_local std::vector<std::uint32_t> hits;
    crowd_.query(p, radius, hits);
    for (const std::uint32_t i : hits) {
        if (i < crowdOwner_.size() && crowdOwner_[i] == self) {
            continue; // a body does not push itself
        }
        const spatial::NavigationObstacle& other = crowd_.obstacles()[i];
        const float reach = other.radius + radius;
        glm::vec2 d = p - other.center;
        const float distSq = glm::dot(d, d);
        if (distSq >= reach * reach) {
            continue;
        }
        if (distSq < 1e-6f) {
            // Exactly coincident. Nothing in the geometry says which way to go, so take a direction
            // from the pair's own indices: arbitrary, and the same arbitrary answer every frame,
            // which is what stops two bodies jittering against each other forever.
            const float angle = static_cast<float>((self + i) % 97u) * 0.06479f;
            push += glm::vec2(std::cos(angle), std::sin(angle)) * reach;
            continue;
        }
        const float dist = std::sqrt(distSq);
        // Softer than the static push: bodies yield to each other rather than bouncing, and a
        // separation that resolved fully in one frame reads as two people repelling like magnets.
        push += (d / dist) * (reach - dist) * 0.5f;
    }
    return push;
}

float EntityWorld::property(std::string_view entity, std::string_view property, float fallback) const {
    const Entity* found = find(entity);
    return found != nullptr && found->hasProperty(property) ? found->property(property) : fallback;
}

bool EntityWorld::startRoutine(std::string_view entity, double now) {
    Entity* found = find(entity);
    if (found == nullptr || found->schedule().desc().entries.empty()) {
        return false;
    }
    found->schedule().start(now);
    found->actions().hold(Authority::Routine, false);
    return true;
}

bool EntityWorld::pauseRoutine(std::string_view entity, double now) {
    Entity* found = find(entity);
    if (found == nullptr || !found->schedule().running()) {
        return false;
    }
    found->schedule().pause(now, found->actions());
    return true;
}

bool EntityWorld::resumeRoutine(std::string_view entity, double now) {
    Entity* found = find(entity);
    if (found == nullptr || !found->schedule().paused()) {
        return false;
    }
    found->schedule().resume(now, found->actions());
    return true;
}

bool EntityWorld::direct(std::string_view entity, std::vector<ActionDesc> actions, double now) {
    Entity* found = find(entity);
    if (found == nullptr) {
        return false;
    }
    // The Director tier, so whatever the entity was doing keeps its place and its elapsed seconds
    // and carries on when this drains. That is ADR-091's "resumes rather than resets", and it is a
    // property of *which tier* the override goes on rather than of anything the caller must do.
    found->actions().override(std::move(actions), Authority::Director, now);
    return true;
}

void EntityWorld::update(const EntityUpdate& ctx, params::ParameterSet& params) {
    if (!parametersLive_) {
        return;
    }
    params_ = &params;
    counts_ = {};
    navPath_.setNavigator(&nav_);
    // Cleared, not freed: the capacity is kept, so a frame in which twelve actions complete costs
    // no allocation after the first one that did (§47). Anything raised since the last update --
    // a director's cancellation -- is folded in rather than lost.
    actionEvents_.clear();
    if (!pendingEvents_.empty()) {
        actionEvents_.swap(pendingEvents_);
        pendingEvents_.clear();
    }
    // The crowd, as it was at the end of the last update. Built before anything moves so every
    // character separates against the same snapshot: building it as they go would make the answer
    // depend on the order they happen to be stored in.
    crowd_.clear();
    crowdOwner_.clear();
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        const Entity& entity = *entities_[i];
        if (!entity.active_ || entity.state_.radius <= 0.0f) {
            continue;
        }
        const glm::vec3 at = entity.state_.position();
        spatial::NavigationObstacle body;
        body.center = glm::vec2(at.x, at.z);
        body.radius = entity.state_.radius;
        body.base = at.y;
        body.height = std::max(entity.state_.radius * 2.0f, 1.0f);
        body.type = spatial::ObstacleType::Creature;
        crowd_.add(body);
        crowdOwner_.push_back(i);
    }
    crowd_.build();

    // The sense stage's snapshot, built here for exactly the reason the crowd's is: every character
    // senses the world as it was at the end of the last step, so what one body notices does not
    // depend on whether the body it noticed had already been updated this frame (ADR-270, ADR-290).
    perceptionCounts_ = PerceptionCounts{};
    gridPerception_.resetCounts();
    if (perceiving_) {
        buildBodyIndex();
        gridPerception_.setIndex(perceptionIndex());
        // The only occlusion this engine owns, and the reason there is a budget at all. A field
        // with a null map means no test is ever performed and no percept claims to have been
        // tested -- which is the honest answer for a scene with no terrain, and is not the same
        // answer as testing and finding nothing in the way.
        gridPerception_.setClearance(nav_.valid() ? &nav_.clearance() : nullptr);
    }

    // Folding an entity's offsets onto its node's parameter *finals*, which has to happen on every
    // frame whether or not the simulation advanced on it.
    //
    // Finals are rebuilt from bases at the top of every `Engine::update`, so "leave the parameters
    // alone" does not mean "leave the entity where it was" -- it means the node snaps back to the
    // position the author placed it at. A coarse entity that skipped the write was therefore drawn
    // at its authored spot on the frames it skipped and at its simulated spot on the frames it did
    // not: past `fullDetailDistance` that is five frames in six, which reads as a character
    // flickering between two places in the world.
    //
    // On a skipped frame the offsets are the ones the last update produced, so the pose is held
    // rather than recomputed. That is what "nothing on screen moves" was always supposed to mean.
    const auto applyOffsets = [](Entity& e) {
        const glm::vec3 offset = e.motion_.position + e.state_.travel;
        if (e.positionParam_ != nullptr) {
            for (int i = 0; i < 3; ++i) {
                const auto c = static_cast<std::size_t>(i);
                e.positionParam_->setFinalComponent(c, e.positionParam_->finalComponent(c) + offset[i]);
            }
        }
        if (e.rotationParam_ != nullptr) {
            // Yaw as a *difference* from the facing the author placed the body at, not as a whole
            // angle added to it (ADR-240). `state_.yaw` starts at `facing_`, so at t = 0 this adds
            // nothing and the body is drawn exactly where the scene file put it -- and after that
            // the drawn heading is the heading, rather than the heading plus a placement angle
            // nobody meant as an offset. Pitch and roll stay additive: those really are offsets on
            // top of whatever tilt the author authored.
            const glm::vec3 rotation(e.motion_.rotation.x,
                                     e.motion_.rotation.y + (e.state_.yaw - e.facing_) * kDegrees,
                                     e.motion_.rotation.z);
            for (int i = 0; i < 3; ++i) {
                const auto c = static_cast<std::size_t>(i);
                // Folded into (-180, 180] before it is written, because the parameter's hard range
                // is +/-360 and `setFinalComponent` *clamps*. Euler degrees are 360-periodic, so
                // this is the same orientation and cannot be anything else; what it removes is the
                // silent clamp, which is not a rotation at all.
                e.rotationParam_->setFinalComponent(
                    c, wrapAngle(e.rotationParam_->finalComponent(c) + rotation[i], 180.0f));
            }
        }
        if (e.scaleParam_ != nullptr) {
            for (int i = 0; i < 3; ++i) {
                const auto c = static_cast<std::size_t>(i);
                e.scaleParam_->setFinalComponent(c, e.scaleParam_->finalComponent(c) * e.motion_.scale[i]);
            }
        }
    };

    for (std::size_t entityIndex = 0; entityIndex < entities_.size(); ++entityIndex) {
        auto& entityPtr = entities_[entityIndex];
        Entity& entity = *entityPtr;
        const glm::vec3 here = entity.state_.position();
        const float distance = glm::length(here - ctx.viewPosition);
        // The one update level of detail may never skip. Everything below reasons about an entity
        // that has state worth keeping; on the first frame there is none, and skipping leaves the
        // pose sink reading a default-constructed LocomotionState at the origin.
        const bool first = !entity.everUpdated_;

        // An entity under orders is never culled. A behaviour is ambient and losing it off camera
        // costs nothing; an *action* is something a director said, and a character that stopped
        // walking to the nightstand because the camera looked away would be a bug nobody could
        // reproduce. The coarse stage below still applies, so the cost stays bounded -- what is
        // refused here is only the "do not update at all" band.
        const bool underOrders = entity.actions_.pending() > 0 || entity.schedule_.running() ||
                                 entity.director_.active;
        if (!first && !underOrders && ctx.distanceDetail && entity.desc_.cullDistance > 0.0f &&
            distance > entity.desc_.cullDistance) {
            // Far enough away that nothing it could do would be visible. Not merely a cheaper
            // update: no update, and no parameter write either, so the node stays exactly where
            // the scene put it.
            ++counts_.skipped;
            entity.active_ = false;
            continue;
        }

        double dt = ctx.dt;
        if (!first && ctx.distanceDetail && entity.desc_.fullDetailDistance > 0.0f &&
            distance > entity.desc_.fullDetailDistance) {
            entity.coarseAccum_ += ctx.dt;
            if (entity.coarseAccum_ < static_cast<double>(entity.desc_.coarseInterval)) {
                ++counts_.skipped;
                // The simulation does not advance, but the transform is still written: finals are
                // rebuilt from bases every frame, so skipping the write puts the node back where
                // the author placed it rather than leaving it where the entity is.
                applyOffsets(entity);
                continue;
            }
            dt = entity.coarseAccum_;
            entity.coarseAccum_ = 0.0;
            entity.state_.detail = static_cast<float>(ctx.dt / std::max(dt, 1e-6));
            ++counts_.coarse;
        } else {
            entity.coarseAccum_ = 0.0;
            entity.state_.detail = 1.0f;
            ++counts_.full;
        }
        entity.active_ = true;
        entity.everUpdated_ = true;

        const glm::vec3 travelBefore = entity.state_.travel;
        // ADR-620: the speed this body had last step, so the authored accel/decel can be applied
        // to whatever the behaviours ask for this one.
        const float speedBefore = entity.state_.speed;
        entity.motion_ = MotionOffset{};
        entity.state_.hasLookTarget = false;
        entity.state_.reaction = 0.0f;
        // Cleared on BOTH paths, deliberately: `seek` and `update` reset the per-frame state
        // separately, so a field cleared in one and not the other is cleared for a played frame
        // and latched for a scrubbed one (ADR-619, testing.md #31).
        entity.state_.activity = Activity::Idle;
        entity.state_.turnRate = 0.0f;
        entity.state_.driven = false;
        entity.state_.airborne = false;

        // ---- the Director tier, above everything (ADR-210) ----
        // A director says where a body *is*. Written as `travel` rather than as an offset so that
        // `position()`, the crowd field, the fields pass and every query that reads an entity's
        // place see the same answer, and `driven` so the locomotor behaviours yield by keeping
        // their state -- an animal put down after an abduction resumes the walk it was on.
        if (entity.director_.active) {
            entity.state_.travel = entity.director_.position - entity.state_.anchor;
            if (entity.director_.hasYaw) {
                entity.state_.yaw = entity.director_.yaw;
            }
            entity.state_.driven = true;
            entity.state_.airborne = true;
        }

        // ---- intent, before behaviour (ADR-091) ----
        // The hierarchy is read top-down, so the action tier gets its say first and the behaviours
        // below it see `driven` and yield. Yielding is not stopping: a wanderer keeps its
        // destination and its pause timer, which is what makes the fall-back a resume.
        entity.schedule_.update(ctx.time, entity.actions_);
        ActionOutput intent;
        if (entity.actions_.pending() > 0) {
            ActionContext ac;
            ac.time = ctx.time;
            ac.dt = dt;
            ac.rng = &entity.rng_;
            ac.self = &entity;
            ac.world = this;
            ac.path = &pathProvider();
            ac.gait = &entity.desc_.gait;
            ac.events = &actionEvents_;
            intent = entity.actions_.update(ac, entity.state_);
        }

        // §21, the *entry* half of the arc, and it goes here rather than after the behaviours
        // because the behaviours already know what to do with it. `wander` reads React and yields
        // -- keeping its destination and its pause timer, and resuming from where it stopped --
        // which is the addendum's `Walking -> Dance -> Walking` contract, already written, already
        // tested, and belonging to the behaviour rather than to the thing that interrupted it. A
        // field that set the activity afterwards would be a second mechanism for the same idea.
        if (entity.arcLevel_ > 0.0f) {
            entity.state_.reaction = entity.arcLevel_;
            entity.state_.activity = entity.arcActivity_;
        }

        // ---- the senses, before the behaviours (ADR-270) ----
        // Above the behaviours and below the action tier, because a percept is something a body
        // *knows* and an order is something it was *told*: a character under orders still notices
        // what is around it, and P3's decider is what will be allowed to act on that.
        //
        // **This used to say "Nothing reads `Entity::percepts()` yet… so this stage currently
        // changes no character's route by a millimetre". That is no longer true** (ADR-615):
        // `Explore` merges `self->percepts()` into its memory and `decision.cpp` scores them, so
        // the sense stage does change routes through the `Perceived` goal source. The accessor is
        // grep-complete at three lines -- declaration, this comment, one consumer -- so there is no
        // second path the old claim could have been narrowly true about. Someone profiling this
        // stage, or tracking down an odd goal, would have been told to stop looking here.
        if (perceiving_) {
            perceiveOne(entityIndex, ctx.time);
        }

        // ---- root motion, before the behaviours (ADR-337) ----
        //
        // **Which position (ADR-260).** It writes `state_.travel` -- the *simulation* position,
        // `MotionAuthority::Simulation`. Everything else in the animation seam is `PoseOnly` and
        // `scene::PoseLayerStack` is structurally unable to be anything else; this is the one
        // place a clip is allowed to move the body, and it is the reason the opt-in exists. It
        // reads the node's scale parameter and `state_.yaw`, and it reads nothing of the pose.
        //
        // **Before the behaviours, not after.** Root motion is displacement, and every behaviour
        // that has an opinion about where a body may be -- grounding, crowd separation, the
        // obstacle push -- gets to have it about this displacement too, on the same frame, exactly
        // as it does about navigation's. Running afterwards would have made an animation clip
        // outrank the terrain, which is the shape of four defects in this repository rather than
        // a feature. The visible consequence is that a *grounded* body keeps no vertical root
        // motion at all: `applyGrounding` assigns `state.travel.y = ground.height - anchor.y`, so
        // the y component is overwritten within the same update that produced it. That is correct
        // -- a body standing on terrain has its height decided by the terrain -- and it is why
        // `RootMotionAxes` exists and why ADR-337 §5 recommends `xz` for `Landing` in a world with
        // ground in it.
        entity.rootMotionStep_ = glm::vec3(0.0f);
        if (entity.rootMotion_ != nullptr) {
            const RootMotionSample sample = entity.rootMotion_->rootMotion(ctx.time);
            if (!sample.active) {
                entity.rootMotionHeld_ = false;
            } else {
                if (!entity.rootMotionHeld_ || sample.generation != entity.rootMotionGeneration_) {
                    // A different clip, or the same clip restarted. The displacement is measured
                    // from the clip's first key, so there is no step across the boundary: the new
                    // run begins where the body already is.
                    entity.rootMotionLast_ = glm::vec3(0.0f);
                    entity.rootMotionGeneration_ = sample.generation;
                    entity.rootMotionHeld_ = true;
                }
                const glm::vec3 local = sample.displacement - entity.rootMotionLast_;
                entity.rootMotionLast_ = sample.displacement;
                // Asset units into world metres, then the body's facing. The scale is read from
                // the node parameter rather than remembered, for the reason ADR-274 §5 paid to
                // learn: Glowmere draws its aliens at 3.344x to 3.610x, a joint offset is in the
                // asset's own units, and a scale-blind read would deliver 28% of the movement.
                glm::vec3 scale(1.0f);
                if (entity.scaleParam_ != nullptr) {
                    for (std::size_t c = 0; c < 3; ++c) {
                        scale[static_cast<int>(c)] = entity.scaleParam_->finalComponent(c);
                    }
                }
                const glm::vec3 scaled = local * scale;
                // Yaw about +Y, the same convention `socketTransform` composes with: the clip's
                // forward is the rig's +Z and the body's facing is where that points now.
                const float cy = std::cos(entity.state_.yaw);
                const float sy = std::sin(entity.state_.yaw);
                const glm::vec3 world(scaled.x * cy + scaled.z * sy, scaled.y,
                                      -scaled.x * sy + scaled.z * cy);
                entity.state_.travel += world;
                entity.rootMotionStep_ = world;
            }
        }

        BehaviorContext bc;
        bc.time = ctx.time;
        bc.dt = dt;
        bc.bus = ctx.bus;
        bc.nav = &nav_;
        bc.world = this;
        bc.self = entityIndex;
        bc.rng = &entity.rng_;
        // ADR-333: the decider's output is an `ActionDesc` list on this entity's own queue. Handed
        // in here rather than reached for through the world, because the world is const to a
        // behaviour and must stay that way -- a behaviour that could reach another body's queue
        // would be a second writer of somebody else's intentions.
        bc.actions = &entity.actions_;
        for (auto& behavior : entity.behaviors_) {
            behavior->update(bc, entity.state_, entity.motion_);
        }
        // The director's *additive* half, after the behaviours rather than before them: the point
        // of the split is that a craft keeps hovering, drifting and banking while it is being flown
        // somewhere, and a spin the director asked for is on top of the spin the scene authored.
        // Speed is written last because the gait reads it, and a body carried by a beam should have
        // its legs going even though nothing navigated it there.
        if (entity.director_.active) {
            entity.motion_.rotation += entity.director_.rotation;
            if (entity.director_.hasSpeed) {
                entity.state_.speed = entity.director_.speed;
            }
        }
        // ADR-620. After every writer of intent -- behaviours, the action tier, the director -- and
        // before the arc, the velocity measurement and the gait, all three of which read the speed
        // and would otherwise read one that teleported. The arc's `arcHoldStill_` stop below stays
        // a hard stop on purpose: a body held by a beam is not decelerating, it is being held.
        limitSpeedToGait(entity.desc_.gait, speedBefore, ctx.dt, entity.state_.speed);
        // The body's facing, not the total it has turned. Canonicalised here, once, after everything
        // that steers has had its turn and before anything reads it -- the node's rotation, the
        // sockets, and the LocomotionState the animation layer is handed all see the same angle.
        entity.state_.yaw = wrapAngle(entity.state_.yaw, 3.14159265358979323846f);

        // Fold the behaviours' offsets onto the node's parameter *finals*. Bases are left alone, so
        // saving the project writes back what the author placed rather than wherever the entity
        // happened to be when they hit save.
        applyOffsets(entity);

        // §21, the return half of the arc. The arc *overrides* an activity while it runs and then
        // stops overriding it; it never writes Idle, never resets a behaviour and never clears a
        // destination. So what comes back when it ends is whatever the behaviours are doing at
        // that moment -- `Walking -> Dance -> Walking`, structurally, rather than by remembering
        // to put something back (addendum §14).
        if (entity.arcLevel_ > 0.0f) {
            // And re-asserted, because a behaviour that ran after the entry write may have decided
            // something else -- `interest` raises Observe on a loud frame, and the arc outranks it
            // for as long as it lasts.
            entity.state_.reaction = std::max(entity.state_.reaction, entity.arcLevel_);
            entity.state_.activity = entity.arcActivity_;
            if (entity.arcHoldStill_) {
                // Restored, not zeroed at the source: the behaviour that was walking keeps its
                // destination and its progress toward it, and picks the walk up where it left it.
                entity.state_.travel = travelBefore;
                entity.state_.speed = 0.0f;
                entity.state_.turnRate = 0.0f;
            }
        }

        // ---- the measured velocity (ADR-545) ----------------------------------------------
        //
        // Taken HERE, after `applyOffsets`, because this is the first instant at which the body's
        // place for this step is settled: every behaviour has run, the action tier and the director
        // have had their say, root motion has been added and crowd separation has pushed. One
        // backward difference at one site is correct for all of them, and it is the only thing in
        // this file that measures rather than decides.
        //
        // `dt <= 0` leaves the previous velocity alone rather than dividing by it. That is not
        // hypothetical: ADR-521 records that `FixedStepClock::tick()` hands out `deltaTime = 0` on
        // the first tick of every render, so a naive division would publish an infinity into the
        // pose layer on frame one of every offline job.
        {
            const glm::vec3 settled = entity.state_.position();
            const glm::vec3 previousVelocity = entity.state_.velocity;
            if (entity.hasLastPosition_ && ctx.dt > 0.0) {
                entity.state_.velocity = (settled - entity.lastPosition_) / static_cast<float>(ctx.dt);
                // Phase B §19/§35. **Measured once, here, beside the velocity it differences** --
                // ADR-545's rule applied one derivative out. Lean, stride and balance all want it
                // and three subsystems differencing the same vector independently is how the
                // engine ended up with two answers to "where is this body" (ADR-260).
                //
                // The second frame of a body's life has a previous velocity of zero, so its
                // acceleration reads as the whole of its speed over one step. That is honest --
                // it did accelerate from rest -- and the lean layer's own limit is what stops it
                // being a visible snap.
                entity.state_.acceleration =
                    entity.hasLastVelocity_
                        ? (entity.state_.velocity - previousVelocity) / static_cast<float>(ctx.dt)
                        : glm::vec3(0.0f);
                entity.hasLastVelocity_ = true;
            } else {
                entity.state_.velocity = glm::vec3(0.0f);
                entity.state_.acceleration = glm::vec3(0.0f);
            }
            entity.lastPosition_ = settled;
            entity.hasLastPosition_ = true;
        }

        // A character doing nothing else, with a reaction still ringing, is reacting. Resolved
        // here rather than in the behaviour that raised it, because whether there was anything
        // else to do is only known once every behaviour has had its turn.
        if (entity.state_.activity == Activity::Idle && entity.state_.reaction > 0.4f) {
            entity.state_.activity = Activity::React;
        }
        // ---- the gait (§7) ----
        // The behaviour said how fast it is going; the gait says what that looks like. Separated
        // because the two answers have different requirements: the speed must be exactly what the
        // navigation produced, and the *clip* must not change four times a second because the
        // speed is sitting on a threshold. Hysteresis lives here and nowhere else, so every
        // behaviour and every action gets it without asking.
        const Activity gait =
            entity.gait_.select(entity.desc_.gait, entity.state_.activity, entity.state_.speed,
                                entity.state_.turnRate, dt);
        entity.locomotion_.activity = gait;
        entity.locomotion_.playbackRate = Gait::playbackRate(entity.desc_.gait, gait, entity.state_.speed);
        // The feet against the ground they are crossing. Two numbers authored by different people
        // in different files -- a behaviour's travel speed and a gait's stride speed -- with
        // nothing comparing them until now; the only symptom is an animation that looks wrong in a
        // way nobody can name. 1.5x is the threshold because a quarter of a stride either way is
        // inside what a viewer reads as a character adjusting its pace.
        if (!entity.warnedFootSlip_) {
            const float slip = Gait::footSlip(entity.desc_.gait, gait, entity.state_.speed);
            if (slip > 1.5f || slip < 1.0f / 1.5f) {
                entity.warnedFootSlip_ = true;
                const float authored =
                    gait == Activity::Run ? entity.desc_.gait.runSpeed : entity.desc_.gait.walkSpeed;
                log::warn("entity '{}': travelling at {:.2f} m/s against a {} clip authored for "
                          "{:.2f} m/s ({:.1f}x foot slip){}",
                          entity.desc_.name, entity.state_.speed,
                          gait == Activity::Run ? "run" : "walk", authored, slip,
                          entity.desc_.gait.matchRate ? "; rate matching is on and saturated"
                                                      : "; rate matching is off");
            }
        }
        entity.locomotion_.blend = entity.desc_.gait.blend;
        // What an action asked to be played, if it asked for anything. Assigned rather than
        // rebuilt so a steady state reuses the string's capacity.
        if (entity.locomotion_.action != intent.activity) {
            entity.locomotion_.action.assign(intent.activity);
        }
        entity.locomotion_.time = ctx.time;
        entity.locomotion_.position = entity.state_.position() + entity.motion_.position;
        entity.locomotion_.yaw = entity.state_.yaw;
        entity.locomotion_.speed = entity.state_.speed;
        entity.locomotion_.turnRate = entity.state_.turnRate;
        // ADR-545's measurement, across the seam -- **and this line was missing**. `seek` published
        // it and `update` did not, so a pose layer got the right velocity while scrubbing and a
        // zero one while playing: an ADR-360 violation in a field the animation layer reads, and
        // backwards from every other kind of staleness bug.
        //
        // It survived because every velocity test asserted on `Entity::state()`, which is the
        // measurement, and none asserted on `Entity::locomotion()`, which is what anything
        // downstream actually receives. Same shape as ADR-553.
        entity.locomotion_.velocity = entity.state_.velocity;
        entity.locomotion_.facing = entity.state_.facing();
        // Published on BOTH paths, deliberately. The sweep that added these asked, for every
        // field of the seam, which of `seek` and `update` writes it -- and two of the answers
        // were wrong: `velocity`/`facing` were written only by `seek`, and `action` only by
        // `update`. `grounded` was written by neither -- it is written by both now, and still
        // read by nobody, which is the half of the fix that did not happen (ADR-615).
        entity.locomotion_.acceleration = entity.state_.acceleration;
        // Phase B §8-§11, on both paths (ADR-554). The plan reads the gait's answer and adds
        // the transitional phase to it -- a body does not go from standing to walking, it
        // goes from standing to STARTING to walking, and `Gait` has no notion of that.
        {
            LocomotionPlanState nextPlan;
            const LocomotionPlan plan = planLocomotion(
                entity.desc_.locomotion, entity.locomotionPlan_, entity.locomotion_.activity,
                entity.state_.groundSpeed(), entity.state_.speed, entity.state_.turnRate,
                entity.state_.strafeAngle(), entity.locomotion_.time, nextPlan);
            entity.locomotionPlan_ = nextPlan;
            entity.locomotion_.phase = plan.phase;
            entity.locomotion_.phaseStride = plan.strideScale;
            entity.locomotion_.strafeAngle = plan.strafeAngle;
        }
        entity.locomotion_.grounded = !entity.state_.airborne;
        entity.locomotion_.dt = static_cast<float>(ctx.dt);
        // Phase B: the provider memory, advanced on this path as on the other one. Both, for
        // ADR-554's reason -- and this is the field that rule was discovered by, so getting it
        // wrong here would be the same bug in the same struct twice.
        entity.advanceMotion(entity.locomotion_.time, static_cast<float>(ctx.dt));
        entity.locomotion_.reaction = entity.state_.reaction;
        entity.locomotion_.lookTarget = entity.state_.lookTarget;
        entity.publishLookSchedule();
        // ADR-359: the ground under this body, for a foot IK layer. Published here beside the look
        // target, and by the same rule -- the entity owns the smoothing, the layer owns the solve.
        entity.locomotion_.groundPoint = entity.state_.groundPoint;
        entity.locomotion_.groundNormal = entity.state_.groundNormal;
        entity.locomotion_.hasGroundPlane = entity.state_.hasGroundPlane;
        if (entity.pose_ != nullptr) {
            entity.pose_->setLocomotion(entity.locomotion_);
        }

        applyAttachments(entity, params);
    }
    if (perceiving_ && perception_ == nullptr) {
        const GridPerception::Counts counts = gridPerception_.counts();
        perceptionCounts_.candidates = counts.candidates;
        perceptionCounts_.percepts = counts.percepts;
        perceptionCounts_.dropped = counts.dropped;
        perceptionCounts_.occlusionTests = counts.occlusionTests;
        perceptionCounts_.occlusionDeferred = counts.occlusionDeferred;
    }
    if (actionListener_) {
        for (const ActionEvent& event : actionEvents_) {
            actionListener_(event);
        }
    }
}

void EntityWorld::applyAttachments(const Entity& entity, params::ParameterSet& params) const {
    for (const AttachmentDesc& attachment : entity.attachments()) {
        scene::Transform t;
        // The attachment is placed on either answer: `EntityFrame` is an approximation and a prop
        // still has to be somewhere. Which one it was is reported by `socketTransform` and is what
        // `tests/unit/test_character_lab_sockets.cpp` asserts on; here only "is there a socket"
        // decides whether anything is written.
        if (!resolved(entity.socketTransform(attachment.socket, t))) {
            continue;
        }
        const NodeBinding* b = binding(attachment.node);
        if (b == nullptr || !b->exists) {
            continue;
        }
        if (auto* position = params.findAs<glm::vec3>(b->transformPrefix + "position")) {
            for (std::size_t c = 0; c < 3; ++c) {
                position->setFinalComponent(c, t.position[static_cast<int>(c)]);
            }
        }
        if (auto* rotation = params.findAs<glm::vec3>(b->transformPrefix + "rotation")) {
            const glm::vec3 euler = eulerDegrees(t.rotation);
            for (std::size_t c = 0; c < 3; ++c) {
                rotation->setFinalComponent(c, euler[static_cast<int>(c)]);
            }
        }
    }
}


// ---- fields (ADR-097) --------------------------------------------------------------------------
//
// The field pass is a separate pass from the behaviour pass, and the separation is not cosmetic.
// A behaviour's output is an offset folded onto a node *after* modulation; a field's output is a
// gain on modulation itself, so it has to be settled *before* the routes run. Doing it inside
// update() would put every reaction in the scene one frame behind its field, which is invisible
// while something is moving slowly and is a different picture the moment anybody scrubs.

glm::vec3 Entity::fieldPosition() const {
    glm::vec3 p = state_.position() + motion_.position;
    if (positionParam_ != nullptr) {
        // Whatever the timeline (or a route that already ran) has moved the node by, relative to
        // where the scene put it. For a behaviour-driven entity this is zero at the moment the
        // field pass runs and the anchor plus travel is the whole answer. For a node a sequencer
        // actor drives it is the entire motion -- which is what makes a field over a baked actor a
        // pure function of time, and therefore scrub-safe and offline-exact (ADR-091).
        for (std::size_t c = 0; c < 3; ++c) {
            p[static_cast<int>(c)] += positionParam_->finalComponent(c) - positionParam_->baseComponent(c);
        }
    }
    return p;
}

void EntityWorld::setFields(std::vector<FieldDesc> fields) {
    fields_ = std::move(fields);
    fieldParams_.assign(fields_.size(), FieldParams{});
    fieldsBound_ = false;
}

// The authored field with its live knobs folded in: strength, scale, inner ratio, floor and centre
// as the timeline and the presets currently have them. Read from the parameter *finals*, which at
// the moment the field pass runs are exactly base + automation -- the frame's resetFinals() has
// wiped the previous frame's routes and this frame's have not run yet. That is what keeps a field
// over a baked source a pure function of time (ADR-091), and it is also why a modulation route
// pointed at a field knob does nothing; the host says so by name.
FieldDesc EntityWorld::resolvedField(std::size_t index) const {
    FieldDesc field = fields_[index];
    if (index >= fieldParams_.size()) {
        return field;
    }
    const FieldParams& p = fieldParams_[index];
    if (p.strength != nullptr) {
        field.strength = p.strength->finalComponent(0);
    }
    if (p.inner != nullptr) {
        field.volume.inner = std::clamp(p.inner->finalComponent(0), 0.0f, 0.999f);
    }
    if (p.floorGain != nullptr) {
        field.floorGain = p.floorGain->finalComponent(0);
    }
    if (p.center != nullptr) {
        for (std::size_t c = 0; c < 3; ++c) {
            field.volume.center[static_cast<int>(c)] = p.center->finalComponent(c);
        }
    }
    if (p.scale != nullptr) {
        // One knob for all three shapes, because "make the field bigger" is one intention and a
        // sphere's radius, a box's half-extents and a capsule's length are three spellings of it.
        const float scale = std::max(p.scale->finalComponent(0), 0.0f);
        field.volume.radius *= scale;
        field.volume.height *= scale;
        field.volume.halfExtents *= scale;
    }
    return field;
}

bool EntityWorld::fieldMatches(const FieldDesc& field, const Entity& entity) const {
    if (field.tags.empty()) {
        return true; // a field with no filter governs everything in the scene
    }
    for (const std::string& tag : field.tags) {
        if (std::find(entity.desc().tags.begin(), entity.desc().tags.end(), tag) !=
            entity.desc().tags.end()) {
            return true;
        }
        // The profile an entity was built from is a tag it did not have to be given. "Every NPC
        // built from the dancer profile" is then one word in a scene file rather than a list of
        // forty names that goes stale the day somebody adds the forty-first.
        if (!entity.desc().profile.empty() && entity.desc().profile == tag) {
            return true;
        }
    }
    return false;
}

bool EntityWorld::resolveFieldSource(const FieldDesc& field, glm::vec3& out,
                                     FieldAuthority& authority) const {
    if (field.source.empty()) {
        out = field.volume.center;
        authority = FieldAuthority::Static;
        return true;
    }
    // `volume.center` is an offset once a source is named: "twelve metres above the dancer".
    if (const Entity* e = find(field.source)) {
        out = e->fieldPosition() + field.volume.center;
        // ADR-091's rule, applied literally: an entity that integrates is the live tier.
        authority = e->behaviors().empty() ? FieldAuthority::Baked : FieldAuthority::Live;
        return true;
    }
    if (const NodeBinding* b = binding(field.source); b != nullptr && b->exists) {
        glm::vec3 p = b->anchor;
        if (params_ != nullptr) {
            if (const params::IParameter* pos = params_->find(b->transformPrefix + "position");
                pos != nullptr && pos->componentCount() >= 3) {
                for (std::size_t c = 0; c < 3; ++c) {
                    p[static_cast<int>(c)] += pos->finalComponent(c) - pos->baseComponent(c);
                }
            }
        }
        out = p + field.volume.center;
        authority = FieldAuthority::Baked;
        for (const auto& e : entities_) {
            if (e->desc().driven() == field.source && !e->behaviors().empty()) {
                authority = FieldAuthority::Live;
                break;
            }
        }
        return true;
    }
    glm::vec3 landmark(0.0f);
    if (pointOfInterest(field.source, landmark)) {
        out = landmark + field.volume.center;
        authority = FieldAuthority::Static; // a hero does not move
        return true;
    }
    out = field.volume.center;
    authority = FieldAuthority::Static;
    return false;
}

void EntityWorld::bindFields() {
    fieldRuntime_.assign(fields_.size(), FieldRuntime{});
    fieldReport_.clear();
    const std::size_t n = fields_.size();
    for (auto& entityPtr : entities_) {
        Entity& e = *entityPtr;
        e.insideField_.assign(n, 0);
        e.insideNow_.assign(n, 0);
        e.entryCount_.assign(n, 0);
        e.governedBy_.assign(n, 0);
        e.governed_ = false;
        e.fieldFloor_ = 0.0f;
        e.influence_ = 1.0f;
        e.arcPhase_ = Entity::ArcPhase::None;
        e.arcField_ = Entity::kNoField;
        e.arcLevel_ = 0.0f;
        e.arcTimer_ = 0.0;
        e.arcCooldownUntil_ = -1.0e30;
        e.fieldAccum_ = 0.0;
    }
    for (std::size_t f = 0; f < n; ++f) {
        const FieldDesc field = resolvedField(f);
        FieldRuntime& rt = fieldRuntime_[f];
        rt.sourceResolved = resolveFieldSource(field, rt.center, rt.authority);
        std::size_t governed = 0;
        for (auto& entityPtr : entities_) {
            Entity& e = *entityPtr;
            if (!field.enabled || !fieldMatches(field, e)) {
                continue;
            }
            e.governedBy_[f] = 1;
            ++governed;
            if (field.scaleReactions) {
                // Only a field that *scales* makes an entity's gain start from zero. A pure
                // trigger volume fires arcs and publishes signals without touching modulation, and
                // an entity governed by nothing else keeps the gain of exactly 1 it had before
                // fields existed -- which is what makes this change invisible to every scene that
                // does not use it.
                e.governed_ = true;
                e.fieldFloor_ = std::max(e.fieldFloor_, field.floorGain);
            }
        }
        rt.governed = governed;
        if (!rt.sourceResolved) {
            const std::string message =
                fmt::format("field '{}': source '{}' names no entity, node or hero in this scene; "
                            "the field stays at its authored centre",
                            field.name, field.source);
            if (std::find(problems_.begin(), problems_.end(), message) == problems_.end()) {
                problems_.push_back(message);
                log::warn("{}", message);
            }
        }
        // ADR-091 made visible. One line per field saying what it followed and which guarantee it
        // therefore has, logged at install and readable in the editor -- because the difference
        // between a scrub-exact field and one that is not is a thing an author has to be able to
        // read, not one they discover by rendering the same frame twice and diffing it.
        std::string line = fmt::format(
            "field '{}' ({} reach {:.1f}, falloff {}, strength {:.2f}) {} -> {}: {} entit{} governed",
            field.name, volumeShapeName(field.volume.shape), field.volume.reach(),
            falloffName(field.volume.falloff), field.strength,
            field.source.empty() ? std::string("fixed at its authored centre")
                                 : fmt::format("follows '{}'", field.source),
            fieldAuthorityName(rt.authority), governed, governed == 1 ? "y" : "ies");
        if (rt.authority == FieldAuthority::Live) {
            line += "; live source: this field's influence is NOT scrub-exact (ADR-091)";
            if (field.requireScrubExact) {
                const std::string message = fmt::format(
                    "field '{}' declares requireScrubExact but follows '{}', which is a live "
                    "entity: its influence depends on how the playhead got here (ADR-091)",
                    field.name, field.source);
                if (std::find(problems_.begin(), problems_.end(), message) == problems_.end()) {
                    problems_.push_back(message);
                    log::warn("{}", message);
                }
            }
        }
        fieldReport_.push_back(std::move(line));
    }
    // A governed entity starts at its floor rather than at 1. Otherwise an entity far enough away
    // to be on the coarse interval would read as fully inside every field until its first query,
    // which is a crowd that lights up before the camera gets to it.
    for (auto& entityPtr : entities_) {
        entityPtr->influence_ = entityPtr->governed_ ? entityPtr->fieldFloor_ : 1.0f;
    }
    fieldsBound_ = true;
}

void EntityWorld::beginArc(Entity& entity, std::uint32_t fieldIndex, double time) {
    const ReactionArc& arc = fields_[fieldIndex].arc;
    // A stream of this entity's own, for this field, for this entry. Never the behaviour stream:
    // that one advances once per frame per behaviour, so a draw taken from it would depend on the
    // frame rate and on what else the entity happens to be doing.
    Rng rng = arcStream(entity.seed(), fields_[fieldIndex].name, entity.entryCount_[fieldIndex]);
    const auto jitter = [&rng](float amount) { return amount * (rng.nextFloat() * 2.0f - 1.0f); };
    entity.arcDelay_ = std::max(0.0f, arc.delaySeconds + jitter(arc.delayJitter));
    entity.arcHold_ = std::max(0.0f, arc.holdSeconds + jitter(arc.holdJitter));
    entity.arcIntensity_ =
        std::clamp(arc.intensity * (1.0f + jitter(arc.intensityJitter)), 0.0f, 8.0f);
    entity.arcRelease_ = std::max(0.0f, arc.releaseSeconds);
    entity.arcHoldStill_ = arc.holdStill;
    entity.arcActivity_ = Activity::React;
    if (!activityFromName(arc.activity, entity.arcActivity_)) {
        const std::string message =
            fmt::format("field '{}': arc activity '{}' is not one of idle, walk, run, turn, "
                        "observe, react; using react",
                        fields_[fieldIndex].name, arc.activity);
        if (std::find(problems_.begin(), problems_.end(), message) == problems_.end()) {
            problems_.push_back(message);
            log::warn("{}", message);
        }
    }
    entity.arcPhase_ = entity.arcDelay_ > 0.0f ? Entity::ArcPhase::Delay : Entity::ArcPhase::Hold;
    entity.arcTimer_ = 0.0;
    entity.arcLevel_ = 0.0f;
    entity.arcField_ = fieldIndex;
    // The whole arc's length is known now, so the refractory gap is too. Set here rather than when
    // the arc ends, so a re-entry during the arc cannot start a second one either.
    entity.arcCooldownUntil_ = time + static_cast<double>(entity.arcDelay_ + entity.arcHold_ +
                                                          entity.arcRelease_ + arc.refractorySeconds);
    // Settle the level on the frame the edge was found rather than on the one after it. The arc
    // pass runs before the behaviour pass, so an arc that began here has to be visible to the
    // behaviours *this* frame -- otherwise crossing a line produces one frame of the old activity
    // and the crowd is a frame late, which is the same defect as evaluating the field after the
    // routes, one layer down.
    advanceArc(entity, 0.0);
}

void EntityWorld::advanceArc(Entity& entity, double dt) {
    if (entity.arcPhase_ == Entity::ArcPhase::None) {
        entity.arcLevel_ = 0.0f;
        return;
    }
    double remaining = std::max(dt, 0.0);
    while (entity.arcPhase_ != Entity::ArcPhase::None) {
        double length = 0.0;
        switch (entity.arcPhase_) {
        case Entity::ArcPhase::Delay: length = static_cast<double>(entity.arcDelay_); break;
        case Entity::ArcPhase::Hold: length = static_cast<double>(entity.arcHold_); break;
        case Entity::ArcPhase::Release: length = static_cast<double>(entity.arcRelease_); break;
        case Entity::ArcPhase::None: break;
        }
        const double budget = length - entity.arcTimer_;
        if (remaining < budget) {
            entity.arcTimer_ += remaining;
            break;
        }
        remaining -= std::max(budget, 0.0);
        entity.arcTimer_ = 0.0;
        switch (entity.arcPhase_) {
        case Entity::ArcPhase::Delay: entity.arcPhase_ = Entity::ArcPhase::Hold; break;
        case Entity::ArcPhase::Hold: entity.arcPhase_ = Entity::ArcPhase::Release; break;
        case Entity::ArcPhase::Release:
        case Entity::ArcPhase::None:
            entity.arcPhase_ = Entity::ArcPhase::None;
            entity.arcField_ = Entity::kNoField;
            break;
        }
        // A zero-length phase is stepped through in the same tick rather than costing a frame.
        if (remaining <= 0.0 && entity.arcPhase_ != Entity::ArcPhase::None) {
            double next = 0.0;
            switch (entity.arcPhase_) {
            case Entity::ArcPhase::Delay: next = static_cast<double>(entity.arcDelay_); break;
            case Entity::ArcPhase::Hold: next = static_cast<double>(entity.arcHold_); break;
            case Entity::ArcPhase::Release: next = static_cast<double>(entity.arcRelease_); break;
            case Entity::ArcPhase::None: break;
            }
            if (next > 0.0) {
                break;
            }
        }
    }
    switch (entity.arcPhase_) {
    case Entity::ArcPhase::None:
    case Entity::ArcPhase::Delay:
        entity.arcLevel_ = 0.0f;
        break;
    case Entity::ArcPhase::Hold:
        entity.arcLevel_ = entity.arcIntensity_;
        break;
    case Entity::ArcPhase::Release: {
        const float t = entity.arcRelease_ > 0.0f
                            ? static_cast<float>(entity.arcTimer_) / entity.arcRelease_
                            : 1.0f;
        entity.arcLevel_ = entity.arcIntensity_ * std::clamp(1.0f - t, 0.0f, 1.0f);
        break;
    }
    }
}

void EntityWorld::updateFields(const FieldUpdate& ctx, params::ParameterSet& params) {
    if (!parametersLive_) {
        // The same guard update() has, for the same reason: between unregisterParameters() and the
        // next registerParameters() the entities' cached parameter pointers are stale, and a stale
        // pointer that is merely usually fine is the bug that surfaces on a scene swap.
        return;
    }
    params_ = &params;
    triggerEvents_.clear();
    fieldCounts_ = FieldCounts{};
    if (!fieldsBound_) {
        bindFields();
    }
    if (fields_.empty() || entities_.empty()) {
        return; // no fields: every gain stays exactly 1 and this pass costs one branch
    }

    // 1. Where each field is this frame, and its signals. `declare` is idempotent and takes a name
    //    at runtime, so a field publishes itself on the ordinary bus without the bus, the analysis
    //    or the modulator learning that fields exist (§39-§43).
    float maxReach = 0.0f;
    resolved_.clear();
    resolved_.reserve(fields_.size());
    for (std::size_t f = 0; f < fields_.size(); ++f) {
        resolved_.push_back(resolvedField(f));
        const FieldDesc& field = resolved_.back();
        FieldRuntime& rt = fieldRuntime_[f];
        rt.inside = 0;
        rt.maxInfluence = 0.0f;
        FieldAuthority authority = rt.authority;
        rt.sourceResolved = resolveFieldSource(field, rt.center, authority);
        rt.authority = authority;
        if (ctx.bus != nullptr && rt.occupancy == signals::kInvalidSignal) {
            rt.occupancy = ctx.bus->declare("field." + field.name + ".occupancy", 0.0f, 1.0f);
            rt.enterSignal = ctx.bus->declare("field." + field.name + ".enter", 0.0f, 1.0f, true);
            rt.exitSignal = ctx.bus->declare("field." + field.name + ".exit", 0.0f, 1.0f, true);
        }
        if (field.enabled) {
            maxReach = std::max(maxReach, field.volume.reach());
        }
    }

    // 2. Advance every arc, whatever the level of detail. An arc that stopped being ticked because
    //    its character walked out of full detail would never end, and the character would come
    //    back into view still dancing to a field it left a minute ago.
    for (auto& entityPtr : entities_) {
        advanceArc(*entityPtr, ctx.dt);
    }

    // 3. The broad phase. Only entities some field's filter actually matches go into the grid, and
    //    the same three-band level of detail the behaviour pass uses applies here: past
    //    `cullDistance` an entity is not queried at all and keeps the influence it had, and past
    //    `fullDetailDistance` it is queried on the coarse interval instead of every frame.
    gridPoints_.clear();
    gridEntity_.clear();
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        Entity& e = *entities_[i];
        const bool anyFilter = std::any_of(e.governedBy_.begin(), e.governedBy_.end(),
                                           [](std::uint8_t g) { return g != 0; });
        if (!anyFilter) {
            continue;
        }
        ++fieldCounts_.governed;
        const glm::vec3 here = e.fieldPosition();
        const float distance = glm::length(here - ctx.viewPosition);
        if (ctx.distanceDetail && e.desc().cullDistance > 0.0f && distance > e.desc().cullDistance) {
            continue;
        }
        if (ctx.distanceDetail && e.desc().fullDetailDistance > 0.0f &&
            distance > e.desc().fullDetailDistance) {
            e.fieldAccum_ += ctx.dt;
            if (e.fieldAccum_ < static_cast<double>(e.desc().coarseInterval)) {
                continue;
            }
            e.fieldAccum_ = 0.0;
        } else {
            e.fieldAccum_ = 0.0;
        }
        std::fill(e.insideNow_.begin(), e.insideNow_.end(), 0);
        e.influence_ = e.governed_ ? e.fieldFloor_ : 1.0f;
        gridPoints_.push_back(here);
        gridEntity_.push_back(static_cast<std::uint32_t>(i));
    }
    fieldCounts_.queried = gridPoints_.size();
    if (gridPoints_.empty()) {
        return;
    }
    // One cell per field reach: a field's bounds then touch at most three cells on an axis, so a
    // query visits a bounded number of cells however large the crowd or the world is.
    grid_.build(gridPoints_, std::max(maxReach, 1.0f));
    fieldCounts_.cells = grid_.cellCount();

    // 4. Field-major exact test over the neighbourhood the grid returned. This is the loop the
    //    brief forbids doing as entities x volumes: the grid is what makes it entities + the few
    //    that are actually near something.
    for (std::size_t f = 0; f < fields_.size(); ++f) {
        const FieldDesc& field = resolved_[f];
        if (!field.enabled) {
            continue;
        }
        FieldRuntime& rt = fieldRuntime_[f];
        TriggerVolume volume = field.volume;
        volume.center = rt.center;
        glm::vec3 lo(0.0f);
        glm::vec3 hi(0.0f);
        volume.bounds(lo, hi);
        grid_.forEachNear(lo, hi, [&](std::uint32_t point) {
            const std::uint32_t index = gridEntity_[point];
            Entity& e = *entities_[index];
            if (e.governedBy_[f] == 0) {
                return;
            }
            ++fieldCounts_.tested;
            const float raw = volume.influenceAt(gridPoints_[point]);
            if (raw <= 0.0f) {
                return;
            }
            e.insideNow_[f] = 1;
            ++rt.inside;
            ++fieldCounts_.inside;
            rt.maxInfluence = std::max(rt.maxInfluence, raw);
            if (field.scaleReactions) {
                // §19. Max rather than sum: influence is a 0..1 statement about how far inside
                // something is, and two overlapping speakers do not make a listener more than
                // fully inside. A field that wants to be louder says so with `strength`.
                e.influence_ = std::max(e.influence_, raw * field.strength);
            }
        });
    }

    // 5. Edges, and the arcs they start.
    for (std::size_t p = 0; p < gridEntity_.size(); ++p) {
        const std::uint32_t index = gridEntity_[p];
        Entity& e = *entities_[index];
        for (std::size_t f = 0; f < fields_.size(); ++f) {
            if (e.governedBy_[f] == 0) {
                continue;
            }
            const bool now = e.insideNow_[f] != 0;
            const bool was = e.insideField_[f] != 0;
            if (now == was) {
                continue;
            }
            e.insideField_[f] = now ? 1 : 0;
            triggerEvents_.push_back(TriggerEvent{static_cast<std::uint32_t>(f), index, now,
                                                  ctx.time, now ? e.influence_ : 0.0f});
            if (!now) {
                continue;
            }
            const std::uint32_t entries = e.entryCount_[f];
            e.entryCount_[f] = entries + 1u;
            if (fields_[f].arc.enabled && e.arcPhase_ == Entity::ArcPhase::None &&
                ctx.time >= e.arcCooldownUntil_) {
                beginArc(e, static_cast<std::uint32_t>(f), ctx.time);
            }
        }
    }

    // 6. Publish. A field is a signal source like any other from here on: a light, a material or a
    //    particle system reacts to `field.<name>.occupancy` through an ordinary reaction, and none
    //    of them has to learn what a volume is.
    if (ctx.bus != nullptr) {
        enteredThisFrame_.assign(fields_.size(), 0);
        exitedThisFrame_.assign(fields_.size(), 0);
        for (const TriggerEvent& event : triggerEvents_) {
            (event.enter ? enteredThisFrame_ : exitedThisFrame_)[event.field] = 1;
        }
        for (std::size_t f = 0; f < fields_.size(); ++f) {
            const FieldRuntime& rt = fieldRuntime_[f];
            if (rt.occupancy != signals::kInvalidSignal) {
                const float denominator = static_cast<float>(std::max<std::size_t>(rt.governed, 1));
                ctx.bus->set(rt.occupancy, static_cast<float>(rt.inside) / denominator);
            }
            if (rt.enterSignal != signals::kInvalidSignal) {
                ctx.bus->setEvent(rt.enterSignal, enteredThisFrame_[f] != 0);
            }
            if (rt.exitSignal != signals::kInvalidSignal) {
                ctx.bus->setEvent(rt.exitSignal, exitedThisFrame_[f] != 0);
            }
        }
    }
}

void EntityWorld::applySpatialGain(std::vector<params::ModRoute>& routes) const {
    for (params::ModRoute& route : routes) {
        if (!route.fromEntity || route.ownerEntity >= entities_.size()) {
            continue;
        }
        route.spatialGain = entities_[route.ownerEntity]->influence_;
    }
}

// ---- serialisation ---------------------------------------------------------------------------

Result<EntityDesc> entityFromJson(const nlohmann::json& j, const std::filesystem::path& baseDir,
                                  const ProfileLibrary* library) {
    if (!j.is_object()) {
        return fail("entity must be an object");
    }
    if (!j.contains("name") || !j["name"].is_string() || j["name"].get<std::string>().empty()) {
        return fail("entity: 'name' is required and must be a non-empty string");
    }
    EntityDesc desc;
    desc.name = j["name"].get<std::string>();
    // A profile first, so the entity's own blocks append to it rather than replacing it: a scene
    // takes "a hovering craft that answers the music" and then adds the one reaction that is about
    // this craft in this shot.
    if (j.contains("profile") && j["profile"].is_string()) {
        desc.profile = j["profile"].get<std::string>();
        EntityDesc loaded;
        // The library first (ADR-097): a scene that named one has already paid to read it, and ten
        // NPCs sharing "dancer" should not be ten file opens of the same twenty lines. A name the
        // library does not have is still a path, so every scene written before libraries existed
        // resolves exactly as it did.
        if (const EntityDesc* fromLibrary = library != nullptr ? library->find(desc.profile) : nullptr) {
            loaded = *fromLibrary;
        } else {
            const std::filesystem::path path =
                baseDir.empty() ? std::filesystem::path(desc.profile) : baseDir / desc.profile;
            auto read = loadProfile(path);
            if (!read) {
                if (library != nullptr && !library->empty()) {
                    std::string known;
                    for (const std::string& name : library->names()) {
                        if (!known.empty()) {
                            known += ", ";
                        }
                        known += name;
                    }
                    return fail("entity '{}': profile '{}' is not in the library '{}' (which has: "
                                "{}) and is not a file either: {}",
                                desc.name, desc.profile, library->source(), known,
                                read.error().message);
                }
                return fail("entity '{}': {}", desc.name, read.error().message);
            }
            loaded = std::move(*read);
        }
        desc.behaviors = std::move(loaded.behaviors);
        desc.reactions = std::move(loaded.reactions);
        desc.clips = std::move(loaded.clips);
        desc.sockets = std::move(loaded.sockets);
        desc.tags = std::move(loaded.tags);
        desc.profileBehaviors = desc.behaviors.size();
        desc.profileReactions = desc.reactions.size();
        desc.profileClips = desc.clips.size();
        desc.profileSockets = desc.sockets.size();
        desc.profileTags = desc.tags.size();
    }
    if (j.contains("node") && j["node"].is_string()) {
        desc.node = j["node"].get<std::string>();
    }
    if (j.contains("seed") && j["seed"].is_number_unsigned()) {
        desc.seed = j["seed"].get<std::uint32_t>();
    }
    if (j.contains("fullDetailDistance") && j["fullDetailDistance"].is_number()) {
        desc.fullDetailDistance = j["fullDetailDistance"].get<float>();
    }
    if (j.contains("coarseInterval") && j["coarseInterval"].is_number()) {
        desc.coarseInterval = std::max(0.0f, j["coarseInterval"].get<float>());
    }
    if (j.contains("cullDistance") && j["cullDistance"].is_number()) {
        desc.cullDistance = j["cullDistance"].get<float>();
    }
    // Phase B's opt-in. Absent means false, which is every scene that exists today: the change
    // this key turns on touches the path every shipping scene renders through, and "default off
    // is inert" is measured by rendering the Glowmere project with and without this branch and
    // comparing sequence hashes, not asserted.
    if (j.contains("proceduralMotion") && j["proceduralMotion"].is_boolean()) {
        desc.proceduralMotion = j["proceduralMotion"].get<bool>();
    }
    // ADR-623. A malformed block is a load error, not a silent no-op: an author who wrote it meant
    // the matcher to run, and a character quietly left on its clips is the failure this project
    // keeps shipping.
    if (j.contains("motionMatching")) {
        const auto& m = j["motionMatching"];
        if (!m.is_object()) {
            return fail("entity '{}': 'motionMatching' must be an object", desc.name);
        }
        const auto names = [&](const char* key, std::vector<std::string>& out) -> Result<void> {
            if (!m.contains(key)) {
                return {};
            }
            if (!m[key].is_array()) {
                return fail("entity '{}': 'motionMatching.{}' must be an array of joint names",
                            desc.name, key);
            }
            for (const auto& n : m[key]) {
                if (!n.is_string()) {
                    return fail("entity '{}': 'motionMatching.{}' must be an array of joint names",
                                desc.name, key);
                }
                out.push_back(n.get<std::string>());
            }
            return {};
        };
        if (auto ok = names("joints", desc.motionMatching.joints); !ok) {
            return std::unexpected(ok.error());
        }
        if (desc.motionMatching.joints.empty()) {
            return fail("entity '{}': 'motionMatching.joints' is required and names the feature "
                        "joints (the feet and the head, for a biped)",
                        desc.name);
        }
        if (auto ok = names("contacts", desc.motionMatching.contacts); !ok) {
            return std::unexpected(ok.error());
        }
        if (m.contains("trajectory")) {
            if (!m["trajectory"].is_array()) {
                return fail("entity '{}': 'motionMatching.trajectory' must be an array of seconds",
                            desc.name);
            }
            desc.motionMatching.trajectory.clear();
            for (const auto& s : m["trajectory"]) {
                if (!s.is_number() || s.get<float>() <= 0.0f) {
                    return fail("entity '{}': 'motionMatching.trajectory' must be positive seconds",
                                desc.name);
                }
                desc.motionMatching.trajectory.push_back(s.get<float>());
            }
        }
        if (m.contains("pack")) {
            if (!m["pack"].is_string() || m["pack"].get<std::string>().empty()) {
                return fail("entity '{}': 'motionMatching.pack' must be a path", desc.name);
            }
            desc.motionMatching.pack = m["pack"].get<std::string>();
            const std::filesystem::path authored(desc.motionMatching.pack);
            desc.motionMatching.packResolved =
                (authored.is_absolute() ? authored : (baseDir / authored)).lexically_normal().string();
        }
        desc.motionMatching.enabled = true;
        desc.proceduralMotion = true;
    }
    if (j.contains("tags")) {
        if (!j["tags"].is_array()) {
            return fail("entity '{}': 'tags' must be an array of strings", desc.name);
        }
        for (const auto& tag : j["tags"]) {
            if (!tag.is_string()) {
                return fail("entity '{}': 'tags' must be an array of strings", desc.name);
            }
            const std::string value = tag.get<std::string>();
            if (std::find(desc.tags.begin(), desc.tags.end(), value) == desc.tags.end()) {
                desc.tags.push_back(value);
            }
        }
    }
    if (j.contains("behaviors") && j["behaviors"].is_array()) {
        for (const auto& item : j["behaviors"]) {
            if (!item.is_object() || !item.contains("kind") || !item["kind"].is_string()) {
                return fail("entity '{}': every behaviour needs a string 'kind'", desc.name);
            }
            BehaviorDesc behavior;
            behavior.kind = item["kind"].get<std::string>();
            behavior.name = item.contains("name") && item["name"].is_string()
                                ? item["name"].get<std::string>()
                                : behavior.kind;
            behavior.settings = item;
            desc.behaviors.push_back(std::move(behavior));
        }
    }
    if (j.contains("reactions") && j["reactions"].is_array()) {
        for (const auto& item : j["reactions"]) {
            if (!item.is_object()) {
                return fail("entity '{}': every reaction must be an object", desc.name);
            }
            ReactionDesc reaction;
            if (!item.contains("signal") || !item["signal"].is_string()) {
                return fail("entity '{}': a reaction needs a string 'signal'", desc.name);
            }
            if (!item.contains("target") || !item["target"].is_string()) {
                return fail("entity '{}': a reaction needs a string 'target'", desc.name);
            }
            reaction.signal = item["signal"].get<std::string>();
            reaction.target = item["target"].get<std::string>();
            if (item.contains("component") && item["component"].is_number_integer()) {
                reaction.component = item["component"].get<int>();
            }
            if (item.contains("depth") && item["depth"].is_number()) {
                reaction.depth = item["depth"].get<float>();
            }
            if (item.contains("enabled") && item["enabled"].is_boolean()) {
                reaction.enabled = item["enabled"].get<bool>();
            }
            if (item.contains("spatial") && item["spatial"].is_boolean()) {
                reaction.spatial = item["spatial"].get<bool>();
            }
            if (item.contains("op") && item["op"].is_string()) {
                const auto op = params::modOpFromName(item["op"].get<std::string>());
                if (!op) {
                    return fail("entity '{}': unknown reaction op '{}'", desc.name,
                                item["op"].get<std::string>());
                }
                reaction.op = *op;
            }
            if (item.contains("polarity") && item["polarity"].is_string()) {
                reaction.polarity = item["polarity"].get<std::string>() == "bipolar"
                                        ? params::Polarity::Bipolar
                                        : params::Polarity::Unipolar;
            }
            if (item.contains("chain")) {
                auto chain = params::chainFromJson(item["chain"]);
                if (!chain) {
                    return fail("entity '{}': reaction chain: {}", desc.name, chain.error().message);
                }
                reaction.chain = *chain;
            }
            desc.reactions.push_back(std::move(reaction));
        }
    }
    if (j.contains("clips") && j["clips"].is_object()) {
        for (const auto& [activity, state] : j["clips"].items()) {
            if (!state.is_string()) {
                return fail("entity '{}': clip for '{}' must be a string", desc.name, activity);
            }
            desc.clips.emplace_back(activity, state.get<std::string>());
        }
    }
    if (j.contains("sockets") && j["sockets"].is_array()) {
        for (const auto& item : j["sockets"]) {
            if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) {
                return fail("entity '{}': every socket needs a string 'name'", desc.name);
            }
            SocketDesc socket;
            socket.name = item["name"].get<std::string>();
            if (item.contains("joint") && item["joint"].is_string()) {
                socket.joint = item["joint"].get<std::string>();
            }
            socket.offset.position = readVec3(item, "position", glm::vec3(0.0f));
            socket.offset.rotation = quatFromEulerDegrees(readVec3(item, "rotation", glm::vec3(0.0f)));
            socket.offset.scale = readVec3(item, "scale", glm::vec3(1.0f));
            desc.sockets.push_back(std::move(socket));
        }
    }
    if (j.contains("attachments") && j["attachments"].is_array()) {
        for (const auto& item : j["attachments"]) {
            if (!item.is_object() || !item.contains("node") || !item["node"].is_string() ||
                !item.contains("socket") || !item["socket"].is_string()) {
                return fail("entity '{}': every attachment needs a 'node' and a 'socket'", desc.name);
            }
            desc.attachments.push_back(
                AttachmentDesc{item["node"].get<std::string>(), item["socket"].get<std::string>()});
        }
    }
    // ---- intent (ADR-096) ----
    if (j.contains("properties")) {
        const nlohmann::json& properties = j["properties"];
        if (properties.is_object()) {
            // The readable spelling: { "headphones": 0, "awake": 1 }. A 0..1 flag is what almost
            // every state property is, so it is the shape that needs no ceremony.
            for (const auto& [name, value] : properties.items()) {
                if (!value.is_number()) {
                    return fail("entity '{}': property '{}' must be a number", desc.name, name);
                }
                desc.properties.push_back(PropertyDesc{name, value.get<float>(), 0.0f, 1.0f});
            }
        } else if (properties.is_array()) {
            for (const auto& item : properties) {
                if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) {
                    return fail("entity '{}': every property needs a string 'name'", desc.name);
                }
                PropertyDesc property;
                property.name = item["name"].get<std::string>();
                property.value = item.contains("value") && item["value"].is_number()
                                     ? item["value"].get<float>() : 0.0f;
                property.min = item.contains("min") && item["min"].is_number()
                                   ? item["min"].get<float>() : 0.0f;
                property.max = item.contains("max") && item["max"].is_number()
                                   ? item["max"].get<float>() : 1.0f;
                desc.properties.push_back(std::move(property));
            }
        } else {
            return fail("entity '{}': 'properties' must be an object or an array", desc.name);
        }
    }
    if (j.contains("interactions")) {
        if (!j["interactions"].is_array()) {
            return fail("entity '{}': 'interactions' must be an array", desc.name);
        }
        for (const auto& item : j["interactions"]) {
            auto interaction = interactionFromJson(item);
            if (!interaction) {
                return fail("entity '{}': {}", desc.name, interaction.error().message);
            }
            desc.interactions.push_back(std::move(*interaction));
        }
    }
    if (j.contains("actions")) {
        auto actions = actionsFromJson(j["actions"]);
        if (!actions) {
            return fail("entity '{}': {}", desc.name, actions.error().message);
        }
        desc.actions = std::move(*actions);
    }
    if (j.contains("schedule")) {
        auto schedule = scheduleFromJson(j["schedule"]);
        if (!schedule) {
            return fail("entity '{}': {}", desc.name, schedule.error().message);
        }
        desc.schedule = std::move(*schedule);
    }
    if (j.contains("gait")) {
        auto gait = gaitFromJson(j["gait"]);
        if (!gait) {
            return fail("entity '{}': {}", desc.name, gait.error().message);
        }
        desc.gait = *gait;
    }
    // The senses (ADR-270, ADR-290). The presence of the key is the whole of the opt-in: an entity
    // without one perceives nothing and pays nothing, which is the right answer for every craft,
    // every rock and the nine farm animals of Glowmere, and it is why this is a flag rather than a
    // settings block that is always there with a range of zero.
    if (j.contains("perception")) {
        auto perception = perceptionFromJson(j["perception"]);
        if (!perception) {
            return fail("entity '{}': {}", desc.name, perception.error().message);
        }
        desc.perception = *perception;
        desc.perceives = true;
    }
    return desc;
}

Result<EntityDesc> profileFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("an entity profile must be an object");
    }
    if (j.contains("format") && j["format"].is_string() &&
        j["format"].get<std::string>() != "avgen-entity-profile") {
        return fail("not an entity profile: format '{}'", j["format"].get<std::string>());
    }
    nlohmann::json copy = j;
    copy["name"] = "profile";
    copy.erase("profile"); // profiles do not chain: one level of indirection is a library, two is a maze
    return entityFromJson(copy);
}

Result<EntityDesc> loadProfile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return fail("cannot open entity profile '{}'", path.string());
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        return fail("entity profile '{}': {}", path.string(), e.what());
    }
    auto desc = profileFromJson(j);
    if (!desc) {
        return fail("entity profile '{}': {}", path.string(), desc.error().message);
    }
    return desc;
}

const EntityDesc* ProfileLibrary::find(std::string_view name) const {
    const auto it = std::find_if(profiles_.begin(), profiles_.end(),
                                 [&](const std::pair<std::string, EntityDesc>& p) { return p.first == name; });
    return it == profiles_.end() ? nullptr : &it->second;
}

void ProfileLibrary::add(std::string name, EntityDesc profile) {
    for (auto& existing : profiles_) {
        if (existing.first == name) {
            existing.second = std::move(profile);
            return;
        }
    }
    profiles_.emplace_back(std::move(name), std::move(profile));
}

std::vector<std::string> ProfileLibrary::names() const {
    std::vector<std::string> out;
    out.reserve(profiles_.size());
    for (const auto& p : profiles_) {
        out.push_back(p.first);
    }
    return out;
}

Result<ProfileLibrary> profileLibraryFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("an entity profile library must be an object");
    }
    if (j.contains("format") && j["format"].is_string() &&
        j["format"].get<std::string>() != "avgen-entity-profile-library") {
        return fail("not an entity profile library: format '{}'", j["format"].get<std::string>());
    }
    if (!j.contains("profiles") || !j["profiles"].is_object()) {
        return fail("an entity profile library needs a 'profiles' object of name -> profile");
    }
    ProfileLibrary library;
    for (const auto& [name, body] : j["profiles"].items()) {
        if (name.empty()) {
            return fail("entity profile library: a profile with an empty name");
        }
        auto profile = profileFromJson(body);
        if (!profile) {
            return fail("entity profile library: profile '{}': {}", name, profile.error().message);
        }
        library.add(name, std::move(*profile));
    }
    return library;
}

Result<ProfileLibrary> loadProfileLibrary(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return fail("cannot open entity profile library '{}'", path.string());
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        return fail("entity profile library '{}': {}", path.string(), e.what());
    }
    auto library = profileLibraryFromJson(j);
    if (!library) {
        return fail("entity profile library '{}': {}", path.string(), library.error().message);
    }
    library->setSource(path.string());
    return library;
}

Result<std::vector<EntityDesc>> entitiesFromJson(const nlohmann::json& j,
                                                 const std::filesystem::path& baseDir,
                                                 const ProfileLibrary* library) {
    if (!j.is_array()) {
        return fail("'entities' must be an array");
    }
    std::vector<EntityDesc> out;
    out.reserve(j.size());
    for (const auto& item : j) {
        auto entity = entityFromJson(item, baseDir, library);
        if (!entity) {
            return fail("{}", entity.error().message);
        }
        const std::string& name = entity->name;
        if (std::any_of(out.begin(), out.end(), [&](const EntityDesc& d) { return d.name == name; })) {
            return fail("entity '{}' is declared twice", name);
        }
        out.push_back(std::move(*entity));
    }
    return out;
}

nlohmann::json entityToJson(const EntityDesc& entity) {
    nlohmann::json j = nlohmann::json::object();
    j["name"] = entity.name;
    if (!entity.profile.empty()) {
        j["profile"] = entity.profile;
    }
    if (!entity.node.empty()) {
        j["node"] = entity.node;
    }
    if (entity.seed != 0) {
        j["seed"] = entity.seed;
    }
    if (entity.fullDetailDistance > 0.0f) {
        j["fullDetailDistance"] = entity.fullDetailDistance;
        j["coarseInterval"] = entity.coarseInterval;
    }
    if (entity.cullDistance > 0.0f) {
        j["cullDistance"] = entity.cullDistance;
    }
    // Written only when true, so saving a scene that never opted in produces the same bytes it
    // had. A writer that emitted `false` everywhere would rewrite every entity in every project
    // the first time anyone saved -- the diff-noise failure ADR-225 already had to fix once.
    if (entity.proceduralMotion) {
        j["proceduralMotion"] = true;
    }
    // ADR-623. Written only when on, for the reason above; and every field, including the ones at
    // their defaults, because a reader that fills a default the writer dropped is how a save
    // changes what a scene means (ADR-618).
    if (entity.motionMatching.enabled) {
        nlohmann::json m;
        m["joints"] = entity.motionMatching.joints;
        if (!entity.motionMatching.contacts.empty()) {
            m["contacts"] = entity.motionMatching.contacts;
        }
        m["trajectory"] = entity.motionMatching.trajectory;
        if (!entity.motionMatching.pack.empty()) {
            m["pack"] = entity.motionMatching.pack; // as authored, so a save moves with its scene
        }
        j["motionMatching"] = std::move(m);
    }
    if (entity.behaviors.size() > entity.profileBehaviors) {
        nlohmann::json behaviors = nlohmann::json::array();
        for (const BehaviorDesc& behavior :
             std::span(entity.behaviors).subspan(entity.profileBehaviors)) {
            // The settings object is the authored form and already carries `kind` and `name`.
            behaviors.push_back(behavior.settings.is_object() ? behavior.settings
                                                              : nlohmann::json{{"kind", behavior.kind}});
        }
        j["behaviors"] = std::move(behaviors);
    }
    if (entity.reactions.size() > entity.profileReactions) {
        nlohmann::json reactions = nlohmann::json::array();
        for (const ReactionDesc& reaction :
             std::span(entity.reactions).subspan(entity.profileReactions)) {
            nlohmann::json r = nlohmann::json::object();
            r["signal"] = reaction.signal;
            r["target"] = reaction.target;
            r["depth"] = reaction.depth;
            r["op"] = params::modOpName(reaction.op);
            r["polarity"] = reaction.polarity == params::Polarity::Bipolar ? "bipolar" : "unipolar";
            if (reaction.component >= 0) {
                r["component"] = reaction.component;
            }
            if (!reaction.enabled) {
                r["enabled"] = false;
            }
            if (!reaction.spatial) {
                r["spatial"] = false;
            }
            r["chain"] = params::chainToJson(reaction.chain);
            reactions.push_back(std::move(r));
        }
        j["reactions"] = std::move(reactions);
    }
    if (entity.clips.size() > entity.profileClips) {
        nlohmann::json clips = nlohmann::json::object();
        for (const auto& [activity, state] : std::span(entity.clips).subspan(entity.profileClips)) {
            clips[activity] = state;
        }
        j["clips"] = std::move(clips);
    }
    if (entity.tags.size() > entity.profileTags) {
        nlohmann::json tags = nlohmann::json::array();
        for (const std::string& tag : std::span(entity.tags).subspan(entity.profileTags)) {
            tags.push_back(tag);
        }
        j["tags"] = std::move(tags);
    }
    if (entity.sockets.size() > entity.profileSockets) {
        nlohmann::json sockets = nlohmann::json::array();
        for (const SocketDesc& socket : std::span(entity.sockets).subspan(entity.profileSockets)) {
            nlohmann::json s = nlohmann::json::object();
            s["name"] = socket.name;
            if (!socket.joint.empty()) {
                s["joint"] = socket.joint;
            }
            const glm::vec3 euler = eulerDegrees(socket.offset.rotation);
            s["position"] = {socket.offset.position.x, socket.offset.position.y, socket.offset.position.z};
            s["rotation"] = {euler.x, euler.y, euler.z};
            s["scale"] = {socket.offset.scale.x, socket.offset.scale.y, socket.offset.scale.z};
            sockets.push_back(std::move(s));
        }
        j["sockets"] = std::move(sockets);
    }
    if (!entity.attachments.empty()) {
        nlohmann::json attachments = nlohmann::json::array();
        for (const AttachmentDesc& attachment : entity.attachments) {
            attachments.push_back(nlohmann::json{{"node", attachment.node}, {"socket", attachment.socket}});
        }
        j["attachments"] = std::move(attachments);
    }
    if (!entity.properties.empty()) {
        const bool simple = std::all_of(entity.properties.begin(), entity.properties.end(),
                                        [](const PropertyDesc& p) { return p.min == 0.0f && p.max == 1.0f; });
        if (simple) {
            nlohmann::json properties = nlohmann::json::object();
            for (const PropertyDesc& property : entity.properties) {
                properties[property.name] = property.value;
            }
            j["properties"] = std::move(properties);
        } else {
            nlohmann::json properties = nlohmann::json::array();
            for (const PropertyDesc& property : entity.properties) {
                properties.push_back(nlohmann::json{{"name", property.name},
                                                    {"value", property.value},
                                                    {"min", property.min},
                                                    {"max", property.max}});
            }
            j["properties"] = std::move(properties);
        }
    }
    if (!entity.interactions.empty()) {
        nlohmann::json interactions = nlohmann::json::array();
        for (const InteractionDesc& interaction : entity.interactions) {
            interactions.push_back(interactionToJson(interaction));
        }
        j["interactions"] = std::move(interactions);
    }
    if (!entity.actions.empty()) {
        j["actions"] = actionsToJson(entity.actions);
    }
    if (!entity.schedule.entries.empty()) {
        j["schedule"] = scheduleToJson(entity.schedule);
    }
    if (!(entity.gait == GaitSettings{})) {
        j["gait"] = gaitToJson(entity.gait);
    }
    // Written when the entity declared senses and never otherwise, because the key's *presence* is
    // the opt-in: emitting a default block for every entity would give every body in every scene
    // this repository ships a sense stage it never asked for, on the next save.
    if (entity.perceives) {
        j["perception"] = perceptionToJson(entity.perception);
    }
    return j;
}

nlohmann::json entitiesToJson(const std::vector<EntityDesc>& entities) {
    nlohmann::json j = nlohmann::json::array();
    for (const EntityDesc& entity : entities) {
        j.push_back(entityToJson(entity));
    }
    return j;
}

} // namespace avgen::entity
