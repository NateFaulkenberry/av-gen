#include "entity/action.hpp"

#include "entity/entity.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Signed shortest angular distance from `from` to `to`, in radians.
float angleDelta(float from, float to) {
    float d = to - from;
    while (d > kPi) {
        d -= 2.0f * kPi;
    }
    while (d < -kPi) {
        d += 2.0f * kPi;
    }
    return d;
}

glm::vec2 flat(glm::vec3 p) { return glm::vec2(p.x, p.z); }

float readFloat(const nlohmann::json& j, const char* key, float fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}
double readDouble(const nlohmann::json& j, const char* key, double fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<double>() : fallback;
}
bool readBool(const nlohmann::json& j, const char* key, bool fallback) {
    return j.contains(key) && j[key].is_boolean() ? j[key].get<bool>() : fallback;
}
std::string readString(const nlohmann::json& j, const char* key) {
    return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string{};
}

Result<std::vector<Condition>> conditionsFromJson(const nlohmann::json& j) {
    std::vector<Condition> out;
    if (!j.is_array()) {
        return fail("'conditions' must be an array");
    }
    for (const auto& item : j) {
        if (!item.is_object() || !item.contains("property") || !item["property"].is_string()) {
            return fail("every condition needs a string 'property'");
        }
        Condition c;
        c.property = item["property"].get<std::string>();
        c.on = readString(item, "on");
        if (item.contains("test") && item["test"].is_string()) {
            const auto test = testFromName(item["test"].get<std::string>());
            if (!test) {
                return fail("unknown condition test '{}'", item["test"].get<std::string>());
            }
            c.test = *test;
        }
        c.value = readFloat(item, "value", 0.0f);
        out.push_back(std::move(c));
    }
    return out;
}

nlohmann::json conditionsToJson(const std::vector<Condition>& conditions) {
    nlohmann::json out = nlohmann::json::array();
    for (const Condition& c : conditions) {
        nlohmann::json item = nlohmann::json::object();
        item["property"] = c.property;
        if (!c.on.empty()) {
            item["on"] = c.on;
        }
        item["test"] = testName(c.test);
        if (c.test != Test::Set && c.test != Test::Clear) {
            item["value"] = c.value;
        }
        out.push_back(std::move(item));
    }
    return out;
}

// "set": { "headphones": 1 } for the common case, or an array when an effect names another entity.
Result<std::vector<PropertySet>> setsFromJson(const nlohmann::json& j) {
    std::vector<PropertySet> out;
    if (j.is_object()) {
        for (const auto& [key, value] : j.items()) {
            if (!value.is_number()) {
                return fail("'set': '{}' must be a number", key);
            }
            out.push_back(PropertySet{key, {}, value.get<float>()});
        }
        return out;
    }
    if (!j.is_array()) {
        return fail("'set' must be an object or an array");
    }
    for (const auto& item : j) {
        if (!item.is_object() || !item.contains("property") || !item["property"].is_string()) {
            return fail("every 'set' entry needs a string 'property'");
        }
        out.push_back(PropertySet{item["property"].get<std::string>(), readString(item, "on"),
                                  readFloat(item, "value", 1.0f)});
    }
    return out;
}

nlohmann::json setsToJson(const std::vector<PropertySet>& sets) {
    // The readable spelling survives the round trip whenever nothing needs `on`.
    const bool simple = std::all_of(sets.begin(), sets.end(),
                                    [](const PropertySet& s) { return s.on.empty(); });
    if (simple) {
        nlohmann::json out = nlohmann::json::object();
        for (const PropertySet& s : sets) {
            out[s.property] = s.value;
        }
        return out;
    }
    nlohmann::json out = nlohmann::json::array();
    for (const PropertySet& s : sets) {
        nlohmann::json item = nlohmann::json::object();
        item["property"] = s.property;
        if (!s.on.empty()) {
            item["on"] = s.on;
        }
        item["value"] = s.value;
        out.push_back(std::move(item));
    }
    return out;
}

} // namespace

// ---- names -----------------------------------------------------------------------------------

const char* routeStatusName(RouteStatus status) {
    switch (status) {
    case RouteStatus::Ready: return "ready";
    case RouteStatus::Pending: return "pending";
    case RouteStatus::Unreachable: return "unreachable";
    }
    return "?";
}

const char* targetKindName(TargetKind kind) {
    switch (kind) {
    case TargetKind::None: return "none";
    case TargetKind::Point: return "point";
    case TargetKind::EntityRef: return "entity";
    case TargetKind::Node: return "node";
    case TargetKind::Interaction: return "interaction";
    }
    return "?";
}

const char* testName(Test test) {
    switch (test) {
    case Test::Set: return "set";
    case Test::Clear: return "clear";
    case Test::Equal: return "equal";
    case Test::NotEqual: return "notEqual";
    case Test::Less: return "less";
    case Test::Greater: return "greater";
    }
    return "?";
}

std::optional<Test> testFromName(std::string_view name) {
    if (name == "set") return Test::Set;
    if (name == "clear") return Test::Clear;
    if (name == "equal") return Test::Equal;
    if (name == "notEqual") return Test::NotEqual;
    if (name == "less") return Test::Less;
    if (name == "greater") return Test::Greater;
    return std::nullopt;
}

const char* actionKindName(ActionKind kind) {
    switch (kind) {
    case ActionKind::Wait: return "wait";
    case ActionKind::Move: return "move";
    case ActionKind::Face: return "face";
    case ActionKind::Pose: return "pose";
    case ActionKind::Interact: return "interact";
    case ActionKind::Equip: return "equip";
    case ActionKind::Unequip: return "unequip";
    case ActionKind::Set: return "set";
    }
    return "?";
}

std::optional<ActionKind> actionKindFromName(std::string_view name) {
    if (name == "wait") return ActionKind::Wait;
    if (name == "move" || name == "walkTo" || name == "goTo") return ActionKind::Move;
    if (name == "face" || name == "lookAt") return ActionKind::Face;
    if (name == "pose" || name == "play") return ActionKind::Pose;
    if (name == "interact") return ActionKind::Interact;
    if (name == "equip") return ActionKind::Equip;
    if (name == "unequip") return ActionKind::Unequip;
    if (name == "set") return ActionKind::Set;
    return std::nullopt;
}

const char* actionResultName(ActionResult result) {
    switch (result) {
    case ActionResult::Completed: return "completed";
    case ActionResult::Skipped: return "skipped";
    case ActionResult::Failed: return "failed";
    case ActionResult::Cancelled: return "cancelled";
    }
    return "?";
}

const char* authorityName(Authority authority) {
    switch (authority) {
    case Authority::Routine: return "routine";
    case Authority::Action: return "action";
    case Authority::Director: return "director";
    }
    return "?";
}

// ---- the navigator-backed path provider ---------------------------------------------------------

RouteStatus NavigatorPath::route(glm::vec2 from, glm::vec2 to, std::vector<glm::vec2>& out) const {
    out.clear();
    if (nav_ == nullptr || !nav_->valid()) {
        // No terrain: the world is the y = 0 plane and every straight line across it is a route.
        // The same answer `Navigator::groundHeight` gives, for the same reason.
        out.push_back(to);
        return RouteStatus::Ready;
    }
    if (!nav_->navigable(to)) {
        return RouteStatus::Unreachable;
    }
    (void)from;
    out.push_back(to);
    return RouteStatus::Ready;
}

glm::vec2 NavigatorPath::steer(glm::vec2 from, glm::vec2 to, float lookahead) const {
    if (nav_ == nullptr || !nav_->valid()) {
        const glm::vec2 d = to - from;
        const float length = glm::length(d);
        return length > 1e-5f ? d / length : glm::vec2(0.0f);
    }
    return nav_->steer(from, to, lookahead);
}

float NavigatorPath::groundHeight(glm::vec2 p) const {
    return nav_ == nullptr ? 0.0f : nav_->groundHeight(p);
}

// ---- the queue ---------------------------------------------------------------------------------

void ActionQueue::push(ActionDesc action, Authority authority) {
    layers_[static_cast<std::size_t>(authority)].actions.push_back(std::move(action));
}

void ActionQueue::push(std::vector<ActionDesc> actions, Authority authority) {
    Layer& layer = layers_[static_cast<std::size_t>(authority)];
    layer.actions.insert(layer.actions.end(), std::make_move_iterator(actions.begin()),
                         std::make_move_iterator(actions.end()));
}

void ActionQueue::override(std::vector<ActionDesc> actions, Authority authority, double now) {
    cancel(authority, now);
    Layer& layer = layers_[static_cast<std::size_t>(authority)];
    layer.actions = std::move(actions);
    layer.index = 0;
    layer.started = false;
    layer.elapsed = 0.0;
    layer.progress = Progress{};
}

void ActionQueue::cancel(Authority authority, double now) {
    Layer& layer = layers_[static_cast<std::size_t>(authority)];
    if (layer.started && layer.index < layer.actions.size()) {
        ActionEvent event;
        event.entity = owner_;
        event.action = layer.actions[layer.index].name.empty()
                           ? actionKindName(layer.actions[layer.index].kind)
                           : layer.actions[layer.index].name;
        event.result = ActionResult::Cancelled;
        event.time = now;
        if (listener_) {
            listener_(event);
        }
        if (sink_ != nullptr) {
            sink_->push_back(std::move(event));
        }
    }
    layer = Layer{};
}

void ActionQueue::clear(double now) {
    for (std::size_t i = 0; i < kAuthorityCount; ++i) {
        cancel(static_cast<Authority>(i), now);
    }
    driver_ = -1;
}

void ActionQueue::hold(Authority authority, bool held) {
    layers_[static_cast<std::size_t>(authority)].heldFlag = held;
}

bool ActionQueue::held(Authority authority) const {
    return layers_[static_cast<std::size_t>(authority)].heldFlag;
}

int ActionQueue::topLayer() const {
    for (int i = static_cast<int>(kAuthorityCount) - 1; i >= 0; --i) {
        if (layers_[static_cast<std::size_t>(i)].live()) {
            return i;
        }
    }
    return -1;
}

bool ActionQueue::running() const { return topLayer() >= 0; }

Authority ActionQueue::authority() const {
    const int top = topLayer();
    return top < 0 ? Authority::Routine : static_cast<Authority>(top);
}

const ActionDesc* ActionQueue::current() const {
    const int top = topLayer();
    return top < 0 ? nullptr : current(static_cast<Authority>(top));
}

const ActionDesc* ActionQueue::current(Authority authority) const {
    const Layer& layer = layers_[static_cast<std::size_t>(authority)];
    return layer.index < layer.actions.size() ? &layer.actions[layer.index] : nullptr;
}

std::span<const glm::vec2> ActionQueue::route() const {
    const int top = topLayer();
    if (top < 0) {
        return {};
    }
    const Layer& layer = layers_[static_cast<std::size_t>(top)];
    const ActionDesc* action = current(static_cast<Authority>(top));
    if (action == nullptr || action->kind != ActionKind::Move || !layer.progress.routed) {
        return {};
    }
    return layer.progress.waypoints;
}

std::size_t ActionQueue::routeLeg() const {
    const int top = topLayer();
    return top < 0 ? 0 : layers_[static_cast<std::size_t>(top)].progress.waypoint;
}

double ActionQueue::elapsed() const {
    const int top = topLayer();
    return top < 0 ? 0.0 : layers_[static_cast<std::size_t>(top)].elapsed;
}

double ActionQueue::elapsed(Authority authority) const {
    return layers_[static_cast<std::size_t>(authority)].elapsed;
}

std::size_t ActionQueue::pending() const {
    std::size_t total = 0;
    for (std::size_t i = 0; i < kAuthorityCount; ++i) {
        total += pending(static_cast<Authority>(i));
    }
    return total;
}

std::size_t ActionQueue::pending(Authority authority) const {
    const Layer& layer = layers_[static_cast<std::size_t>(authority)];
    return layer.index < layer.actions.size() ? layer.actions.size() - layer.index : 0;
}

void ActionQueue::reset() {
    for (Layer& layer : layers_) {
        layer = Layer{};
    }
    driver_ = -1;
}

std::size_t ActionQueue::labelIndex(const Layer& layer, const std::string& label) const {
    for (std::size_t i = 0; i < layer.actions.size(); ++i) {
        if (layer.actions[i].name == label) {
            return i;
        }
    }
    return layer.actions.size();
}

void ActionQueue::emit(const ActionContext& ctx, const ActionDesc& action, ActionResult result,
                       const std::string& reason) {
    ActionEvent event;
    event.entity = ctx.self != nullptr ? ctx.self->name() : std::string{};
    event.action = action.name.empty() ? actionKindName(action.kind) : action.name;
    event.event = action.onComplete;
    event.result = result;
    event.reason = reason;
    event.time = ctx.time;
    if (listener_) {
        listener_(event);
    }
    if (ctx.events != nullptr) {
        ctx.events->push_back(std::move(event));
    }
}

namespace {

// Where a target is, in world space. False when it cannot be resolved *now* -- which is not the
// same as never: an entity that has not been placed yet resolves later, so a caller retries rather
// than failing the action outright.
bool resolvePoint(const ActionContext& ctx, const ActionTarget& target, glm::vec3& out) {
    switch (target.kind) {
    case TargetKind::None:
        return false;
    case TargetKind::Point:
        out = target.point;
        return true;
    case TargetKind::EntityRef:
    case TargetKind::Node:
    case TargetKind::Interaction: {
        if (ctx.world == nullptr) {
            return false;
        }
        // A socket on the target, when one was named and the target has it: that is the brief's
        // "a socket on a prop" -- the place to stand to sit down, the place the item hangs.
        if (!target.member.empty() && target.kind != TargetKind::Interaction) {
            if (const Entity* other = ctx.world->find(target.name)) {
                scene::Transform t;
                if (resolved(other->socketTransform(target.member, t))) {
                    out = t.position;
                    return true;
                }
            }
        }
        if (target.kind == TargetKind::Interaction) {
            if (const Entity* other = ctx.world->find(target.name)) {
                if (const InteractionDesc* verb = other->interaction(target.member)) {
                    scene::Transform t;
                    if (!verb->socket.empty() && resolved(other->socketTransform(verb->socket, t))) {
                        out = t.position;
                        return true;
                    }
                }
            }
        }
        return ctx.world->pointOfInterest(target.name, out);
    }
    }
    return false;
}

Entity* findEntity(const ActionContext& ctx, const std::string& name) {
    if (ctx.world == nullptr) {
        return nullptr;
    }
    if (name.empty()) {
        return ctx.self;
    }
    return ctx.world->find(name);
}

bool testProperty(Test test, float value, float against, bool declared) {
    switch (test) {
    case Test::Set: return declared && value > 0.5f;
    case Test::Clear: return !declared || value <= 0.5f;
    case Test::Equal: return declared && std::abs(value - against) < 1e-4f;
    case Test::NotEqual: return !declared || std::abs(value - against) >= 1e-4f;
    case Test::Less: return declared && value < against;
    case Test::Greater: return declared && value > against;
    }
    return false;
}

bool conditionsMet(const ActionContext& ctx, const std::vector<Condition>& conditions,
                   const Entity* defaultSubject) {
    for (const Condition& condition : conditions) {
        const Entity* subject = condition.on.empty() ? defaultSubject
                                                     : (ctx.world != nullptr ? ctx.world->find(condition.on) : nullptr);
        const bool declared = subject != nullptr && subject->hasProperty(condition.property);
        const float value = declared ? subject->property(condition.property) : 0.0f;
        if (!testProperty(condition.test, value, condition.value, declared)) {
            return false;
        }
    }
    return true;
}

void applySets(const ActionContext& ctx, const std::vector<PropertySet>& sets, Entity* defaultSubject) {
    for (const PropertySet& set : sets) {
        Entity* subject = set.on.empty() ? defaultSubject : findEntity(ctx, set.on);
        if (subject != nullptr) {
            subject->setProperty(set.property, set.value);
        }
    }
}

} // namespace

void ActionQueue::begin(Layer& layer, const ActionContext& ctx, EntityState& state) {
    (void)state;
    layer.started = true;
    layer.elapsed = 0.0;
    layer.progress = Progress{};
    const ActionDesc& action = layer.actions[layer.index];
    if (action.kind == ActionKind::Interact && ctx.world != nullptr) {
        Entity* prop = ctx.world->find(action.target.name);
        if (prop != nullptr && ctx.self != nullptr) {
            if (const InteractionDesc* verb = prop->interaction(action.target.member)) {
                if (verb->exclusive) {
                    layer.progress.claimed = prop->claim(action.target.member, ctx.self->name());
                }
            }
        }
    }
}

void ActionQueue::release(Layer& layer, const ActionContext& ctx) {
    if (!layer.progress.claimed || ctx.world == nullptr || ctx.self == nullptr) {
        return;
    }
    const ActionDesc& action = layer.actions[layer.index];
    if (Entity* prop = ctx.world->find(action.target.name)) {
        prop->release(action.target.member, ctx.self->name());
    }
    layer.progress.claimed = false;
}

void ActionQueue::finish(Layer& layer, const ActionContext& ctx, ActionResult result, std::string reason) {
    const ActionDesc& action = layer.actions[layer.index];
    release(layer, ctx);
    if (result == ActionResult::Completed) {
        applySets(ctx, action.set, ctx.self);
    }
    emit(ctx, action, result, reason);

    // The branch. A skipped action may say where to carry on from; everything else simply moves to
    // the next. A label that names nothing is a jump nobody can follow, so it falls through to the
    // next action and says so through the world's problem list rather than silently looping.
    std::size_t next = layer.index + 1;
    if (result == ActionResult::Skipped && !action.otherwise.empty()) {
        const std::size_t target = labelIndex(layer, action.otherwise);
        if (target < layer.actions.size()) {
            next = target;
        }
    }
    layer.index = next;
    layer.started = false;
    layer.elapsed = 0.0;
    layer.progress = Progress{};
}

ActionOutput ActionQueue::update(const ActionContext& ctx, EntityState& state) {
    ActionOutput out;
    // Drained tiers are emptied at the *top* of an update rather than the moment the last action
    // finished, for two reasons that both bite. `ActionOutput::activity` is a view into the action
    // that is running, and clearing the vector under it would dangle; and a schedule pushes its
    // next entry between two updates, onto a tier whose index is already past the end -- clearing
    // then would throw away what it just pushed. Checking here, before anything is read, is the
    // one moment when neither is true.
    for (Layer& layer : layers_) {
        if (!layer.actions.empty() && layer.index >= layer.actions.size()) {
            layer.actions.clear();
            layer.index = 0;
            layer.started = false;
            layer.elapsed = 0.0;
            layer.progress = Progress{};
        }
    }
    const int top = topLayer();
    if (top != driver_) {
        // The driver changed. Whatever the *lower* tier was doing keeps its elapsed seconds and its
        // progress exactly as they are -- that is the resume ADR-091 requires -- unless the author
        // asked for a replay, in which case it is put back to the start of the same action.
        if (driver_ >= 0 && driver_ != top) {
            Layer& previous = layers_[static_cast<std::size_t>(driver_)];
            if (previous.started && previous.index < previous.actions.size() &&
                !previous.actions[previous.index].resumable) {
                release(previous, ctx);
                previous.started = false;
                previous.elapsed = 0.0;
                previous.progress = Progress{};
            }
        }
        driver_ = top;
    }
    if (top < 0) {
        return out;
    }
    Layer& layer = layers_[static_cast<std::size_t>(top)];

    // At most two actions are touched per tick. Without a bound, a list of instantaneous `set`s --
    // or a conditional branch that jumps backwards over one -- would drain, or spin, inside a
    // single frame. Bounded rather than forbidden: a queue of eight sets takes four ticks, an
    // unsatisfiable branch burns two steps a frame instead of hanging, and nothing loops forever.
    for (int step = 0; step < 2 && layer.index < layer.actions.size(); ++step) {
        const ActionDesc& action = layer.actions[layer.index];
        if (!layer.started) {
            if (!conditionsMet(ctx, action.when, ctx.self)) {
                finish(layer, ctx, ActionResult::Skipped, "conditions not met");
                continue;
            }
            begin(layer, ctx, state);
        }
        layer.elapsed += ctx.dt;

        bool done = false;
        std::string reason;
        bool failed = false;
        std::string_view activity = action.activity;
        bool movement = false;

        switch (action.kind) {
        case ActionKind::Set: {
            done = true;
            break;
        }
        case ActionKind::Wait: {
            state.speed = 0.0f;
            done = layer.elapsed >= action.duration;
            break;
        }
        case ActionKind::Pose: {
            state.speed = 0.0f;
            // A pose with no duration runs until something else ends it: a character told to sleep
            // sleeps until the director says otherwise, and inventing a length would be a lie.
            done = action.duration > 0.0 && layer.elapsed >= action.duration;
            break;
        }
        case ActionKind::Face: {
            glm::vec3 point{0.0f};
            if (!resolvePoint(ctx, action.target, point)) {
                failed = true;
                reason = "no such target";
                break;
            }
            const glm::vec2 to = flat(point) - flat(state.position());
            if (glm::length(to) > 1e-4f) {
                const float wanted = std::atan2(to.x, to.y);
                const float delta = angleDelta(state.yaw, wanted);
                const float rate = action.speed > 0.0f ? action.speed : 2.5f; // rad/s
                const float step2 = rate * static_cast<float>(ctx.dt);
                const float applied = std::clamp(delta, -step2, step2);
                state.yaw += applied;
                state.turnRate = applied / std::max(static_cast<float>(ctx.dt), 1e-4f);
                const float tolerance = action.tolerance > 0.0f ? action.tolerance : 0.05f;
                done = std::abs(angleDelta(state.yaw, wanted)) <= tolerance;
            } else {
                done = true;
            }
            state.speed = 0.0f;
            movement = true;
            if (!done && action.duration > 0.0 && layer.elapsed >= action.duration) {
                done = true;
            }
            break;
        }
        case ActionKind::Move: {
            glm::vec3 point{0.0f};
            if (!resolvePoint(ctx, action.target, point)) {
                failed = true;
                reason = "no such target";
                break;
            }
            const glm::vec2 goal = flat(point);
            const glm::vec2 here = flat(state.position());
            const IPathProvider* path = ctx.path;
            if (!layer.progress.routed) {
                if (path == nullptr) {
                    layer.progress.waypoints.assign(1, goal);
                    layer.progress.routed = true;
                } else {
                    const RouteStatus status = path->route(here, goal, layer.progress.waypoints);
                    if (status == RouteStatus::Unreachable) {
                        failed = true;
                        reason = "unreachable";
                        break;
                    }
                    if (status == RouteStatus::Pending) {
                        state.speed = 0.0f;
                        movement = true;
                        break; // ask again next tick; a planner is allowed to take its time
                    }
                    layer.progress.routed = true;
                }
                layer.progress.waypoint = 0;
                layer.progress.bestDistance = glm::length(goal - here);
                layer.progress.sinceProgress = 0.0;
            }
            // The target may move (it is an entity, not a pin), so the last waypoint always tracks
            // the goal rather than the place the goal was when the route was asked for.
            if (!layer.progress.waypoints.empty()) {
                layer.progress.waypoints.back() = goal;
            }
            const float tolerance = action.tolerance > 0.0f ? action.tolerance : 0.75f;
            const float distance = glm::length(goal - here);
            if (distance <= tolerance) {
                state.speed = 0.0f;
                done = true;
                movement = true;
                break;
            }
            const glm::vec2 waypoint =
                layer.progress.waypoint < layer.progress.waypoints.size()
                    ? layer.progress.waypoints[layer.progress.waypoint]
                    : goal;
            if (glm::length(waypoint - here) <= tolerance &&
                layer.progress.waypoint + 1 < layer.progress.waypoints.size()) {
                ++layer.progress.waypoint;
            }

            const GaitSettings gait = ctx.gait != nullptr ? *ctx.gait : GaitSettings{};
            const float wantedSpeed = action.speed > 0.0f ? action.speed : gait.walkSpeed;
            glm::vec2 direction = waypoint - here;
            const float toWaypoint = glm::length(direction);
            direction = toWaypoint > 1e-5f ? direction / toWaypoint : glm::vec2(0.0f);
            if (path != nullptr) {
                const glm::vec2 steered = path->steer(here, waypoint, std::max(wantedSpeed * 1.5f, 2.0f));
                if (glm::length(steered) > 0.5f) {
                    direction = steered;
                } else {
                    failed = true;
                    reason = "blocked";
                    break;
                }
            }
            // Turn towards the heading, then travel at the fraction of full speed the facing
            // allows, then let the accel/decel model decide how much of that it may actually have.
            // The same order the wander behaviour uses, so a character driven by an action and a
            // character driven by a behaviour move identically.
            const float wantedYaw = std::atan2(direction.x, direction.y);
            const float delta = angleDelta(state.yaw, wantedYaw);
            const float turnStep = 2.45f * static_cast<float>(ctx.dt); // ~140 deg/s, wander's default
            const float applied = std::clamp(delta, -turnStep, turnStep);
            state.yaw += applied;
            state.turnRate = applied / std::max(static_cast<float>(ctx.dt), 1e-4f);
            const float alignment = std::max(0.0f, std::cos(angleDelta(state.yaw, wantedYaw)));
            // Slow into the goal rather than stopping dead on it: v = sqrt(2 a d) is the fastest
            // speed from which the remaining distance is still enough to decelerate in.
            const float arrival = std::sqrt(std::max(0.0f, 2.0f * gait.decel * std::max(0.0f, distance - tolerance)));
            const float desired = std::min({wantedSpeed * alignment, arrival, wantedSpeed});
            layer.progress.speed =
                Gait::approach(layer.progress.speed, desired, gait.accel, gait.decel, ctx.dt);
            const float travel =
                std::min(layer.progress.speed, distance / std::max(static_cast<float>(ctx.dt), 1e-4f));
            const glm::vec2 heading(std::sin(state.yaw), std::cos(state.yaw));
            state.travel.x += heading.x * travel * static_cast<float>(ctx.dt);
            state.travel.z += heading.y * travel * static_cast<float>(ctx.dt);
            state.speed = travel;
            if (path != nullptr) {
                const glm::vec3 p = state.position();
                state.travel.y = path->groundHeight(glm::vec2(p.x, p.z)) - state.anchor.y;
            }
            movement = true;

            // Stuck detection. Not a timeout -- a long walk is not a stuck walk -- but a lack of
            // *progress*: a character that has not got closer to its goal for `kStuckSeconds`
            // gives up with a reason, which is the difference between a bug report and a mystery.
            constexpr double kStuckSeconds = 4.0;
            if (distance < layer.progress.bestDistance - 0.05f) {
                layer.progress.bestDistance = distance;
                layer.progress.sinceProgress = 0.0;
            } else {
                layer.progress.sinceProgress += ctx.dt;
                if (layer.progress.sinceProgress >= kStuckSeconds) {
                    failed = true;
                    reason = "stuck";
                }
            }
            if (!done && !failed && action.duration > 0.0 && layer.elapsed >= action.duration) {
                failed = true;
                reason = "out of time";
            }
            break;
        }
        case ActionKind::Interact: {
            Entity* prop = ctx.world != nullptr ? ctx.world->find(action.target.name) : nullptr;
            if (prop == nullptr) {
                failed = true;
                reason = "no such prop";
                break;
            }
            const InteractionDesc* verb = prop->interaction(action.target.member);
            if (verb == nullptr) {
                failed = true;
                reason = "prop offers no such interaction";
                break;
            }
            if (verb->exclusive && !layer.progress.claimed) {
                failed = true;
                reason = "in use";
                break;
            }
            if (!conditionsMet(ctx, verb->conditions, prop)) {
                failed = true;
                reason = "interaction conditions not met";
                break;
            }
            glm::vec3 point{0.0f};
            if (resolvePoint(ctx, action.target, point)) {
                const float distance = glm::length(flat(point) - flat(state.position()));
                if (distance > verb->range) {
                    // Getting there is a `move`, written by whoever wrote the sequence. An
                    // interaction that walked to its own target would be two systems in one and
                    // would make "why did my character walk off" unanswerable.
                    failed = true;
                    reason = "out of range";
                    break;
                }
            }
            state.speed = 0.0f;
            if (activity.empty()) {
                activity = verb->activity;
            }
            const double duration = action.duration > 0.0 ? action.duration : verb->duration;
            done = duration <= 0.0 ? false : layer.elapsed >= duration;
            if (done) {
                applySets(ctx, verb->set, prop);
                if (!verb->onComplete.empty()) {
                    ActionDesc synthetic;
                    synthetic.kind = ActionKind::Interact;
                    synthetic.name = verb->name;
                    synthetic.onComplete = verb->onComplete;
                    emit(ctx, synthetic, ActionResult::Completed, {});
                }
            }
            break;
        }
        case ActionKind::Equip: {
            Entity* self = ctx.self;
            if (self == nullptr || action.target.name.empty()) {
                failed = true;
                reason = "nothing to equip";
                break;
            }
            const std::string& socket = !action.socket.empty() ? action.socket : action.target.member;
            if (!self->attach(action.target.name, socket)) {
                failed = true;
                reason = "no socket '" + socket + "'";
                break;
            }
            done = action.duration <= 0.0 || layer.elapsed >= action.duration;
            break;
        }
        case ActionKind::Unequip: {
            Entity* self = ctx.self;
            if (self == nullptr || !self->detach(action.target.name)) {
                failed = true;
                reason = "not carrying '" + action.target.name + "'";
                break;
            }
            done = action.duration <= 0.0 || layer.elapsed >= action.duration;
            break;
        }
        }

        out.driving = true;
        out.movement = movement;
        out.activity = activity;
        state.driven = true;

        if (failed) {
            finish(layer, ctx, ActionResult::Failed, std::move(reason));
            // A failure does not fall through to the next action in the same tick: the entity
            // stands still for one update and the reader sees the failure before whatever follows.
            out.activity = {};
            break;
        }
        if (!done) {
            break;
        }
        finish(layer, ctx, ActionResult::Completed, {});
        if (layer.index >= layer.actions.size()) {
            break;
        }
    }
    return out;
}

// ---- the schedule --------------------------------------------------------------------------------

void Schedule::setDesc(ScheduleDesc desc) {
    desc_ = std::move(desc);
    std::stable_sort(desc_.entries.begin(), desc_.entries.end(),
                     [](const ScheduleEntry& a, const ScheduleEntry& b) { return a.time < b.time; });
    reset();
}

void Schedule::start(double now) {
    running_ = true;
    paused_ = false;
    start_ = now;
    pausedFor_ = 0.0;
    pausedAt_ = 0.0;
    next_ = 0;
    cycle_ = 0;
}

void Schedule::stop() {
    running_ = false;
    paused_ = false;
    next_ = 0;
}

void Schedule::pause(double now, ActionQueue& queue) {
    if (!running_ || paused_) {
        return;
    }
    paused_ = true;
    pausedAt_ = now;
    // Freezing the tier rather than cancelling it is the whole difference between pause and stop:
    // the action it was in the middle of keeps its elapsed seconds, and the tier below -- the
    // entity's own behaviours -- takes over while the routine is out of the way.
    queue.hold(Authority::Routine, true);
}

void Schedule::resume(double now, ActionQueue& queue) {
    if (!running_ || !paused_) {
        return;
    }
    paused_ = false;
    pausedFor_ += now - pausedAt_;
    queue.hold(Authority::Routine, false);
}

double Schedule::localTime(double now) const {
    if (!running_) {
        return 0.0;
    }
    const double stopped = paused_ ? now - pausedAt_ : 0.0;
    return now - start_ - pausedFor_ - stopped;
}

void Schedule::update(double now, ActionQueue& queue) {
    if (!running_ || paused_ || desc_.entries.empty()) {
        return;
    }
    const double local = localTime(now);
    while (next_ < desc_.entries.size() && desc_.entries[next_].time <= local) {
        queue.push(desc_.entries[next_].actions, Authority::Routine);
        ++next_;
    }
    if (next_ >= desc_.entries.size() && desc_.loop) {
        const double period =
            desc_.period > 0.0 ? desc_.period : desc_.entries.back().time + 1.0;
        // Advance the origin by exactly one period rather than to `now`, so a loop stays on its
        // grid however long a frame took and however coarsely the entity was updated.
        start_ += period;
        next_ = 0;
        ++cycle_;
    }
}

bool Schedule::trigger(std::string_view entry, ActionQueue& queue, Authority authority) {
    for (const ScheduleEntry& item : desc_.entries) {
        if (item.name == entry) {
            queue.push(item.actions, authority);
            return true;
        }
    }
    return false;
}

void Schedule::reset() {
    running_ = false;
    paused_ = false;
    start_ = 0.0;
    pausedAt_ = 0.0;
    pausedFor_ = 0.0;
    next_ = 0;
    cycle_ = 0;
}

// ---- serialisation ---------------------------------------------------------------------------

Result<ActionTarget> targetFromJson(const nlohmann::json& j) {
    ActionTarget target;
    if (j.is_string()) {
        // The shorthand: "nightstand" is an entity, "nightstand.pickUp" is one of its verbs.
        const std::string s = j.get<std::string>();
        const std::size_t dot = s.find('.');
        if (dot == std::string::npos) {
            target.kind = TargetKind::EntityRef;
            target.name = s;
        } else {
            target.kind = TargetKind::Interaction;
            target.name = s.substr(0, dot);
            target.member = s.substr(dot + 1);
        }
        return target;
    }
    if (j.is_array() && j.size() >= 3) {
        target.kind = TargetKind::Point;
        for (int i = 0; i < 3; ++i) {
            if (!j[static_cast<std::size_t>(i)].is_number()) {
                return fail("a point target must be three numbers");
            }
            target.point[i] = j[static_cast<std::size_t>(i)].get<float>();
        }
        return target;
    }
    if (!j.is_object()) {
        return fail("a target must be a string, a point or an object");
    }
    if (j.contains("interaction") && j["interaction"].is_string()) {
        const std::string s = j["interaction"].get<std::string>();
        const std::size_t dot = s.find('.');
        target.kind = TargetKind::Interaction;
        target.name = dot == std::string::npos ? s : s.substr(0, dot);
        target.member = dot == std::string::npos ? readString(j, "verb") : s.substr(dot + 1);
        if (target.member.empty()) {
            return fail("an interaction target needs a verb: \"<prop>.<verb>\"");
        }
        return target;
    }
    if (j.contains("entity") && j["entity"].is_string()) {
        target.kind = TargetKind::EntityRef;
        target.name = j["entity"].get<std::string>();
        target.member = readString(j, "socket");
        return target;
    }
    if (j.contains("node") && j["node"].is_string()) {
        target.kind = TargetKind::Node;
        target.name = j["node"].get<std::string>();
        target.member = readString(j, "socket");
        return target;
    }
    if (j.contains("point") && j["point"].is_array() && j["point"].size() >= 3) {
        target.kind = TargetKind::Point;
        for (int i = 0; i < 3; ++i) {
            target.point[i] = j["point"][static_cast<std::size_t>(i)].is_number()
                                  ? j["point"][static_cast<std::size_t>(i)].get<float>()
                                  : 0.0f;
        }
        return target;
    }
    return fail("a target object needs one of 'entity', 'node', 'point' or 'interaction'");
}

nlohmann::json targetToJson(const ActionTarget& target) {
    nlohmann::json j = nlohmann::json::object();
    switch (target.kind) {
    case TargetKind::None:
        return j;
    case TargetKind::Point:
        j["point"] = {target.point.x, target.point.y, target.point.z};
        return j;
    case TargetKind::EntityRef:
        j["entity"] = target.name;
        break;
    case TargetKind::Node:
        j["node"] = target.name;
        break;
    case TargetKind::Interaction:
        j["interaction"] = target.name + "." + target.member;
        return j;
    }
    if (!target.member.empty()) {
        j["socket"] = target.member;
    }
    return j;
}

Result<ActionDesc> actionFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("an action must be an object");
    }
    if (!j.contains("kind") || !j["kind"].is_string()) {
        return fail("an action needs a string 'kind'");
    }
    const std::string kindName = j["kind"].get<std::string>();
    const auto kind = actionKindFromName(kindName);
    if (!kind) {
        std::string known;
        for (int i = 0; i <= static_cast<int>(ActionKind::Set); ++i) {
            if (!known.empty()) {
                known += ", ";
            }
            known += actionKindName(static_cast<ActionKind>(i));
        }
        return fail("unknown action kind '{}' (known: {})", kindName, known);
    }
    ActionDesc action;
    action.kind = *kind;
    action.name = readString(j, "name");
    action.activity = readString(j, "activity");
    action.socket = readString(j, "socket");
    action.otherwise = readString(j, "otherwise");
    action.onComplete = readString(j, "onComplete");
    action.duration = readDouble(j, "duration", 0.0);
    action.speed = readFloat(j, "speed", 0.0f);
    action.tolerance = readFloat(j, "tolerance", 0.0f);
    action.resumable = readBool(j, "resumable", true);
    if (j.contains("target")) {
        auto target = targetFromJson(j["target"]);
        if (!target) {
            return fail("action '{}': {}", kindName, target.error().message);
        }
        action.target = std::move(*target);
    }
    if (j.contains("when")) {
        auto when = conditionsFromJson(j["when"]);
        if (!when) {
            return fail("action '{}': {}", kindName, when.error().message);
        }
        action.when = std::move(*when);
    }
    if (j.contains("set")) {
        auto sets = setsFromJson(j["set"]);
        if (!sets) {
            return fail("action '{}': {}", kindName, sets.error().message);
        }
        action.set = std::move(*sets);
    }
    return action;
}

nlohmann::json actionToJson(const ActionDesc& action) {
    nlohmann::json j = nlohmann::json::object();
    j["kind"] = actionKindName(action.kind);
    if (!action.name.empty()) {
        j["name"] = action.name;
    }
    if (!action.target.empty()) {
        j["target"] = targetToJson(action.target);
    }
    if (!action.activity.empty()) {
        j["activity"] = action.activity;
    }
    if (action.duration != 0.0) {
        j["duration"] = action.duration;
    }
    if (action.speed != 0.0f) {
        j["speed"] = action.speed;
    }
    if (action.tolerance != 0.0f) {
        j["tolerance"] = action.tolerance;
    }
    if (!action.socket.empty()) {
        j["socket"] = action.socket;
    }
    if (!action.when.empty()) {
        j["when"] = conditionsToJson(action.when);
    }
    if (!action.otherwise.empty()) {
        j["otherwise"] = action.otherwise;
    }
    if (!action.set.empty()) {
        j["set"] = setsToJson(action.set);
    }
    if (!action.onComplete.empty()) {
        j["onComplete"] = action.onComplete;
    }
    if (!action.resumable) {
        j["resumable"] = false;
    }
    return j;
}

Result<std::vector<ActionDesc>> actionsFromJson(const nlohmann::json& j) {
    if (!j.is_array()) {
        return fail("'actions' must be an array");
    }
    std::vector<ActionDesc> out;
    out.reserve(j.size());
    for (const auto& item : j) {
        auto action = actionFromJson(item);
        if (!action) {
            return fail("{}", action.error().message);
        }
        out.push_back(std::move(*action));
    }
    return out;
}

nlohmann::json actionsToJson(const std::vector<ActionDesc>& actions) {
    nlohmann::json j = nlohmann::json::array();
    for (const ActionDesc& action : actions) {
        j.push_back(actionToJson(action));
    }
    return j;
}

Result<InteractionDesc> interactionFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("an interaction must be an object");
    }
    if (!j.contains("name") || !j["name"].is_string() || j["name"].get<std::string>().empty()) {
        return fail("an interaction needs a non-empty string 'name' -- the verb it offers");
    }
    InteractionDesc interaction;
    interaction.name = j["name"].get<std::string>();
    interaction.socket = readString(j, "socket");
    interaction.activity = readString(j, "activity");
    interaction.duration = readDouble(j, "duration", 1.0);
    interaction.range = readFloat(j, "range", 1.5f);
    interaction.exclusive = readBool(j, "exclusive", true);
    interaction.onComplete = readString(j, "onComplete");
    if (j.contains("conditions")) {
        auto conditions = conditionsFromJson(j["conditions"]);
        if (!conditions) {
            return fail("interaction '{}': {}", interaction.name, conditions.error().message);
        }
        interaction.conditions = std::move(*conditions);
    }
    if (j.contains("set")) {
        auto sets = setsFromJson(j["set"]);
        if (!sets) {
            return fail("interaction '{}': {}", interaction.name, sets.error().message);
        }
        interaction.set = std::move(*sets);
    }
    return interaction;
}

nlohmann::json interactionToJson(const InteractionDesc& interaction) {
    nlohmann::json j = nlohmann::json::object();
    j["name"] = interaction.name;
    if (!interaction.socket.empty()) {
        j["socket"] = interaction.socket;
    }
    if (!interaction.activity.empty()) {
        j["activity"] = interaction.activity;
    }
    j["duration"] = interaction.duration;
    j["range"] = interaction.range;
    if (!interaction.exclusive) {
        j["exclusive"] = false;
    }
    if (!interaction.conditions.empty()) {
        j["conditions"] = conditionsToJson(interaction.conditions);
    }
    if (!interaction.set.empty()) {
        j["set"] = setsToJson(interaction.set);
    }
    if (!interaction.onComplete.empty()) {
        j["onComplete"] = interaction.onComplete;
    }
    return j;
}

Result<ScheduleDesc> scheduleFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("a schedule must be an object");
    }
    ScheduleDesc schedule;
    schedule.name = readString(j, "name");
    schedule.loop = readBool(j, "loop", false);
    schedule.period = readDouble(j, "period", 0.0);
    if (!j.contains("entries") || !j["entries"].is_array()) {
        return fail("a schedule needs an 'entries' array");
    }
    for (const auto& item : j["entries"]) {
        if (!item.is_object()) {
            return fail("every schedule entry must be an object");
        }
        ScheduleEntry entry;
        entry.time = readDouble(item, "time", 0.0);
        entry.name = readString(item, "name");
        if (!item.contains("actions")) {
            return fail("schedule entry '{}' has no actions", entry.name);
        }
        auto actions = actionsFromJson(item["actions"]);
        if (!actions) {
            return fail("schedule entry '{}': {}", entry.name, actions.error().message);
        }
        entry.actions = std::move(*actions);
        schedule.entries.push_back(std::move(entry));
    }
    std::stable_sort(schedule.entries.begin(), schedule.entries.end(),
                     [](const ScheduleEntry& a, const ScheduleEntry& b) { return a.time < b.time; });
    return schedule;
}

nlohmann::json scheduleToJson(const ScheduleDesc& schedule) {
    nlohmann::json j = nlohmann::json::object();
    if (!schedule.name.empty()) {
        j["name"] = schedule.name;
    }
    if (schedule.loop) {
        j["loop"] = true;
    }
    if (schedule.period > 0.0) {
        j["period"] = schedule.period;
    }
    nlohmann::json entries = nlohmann::json::array();
    for (const ScheduleEntry& entry : schedule.entries) {
        nlohmann::json item = nlohmann::json::object();
        item["time"] = entry.time;
        if (!entry.name.empty()) {
            item["name"] = entry.name;
        }
        item["actions"] = actionsToJson(entry.actions);
        entries.push_back(std::move(item));
    }
    j["entries"] = std::move(entries);
    return j;
}

Result<GaitSettings> gaitFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'gait' must be an object");
    }
    GaitSettings gait;
    gait.walkSpeed = readFloat(j, "walkSpeed", gait.walkSpeed);
    gait.runSpeed = readFloat(j, "runSpeed", gait.runSpeed);
    gait.moveEnter = readFloat(j, "moveEnter", gait.moveEnter);
    gait.moveExit = readFloat(j, "moveExit", gait.moveExit);
    gait.runEnter = readFloat(j, "runEnter", gait.runEnter);
    gait.runExit = readFloat(j, "runExit", gait.runExit);
    gait.turnEnter = readFloat(j, "turnEnter", gait.turnEnter);
    gait.minDwell = readFloat(j, "minDwell", gait.minDwell);
    gait.accel = readFloat(j, "accel", gait.accel);
    gait.decel = readFloat(j, "decel", gait.decel);
    gait.blend = readFloat(j, "blend", gait.blend);
    gait.matchRate = readBool(j, "matchRate", gait.matchRate);
    gait.rateMin = readFloat(j, "rateMin", gait.rateMin);
    gait.rateMax = readFloat(j, "rateMax", gait.rateMax);
    gait.idleRate = readFloat(j, "idleRate", gait.idleRate);
    // An exit above an enter is not hysteresis, it is a latch that never releases. Caught here
    // rather than discovered as a character that never stops running.
    if (gait.moveExit > gait.moveEnter) {
        return fail("gait: 'moveExit' ({}) must not be above 'moveEnter' ({})", gait.moveExit,
                    gait.moveEnter);
    }
    if (gait.runExit > gait.runEnter) {
        return fail("gait: 'runExit' ({}) must not be above 'runEnter' ({})", gait.runExit, gait.runEnter);
    }
    return gait;
}

nlohmann::json gaitToJson(const GaitSettings& gait) {
    nlohmann::json j = nlohmann::json::object();
    j["walkSpeed"] = gait.walkSpeed;
    j["runSpeed"] = gait.runSpeed;
    j["moveEnter"] = gait.moveEnter;
    j["moveExit"] = gait.moveExit;
    j["runEnter"] = gait.runEnter;
    j["runExit"] = gait.runExit;
    j["turnEnter"] = gait.turnEnter;
    j["minDwell"] = gait.minDwell;
    j["accel"] = gait.accel;
    j["decel"] = gait.decel;
    j["blend"] = gait.blend;
    if (gait.matchRate) {
        j["matchRate"] = true;
        j["rateMin"] = gait.rateMin;
        j["rateMax"] = gait.rateMax;
        // Written only when it is not the default, so every existing scene round-trips unchanged.
        if (gait.idleRate != GaitSettings{}.idleRate) {
            j["idleRate"] = gait.idleRate;
        }
    }
    return j;
}

} // namespace avgen::entity
