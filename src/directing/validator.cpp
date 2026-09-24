#include "directing/validator.hpp"

#include "directing/compiler.hpp"
#include "directing/text.hpp"
#include "seq/events.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <map>

namespace avgen::directing {
namespace {

constexpr float kDefaultClearance = 0.25f; // metres above an obstacle a jump must pass

struct Collector {
    Validation& v;

    Issue& add(Severity severity, IssueCode code, std::string item, std::string location, std::string message) {
        Issue issue;
        issue.severity = severity;
        issue.code = code;
        issue.item = std::move(item);
        issue.location = std::move(location);
        issue.message = std::move(message);
        if (severity == Severity::Error && !issue.item.empty()) {
            v.blocked.insert(issue.item);
        }
        v.issues.push_back(std::move(issue));
        return v.issues.back();
    }
    Issue& error(IssueCode code, std::string item, std::string location, std::string message) {
        return add(Severity::Error, code, std::move(item), std::move(location), std::move(message));
    }
    Issue& warning(IssueCode code, std::string item, std::string location, std::string message) {
        return add(Severity::Warning, code, std::move(item), std::move(location), std::move(message));
    }
};

// "/shots/2/start" -> the key of shots[2]. How a time or subject issue finds the item it blocks.
std::string itemAt(const Plan& plan, std::string_view location) {
    const auto index = [&](std::string_view prefix) -> std::optional<std::size_t> {
        if (!location.starts_with(prefix)) {
            return std::nullopt;
        }
        std::size_t i = 0;
        std::size_t pos = prefix.size();
        bool any = false;
        while (pos < location.size() && std::isdigit(static_cast<unsigned char>(location[pos])) != 0) {
            i = (i * 10) + static_cast<std::size_t>(location[pos] - '0');
            ++pos;
            any = true;
        }
        return any ? std::optional<std::size_t>(i) : std::nullopt;
    };
    if (auto i = index("/shots/"); i && *i < plan.shots.size()) return plan.shots[*i].key;
    if (auto i = index("/markers/"); i && *i < plan.markers.size()) return plan.markers[*i].key;
    if (auto i = index("/performances/"); i && *i < plan.performances.size()) return plan.performances[*i].key;
    if (auto i = index("/cues/"); i && *i < plan.cues.size()) return plan.cues[*i].key;
    if (auto i = index("/retimes/"); i && *i < plan.retimes.size()) return plan.retimes[*i].key;
    return {};
}

// Every item that names `alias`, so an unresolved subject blocks exactly what depends on it.
std::vector<std::string> itemsNaming(const Plan& plan, const std::string& alias) {
    std::vector<std::string> out;
    for (const PlanShot& s : plan.shots) {
        bool names = s.subject == alias;
        for (const CameraBeat& b : s.camera) {
            names = names || b.subject == alias;
        }
        if (names) {
            out.push_back(s.key);
        }
    }
    for (const PlanPerformance& p : plan.performances) {
        bool names = p.subject == alias;
        for (const PerformanceBeat& b : p.beats) {
            names = names || b.target == alias;
        }
        if (names) {
            out.push_back(p.key);
        }
    }
    for (const PlanCue& c : plan.cues) {
        if (c.effect && c.effect->owner == alias) {
            out.push_back(c.key);
        }
    }
    return out;
}

// True when no issue of `code` concerns `item`.
bool noIssue(const std::vector<Issue>& issues, IssueCode code, const std::string& item) {
    return std::none_of(issues.begin(), issues.end(), [&](const Issue& i) { return i.code == code && i.item == item; });
}

bool positional(SubjectKind kind) {
    return kind == SubjectKind::Entity || kind == SubjectKind::Hero || kind == SubjectKind::Node;
}

const PlanPerformance* performance(const Plan& plan, std::string_view key) {
    for (const PlanPerformance& p : plan.performances) {
        if (p.key == key) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace

// ---- vocabulary shared with the compiler ----------------------------------------------------------

CameraSupport cameraSupport(CameraMove move, SubjectKind subject) {
    switch (move) {
    case CameraMove::LowAngle: return CameraSupport::Modifier;
    case CameraMove::Chase:
    case CameraMove::Follow:
        // A character moves; its position is a simulation's, so the camera must follow its node
        // every frame. A place does not move: following it is holding on it.
        return subject == SubjectKind::Entity ? CameraSupport::FollowRig : CameraSupport::Framing;
    case CameraMove::Hold:
    case CameraMove::Wide:
    case CameraMove::Close:
    case CameraMove::TopDown:
    case CameraMove::Reveal:
    case CameraMove::PushIn:
    case CameraMove::PullOut:
    case CameraMove::Orbit:
        // Framing a character by a move would frame where it stood at the cut, not where it is; that
        // needs the character to be a performance (Slice 2).
        return subject == SubjectKind::Entity ? CameraSupport::Unsupported : CameraSupport::Framing;
    case CameraMove::RiseOver:
    case CameraMove::Pass:
        // Time-varying offsets on a behaviour: Slice 3.
        return CameraSupport::Unsupported;
    }
    return CameraSupport::Unsupported;
}

std::string activityFor(std::string_view action) {
    static const std::map<std::string, std::string, std::less<>> kMap = {
        {"run", "run"},   {"run_to", "run"},   {"run_past", "run"},   {"sprint", "run"},
        {"walk", "walk"}, {"walk_to", "walk"}, {"walk_past", "walk"},
        {"jump", "jump"}, {"land", "land"},    {"fall", "fall"},
        {"hold", "idle"}, {"idle", "idle"},    {"wait", "idle"},      {"stand", "idle"},
        {"turn", "turn"}, {"observe", "observe"}, {"react", "react"},
    };
    if (const auto it = kMap.find(action); it != kMap.end()) {
        return it->second;
    }
    return std::string(action);
}

bool actionClearsTarget(std::string_view action) {
    static const std::vector<std::string_view> kOver = {"jump", "backflip", "frontflip", "flip", "somersault",
                                                        "vault", "leap", "hurdle"};
    return std::find(kOver.begin(), kOver.end(), action) != kOver.end();
}

// ---- effects -------------------------------------------------------------------------------------

ResolvedEffect resolveEffect(const EffectRef& ref, const Plan& plan, const SceneFacts& facts, std::string_view location) {
    ResolvedEffect out;
    const auto fail = [&](IssueCode code, std::string message) -> Issue& {
        Issue issue;
        issue.code = code;
        issue.location = std::string(location);
        issue.subject = ref.id.empty() ? fmt::format("{} on {}", ref.type, ref.owner) : ref.id;
        issue.message = std::move(message);
        out.issue = std::move(issue);
        return *out.issue;
    };
    // The owner: the world, or a subject with a place (ADR-702's Entity owner is "a hero, a
    // composition node" -- a character's node is the node it drives).
    if (ref.owner == "world") {
        out.owner = world::EffectOwner::world();
    } else {
        const Subject* subject = plan.subject(ref.owner);
        if (subject == nullptr || subject->kind == SubjectKind::Unresolved) {
            fail(IssueCode::UnknownSubject, fmt::format("the effect's owner '{}' is not resolved", ref.owner));
            return out;
        }
        std::string node = subject->id;
        for (const SubjectIdentity& i : facts.subjects.identities()) {
            if (i.kind == subject->kind && i.id == subject->id && !i.node.empty()) {
                node = i.node;
            }
        }
        if (subject->kind != SubjectKind::Hero && subject->kind != SubjectKind::Node && subject->kind != SubjectKind::Entity) {
            fail(IssueCode::Unsupported, fmt::format("effects on a {} are not compiled yet; only the world, heroes, "
                                                     "characters and nodes",
                                                     subjectKindName(subject->kind)));
            return out;
        }
        out.owner = world::EffectOwner::entity(node);
    }
    const auto kind = world::effectKindFromName(ref.type);
    if (!kind) {
        Issue& issue = fail(IssueCode::UnknownSubject, fmt::format("'{}' is not an effect type", ref.type));
        std::vector<std::string> types;
        for (const EffectTypeCapability& t : facts.capabilities.effects().types) {
            types.push_back(t.type);
        }
        issue.suggestions = text::nearest(ref.type, types);
        issue.details = {{"types", types}};
        return out;
    }
    out.kind = *kind;
    if (!world::effectAllowedOn(*kind, out.owner.kind)) {
        Issue& issue = fail(IssueCode::CapabilityUnavailable,
                            fmt::format("a {} cannot be attached to {}", ref.type,
                                        out.owner.isWorld() ? std::string("the world") : "'" + out.owner.name + "'"));
        if (const EffectTypeCapability* t = facts.capabilities.effects().type(ref.type)) {
            issue.details = {{"owners", t->owners}};
        }
        return out;
    }
    std::vector<const world::EffectInstance*> matches;
    for (const world::EffectInstance& e : facts.staged.effects) {
        if (!ref.id.empty() ? e.id == ref.id : (e.owner == out.owner && e.kind == *kind)) {
            matches.push_back(&e);
        }
    }
    if (!ref.id.empty() && matches.empty()) {
        fail(IssueCode::UnknownSubject, fmt::format("there is no effect '{}'", ref.id));
        return out;
    }
    if (!ref.id.empty() && (matches.front()->kind != *kind || !(matches.front()->owner == out.owner))) {
        fail(IssueCode::SchemaInvalid, fmt::format("effect '{}' is not a {} on {}", ref.id, ref.type, ref.owner));
        return out;
    }
    if (matches.size() > 1) {
        Issue& issue = fail(IssueCode::AmbiguousReference,
                            fmt::format("{} has {} {} effects; name one by id", ref.owner, matches.size(), ref.type));
        for (const world::EffectInstance* e : matches) {
            issue.suggestions.push_back(e->id);
        }
        return out;
    }
    if (matches.size() == 1) {
        out.id = matches.front()->id;
    }
    return out;
}

// ---- the validator ------------------------------------------------------------------------------

Validation validatePlan(Plan& plan, const SceneFacts& facts) {
    Validation v;
    Collector c{v};

    // ---- references: subjects ------------------------------------------------------------------
    for (Issue& issue : resolvePlanSubjects(plan, facts.subjects)) {
        const std::string alias = issue.subject;
        const std::vector<std::string> items = itemsNaming(plan, alias);
        for (const std::string& item : items) {
            v.blocked.insert(item);
        }
        issue.details["blocks"] = items;
        v.issues.push_back(std::move(issue));
    }
    const auto kindOf = [&](const std::string& alias) {
        const Subject* s = plan.subject(alias);
        return s == nullptr ? SubjectKind::Unresolved : s->kind;
    };
    const auto idOf = [&](const std::string& alias) {
        const Subject* s = plan.subject(alias);
        return s == nullptr ? std::string() : s->id;
    };

    // ---- times -----------------------------------------------------------------------------------
    v.times = resolvePlanTimes(plan, facts.music);
    for (Issue& issue : v.times.issues) {
        issue.item = itemAt(plan, issue.location);
        if (issue.severity == Severity::Error && !issue.item.empty()) {
            v.blocked.insert(issue.item);
        }
        v.issues.push_back(issue);
    }

    // ---- the previous revision: hand edits are never overwritten ------------------------------------
    std::vector<std::string> ownShots;   // seq shots the previous revision made, still as made
    std::vector<std::string> ownMarkers; // "name@time"
    if (const Plan* previous = facts.plan(plan.id); previous != nullptr) {
        for (const ContentRef& ref : previous->produced) {
            const auto now = contentOf(ref, facts.staged);
            if (!now) {
                continue; // deleted by hand since: nothing to overwrite, nothing to protect
            }
            if (!ref.fingerprint.empty() && fingerprint(*now) != ref.fingerprint) {
                Issue& i = c.error(IssueCode::HandEdited, ref.item, {},
                                   fmt::format("{} '{}' was edited by hand after revision {} made it; this revision "
                                               "will not overwrite it",
                                               contentDomainName(ref.domain), ref.id, previous->revision));
                i.subject = ref.id;
                i.suggestions = {"keep the hand edit and drop this item from the plan",
                                 "delete or rename the edited content, then revise again"};
                continue;
            }
            if (ref.domain == ContentDomain::SequenceShot) {
                ownShots.push_back(ref.id);
            } else if (ref.domain == ContentDomain::SequenceMarker) {
                ownMarkers.push_back(ref.id);
            }
        }
    }

    // ---- shots -------------------------------------------------------------------------------------
    const bool eventCameras = std::any_of(facts.staged.cameras.cameras.begin(), facts.staged.cameras.cameras.end(),
                                          [](const scene::CameraRig& r) { return !r.eventScenario.empty(); });
    std::vector<std::pair<double, double>> planned; // placed spans of shots already accepted, for overlap
    for (std::size_t i = 0; i < plan.shots.size(); ++i) {
        const PlanShot& shot = plan.shots[i];
        const std::string at = fmt::format("/shots/{}", i);
        if (!seq::transitionKindFromName(shot.transition)) {
            Issue& issue = c.error(IssueCode::SchemaInvalid, shot.key, at + "/transition",
                                   fmt::format("'{}' is not a transition", shot.transition));
            issue.suggestions = facts.capabilities.cameras().sequenceTransitions;
        }
        if (!shot.subject.empty() && kindOf(shot.subject) != SubjectKind::Unresolved && !positional(kindOf(shot.subject))) {
            c.error(IssueCode::CameraConflict, shot.key, at + "/subject",
                    fmt::format("a shot is about something with a place; '{}' is a {}", shot.subject,
                                subjectKindName(kindOf(shot.subject))));
        }
        if (!shot.rig.empty()) {
            const auto rig = std::find_if(facts.staged.cameras.cameras.begin(), facts.staged.cameras.cameras.end(), [&](const scene::CameraRig& r) {
                return r.name == shot.rig || r.slug == shot.rig;
            });
            if (rig == facts.staged.cameras.cameras.end()) {
                Issue& issue = c.error(IssueCode::UnknownSubject, shot.key, at + "/rig",
                                       fmt::format("there is no camera called '{}'", shot.rig));
                std::vector<std::string> names;
                for (const scene::CameraRig& r : facts.staged.cameras.cameras) {
                    names.push_back(r.name);
                }
                issue.suggestions = text::nearest(shot.rig, names);
            } else if (!shot.camera.empty()) {
                c.warning(IssueCode::Unsupported, shot.key, at + "/camera",
                          fmt::format("the shot names camera '{}', so its camera moves are not compiled", shot.rig));
            }
        }
        // One primary camera move per shot: a move that changes within the shot (chase -> rise ->
        // pass) needs time-varying behaviour state, which is Slice 3's.
        bool primary = false;
        for (std::size_t b = 0; b < shot.camera.size() && shot.rig.empty(); ++b) {
            const CameraBeat& beat = shot.camera[b];
            const std::string subject = beat.subject.empty() ? shot.subject : beat.subject;
            const SubjectKind kind = kindOf(subject);
            const std::string where = fmt::format("{}/camera/{}", at, b);
            if (subject.empty()) {
                c.error(IssueCode::SchemaInvalid, shot.key, where + "/subject",
                        fmt::format("'{}' needs a subject: the shot has none and the move names none", cameraMoveName(beat.move)));
                continue;
            }
            const CameraSupport support = cameraSupport(beat.move, kind);
            if (support == CameraSupport::Modifier) {
                continue;
            }
            if (support == CameraSupport::Unsupported) {
                Issue& issue = c.warning(IssueCode::Unsupported, shot.key, where,
                                         fmt::format("'{}' on {} '{}' is not compiled yet; the shot is built without it",
                                                     cameraMoveName(beat.move), subjectKindName(kind), idOf(subject)));
                issue.cause = (beat.move == CameraMove::RiseOver || beat.move == CameraMove::Pass)
                                  ? "a move whose offset changes over the shot needs time-varying camera behaviour (Slice 3)"
                                  : "framing a moving character needs it to be a scripted performance (Slice 2)";
                continue;
            }
            if (primary) {
                Issue& issue = c.warning(IssueCode::Unsupported, shot.key, where,
                                         fmt::format("a second camera move ('{}') within one shot is not compiled yet; "
                                                     "the first move holds for the whole shot",
                                                     cameraMoveName(beat.move)));
                issue.cause = "a camera that changes move within a shot needs time-varying behaviour (Slice 3)";
                issue.suggestions = {"split the shot at the moment the move changes"};
                continue;
            }
            primary = true;
            if (support == CameraSupport::Framing && facts.place(idOf(subject)) == nullptr) {
                c.error(IssueCode::SpatialInfeasible, shot.key, where,
                        fmt::format("cannot frame '{}': its position is not known", idOf(subject)));
            }
        }

        // Timing: inside the piece, not over another shot. Not for an item a hand edit already
        // keeps: its content stays as the person left it, so nothing new is placed to collide.
        const auto start = v.times.at(at + "/start");
        if (!start || !noIssue(v.issues, IssueCode::HandEdited, shot.key)) {
            continue; // unplaceable (already reported), or kept as the person edited it
        }
        const double end = *start + shot.durationSeconds;
        if (facts.music.durationSeconds > 0.0 && end > facts.music.durationSeconds + 1e-6) {
            c.error(IssueCode::TimeOutOfRange, shot.key, at + "/durationSeconds",
                    fmt::format("the shot ends at {:.3f}s, after the piece ({:.3f}s)", end, facts.music.durationSeconds));
        }
        for (const seq::Shot& existing : facts.staged.sequence.shots) {
            if (std::find(ownShots.begin(), ownShots.end(), existing.name) != ownShots.end()) {
                continue; // this plan's own earlier revision: it is being replaced
            }
            if (existing.name == shot.name) {
                c.error(IssueCode::DuplicateKey, shot.key, at + "/name",
                        fmt::format("the sequence already has a shot called '{}'", shot.name))
                    .suggestions = {"name the shot differently"};
            }
            if (existing.startSeconds < end - 1e-6 && existing.endSeconds() > *start + 1e-6) {
                Issue& issue = c.error(IssueCode::TimingConflict, shot.key, at + "/start",
                                       fmt::format("{:.3f}-{:.3f}s overlaps shot '{}' ({:.3f}-{:.3f}s)", *start, end,
                                                   existing.name, existing.startSeconds, existing.endSeconds()));
                issue.details = {{"existing", existing.name}, {"start", existing.startSeconds}, {"end", existing.endSeconds()}};
                issue.suggestions = {fmt::format("start after {:.3f}s", existing.endSeconds()),
                                     fmt::format("remove or trim shot '{}' first", existing.name)};
            }
        }
        bool overlapsPlanned = false;
        for (const auto& [a, b] : planned) {
            overlapsPlanned = overlapsPlanned || (a < end - 1e-6 && b > *start + 1e-6);
        }
        if (overlapsPlanned) {
            c.error(IssueCode::TimingConflict, shot.key, at + "/start",
                    fmt::format("shot '{}' overlaps another shot in this plan", shot.name));
        } else if (!v.isBlocked(shot.key)) {
            planned.emplace_back(*start, end);
        }
        if (eventCameras && !shot.locked && !v.isBlocked(shot.key)) {
            Issue& issue = c.warning(IssueCode::CameraConflict, shot.key, at + "/locked",
                                     "an event camera in this scene can take the frame during this shot");
            for (const scene::CameraRig& r : facts.staged.cameras.cameras) {
                if (!r.eventScenario.empty()) {
                    issue.details["eventCameras"].push_back({{"camera", r.name}, {"scenario", r.eventScenario}});
                }
            }
            issue.suggestions = {"lock the shot (\"locked\": true)"};
        }
    }

    // ---- markers -----------------------------------------------------------------------------------
    for (std::size_t i = 0; i < plan.markers.size(); ++i) {
        const PlanMarker& m = plan.markers[i];
        for (const seq::Marker& existing : facts.staged.sequence.markers) {
            const std::string id = fmt::format("{}@{:.3f}", existing.name, existing.timeSeconds);
            if (existing.name == m.name && existing.kind == seq::MarkerKind::Cue &&
                std::find(ownMarkers.begin(), ownMarkers.end(), id) == ownMarkers.end()) {
                c.warning(IssueCode::DuplicateKey, m.key, fmt::format("/markers/{}/name", i),
                          fmt::format("a cue marker called '{}' already exists at {:.3f}s; an event on that name "
                                      "will fire at both",
                                      m.name, existing.timeSeconds));
            }
        }
    }

    // ---- performances --------------------------------------------------------------------------------
    for (std::size_t i = 0; i < plan.performances.size(); ++i) {
        const PlanPerformance& p = plan.performances[i];
        const std::string at = fmt::format("/performances/{}", i);
        if (plan.tier == Tier::Baked && p.mode != PerformanceMode::Scripted) {
            Issue& issue = c.error(IssueCode::NonDeterministic, p.key, at + "/mode",
                                   fmt::format("a {} performance is live: it would not render the same way twice, and "
                                               "this plan is baked",
                                               performanceModeName(p.mode)));
            issue.suggestions = {"make it scripted", "mark the plan \"directed\" if a live result is intended"};
        }
        const SubjectKind kind = kindOf(p.subject);
        if (kind != SubjectKind::Unresolved && kind != SubjectKind::Entity) {
            c.error(IssueCode::CapabilityUnavailable, p.key, at + "/subject",
                    fmt::format("'{}' is a {}, and only a character can perform", idOf(p.subject), subjectKindName(kind)));
            continue;
        }
        const CharacterCard* card = facts.capabilities.character(idOf(p.subject));
        if (card == nullptr) {
            continue; // unresolved: already reported and blocked
        }
        for (std::size_t b = 0; b < p.beats.size(); ++b) {
            const PerformanceBeat& beat = p.beats[b];
            const std::string where = fmt::format("{}/beats/{}", at, b);
            if (beat.action == "look_at") {
                continue; // the aim layer, not an activity
            }
            const std::string activity = activityFor(beat.action);
            if (!card->can(activity)) {
                Issue& issue = c.error(IssueCode::CapabilityUnavailable, p.key, where + "/action",
                                       fmt::format("{} does not have a {} capability.", card->subject, activity));
                issue.subject = card->subject;
                std::vector<std::string> available;
                for (const ActivityCapability& a : card->activities) {
                    if (a.available) {
                        available.push_back(a.activity);
                    }
                }
                const std::vector<std::string> airborne = card->available(ActivityKind::Airborne);
                issue.details = {{"requested", activity}, {"available", available}, {"airborne", airborne},
                                 {"rigLoaded", card->rigLoaded}};
                if (const ActivityCapability* mapped = card->activity(activity); mapped != nullptr) {
                    issue.cause = fmt::format("'{}' is mapped to clip '{}', which is not on the loaded rig", activity, mapped->clip);
                } else {
                    issue.cause = fmt::format("no animation on {} is mapped to '{}'", card->subject, activity);
                }
                if (!airborne.empty() && actionClearsTarget(beat.action)) {
                    issue.message += fmt::format(" Available airborne actions: {}.", fmt::join(airborne, ", "));
                    issue.suggestions.push_back(beat.target.empty() ? "jump instead" : "jump over the target instead");
                }
                issue.suggestions.push_back("use another character");
                issue.suggestions.push_back(fmt::format("add a {} animation to {}", activity, card->subject));
            }
            if (actionClearsTarget(beat.action) && !beat.target.empty()) {
                const Place* obstacle = facts.place(idOf(beat.target));
                if (obstacle != nullptr && obstacle->height > 0.0f) {
                    const float clearance = beat.clearanceMetres.value_or(kDefaultClearance);
                    const float required = obstacle->height + clearance;
                    if (card->jump.apex < required) {
                        Issue& issue = c.error(IssueCode::SpatialInfeasible, p.key, where,
                                               fmt::format("{}'s jump peaks {:.2f} m up; clearing '{}' ({:.2f} m tall, "
                                                           "+{:.2f} m clearance) needs {:.2f} m",
                                                           card->subject, card->jump.apex, obstacle->id, obstacle->height,
                                                           clearance, required));
                        issue.subject = card->subject;
                        issue.details = {{"apex", card->jump.apex}, {"apexSource", card->jump.source},
                                         {"obstacle", obstacle->id}, {"obstacleHeight", obstacle->height},
                                         {"required", required}};
                        issue.suggestions = {"jump past the target rather than over it", "choose a lower obstacle",
                                             fmt::format("raise {}'s jump apex to at least {:.2f} m", card->subject, required)};
                    }
                }
            }
        }
        // Scripted performances compile in Slice 2. Reported last, so the findings above -- which a
        // revision can act on -- come first.
        Issue& issue = c.error(IssueCode::Unsupported, p.key, at,
                               "character performances are not compiled yet; nothing will move");
        issue.cause = "scripted performance compilation is Slice 2";
    }

    // ---- cues ---------------------------------------------------------------------------------------
    for (std::size_t i = 0; i < plan.cues.size(); ++i) {
        const PlanCue& cue = plan.cues[i];
        const std::string at = fmt::format("/cues/{}", i);
        if (!cue.parameter.empty()) {
            if (!facts.hasParameter(cue.parameter)) {
                Issue& issue = c.error(IssueCode::UnknownSubject, cue.key, at + "/parameter",
                                       fmt::format("there is no parameter '{}'", cue.parameter));
                issue.suggestions = text::nearest(cue.parameter, facts.subjects.parameters());
            }
            if (!cue.value) {
                c.error(IssueCode::SchemaInvalid, cue.key, at + "/value", "a parameter cue needs a value");
            }
            if (const std::vector<float>* base = facts.base(cue.parameter); base != nullptr && base->size() != 1) {
                c.error(IssueCode::Unsupported, cue.key, at + "/parameter",
                        fmt::format("'{}' has {} components; a cue sets one number", cue.parameter, base->size()))
                    .suggestions = {"key it on the timeline instead"};
            }
            if (std::find(facts.authorTrackTargets.begin(), facts.authorTrackTargets.end(), cue.parameter) !=
                facts.authorTrackTargets.end()) {
                Issue& issue = c.error(IssueCode::TimingConflict, cue.key, at + "/parameter",
                                       fmt::format("'{}' has keys of your own; a baked cue owns its parameter and "
                                                   "installing it would erase them",
                                                   cue.parameter));
                issue.suggestions = {"key the change on your own track instead", "remove your keys on it first"};
            }
        }
        if (cue.effect) {
            ResolvedEffect effect = resolveEffect(*cue.effect, plan, facts, at + "/effect");
            if (effect.issue) {
                effect.issue->item = cue.key;
                v.blocked.insert(cue.key);
                v.issues.push_back(std::move(*effect.issue));
            } else if (!cue.field.empty()) {
                // Moving one of the instance's own numbers: a baked cue on fx/<id>/<leaf>.
                const EffectTypeCapability* type = facts.capabilities.effects().type(cue.effect->type);
                if (effect.id.empty()) {
                    c.error(IssueCode::UnknownSubject, cue.key, at + "/field",
                            fmt::format("there is no {} on {} whose '{}' could change", cue.effect->type,
                                        cue.effect->owner, cue.field));
                } else if (type != nullptr && std::find(type->fields.begin(), type->fields.end(), cue.field) == type->fields.end()) {
                    Issue& issue = c.error(IssueCode::SchemaInvalid, cue.key, at + "/field",
                                           fmt::format("a {} has no field '{}'", cue.effect->type, cue.field));
                    issue.suggestions = text::nearest(cue.field, type->fields);
                } else {
                    const std::string path = fmt::format("fx/{}/{}", effect.id, cue.field);
                    const std::vector<float>* base = facts.base(path);
                    if (base == nullptr || base->size() != 1) {
                        c.error(IssueCode::Unsupported, cue.key, at + "/field",
                                fmt::format("'{}' is not a single number a cue can set", path));
                    }
                    if (!cue.value) {
                        c.error(IssueCode::SchemaInvalid, cue.key, at + "/value", "an effect field cue needs a value");
                    }
                    if (std::find(facts.authorTrackTargets.begin(), facts.authorTrackTargets.end(), path) !=
                        facts.authorTrackTargets.end()) {
                        c.error(IssueCode::TimingConflict, cue.key, at + "/field",
                                fmt::format("'{}' has keys of your own; a baked cue would erase them", path));
                    }
                }
            }
        }
        if (!cue.on.empty()) {
            const PlanPerformance* source = nullptr;
            for (const PlanPerformance& p : plan.performances) {
                for (const PerformanceBeat& b : p.beats) {
                    if (b.emits == cue.on) {
                        source = &p;
                    }
                }
            }
            if (source == nullptr) {
                Issue& issue = c.error(IssueCode::UnknownEvent, cue.key, at + "/on",
                                       fmt::format("nothing in this plan emits '{}'", cue.on));
                std::vector<std::string> events;
                for (const PlanPerformance& p : plan.performances) {
                    for (const PerformanceBeat& b : p.beats) {
                        if (!b.emits.empty()) {
                            events.push_back(b.emits);
                        }
                    }
                }
                issue.suggestions = text::nearest(cue.on, events);
            } else {
                if (source->mode != PerformanceMode::Scripted) {
                    c.add(plan.tier == Tier::Baked ? Severity::Error : Severity::Warning, IssueCode::NonDeterministic,
                          cue.key, at + "/on",
                          fmt::format("'{}' comes from a {} performance, so its time is only known live; a rendered "
                                      "effect on it would not reproduce",
                                      cue.on, performanceModeName(source->mode)));
                }
                if (v.isBlocked(source->key)) {
                    Issue& issue = c.error(IssueCode::Blocked, cue.key, at + "/on",
                                           fmt::format("waits on '{}', which cannot happen: performance '{}' is not "
                                                       "possible as planned",
                                                       cue.on, source->key));
                    issue.details = {{"dependsOn", source->key}};
                }
            }
        }
        if (cue.at && cue.until) {
            const auto a = v.times.at(at + "/at");
            const auto b = v.times.at(at + "/until");
            if (a && b && *b <= *a) {
                c.error(IssueCode::TimingConflict, cue.key, at + "/until", "the cue ends before it starts");
            }
        }
    }

    // ---- retimes -------------------------------------------------------------------------------------
    for (std::size_t i = 0; i < plan.retimes.size(); ++i) {
        const PlanRetime& r = plan.retimes[i];
        Issue& issue = c.error(IssueCode::Unsupported, r.key, fmt::format("/retimes/{}", i),
                               fmt::format("slow motion ({:.2f}x) is not compiled yet", r.factor));
        issue.cause = "performance-local retiming is Slice 3; there is no global time warp (spec §33)";
        if (const PlanPerformance* p = performance(plan, r.performance); p != nullptr && v.isBlocked(p->key)) {
            issue.details = {{"dependsOn", p->key}};
        }
    }
    return v;
}

} // namespace avgen::directing
