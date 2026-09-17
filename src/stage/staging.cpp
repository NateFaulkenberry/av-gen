#include "stage/staging.hpp"

#include "core/log.hpp"
#include "entity/entity.hpp"
#include "entity/navigation.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace avgen::stage {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegrees = 180.0f / kPi;
// The candidate index's cell size. Large enough that a typical search radius touches a handful of
// cells and small enough that a cell is not the whole meadow; the grid costs one pass to build and
// the number is not load-bearing beyond that.
constexpr float kCellSize = 24.0f;

[[nodiscard]] float smoothstep(float u) {
    const float t = std::clamp(u, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] float wrapAngle(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

[[nodiscard]] float angleDelta(float from, float to) { return wrapAngle(to - from); }

[[nodiscard]] glm::vec2 flat(const glm::vec3& p) { return {p.x, p.z}; }

// FNV-1a, the same one `entity.cpp` uses to derive a seed from a name. A scenario with no seed of
// its own gets one from its name rather than from zero, so two scenarios in the same scene do not
// share a stream and a scene that renames one gets a different -- but still reproducible -- draw.
[[nodiscard]] std::uint32_t nameSeed(std::string_view name) {
    std::uint32_t h = 2166136261u;
    for (const char c : name) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    return h == 0u ? 1u : h;
}

// Did the entity's Director tier fail in the update that just ran? `EntityWorld::actionEvents()`
// holds the last entity update's events, and the director runs *before* the next one, so this is
// the previous frame's verdict -- which is exactly the frame the queue drained on.
//
// Without it a `move` that came back `Unreachable` would drain the queue and read as arrival, and
// "the path was unavailable" would be indistinguishable from "it got there". That is the silent
// no-op this project keeps writing ADRs about, and it is the one failure a director most needs to
// hear: a saucer told to walk into a lake should give up and pick something else.
[[nodiscard]] bool actionFailed(const entity::EntityWorld& world, std::string_view entity,
                                std::string& reason) {
    for (const entity::ActionEvent& e : world.actionEvents()) {
        if (e.entity == entity && e.result == entity::ActionResult::Failed) {
            reason = e.reason.empty() ? std::string("failed") : e.reason;
            return true;
        }
    }
    return false;
}

} // namespace

// ---- names --------------------------------------------------------------------------------------

Value literal(float v) { return Value{.literal = v}; }
Value bound(std::string param, float fallback) {
    return Value{.literal = fallback, .param = std::move(param)};
}

const char* pickName(Pick pick) {
    switch (pick) {
    case Pick::Nearest: return "nearest";
    case Pick::Farthest: return "farthest";
    case Pick::Random: return "random";
    case Pick::First: return "first";
    }
    return "?";
}
std::optional<Pick> pickFromName(std::string_view name) {
    if (name == "nearest") return Pick::Nearest;
    if (name == "farthest") return Pick::Farthest;
    if (name == "random") return Pick::Random;
    if (name == "first") return Pick::First;
    return std::nullopt;
}

const char* stepKindName(StepKind kind) {
    switch (kind) {
    case StepKind::Wait: return "wait";
    case StepKind::MoveTo: return "moveTo";
    case StepKind::Follow: return "follow";
    case StepKind::LookAt: return "lookAt";
    case StepKind::Play: return "play";
    case StepKind::Show: return "show";
    case StepKind::Hide: return "hide";
    case StepKind::Set: return "set";
    case StepKind::Actions: return "actions";
    case StepKind::Attach: return "attach";
    case StepKind::Detach: return "detach";
    case StepKind::Release: return "release";
    case StepKind::Retire: return "retire";
    }
    return "?";
}
std::optional<StepKind> stepKindFromName(std::string_view name) {
    if (name == "wait") return StepKind::Wait;
    if (name == "moveTo" || name == "moveBy" || name == "flyTo") return StepKind::MoveTo;
    if (name == "follow" || name == "hover") return StepKind::Follow;
    if (name == "lookAt" || name == "rotateTo" || name == "face") return StepKind::LookAt;
    if (name == "play" || name == "playAnimation" || name == "setAnimationState") return StepKind::Play;
    if (name == "show") return StepKind::Show;
    if (name == "hide") return StepKind::Hide;
    if (name == "set" || name == "fade") return StepKind::Set;
    if (name == "actions") return StepKind::Actions;
    if (name == "attach") return StepKind::Attach;
    if (name == "detach") return StepKind::Detach;
    if (name == "release") return StepKind::Release;
    if (name == "retire") return StepKind::Retire;
    return std::nullopt;
}

const char* travelName(Travel travel) {
    switch (travel) {
    case Travel::Fly: return "fly";
    case Travel::Walk: return "walk";
    }
    return "?";
}
std::optional<Travel> travelFromName(std::string_view name) {
    if (name == "fly") return Travel::Fly;
    if (name == "walk") return Travel::Walk;
    return std::nullopt;
}

const char* anchorName(Anchor anchor) {
    switch (anchor) {
    case Anchor::Travel: return "travel";
    case Anchor::Visual: return "visual";
    }
    return "?";
}
std::optional<Anchor> anchorFromName(std::string_view name) {
    if (name == "travel" || name == "simulated") return Anchor::Travel;
    if (name == "visual" || name == "drawn" || name == "rendered") return Anchor::Visual;
    return std::nullopt;
}

const char* stageEventKindName(StageEventKind kind) {
    switch (kind) {
    case StageEventKind::Started: return "started";
    case StageEventKind::Bound: return "bound";
    case StageEventKind::Unbound: return "unbound";
    case StageEventKind::Beat: return "beat";
    case StageEventKind::StepDone: return "stepDone";
    case StageEventKind::StepFailed: return "stepFailed";
    case StageEventKind::Retired: return "retired";
    case StageEventKind::Cycled: return "cycled";
    case StageEventKind::Finished: return "finished";
    case StageEventKind::Cancelled: return "cancelled";
    }
    return "?";
}

// ---- construction -------------------------------------------------------------------------------

Staging::Staging() = default;
Staging::~Staging() = default;

namespace {

// Resolves one `Value` against a scenario's parameter list. A name that is not there is an error
// rather than a zero, because a parameter that silently reads zero is a scenario whose hover
// duration is nothing and whose search radius is nothing -- which looks exactly like a director
// that is broken for a reason nobody can find.
[[nodiscard]] Result<void> resolveValue(Value& v, const ScenarioDesc& scenario, const char* where) {
    if (!v.bound()) {
        v.index = -1;
        return {};
    }
    for (std::size_t i = 0; i < scenario.params.size(); ++i) {
        if (scenario.params[i].name == v.param) {
            v.index = static_cast<int>(i);
            return {};
        }
    }
    return fail("scenario '{}': {} names the parameter '{}', which the scenario does not declare",
                scenario.name, where, v.param);
}

// Two cues in one beat, each taking its *height* from the other, is a runaway.
//
// It is not a hypothetical. The shipped abduction had the saucer hovering `hoverHeight` above the
// cow while the cow rose to `liftHeight` under the saucer, and each frame both read the other's new
// y: measured, the pair climbed to 488 metres in four and a half seconds. It had been invisible
// because `ground` was pinning the cow to the terrain and breaking the loop -- so fixing grounding
// (ADR-210) is what *exposed* it, which is the usual shape of these.
//
// The horizontal half of that coupling is fine and is the point: a craft should follow a target
// that walks. Only the vertical half is circular, and only when neither end is anchored to
// something that does not move -- which is what `aboveGround` is: a height measured from the
// terrain rather than from the other body.
[[nodiscard]] bool verticalCycle(const BeatDesc& beat, std::string& a, std::string& b) {
    struct Link {
        std::string from;
        std::string to;
    };
    std::vector<Link> links;
    for (const CueDesc& cue : beat.cues) {
        for (const StepDesc& step : cue.steps) {
            if (step.kind != StepKind::MoveTo && step.kind != StepKind::Follow) {
                continue;
            }
            // Anchored to the ground, asking for no height at all, or resolved once and held:
            // not a live vertical reference.
            if (step.aboveGround || step.toRole.empty() || step.hold) {
                continue;
            }
            if (!step.height.bound() && step.height.literal == 0.0f && step.point.y == 0.0f) {
                continue;
            }
            const std::string& self = step.role.empty() ? cue.role : step.role;
            links.push_back(Link{.from = self, .to = step.toRole});
        }
    }
    for (const Link& x : links) {
        for (const Link& y : links) {
            if (x.from == y.to && x.to == y.from && x.from != x.to) {
                a = x.from;
                b = x.to;
                return true;
            }
        }
    }
    return false;
}

// The same shape of runaway, horizontally, and it only exists once a step can measure from where a
// body is *drawn* rather than from where the simulation put it.
//
// Two cues taking their horizontal station from each other is the thing ADR-210 called fine and is
// fine, because what goes round the loop each frame is the pair's own wobble -- a sinusoid whose
// time integral is bounded by amplitude/(2*pi*rate), and the shipped craft's is 0.3 m at 0.45 Hz,
// or eleven centimetres. `Anchor::Visual` puts the *behaviours'* offsets into the loop instead, and
// Glowmere's saucer drifts 2.4 m at 0.031 Hz -- an integral of about twelve metres. The same
// structure, three orders of magnitude apart, so the loop stops being benign exactly when a step
// asks for the drawn position.
//
// `hold` is the anchor here, the way `aboveGround` is the anchor for the vertical one: a station
// resolved once has nothing going round it.
[[nodiscard]] bool visualCycle(const BeatDesc& beat, std::string& a, std::string& b) {
    struct Link {
        std::string from;
        std::string to;
        bool visual = false;
    };
    std::vector<Link> links;
    for (const CueDesc& cue : beat.cues) {
        for (const StepDesc& step : cue.steps) {
            if (step.kind != StepKind::MoveTo && step.kind != StepKind::Follow) {
                continue;
            }
            if (step.toRole.empty() || step.hold) {
                continue;
            }
            const std::string& self = step.role.empty() ? cue.role : step.role;
            links.push_back(Link{.from = self,
                                 .to = step.toRole,
                                 .visual = step.anchor == Anchor::Visual});
        }
    }
    for (const Link& x : links) {
        for (const Link& y : links) {
            if (x.from == y.to && x.to == y.from && x.from != x.to && (x.visual || y.visual)) {
                a = x.from;
                b = x.to;
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool hasBeat(const ScenarioDesc& scenario, const std::string& name) {
    if (name.empty()) {
        return true;
    }
    return std::any_of(scenario.beats.begin(), scenario.beats.end(),
                       [&](const BeatDesc& b) { return b.name == name; });
}

} // namespace

Result<void> Staging::setDesc(StagingDesc desc) {
    // Validate and resolve before anything is installed, so a description that would have run
    // half-way is refused whole.
    for (const ActorDesc& actor : desc.actors) {
        if (actor.name.empty()) {
            return fail("an actor has no name");
        }
    }
    for (ScenarioDesc& scenario : desc.scenarios) {
        if (scenario.name.empty()) {
            return fail("a scenario has no name");
        }
        if (scenario.beats.empty()) {
            return fail("scenario '{}' has no beats", scenario.name);
        }
        if (!scenario.actor.empty()) {
            const bool known = std::any_of(desc.actors.begin(), desc.actors.end(),
                                           [&](const ActorDesc& a) { return a.name == scenario.actor; });
            if (!known) {
                return fail("scenario '{}' names the actor '{}', which is not declared",
                            scenario.name, scenario.actor);
            }
        }
        for (const ScenarioParam& p : scenario.params) {
            if (p.name.empty()) {
                return fail("scenario '{}' declares a parameter with no name", scenario.name);
            }
        }
        for (BeatDesc& beat : scenario.beats) {
            if (!hasBeat(scenario, beat.then)) {
                return fail("scenario '{}', beat '{}': `then` names '{}', which is not a beat",
                            scenario.name, beat.name, beat.then);
            }
            if (!hasBeat(scenario, beat.otherwise)) {
                return fail("scenario '{}', beat '{}': `otherwise` names '{}', which is not a beat",
                            scenario.name, beat.name, beat.otherwise);
            }
            for (QueryDesc& q : beat.find) {
                if (!q.valid()) {
                    return fail("scenario '{}', beat '{}': a find binds no role", scenario.name,
                                beat.name);
                }
                if (auto r = resolveValue(q.radius, scenario, "a find's radius"); !r) return r;
                if (auto r = resolveValue(q.minRadius, scenario, "a find's minRadius"); !r) return r;
                if (auto r = resolveValue(q.clearance, scenario, "a find's clearance"); !r) return r;
            }
            std::string a;
            std::string b;
            if (verticalCycle(beat, a, b)) {
                return fail("scenario '{}', beat '{}': '{}' takes its height from '{}' and '{}' "
                            "takes its height from '{}' -- each frame both read the other's new "
                            "height and the pair climbs away. Anchor one of them with "
                            "`aboveGround`.",
                            scenario.name, beat.name, a, b, b, a);
            }
            if (visualCycle(beat, a, b)) {
                return fail("scenario '{}', beat '{}': '{}' and '{}' each take their station from "
                            "the other and one of them measures from the drawn position, so the "
                            "behaviours' own offsets go round the loop every frame and the pair "
                            "walks away. Hold one of the two stations with `hold`.",
                            scenario.name, beat.name, a, b);
            }
            for (CueDesc& cue : beat.cues) {
                for (StepDesc& step : cue.steps) {
                    const char* w = stepKindName(step.kind);
                    if (auto r = resolveValue(step.duration, scenario, w); !r) return r;
                    if (auto r = resolveValue(step.height, scenario, w); !r) return r;
                    if (auto r = resolveValue(step.speed, scenario, w); !r) return r;
                    if (auto r = resolveValue(step.tolerance, scenario, w); !r) return r;
                    if (auto r = resolveValue(step.clearance, scenario, w); !r) return r;
                    if (auto r = resolveValue(step.spin, scenario, w); !r) return r;
                    if (auto r = resolveValue(step.wobble, scenario, w); !r) return r;
                    if (auto r = resolveValue(step.wobbleRate, scenario, w); !r) return r;
                    if (auto r = resolveValue(step.to, scenario, w); !r) return r;
                    if (auto r = resolveValue(step.from, scenario, w); !r) return r;
                    if (auto r = resolveValue(step.rate, scenario, w); !r) return r;
                }
            }
        }
    }

    desc_ = std::move(desc);
    runs_.clear();
    runs_.resize(desc_.scenarios.size());
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        const ScenarioDesc& s = desc_.scenarios[i];
        runs_[i].scenario = i;
        const std::uint32_t seed =
            s.seed != 0 ? s.seed : nameSeed(s.name);
        runs_[i].rng = Rng(seed);
    }

    // The tags any query mentions. This is what keeps the candidate index off the "brute-force
    // search across every scene entity" the brief forbids: an index built from the entities a
    // query could possibly want is usually a dozen animals, not the world.
    searchTags_.clear();
    for (const ScenarioDesc& s : desc_.scenarios) {
        for (const BeatDesc& b : s.beats) {
            for (const QueryDesc& q : b.find) {
                if (!q.tag.empty() &&
                    std::find(searchTags_.begin(), searchTags_.end(), q.tag) == searchTags_.end()) {
                    searchTags_.push_back(q.tag);
                }
            }
        }
    }
    needSearch_ = true;
    lastSearch_ = -1.0e30;
    claims_.clear();
    retired_.clear();
    problems_.clear();
    report_ = StageReport{};
    return {};
}

void Staging::clear() {
    desc_ = StagingDesc{};
    runs_.clear();
    claims_.clear();
    retired_.clear();
    candidates_.clear();
    candidatePoints_.clear();
    searchTags_.clear();
    written_.clear();
    events_.clear();
    log_.clear();
    problems_.clear();
    report_ = StageReport{};
    needSearch_ = true;
}

// ---- parameters ---------------------------------------------------------------------------------

void Staging::registerParameters(params::ParameterSet& params, const std::string& prefix) {
    prefix_ = prefix;
    registered_.clear();
    for (std::size_t i = 0; i < desc_.scenarios.size(); ++i) {
        const ScenarioDesc& scenario = desc_.scenarios[i];
        Run& run = runs_[i];
        run.params.clear();
        for (const ScenarioParam& p : scenario.params) {
            const std::string path = prefix + scenario.name + "/" + p.name;
            const float lo = std::min(p.min, p.max);
            const float hi = std::max(p.min, p.max);
            run.params.push_back(&params.add(params::ParamDesc<float>{
                .path = path,
                .defaultValue = std::clamp(p.value, lo, hi),
                .hardMin = lo,
                .hardMax = hi}));
            registered_.push_back(path);
        }
    }
}

void Staging::unregisterParameters(params::ParameterSet& params) {
    for (const std::string& path : registered_) {
        params.remove(path);
    }
    registered_.clear();
    for (Run& run : runs_) {
        run.params.clear();
    }
}

float Staging::value(const Run& run, const Value& v) const {
    if (v.index >= 0 && static_cast<std::size_t>(v.index) < run.params.size() &&
        run.params[static_cast<std::size_t>(v.index)] != nullptr) {
        return run.params[static_cast<std::size_t>(v.index)]->value();
    }
    // Registration has not happened yet, or the scenario declared the parameter and nobody
    // registered it. The authored default is the honest answer, and it is the same number.
    if (v.index >= 0) {
        const ScenarioDesc& s = desc_.scenarios[run.scenario];
        const auto i = static_cast<std::size_t>(v.index);
        if (i < s.params.size()) {
            return s.params[i].value;
        }
    }
    return v.literal;
}

float Staging::parameter(std::string_view scenario, std::string_view name) const {
    for (const Run& run : runs_) {
        const ScenarioDesc& s = desc_.scenarios[run.scenario];
        if (s.name != scenario) {
            continue;
        }
        for (std::size_t i = 0; i < s.params.size(); ++i) {
            if (s.params[i].name == name) {
                return i < run.params.size() && run.params[i] != nullptr ? run.params[i]->value()
                                                                        : s.params[i].value;
            }
        }
    }
    return 0.0f;
}

bool Staging::setParameter(std::string_view scenario, std::string_view name, float v) {
    for (Run& run : runs_) {
        const ScenarioDesc& s = desc_.scenarios[run.scenario];
        if (s.name != scenario) {
            continue;
        }
        for (std::size_t i = 0; i < s.params.size(); ++i) {
            if (s.params[i].name == name && i < run.params.size() && run.params[i] != nullptr) {
                const float clamped = std::clamp(v, s.params[i].min, s.params[i].max);
                // Base *and* final. The base is what a save writes and what the next
                // `resetFinals` restores; the final is what everything reads this frame, and a
                // caller that set a knob and read back the value it replaced would be a caller
                // debugging the wrong thing.
                run.params[i]->setBase(clamped);
                run.params[i]->setFinalComponent(0, clamped);
                return true;
            }
        }
    }
    return false;
}

// ---- the sequencer's API ------------------------------------------------------------------------

bool Staging::start(std::string_view scenario, double now) {
    for (Run& run : runs_) {
        if (desc_.scenarios[run.scenario].name != scenario) {
            continue;
        }
        run.running = true;
        run.beat = 0;
        run.entered = false;
        run.cycle = 0;
        run.beatStart = now;
        run.bindings.clear();
        run.cues.clear();
        const ScenarioDesc& s = desc_.scenarios[run.scenario];
        const std::uint32_t seed =
            s.seed != 0 ? s.seed : nameSeed(s.name);
        run.rng = Rng(seed);
        events_.push_back(StageEvent{.kind = StageEventKind::Started, .scenario = s.name, .time = now});
        return true;
    }
    return false;
}

bool Staging::stop(std::string_view scenario, double now) {
    for (Run& run : runs_) {
        if (desc_.scenarios[run.scenario].name != scenario || !run.running) {
            continue;
        }
        StageContext ctx;
        ctx.time = now;
        // The world the last update ran against, so a cancellation actually lets the bodies go. A
        // `stop` that left a cow held twenty metres in the air by a shot that is over would be the
        // "permanently running/broken state" the brief asks the director not to end up in.
        ctx.world = lastWorld_;
        ctx.params = lastParams_;
        finish(run, ctx, StageEventKind::Cancelled);
        return true;
    }
    return false;
}

bool Staging::trigger(std::string_view scenario, std::string_view beatName, double now) {
    for (Run& run : runs_) {
        const ScenarioDesc& s = desc_.scenarios[run.scenario];
        if (s.name != scenario) {
            continue;
        }
        for (std::size_t i = 0; i < s.beats.size(); ++i) {
            if (s.beats[i].name != beatName) {
                continue;
            }
            run.running = true;
            run.beat = i;
            run.entered = false;
            run.beatStart = now;
            run.cues.clear();
            return true;
        }
        return false;
    }
    return false;
}

bool Staging::running(std::string_view scenario) const {
    for (const Run& run : runs_) {
        if (desc_.scenarios[run.scenario].name == scenario) {
            return run.running;
        }
    }
    return false;
}

int Staging::cycles(std::string_view scenario) const {
    for (const Run& run : runs_) {
        if (desc_.scenarios[run.scenario].name == scenario) {
            return run.cycle;
        }
    }
    return 0;
}

std::string_view Staging::beat(std::string_view scenario) const {
    for (const Run& run : runs_) {
        const ScenarioDesc& s = desc_.scenarios[run.scenario];
        if (s.name == scenario && run.running && run.beat < s.beats.size()) {
            return s.beats[run.beat].name;
        }
    }
    return {};
}

std::string_view Staging::binding(std::string_view scenario, std::string_view role) const {
    for (const Run& run : runs_) {
        if (desc_.scenarios[run.scenario].name != scenario) {
            continue;
        }
        for (const Binding& b : run.bindings) {
            if (b.role == role) {
                return b.entity;
            }
        }
    }
    return {};
}

// ---- roles --------------------------------------------------------------------------------------

std::string Staging::resolveName(const Run& run, std::string_view role) const {
    if (role.empty()) {
        return {};
    }
    // "<role>.<part>": the actor's named part. The whole of why a step can say `actor.beam` without
    // knowing the beam's entity is called `visitor-beam`.
    const std::size_t dot = role.find('.');
    const std::string_view head = dot == std::string_view::npos ? role : role.substr(0, dot);
    const std::string_view part = dot == std::string_view::npos ? std::string_view{} : role.substr(dot + 1);

    std::string actorName;
    std::string entityName;
    for (const Binding& b : run.bindings) {
        if (b.role == head) {
            actorName = b.actor;
            entityName = b.entity;
            break;
        }
    }
    if (entityName.empty() && actorName.empty()) {
        // Not a bound role: take it as a literal entity name, which is what a hand-written cue that
        // names a prop directly wants.
        entityName = std::string(head);
    }
    if (part.empty()) {
        return entityName;
    }
    if (actorName.empty()) {
        return {};
    }
    for (const ActorDesc& a : desc_.actors) {
        if (a.name != actorName) {
            continue;
        }
        for (const ActorPart& p : a.parts) {
            if (p.name == part) {
                return p.entity;
            }
        }
    }
    return {};
}

entity::Entity* Staging::resolve(const Run& run, std::string_view role,
                                 const StageContext& ctx) const {
    if (ctx.world == nullptr) {
        return nullptr;
    }
    const std::string name = resolveName(run, role);
    return name.empty() ? nullptr : ctx.world->find(name);
}

void Staging::bindRole(Run& run, const std::string& role, const std::string& entity,
                       const std::string& actor) {
    for (Binding& b : run.bindings) {
        if (b.role == role) {
            b.entity = entity;
            b.actor = actor;
            return;
        }
    }
    run.bindings.push_back(Binding{.role = role, .actor = actor, .entity = entity});
}

// ---- claims -------------------------------------------------------------------------------------

bool Staging::claimed(std::string_view entity) const {
    return std::any_of(claims_.begin(), claims_.end(),
                       [&](const Claim& c) { return c.entity == entity; });
}

// Is this body one the director owns -- an actor, or one of an actor's parts?
//
// Glowmere's tractor beam is authored *invisible* and shown for the four seconds it fires, so a rule
// about invisibility that does not except the director's own bodies retires the beam. That cost two
// tests: the saucer beamed at nothing, and a camera test lost the precondition it was built around.
bool Staging::isDirectorsOwn(std::string_view name) const {
    for (const ActorDesc& a : desc_.actors) {
        if (a.driven() == name) {
            return true;
        }
        for (const ActorPart& p : a.parts) {
            if (p.entity == name || p.name == name) {
                return true;
            }
        }
    }
    return false;
}

// Would anybody see this body if the camera looked at it?
//
// Reported as "once that animal has been abducted it becomes invisible; the UFO should never target
// invisible animals". Within one session `isRetired` already covered it. What it did not survive is
// a **reload**: `retired_` is runtime state and `visible` is saved, so reopening a project whose
// cows had all been abducted gave a fresh scenario an empty retired list and a farm of invisible
// animals it was perfectly willing to fly to, hover over and beam at nothing.
//
// The **base**, not the final, and that is the whole reason this is safe to ask per query. A `hide`
// step writes the base (`writeParameter` ends in `setBaseComponent`) and the base is what a project
// file carries, so this reads "somebody hid this and it stayed hidden". The final additionally
// carries whatever a modulation route is doing this instant, and filtering on that would make the
// director's choice of subject depend on the frame it happened to ask on.
bool Staging::hiddenForGood(const entity::Entity& e, const StageContext& ctx) const {
    if (ctx.params == nullptr || ctx.world == nullptr || isDirectorsOwn(e.name())) {
        return false;
    }
    std::vector<std::string> tried;
    const std::string path = ctx.world->resolveTarget(e, "visible", "entity/", &tried);
    if (path.empty()) {
        return false; // nothing can hide it, so it is not hidden
    }
    const params::IParameter* p = ctx.params->find(path);
    return p != nullptr && p->baseComponent(0) <= 0.5f;
}

bool Staging::isRetired(std::string_view entity) const {
    return std::find(retired_.begin(), retired_.end(), entity) != retired_.end();
}

void Staging::releaseClaims(const Run& run) {
    const std::string holder = desc_.scenarios[run.scenario].name + "/";
    claims_.erase(std::remove_if(claims_.begin(), claims_.end(),
                                 [&](const Claim& c) {
                                     return c.holder.compare(0, holder.size(), holder) == 0;
                                 }),
                  claims_.end());
}

// ---- the candidate index ------------------------------------------------------------------------

void Staging::refreshCandidates(const StageContext& ctx) {
    if (ctx.world == nullptr) {
        return;
    }
    const ScenarioDesc* soonest = nullptr;
    for (const Run& run : runs_) {
        if (run.running) {
            const ScenarioDesc& s = desc_.scenarios[run.scenario];
            if (soonest == nullptr || s.searchInterval < soonest->searchInterval) {
                soonest = &s;
            }
        }
    }
    const double interval = soonest != nullptr ? std::max(soonest->searchInterval, 0.0) : 0.5;
    if (!needSearch_ && ctx.time - lastSearch_ < interval) {
        return;
    }
    lastSearch_ = ctx.time;
    needSearch_ = false;

    candidates_.clear();
    candidatePoints_.clear();
    const auto& entities = ctx.world->entities();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const entity::Entity& e = *entities[i];
        // Only the entities a query could possibly want. An empty tag list means the description
        // asks no tag question at all, and then every entity is a candidate -- which is honest and
        // is also the case that never arises in a scene big enough for it to matter.
        bool wanted = searchTags_.empty();
        for (const std::string& tag : searchTags_) {
            const auto& tags = e.desc().tags;
            if (std::find(tags.begin(), tags.end(), tag) != tags.end() || e.desc().profile == tag) {
                wanted = true;
                break;
            }
        }
        if (!wanted) {
            continue;
        }
        candidates_.push_back(Candidate{.name = e.name(), .position = e.state().position(), .entity = i});
        candidatePoints_.push_back(e.state().position());
    }
    grid_.build(candidatePoints_, kCellSize);
    ++report_.searches;
    report_.candidates = candidates_.size();
}

// ---- queries ------------------------------------------------------------------------------------

bool Staging::runQuery(Run& run, const QueryDesc& query, const StageContext& ctx) {
    ++report_.queries;
    if (ctx.world == nullptr) {
        return false;
    }
    // FindEntity: an exact name, which does not want an index at all.
    if (!query.name.empty()) {
        const entity::Entity* e = ctx.world->find(query.name);
        if (e == nullptr || (query.excludeRetired && isRetired(query.name)) ||
            (query.excludeClaimed && claimed(query.name))) {
            ++report_.unbound;
            return false;
        }
        bindRole(run, query.bind, query.name, {});
        if (query.claim) {
            claims_.push_back(
                Claim{.entity = query.name,
                      .holder = desc_.scenarios[run.scenario].name + "/" + query.bind});
        }
        ++report_.bound;
        return true;
    }

    glm::vec3 center = query.center;
    if (!query.from.empty()) {
        const entity::Entity* origin = resolve(run, query.from, ctx);
        if (origin == nullptr) {
            ++report_.unbound;
            return false;
        }
        center = origin->state().position();
    }
    const float radius = value(run, query.radius);
    const float minRadius = value(run, query.minRadius);
    const float clearance = value(run, query.clearance);
    const entity::Navigator& nav = ctx.world->navigator();

    // The disc query, over the cached index rather than over the scene. When the radius is 0 the
    // query is over the candidate set as a whole, which is still bounded by the tag filter.
    hits_.clear();
    if (radius > 0.0f) {
        grid_.query(center, radius, hits_);
    } else {
        hits_.resize(candidates_.size());
        for (std::uint32_t i = 0; i < hits_.size(); ++i) {
            hits_[i] = i;
        }
    }

    std::size_t best = candidates_.size();
    float bestScore = query.pick == Pick::Farthest ? -1.0f : std::numeric_limits<float>::max();
    std::size_t matches = 0;
    for (const std::uint32_t hit : hits_) {
        const Candidate& c = candidates_[hit];
        ++report_.tested;
        if (query.excludeRetired && isRetired(c.name)) {
            continue;
        }
        if (query.excludeClaimed && claimed(c.name)) {
            continue;
        }
        const entity::Entity* e = ctx.world->entities()[c.entity].get();
        if (e == nullptr || e->name() != c.name) {
            continue; // the entity set changed under the index; it rebuilds next interval
        }
        if (hiddenForGood(*e, ctx)) {
            continue; // nobody can see it; it is not a thing to make a shot about
        }
        if (!query.tag.empty()) {
            const auto& tags = e->desc().tags;
            if (std::find(tags.begin(), tags.end(), query.tag) == tags.end() &&
                e->desc().profile != query.tag) {
                continue;
            }
        }
        // Live position, not the indexed one: the index is a search structure, and an answer that
        // was true half a second ago is the wrong place to fly to.
        const glm::vec3 p = e->state().position();
        const float distance = glm::length(p - center);
        if (radius > 0.0f && distance > radius) {
            continue;
        }
        if (minRadius > 0.0f && distance < minRadius) {
            continue;
        }
        if (clearance > 0.0f && nav.valid() && nav.canopyHeight(flat(p)) > clearance) {
            continue; // under a tree: the beam would not reach and the shot would not read
        }
        if (query.requireNavigable && nav.valid() && !nav.navigable(flat(p))) {
            continue;
        }
        ++matches;
        float score = 0.0f;
        switch (query.pick) {
        case Pick::Nearest: score = distance; break;
        case Pick::Farthest: score = -distance; break;
        // A stable key rather than a draw per candidate, so the winner does not depend on how many
        // candidates happened to be examined before it.
        case Pick::Random:
            score = static_cast<float>((run.rng.nextU32() ^ nameSeed(c.name)) & 0xFFFFu);
            break;
        case Pick::First: score = static_cast<float>(hit); break;
        }
        if (score < bestScore) {
            bestScore = score;
            best = hit;
        }
    }
    (void)matches;
    if (best >= candidates_.size()) {
        ++report_.unbound;
        return false;
    }
    const std::string& won = candidates_[best].name;
    bindRole(run, query.bind, won, {});
    if (query.claim) {
        claims_.push_back(Claim{.entity = won,
                                .holder = desc_.scenarios[run.scenario].name + "/" + query.bind});
    }
    ++report_.bound;
    return true;
}

// ---- points -------------------------------------------------------------------------------------

bool Staging::resolvePoint(const Run& run, const StepDesc& step, const StageContext& ctx,
                           glm::vec3& out) const {
    // `Visual` is where the body is *drawn* -- the simulation's number plus the behaviours' offsets
    // the entity layer folds onto the node afterwards. A step that has to line up with something
    // parented to that node (a tractor beam) must measure from the drawn place, not the simulated
    // one; see the note on `Anchor`.
    const auto placeOf = [&](const entity::Entity& e) {
        return step.anchor == Anchor::Visual ? e.visualPosition() : e.state().position();
    };
    glm::vec3 base(0.0f);
    if (!step.toRole.empty()) {
        const entity::Entity* target = resolve(run, step.toRole, ctx);
        if (target == nullptr) {
            return false;
        }
        base = placeOf(*target);
    } else if (step.relative) {
        const entity::Entity* self = resolve(run, step.role, ctx);
        if (self == nullptr) {
            return false;
        }
        base = placeOf(*self);
    }
    out = base + step.point;
    const float height = value(run, step.height);
    if (step.aboveGround && ctx.world != nullptr && ctx.world->navigator().valid()) {
        out.y = ctx.world->navigator().groundHeight(flat(out)) + height;
    } else {
        out.y += height;
    }
    return true;
}

// ---- parameters a step writes ---------------------------------------------------------------------

// `role` is the *resolved* role -- the step's own, or the cue's when the step named none. Taking
// the step's raw field here was a bug that made every `show`, `hide` and `set` in a cue that relied
// on the cue's role fail silently, and the test that should have caught it passed for a different
// reason (a bool parameter whose fixture gave it a hard range of [false, false], so "hidden" was
// already what it held). ADR-182: a probe that cannot fail proves nothing.
std::string Staging::parameterPath(const Run& run, std::string_view role, std::string_view what,
                                   const StageContext& ctx) const {
    std::string target = what.empty() ? std::string("visible") : std::string(what);
    if (target.find('/') != std::string::npos) {
        return target; // an absolute path, written as the author wrote it
    }
    if (ctx.world == nullptr) {
        return {};
    }
    const entity::Entity* e = resolve(run, role, ctx);
    if (e == nullptr) {
        return {};
    }
    // The same resolution an entity reaction's target gets, so "spawnRate" on a particle node finds
    // `particles/<node>/spawnRate` and "visible" finds `nodes/<node>/visible` -- one rule, and the
    // director does not get its own spelling of it.
    std::vector<std::string> tried;
    std::string path = ctx.world->resolveTarget(*e, target, "entity/", &tried);
    return path;
}

void Staging::writeParameter(const Run& run, std::string_view role, const std::string& path,
                             float v, const StageContext& ctx) {
    if (ctx.params == nullptr || path.empty()) {
        return;
    }
    params::IParameter* p = ctx.params->find(path);
    if (p == nullptr) {
        const std::string message = fmt::format("staging: no parameter '{}'", path);
        if (std::find(problems_.begin(), problems_.end(), message) == problems_.end()) {
            problems_.push_back(message);
            log::warn("{}", message);
        }
        return;
    }
    // Remember the authored value the first time this director touches a parameter, so `reset`
    // can put the scene back exactly as the file described it. Writing the *base* rather than the
    // final, because finals are rebuilt from bases every frame and a beam that had to be re-lit
    // sixty times a second would be a beam nothing else could modulate.
    const bool known = std::any_of(written_.begin(), written_.end(),
                                   [&](const Written& w) { return w.path == path; });
    if (!known) {
        Written w;
        w.path = path;
        for (std::size_t c = 0; c < p->componentCount(); ++c) {
            w.base.push_back(p->baseComponent(c));
        }
        // Who reached it, so the Inspector can say so (ADR-241). Recorded on the first write
        // alongside the authored base, because that is the moment the fact is known and the two
        // have exactly the same lifetime -- `reset` clears both.
        w.scenario = desc_.scenarios[run.scenario].name;
        w.role = std::string(role);
        written_.push_back(std::move(w));
    }
    p->setBaseComponent(0, v);
}

// ---- "why is this parameter moving?" --------------------------------------------------------------

std::vector<Staging::PathWriter> Staging::writersOf(std::string_view path) const {
    std::vector<PathWriter> out;
    // One entry per scenario, because this answers "what can write this" and a scenario that both
    // declares an absolute target and has since written it is one answer, not two. Deliberately not
    // keyed on the role as well: a `set` step naming an absolute path still carries its cue's role,
    // so keying on the pair would report the same scenario twice.
    const auto already = [&out](std::string_view scenario) {
        return std::any_of(out.begin(), out.end(),
                           [&](const PathWriter& w) { return w.scenario == scenario; });
    };
    const auto isRunning = [this](std::string_view scenario) {
        return std::any_of(runs_.begin(), runs_.end(), [&](const Run& r) {
            return r.running && desc_.scenarios[r.scenario].name == scenario;
        });
    };

    // Observed. A role-relative target can only be known this way: the path it resolves to does not
    // exist until the role binds to a body.
    for (const Written& w : written_) {
        if (w.path != path) {
            continue;
        }
        PathWriter pw;
        pw.path = w.path;
        pw.scenario = w.scenario;
        pw.role = w.role;
        pw.running = isRunning(w.scenario);
        pw.live = true;
        out.push_back(std::move(pw));
    }

    // Declared. A step that names an absolute path is answerable from the description alone, which
    // is the whole point -- a beam nobody has fired yet still has a director that can hide it, and
    // "nothing modulates this" would be the wrong answer to give about it.
    for (const ScenarioDesc& sc : desc_.scenarios) {
        for (const BeatDesc& beat : sc.beats) {
            for (const CueDesc& cue : beat.cues) {
                for (const StepDesc& step : cue.steps) {
                    if (step.kind != StepKind::Show && step.kind != StepKind::Hide &&
                        step.kind != StepKind::Set) {
                        continue;
                    }
                    if (step.target.find('/') == std::string::npos || step.target != path) {
                        continue;
                    }
                    if (already(sc.name)) {
                        continue;
                    }
                    PathWriter pw;
                    pw.path = std::string(path);
                    pw.scenario = sc.name;
                    pw.running = isRunning(sc.name);
                    pw.live = false;
                    out.push_back(std::move(pw));
                }
            }
        }
    }
    return out;
}

// ---- the frame ------------------------------------------------------------------------------------

void Staging::emit(StageEventKind kind, const Run& run, const StageContext& ctx, std::string step,
                   std::string role, std::string detail) {
    const ScenarioDesc& s = desc_.scenarios[run.scenario];
    StageEvent e;
    e.kind = kind;
    e.scenario = s.name;
    e.beat = run.beat < s.beats.size() ? s.beats[run.beat].name : std::string{};
    e.step = std::move(step);
    e.role = std::move(role);
    e.detail = std::move(detail);
    e.time = ctx.time;
    if (log_.size() < logLimit_) {
        log_.push_back(e);
    }
    events_.push_back(std::move(e));
}

void Staging::finish(Run& run, const StageContext& ctx, StageEventKind why) {
    // Release everything before the flags go down, so a scenario that ended -- for any reason,
    // including a cancellation from the sequencer -- leaves no cow claimed by a shot that is over.
    // This is the whole of "do not leave the director in a permanently running/broken state".
    releaseClaims(run);
    for (const Binding& b : run.bindings) {
        if (ctx.world != nullptr) {
            if (entity::Entity* e = ctx.world->find(b.entity); e != nullptr) {
                e->clearDirectorMotion();
                e->actions().cancel(entity::Authority::Director, ctx.time);
            }
        }
    }
    run.running = false;
    run.entered = false;
    run.cues.clear();
    emit(why, run, ctx, {}, {}, {});
    run.bindings.clear();
}

void Staging::enterBeat(Run& run, const StageContext& ctx) {
    const ScenarioDesc& scenario = desc_.scenarios[run.scenario];
    const BeatDesc& beat = scenario.beats[run.beat];
    run.beatStart = ctx.time;
    run.cues.assign(beat.cues.size(), CueRun{});
    for (CueRun& cue : run.cues) {
        cue.startedAt = ctx.time;
    }
    run.entered = true;
    emit(StageEventKind::Beat, run, ctx, {}, {}, {});

    for (const QueryDesc& query : beat.find) {
        if (runQuery(run, query, ctx)) {
            emit(StageEventKind::Bound, run, ctx, {}, query.bind, resolveName(run, query.bind));
            continue;
        }
        emit(StageEventKind::Unbound, run, ctx, {}, query.bind, {});
        // A failed find is a normal outcome, not a fault: there may simply be no cow left. It takes
        // `otherwise`, and an empty `otherwise` ends the cycle -- which leaves the scenario idle and
        // restartable rather than stuck in a beat it can never satisfy.
        run.entered = false;
        run.cues.clear();
        if (!beat.otherwise.empty()) {
            for (std::size_t i = 0; i < scenario.beats.size(); ++i) {
                if (scenario.beats[i].name == beat.otherwise) {
                    run.beat = i;
                    return;
                }
            }
        }
        releaseClaims(run);
        finish(run, ctx, StageEventKind::Finished);
        return;
    }
}

void Staging::leaveBeat(Run& run, const StageContext& ctx) {
    const ScenarioDesc& scenario = desc_.scenarios[run.scenario];
    const BeatDesc& beat = scenario.beats[run.beat];
    if (beat.release) {
        releaseClaims(run);
    }
    run.entered = false;
    run.cues.clear();
    if (!beat.then.empty()) {
        for (std::size_t i = 0; i < scenario.beats.size(); ++i) {
            if (scenario.beats[i].name == beat.then) {
                run.beat = i;
                return;
            }
        }
    }
    ++run.beat;
    if (run.beat < scenario.beats.size()) {
        return;
    }
    // A cycle. The brief's Repeat, and the place its "Maximum Abductions" is enforced.
    ++run.cycle;
    ++report_.cycles;
    emit(StageEventKind::Cycled, run, ctx, {}, {}, {});
    if (scenario.maxCycles > 0 && run.cycle >= scenario.maxCycles) {
        finish(run, ctx, StageEventKind::Finished);
        return;
    }
    run.beat = 0;
}

Staging::StepStatus Staging::advance(Run& run, CueRun& cue, const CueDesc& desc,
                                     const StepDesc& step, const StageContext& ctx) {
    const std::string_view role = step.role.empty() ? std::string_view(desc.role)
                                                    : std::string_view(step.role);
    entity::Entity* self = resolve(run, role, ctx);
    const double duration = static_cast<double>(value(run, step.duration));
    const double elapsed = ctx.time - cue.startedAt;

    switch (step.kind) {
    case StepKind::Wait:
        return elapsed >= duration ? StepStatus::Done : StepStatus::Running;

    case StepKind::MoveTo: {
        if (self == nullptr) {
            return StepStatus::Failed;
        }
        glm::vec3 goal(0.0f);
        if (!resolvePoint(run, step, ctx, goal)) {
            return StepStatus::Failed; // the target went away mid-approach
        }
        if (step.travel == Travel::Walk) {
            // Delegated to ADR-096 entirely: one `move` on the Director tier, routed, steered and
            // gaited by the layer that already does all three.
            if (!cue.issued) {
                entity::ActionDesc move;
                move.kind = entity::ActionKind::Move;
                move.name = step.name.empty() ? "move" : step.name;
                move.target.kind = entity::TargetKind::Point;
                move.target.point = goal;
                move.speed = value(run, step.speed);
                move.tolerance = value(run, step.tolerance);
                move.duration = duration;
                ctx.world->direct(self->name(), {std::move(move)}, ctx.time);
                cue.issued = true;
                return StepStatus::Running;
            }
            if (self->actions().pending(entity::Authority::Director) > 0) {
                if (actionFailed(*ctx.world, self->name(), cue.reason)) {
                    return StepStatus::Failed; // unreachable, blocked, stuck or out of time
                }
                return StepStatus::Running;
            }
            return actionFailed(*ctx.world, self->name(), cue.reason) ? StepStatus::Failed
                                                                     : StepStatus::Done;
        }

        // A director tween. `from` is captured once so the path is a lerp with easing rather than a
        // chase that decelerates for ever, and the goal is re-read every frame so a moving target
        // is followed rather than aimed at where it used to be.
        if (!cue.started) {
            cue.from = self->state().position();
            cue.span = glm::length(goal - cue.from);
            cue.started = true;
        }
        // Both knobs, and both mean something. `speed` is how fast it travels; `duration` is the
        // *least* time the move may take. A shot that says "26 m/s" and "no less than nine seconds"
        // gets a craft that crosses two hundred metres at speed and crosses twenty metres without
        // looking like it teleported -- which is the difference between the brief's "Travel Speed"
        // and its "Approach Duration / smoothing", and the reason it asked for both.
        const float speed = value(run, step.speed);
        float rate = 0.0f;
        if (speed > 0.0f) {
            rate = speed / std::max(cue.span, 0.01f);
        }
        if (duration > 0.0) {
            const auto cap = static_cast<float>(1.0 / duration);
            rate = rate > 0.0f ? std::min(rate, cap) : cap;
        }
        if (rate <= 0.0f) {
            rate = 1.0e9f; // neither was set: put it there
        }
        cue.progress = std::min(1.0f, cue.progress + static_cast<float>(ctx.dt) * rate);
        const float eased = smoothstep(cue.progress);
        glm::vec3 p = glm::mix(cue.from, goal, eased);

        cue.phase += ctx.dt;
        const float wobble = value(run, step.wobble);
        if (wobble > 0.0f) {
            const float rate = value(run, step.wobbleRate);
            const auto t = static_cast<float>(cue.phase) * rate * 2.0f * kPi;
            p.x += std::sin(t) * wobble;
            p.z += std::cos(t * 0.73f) * wobble;
        }
        // Never closer to the ground than the shot said. The brief's "avoid terrain collisions",
        // asked of the navigation layer's own height query rather than hoped for.
        const float clearance = value(run, step.clearance);
        if (clearance > 0.0f && ctx.world->navigator().valid()) {
            p.y = std::max(p.y, ctx.world->navigator().groundHeight(flat(p)) + clearance);
        }

        entity::DirectorMotion motion;
        motion.active = true;
        motion.position = p;
        const glm::vec3 heading = goal - self->state().position();
        if (glm::length(glm::vec2(heading.x, heading.z)) > 0.5f) {
            const float wanted = std::atan2(heading.x, heading.z);
            const float delta = angleDelta(self->state().yaw, wanted);
            // Turn towards the destination at a bounded rate, so a craft banks into its course
            // instead of snapping onto it. 90 deg/s is a craft, not a dancer.
            const float turn = std::clamp(delta, -1.57f * static_cast<float>(ctx.dt),
                                          1.57f * static_cast<float>(ctx.dt));
            motion.yaw = self->state().yaw + turn;
            motion.hasYaw = true;
        }
        const float spin = value(run, step.spin);
        if (spin != 0.0f) {
            motion.rotation.y = std::fmod(static_cast<float>(cue.phase) * spin, 360.0f);
        }
        const float gaitSpeed = value(run, step.rate);
        if (gaitSpeed > 0.0f) {
            motion.speed = gaitSpeed;
            motion.hasSpeed = true;
        }
        self->setDirectorMotion(motion);
        return cue.progress >= 1.0f ? StepStatus::Done : StepStatus::Running;
    }

    case StepKind::Follow: {
        if (self == nullptr) {
            return StepStatus::Failed;
        }
        glm::vec3 goal(0.0f);
        // `hold` resolves the station once and keeps it. Re-reading it every frame is what makes a
        // `follow` follow, and is exactly wrong when the thing being followed is itself taking its
        // position from this body: see the note on `StepDesc::hold`.
        if (step.hold && cue.started) {
            goal = cue.from;
        } else {
            if (!resolvePoint(run, step, ctx, goal)) {
                return StepStatus::Failed;
            }
            if (step.hold) {
                cue.from = goal;
                cue.started = true;
            }
        }
        cue.phase += ctx.dt;
        glm::vec3 p = goal;
        const float wobble = value(run, step.wobble);
        if (wobble > 0.0f) {
            const float wrate = value(run, step.wobbleRate);
            const auto t = static_cast<float>(cue.phase) * wrate * 2.0f * kPi;
            p.x += std::sin(t) * wobble;
            p.z += std::cos(t * 0.73f) * wobble;
        }
        const float clearance = value(run, step.clearance);
        if (clearance > 0.0f && ctx.world->navigator().valid()) {
            p.y = std::max(p.y, ctx.world->navigator().groundHeight(flat(p)) + clearance);
        }
        entity::DirectorMotion motion;
        motion.active = true;
        motion.position = p;
        const float spin = value(run, step.spin);
        if (spin != 0.0f) {
            motion.rotation.y = std::fmod(static_cast<float>(cue.phase) * spin, 360.0f);
        }
        const float rate = value(run, step.rate);
        if (rate > 0.0f) {
            motion.speed = rate;
            motion.hasSpeed = true;
        }
        self->setDirectorMotion(motion);
        return elapsed >= duration ? StepStatus::Done : StepStatus::Running;
    }

    case StepKind::LookAt: {
        if (self == nullptr) {
            return StepStatus::Failed;
        }
        glm::vec3 goal(0.0f);
        if (!resolvePoint(run, step, ctx, goal)) {
            return StepStatus::Failed;
        }
        const glm::vec3 d = goal - self->state().position();
        if (glm::length(glm::vec2(d.x, d.z)) < 1e-3f) {
            return StepStatus::Done;
        }
        const float wanted = std::atan2(d.x, d.z);
        const float delta = angleDelta(self->state().yaw, wanted);
        const float turnRate = value(run, step.rate) > 0.0f ? value(run, step.rate) / kDegrees : 1.57f;
        const float turn = std::clamp(delta, -turnRate * static_cast<float>(ctx.dt),
                                      turnRate * static_cast<float>(ctx.dt));
        // Keeps whatever hold the director already had on this body -- a `lookAt` turns a head, it
        // does not move a craft -- and takes one over at wherever the body is when it has none.
        entity::DirectorMotion motion = self->directorMotion();
        if (!motion.active) {
            motion.position = self->state().position();
        }
        motion.active = true;
        motion.yaw = self->state().yaw + turn;
        motion.hasYaw = true;
        self->setDirectorMotion(motion);
        const bool aimed = std::abs(delta) < 0.05f;
        if (duration > 0.0) {
            return elapsed >= duration ? StepStatus::Done : StepStatus::Running;
        }
        return aimed ? StepStatus::Done : StepStatus::Running;
    }

    case StepKind::Play: {
        if (self == nullptr) {
            return StepStatus::Failed;
        }
        // An activity name, never a clip name -- the same indirection ADR-096 insists on, through
        // the same `EntityDesc::clips` table, so one cue drives an alien, a deer and a cow.
        if (!cue.issued) {
            entity::ActionDesc pose;
            pose.kind = entity::ActionKind::Pose;
            pose.name = step.name.empty() ? "play" : step.name;
            pose.activity = step.activity;
            pose.duration = duration;
            ctx.world->direct(self->name(), {std::move(pose)}, ctx.time);
            cue.issued = true;
        }
        if (duration <= 0.0) {
            return StepStatus::Done;
        }
        return elapsed >= duration ? StepStatus::Done : StepStatus::Running;
    }

    case StepKind::Show:
    case StepKind::Hide: {
        const std::string path = parameterPath(run, role, step.target, ctx);
        if (path.empty()) {
            return StepStatus::Failed;
        }
        writeParameter(run, role, path, step.kind == StepKind::Show ? 1.0f : 0.0f, ctx);
        return StepStatus::Done;
    }

    case StepKind::Set: {
        const std::string path = parameterPath(run, role, step.target, ctx);
        if (path.empty()) {
            return StepStatus::Failed;
        }
        const float to = value(run, step.to);
        if (duration <= 0.0) {
            writeParameter(run, role, path, to, ctx);
            return StepStatus::Done;
        }
        if (!cue.started) {
            cue.started = true;
            if (step.hasFrom) {
                cue.span = value(run, step.from);
            } else if (ctx.params != nullptr) {
                const params::IParameter* p = ctx.params->find(path);
                cue.span = p != nullptr ? p->baseComponent(0) : to;
            } else {
                cue.span = to;
            }
        }
        const auto u = static_cast<float>(std::clamp(elapsed / duration, 0.0, 1.0));
        writeParameter(run, role, path, glm::mix(cue.span, to, u), ctx);
        return u >= 1.0f ? StepStatus::Done : StepStatus::Running;
    }

    case StepKind::Actions: {
        if (self == nullptr) {
            return StepStatus::Failed;
        }
        if (!cue.issued) {
            ctx.world->direct(self->name(), step.actions, ctx.time);
            cue.issued = true;
            return StepStatus::Running;
        }
        if (self->actions().pending(entity::Authority::Director) > 0) {
            if (duration > 0.0 && elapsed >= duration) {
                self->actions().cancel(entity::Authority::Director, ctx.time);
                return StepStatus::Done;
            }
            if (actionFailed(*ctx.world, self->name(), cue.reason)) {
                return StepStatus::Failed;
            }
            return StepStatus::Running;
        }
        return actionFailed(*ctx.world, self->name(), cue.reason) ? StepStatus::Failed
                                                                  : StepStatus::Done;
    }

    case StepKind::Attach: {
        if (self == nullptr) {
            return StepStatus::Failed;
        }
        const std::string what = resolveName(run, step.toRole);
        if (what.empty()) {
            return StepStatus::Failed;
        }
        return self->attach(what, step.socket) ? StepStatus::Done : StepStatus::Failed;
    }

    case StepKind::Detach: {
        if (self == nullptr) {
            return StepStatus::Failed;
        }
        const std::string what = resolveName(run, step.toRole);
        if (what.empty()) {
            return StepStatus::Failed;
        }
        self->detach(what);
        return StepStatus::Done;
    }

    case StepKind::Release: {
        const std::string name = resolveName(run, role);
        claims_.erase(std::remove_if(claims_.begin(), claims_.end(),
                                     [&](const Claim& c) { return c.entity == name; }),
                      claims_.end());
        if (self != nullptr) {
            self->clearDirectorMotion();
        }
        return StepStatus::Done;
    }

    case StepKind::Retire: {
        const std::string name = resolveName(run, role);
        if (name.empty()) {
            return StepStatus::Failed;
        }
        if (self != nullptr) {
            // Put the body back under its own behaviours before hiding it, so nothing is left
            // holding a director motion that will never be updated again.
            self->clearDirectorMotion();
            self->actions().cancel(entity::Authority::Director, ctx.time);
            writeParameter(run, role, parameterPath(run, role, {}, ctx), 0.0f, ctx);
        }
        claims_.erase(std::remove_if(claims_.begin(), claims_.end(),
                                     [&](const Claim& c) { return c.entity == name; }),
                      claims_.end());
        if (!isRetired(name)) {
            retired_.push_back(name);
            ++report_.retired;
            needSearch_ = true;
            emit(StageEventKind::Retired, run, ctx, step.name, std::string(role), name);
        }
        return StepStatus::Done;
    }
    }
    return StepStatus::Done;
}

void Staging::update(const StageContext& ctx) {
    events_.clear();
    if (desc_.scenarios.empty() || ctx.world == nullptr) {
        return;
    }
    lastWorld_ = ctx.world;
    lastParams_ = ctx.params;
    bool any = false;
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        const ScenarioDesc& s = desc_.scenarios[i];
        if (!runs_[i].running && s.autoStart && runs_[i].cycle == 0 && runs_[i].bindings.empty() &&
            !runs_[i].entered) {
            start(s.name, ctx.time);
        }
        // The signal seam. Read before the cues run, so a scenario cued on the beat starts on the
        // frame the beat fired rather than the one after it.
        if (ctx.bus != nullptr) {
            // Names resolved once and cached, the same way a behaviour resolves a signal: the bus
            // is an id-indexed array and a name lookup per scenario per frame would be paying for
            // a string hash to read a byte.
            const auto fired = [&](const std::string& name, std::optional<signals::SignalId>& id) {
                if (name.empty()) {
                    return false;
                }
                if (!id.has_value()) {
                    id = ctx.bus->find(name);
                    if (!id.has_value()) {
                        return false;
                    }
                }
                return ctx.bus->event(*id);
            };
            if (runs_[i].running && fired(s.stopOn, runs_[i].stopId)) {
                stop(s.name, ctx.time);
            }
            if (!runs_[i].running && fired(s.startOn, runs_[i].startId)) {
                start(s.name, ctx.time);
            }
        }
        any = any || runs_[i].running;
    }
    if (!any) {
        return;
    }
    refreshCandidates(ctx);

    for (Run& run : runs_) {
        if (!run.running) {
            continue;
        }
        const ScenarioDesc& scenario = desc_.scenarios[run.scenario];
        // The actor is bound once per cycle rather than once per scenario, so a scenario whose
        // actor is chosen by a `find` works the same way as one that names it.
        if (!scenario.actor.empty()) {
            for (const ActorDesc& a : desc_.actors) {
                if (a.name == scenario.actor) {
                    bindRole(run, "actor", a.driven(), a.name);
                    break;
                }
            }
        }
        // A beat may be entered, resolved and left within one update -- a `find` that fails takes
        // its `otherwise` immediately -- so this is a loop with a bound rather than an `if`. The
        // bound is the beat count: a scenario cannot visit more beats in one frame than it has.
        std::size_t guard = scenario.beats.size() + 1;
        while (run.running && !run.entered && guard-- > 0) {
            enterBeat(run, ctx);
        }
        if (!run.running || !run.entered) {
            continue;
        }

        const BeatDesc& beat = scenario.beats[run.beat];
        bool allDone = true;
        for (std::size_t c = 0; c < beat.cues.size() && c < run.cues.size(); ++c) {
            CueRun& cue = run.cues[c];
            const CueDesc& cueDesc = beat.cues[c];
            if (cue.done) {
                continue;
            }
            // A cue advances at most one step per frame boundary: a step that finishes this frame
            // hands over to the next one on the next frame, which keeps a cue's cost bounded and
            // makes "how many frames did this take" a number a test can assert on.
            if (cue.step >= cueDesc.steps.size()) {
                cue.done = true;
                continue;
            }
            const StepDesc& step = cueDesc.steps[cue.step];
            const StepStatus status = advance(run, cue, cueDesc, step, ctx);
            if (status == StepStatus::Running) {
                allDone = false;
                continue;
            }
            if (status == StepStatus::Failed) {
                ++report_.stepsFailed;
                emit(StageEventKind::StepFailed, run, ctx,
                     step.name.empty() ? stepKindName(step.kind) : step.name,
                     step.role.empty() ? cueDesc.role : step.role,
                     cue.reason.empty() ? std::string("failed") : cue.reason);
                // A failed step ends its cue rather than the scenario: the other cues still finish,
                // the beat still ends, and the next beat still runs. A director that stopped dead
                // the first time a target moved would be a director nobody could ship.
                cue.done = true;
                continue;
            }
            ++report_.stepsDone;
            emit(StageEventKind::StepDone, run, ctx,
                 step.name.empty() ? stepKindName(step.kind) : step.name,
                 step.role.empty() ? cueDesc.role : step.role, {});
            ++cue.step;
            cue.started = false;
            cue.issued = false;
            cue.progress = 0.0f;
            cue.phase = 0.0;
            cue.reason.clear();
            cue.startedAt = ctx.time;
            if (cue.step >= cueDesc.steps.size()) {
                cue.done = true;
            } else {
                allDone = false;
            }
        }
        if (allDone) {
            leaveBeat(run, ctx);
        }
    }
}

void Staging::reset(entity::EntityWorld* world, params::ParameterSet* params) {
    StageContext ctx;
    ctx.world = world;
    ctx.params = params;
    for (Run& run : runs_) {
        if (run.running) {
            finish(run, ctx, StageEventKind::Cancelled);
        }
        run.beat = 0;
        run.entered = false;
        run.cycle = 0;
        run.cues.clear();
        run.bindings.clear();
        const ScenarioDesc& s = desc_.scenarios[run.scenario];
        const std::uint32_t seed =
            s.seed != 0 ? s.seed : nameSeed(s.name);
        run.rng = Rng(seed);
    }
    // Put every parameter this director wrote back to the value the scene authored. Without this a
    // seek would leave the beam lit and the cow invisible, and the frame would depend on how the
    // playhead got there -- the defect ADR-093 exists to record.
    if (params != nullptr) {
        for (const Written& w : written_) {
            if (params::IParameter* p = params->find(w.path); p != nullptr) {
                for (std::size_t c = 0; c < w.base.size() && c < p->componentCount(); ++c) {
                    p->setBaseComponent(c, w.base[c]);
                }
            }
        }
    }
    written_.clear();
    claims_.clear();
    retired_.clear();
    events_.clear();
    log_.clear();
    problems_.clear();
    report_ = StageReport{};
    needSearch_ = true;
    lastSearch_ = -1.0e30;
}

} // namespace avgen::stage
