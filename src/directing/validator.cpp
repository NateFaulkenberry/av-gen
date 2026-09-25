#include "directing/validator.hpp"

#include "directing/compiler.hpp"
#include "directing/performance.hpp"
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

constexpr float kDefaultClearance = kDefaultJumpClearance;

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

// ADR-763: a goal performance. Everything a goal can be checked for before it runs -- the rest is
// the character's, which is the point of a goal.
template <typename KindOf, typename IdOf>
void validateGoalPerformance(Collector& c, const Plan& plan, std::size_t i, const CharacterCard& card,
                             const SceneFacts& facts, const PlanTimes& times, const KindOf& kindOf, const IdOf& idOf) {
    const PlanPerformance& p = plan.performances[i];
    const std::string at = fmt::format("/performances/{}", i);
    if (!card.goalSlot) {
        Issue& issue = c.error(IssueCode::CapabilityUnavailable, p.key, at + "/subject",
                               fmt::format("{}'s decider has no goal considerer, so a goal given to it would do "
                                           "nothing",
                                           card.subject));
        issue.subject = card.subject;
        issue.suggestions = {fmt::format("add a goal considerer (an empty slot) to {}'s decide behaviour", card.subject),
                             "use a scripted performance"};
    }
    if (p.beats.empty()) {
        c.error(IssueCode::SchemaInvalid, p.key, at + "/beats", "a goal performance needs at least one goal");
    }
    for (std::size_t b = 0; b < p.beats.size(); ++b) {
        const PerformanceBeat& beat = p.beats[b];
        const std::string where = fmt::format("{}/beats/{}", at, b);
        if (goalVerb(beat.action) == nullptr) {
            std::vector<std::string> verbs;
            for (const GoalVerb& v : kGoalVerbs) {
                verbs.emplace_back(v.action);
            }
            Issue& issue = c.error(IssueCode::Unsupported, p.key, where + "/action",
                                   fmt::format("'{}' is not a goal; a goal performance compiles {}", beat.action,
                                               fmt::join(verbs, ", ")));
            issue.suggestions = text::nearest(beat.action, verbs);
            continue;
        }
        if (beat.target.empty()) {
            c.error(IssueCode::SchemaInvalid, p.key, where + "/target", fmt::format("'{}' needs a target", beat.action));
        } else if (idOf(beat.target) == card.subject) {
            c.error(IssueCode::SchemaInvalid, p.key, where + "/target", "a character's goal cannot be itself");
        } else if (const SubjectKind k = kindOf(beat.target);
                   k != SubjectKind::Unresolved && k != SubjectKind::Entity && facts.place(idOf(beat.target)) == nullptr) {
            c.error(IssueCode::SpatialInfeasible, p.key, where + "/target",
                    fmt::format("'{}' is not a place or a character a goal can lead to", idOf(beat.target)));
        } else if (const Place* place = facts.place(idOf(beat.target)); place != nullptr && facts.walkable) {
            // Asked the way the goal's walk will ask it: from the character's authored mark to a
            // point beside the place. A warning, not a refusal -- where the character is when the
            // goal is given is live -- but "no route" is what makes a goal do nothing at all.
            if (const CharacterMark* mark = facts.character(card.subject); mark != nullptr) {
                const glm::vec2 from(mark->anchor.x, mark->anchor.z);
                const glm::vec2 to(place->position.x, place->position.z);
                const glm::vec2 d = glm::length(to - from) > 1e-3f ? glm::normalize(to - from) : glm::vec2(0.0f, 1.0f);
                if (facts.walkable(from, to - (d * (place->radius + 2.0f))) == std::optional<bool>(false)) {
                    Issue& issue = c.warning(IssueCode::SpatialInfeasible, p.key, where + "/target",
                                             fmt::format("there is no walking route from {}'s mark to '{}': the goal "
                                                         "may never be reached",
                                                         card.subject, place->id));
                    issue.suggestions = {"choose a place on the same ground as the character", "use a scripted performance"};
                }
            }
        }
        if (!beat.moment.empty() &&
            std::find(std::begin(kGoalMoments), std::end(kGoalMoments), beat.moment) == std::end(kGoalMoments)) {
            c.error(IssueCode::SchemaInvalid, p.key, where + "/moment",
                    fmt::format("a goal's 'moment' is 'arrived' or 'done', not '{}'", beat.moment));
        }
        if (!goalBeatTime(plan, i, b, times)) {
            Issue& issue = c.error(IssueCode::SchemaInvalid, p.key, where + "/at",
                                   b == 0 ? "a goal performance needs a start: give its first goal a time, or a shot of "
                                            "the same subject"
                                          : "each goal after the first needs its own time: when the last one ends is "
                                            "the character's to decide, not a plan-time fact");
            issue.suggestions = {"\"at\" on the goal"};
        }
    }
}

// ADR-766: a directed performance -- orders at seconds, carried out by the character.
template <typename KindOf, typename IdOf>
void validateDirectedPerformance(Collector& c, const Plan& plan, std::size_t i, const CharacterCard& card,
                                 const SceneFacts& facts, const PlanTimes& times, const KindOf& kindOf,
                                 const IdOf& idOf) {
    const PlanPerformance& p = plan.performances[i];
    const std::string at = fmt::format("/performances/{}", i);
    if (p.beats.empty()) {
        c.error(IssueCode::SchemaInvalid, p.key, at + "/beats", "a directed performance needs at least one order");
    }
    for (std::size_t b = 0; b < p.beats.size(); ++b) {
        const PerformanceBeat& beat = p.beats[b];
        const std::string where = fmt::format("{}/beats/{}", at, b);
        const auto verb = directedBeat(beat.action, card);
        if (!verb) {
            std::vector<std::string> words = {"face", "approach", "go_to", "interact", "release"};
            for (const ActivityCapability& a : card.activities) {
                if (a.available) {
                    words.push_back(a.activity);
                }
            }
            Issue& issue = c.error(IssueCode::Unsupported, p.key, where + "/action",
                                   fmt::format("'{}' is not an order {} can be given", beat.action, card.subject));
            issue.suggestions = text::nearest(beat.action, words);
            continue;
        }
        const SubjectKind tk = beat.target.empty() ? SubjectKind::Unresolved : kindOf(beat.target);
        switch (verb->verb) {
        case DirectedVerb::Face:
        case DirectedVerb::Approach:
            if (beat.target.empty()) {
                c.error(IssueCode::SchemaInvalid, p.key, where + "/target", fmt::format("'{}' needs a target", beat.action));
            } else if (tk != SubjectKind::Unresolved && tk != SubjectKind::Entity) {
                Issue& issue = c.error(IssueCode::Unsupported, p.key, where + "/target",
                                       fmt::format("'{}' is directed at characters; '{}' is a {}", beat.action,
                                                   idOf(beat.target), subjectKindName(tk)));
                issue.suggestions = {"go_to a place instead"};
            } else if (idOf(beat.target) == card.subject) {
                c.error(IssueCode::SchemaInvalid, p.key, where + "/target", "a character cannot be directed at itself");
            }
            break;
        case DirectedVerb::GoTo:
            if (beat.target.empty()) {
                c.error(IssueCode::SchemaInvalid, p.key, where + "/target", "'go_to' needs a place");
            } else if (tk != SubjectKind::Unresolved && facts.place(idOf(beat.target)) == nullptr &&
                       tk != SubjectKind::Entity) {
                c.error(IssueCode::SpatialInfeasible, p.key, where + "/target",
                        fmt::format("'{}' is not a place a character can go to", idOf(beat.target)));
            } else if (!card.goalSlot) {
                c.error(IssueCode::CapabilityUnavailable, p.key, where + "/action",
                        fmt::format("'go_to' a place is carried out by {}'s goal considerer, and it has none",
                                    card.subject));
            }
            break;
        case DirectedVerb::Interact:
            if (beat.target.empty()) {
                c.error(IssueCode::SchemaInvalid, p.key, where + "/target",
                        "'interact' needs the prop and its verb, as 'lamp.light'");
            }
            break;
        case DirectedVerb::Pose:
        case DirectedVerb::Release:
            break;
        }
        // The only event a directed order raises is the one its goal raises: a go_to's arrival.
        if (!beat.emits.empty() && verb->verb != DirectedVerb::GoTo) {
            Issue& issue = c.error(IssueCode::Unsupported, p.key, where + "/emits",
                                   fmt::format("a '{}' order raises no named event to hang things on", beat.action));
            issue.suggestions = {"emit from a go_to (its arrival)", "use a scripted performance, whose events are computed"};
        }
        if (!beat.moment.empty() && (verb->verb != DirectedVerb::GoTo ||
                                     std::find(std::begin(kGoalMoments), std::end(kGoalMoments), beat.moment) ==
                                         std::end(kGoalMoments))) {
            c.error(IssueCode::SchemaInvalid, p.key, where + "/moment", "only a go_to names a moment: 'arrived' or 'done'");
        }
        if (!goalBeatTime(plan, i, b, times)) {
            Issue& issue = c.error(IssueCode::SchemaInvalid, p.key, where + "/at",
                                   b == 0 ? "a directed performance needs a start: give its first order a time, or a "
                                            "shot of the same subject"
                                          : "each order after the first needs its own time: when the last one is done "
                                            "is the character's to decide");
            issue.suggestions = {"\"at\" on the order"};
        }
    }
}

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
        // ADR-760: keys on the follow rig's offset, so only where a follow rig exists -- on a
        // character. Rising over a place is a framing move's elevation, not compiled yet.
        return subject == SubjectKind::Entity ? CameraSupport::KeyedOffset : CameraSupport::Unsupported;
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
    // This plan's own earlier output is not a candidate: a revision replaces it. Without this, a
    // revision of "pulse Umbra's hero effect" found its own previous windows beside Umbra's pulse and
    // called the reference ambiguous (found by the golden plans' revision check).
    std::vector<std::string> ownOutput;
    if (const Plan* previous = facts.plan(plan.id); previous != nullptr) {
        for (const ContentRef& ref : previous->produced) {
            if (ref.domain == ContentDomain::EffectInstance) {
                ownOutput.push_back(ref.id);
            }
        }
    }
    std::vector<const world::EffectInstance*> matches;
    for (const world::EffectInstance& e : facts.staged.effects) {
        if (std::find(ownOutput.begin(), ownOutput.end(), e.id) != ownOutput.end()) {
            continue;
        }
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
    std::vector<std::string> ownActors;
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
            } else if (ref.domain == ContentDomain::SequenceActor) {
                ownActors.push_back(ref.id);
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
        bool followsCharacter = false;
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
            if (support == CameraSupport::KeyedOffset) {
                if (!followsCharacter) {
                    Issue& issue = c.warning(IssueCode::Unsupported, shot.key, where,
                                             fmt::format("'{}' changes a follow camera's offset, and no chase or follow on "
                                                         "a character comes before it in this shot",
                                                         cameraMoveName(beat.move)));
                    issue.suggestions = {"put a chase or follow on the character first"};
                }
                continue;
            }
            if (support == CameraSupport::Unsupported) {
                Issue& issue = c.warning(IssueCode::Unsupported, shot.key, where,
                                         fmt::format("'{}' on {} '{}' is not compiled yet; the shot is built without it",
                                                     cameraMoveName(beat.move), subjectKindName(kind), idOf(subject)));
                issue.cause = (beat.move == CameraMove::RiseOver || beat.move == CameraMove::Pass)
                                  ? "rising over or passing a PLACE is not compiled yet; over a character it is (ADR-760)"
                                  : "framing a moving character by a move would frame where it stood at the cut; "
                                    "follow it with chase or follow";
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
            followsCharacter = support == CameraSupport::FollowRig;
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
        if (plan.tier == Tier::Baked && p.mode != PerformanceMode::Scripted && !p.recording) {
            Issue& issue = c.error(IssueCode::NonDeterministic, p.key, at + "/mode",
                                   fmt::format("a {} performance is live: it would not render the same way twice, and "
                                               "this plan is baked",
                                               performanceModeName(p.mode)));
            issue.suggestions = {"make it scripted", "mark the plan \"directed\" if a live result is intended"};
        }
        if (p.entrySeconds < 0.0) {
            c.error(IssueCode::SchemaInvalid, p.key, at + "/entrySeconds", "an entry blend cannot be negative");
        } else if (p.entrySeconds > 0.0) {
            // ADR-820: a blend starts from wherever the simulation had the body at the cut, which
            // depends on everything the world did before it -- live state, not a plan-time fact.
            Issue& issue = c.add(plan.tier == Tier::Baked ? Severity::Error : Severity::Warning,
                                 IssueCode::NonDeterministic, p.key, at + "/entrySeconds",
                                 fmt::format("an entry blend of {:.2f} s starts from where the simulation left {}, "
                                             "which is live state: the performance would not begin the same way twice",
                                             p.entrySeconds, idOf(p.subject)));
            issue.suggestions = {"set entrySeconds to 0 and start the performance at a cut",
                                 "mark the plan \"directed\" if a live start is intended"};
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
        if (p.recording) {
            // ADR-763: a recorded performance compiles verbatim from its recording (a scripted actor),
            // whatever mode it was recorded from. Its only check: it is for this character.
            const auto actor = recordedActor(*p.recording);
            if (!actor) {
                c.error(IssueCode::SchemaInvalid, p.key, at + "/recording", "the recording's actor does not parse");
            } else if (actor->id != card->subject) {
                c.error(IssueCode::SchemaInvalid, p.key, at + "/recording",
                        fmt::format("the recording is of '{}', not '{}'", actor->id, card->subject));
            }
            continue;
        }
        if (p.mode == PerformanceMode::Directed) {
            validateDirectedPerformance(c, plan, i, *card, facts, v.times, kindOf, idOf);
            for (std::size_t o = 0; o < i; ++o) {
                if (plan.performances[o].subject == p.subject) {
                    c.error(IssueCode::TimingConflict, p.key, at,
                            fmt::format("performance '{}' already directs {}", plan.performances[o].key, card->subject));
                }
            }
            continue;
        }
        if (p.mode == PerformanceMode::Goal) {
            validateGoalPerformance(c, plan, i, *card, facts, v.times, kindOf, idOf);
            for (std::size_t o = 0; o < i; ++o) {
                if (plan.performances[o].subject == p.subject) {
                    c.error(IssueCode::TimingConflict, p.key, at,
                            fmt::format("performance '{}' already directs {}; a goal and another performance would "
                                        "fight for the body, and the performer would win silently",
                                        plan.performances[o].key, card->subject));
                }
            }
            continue;
        }
        std::vector<std::size_t> clearedStatically; // jump beats already refused on height alone
        for (std::size_t b = 0; b < p.beats.size(); ++b) {
            const PerformanceBeat& beat = p.beats[b];
            const std::string where = fmt::format("{}/beats/{}", at, b);
            if (!beat.moment.empty()) {
                const bool known = std::find(std::begin(kJumpMoments), std::end(kJumpMoments), beat.moment) !=
                                   std::end(kJumpMoments);
                if (beat.action != "jump" || !known) {
                    c.error(IssueCode::SchemaInvalid, p.key, where + "/moment",
                            fmt::format("'moment' names one of a jump's moments (takeoff, peak, touchdown); '{}' on a "
                                        "'{}' beat is not one",
                                        beat.moment, beat.action));
                }
            }
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
                    // ADR-822: what a plan may ask is the character's highest leap, not its hop.
                    const float limit = card->jump.maxApex > 0.0f ? card->jump.maxApex : card->jump.apex;
                    if (limit < required) {
                        Issue& issue = c.error(IssueCode::SpatialInfeasible, p.key, where,
                                               fmt::format("{}'s highest jump peaks {:.2f} m up; clearing '{}' ({:.2f} m tall, "
                                                           "+{:.2f} m clearance) needs {:.2f} m",
                                                           card->subject, limit, obstacle->id, obstacle->height,
                                                           clearance, required));
                        issue.subject = card->subject;
                        issue.details = {{"apex", limit}, {"hopApex", card->jump.apex}, {"apexSource", card->jump.source},
                                         {"obstacle", obstacle->id}, {"obstacleHeight", obstacle->height},
                                         {"required", required}};
                        issue.suggestions = {"use a character with a larger jump",
                                             fmt::format("take a different path: run past {} rather than over it", obstacle->id),
                                             "choose a lower obstacle"};
                        clearedStatically.push_back(b);
                    }
                }
            }
        }
        // What this build compiles (ADR-759). Reported after the capability and spatial findings,
        // which a revision can act on.
        if (p.mode != PerformanceMode::Scripted) {
            c.error(IssueCode::Unsupported, p.key, at + "/mode",
                    fmt::format("{} performances are not compiled yet (Slice 4); only scripted ones", performanceModeName(p.mode)));
        }
        for (std::size_t b = 0; b < p.beats.size(); ++b) {
            const PerformanceBeat& beat = p.beats[b];
            const std::string where = fmt::format("{}/beats/{}", at, b);
            if (!beatCompilable(beat.action)) {
                if (card->can(activityFor(beat.action)) || beat.action == "look_at") {
                    Issue& issue = c.error(IssueCode::Unsupported, p.key, where + "/action",
                                           fmt::format("'{}' is not compiled yet", beat.action));
                    issue.cause = beat.action == "fall"
                                      ? "a fall needs a drop to fall from, which a plan cannot place yet; jump and land compile"
                                      : "this build compiles run_to, walk_to, run_past, walk_past, run, walk, hold, look_at, "
                                        "jump and land";
                }
                continue;
            }
            const bool needsTarget = beat.action.ends_with("_to") || beat.action.ends_with("_past");
            if (needsTarget && beat.target.empty()) {
                c.error(IssueCode::SchemaInvalid, p.key, where + "/target", fmt::format("'{}' needs a target", beat.action));
            }
            if (!beat.target.empty()) {
                const SubjectKind tk = kindOf(beat.target);
                if (tk == SubjectKind::Entity) {
                    c.error(IssueCode::Unsupported, p.key, where + "/target",
                            "a performance toward another character is not compiled: where a character is, is its "
                            "simulation's, not a plan-time fact (ADR-758)");
                } else if (tk != SubjectKind::Unresolved && facts.place(idOf(beat.target)) == nullptr) {
                    c.error(IssueCode::SpatialInfeasible, p.key, where + "/target",
                            fmt::format("'{}' has no known place to move toward", idOf(beat.target)));
                }
            }
        }
        const std::optional<double> start = performanceStart(plan, i, v.times);
        if (!start) {
            Issue& issue = c.error(IssueCode::SchemaInvalid, p.key, at,
                                   "a performance needs a start: give its first beat a time, or a shot of the same subject");
            issue.suggestions = {"\"at\" on the first beat", "a shot whose subject is the performer"};
        } else {
            // ADR-758: a performance takes the body at its first instant, from wherever the
            // simulation had it. A cut hides that; anywhere else it is a visible jump.
            bool atCut = false;
            for (std::size_t s = 0; s < plan.shots.size(); ++s) {
                const auto shotStart = v.times.at(fmt::format("/shots/{}/start", s));
                atCut = atCut || (shotStart && std::abs(*shotStart - *start) < 1e-3);
            }
            for (const seq::Shot& shot : facts.staged.sequence.shots) {
                atCut = atCut || std::abs(shot.startSeconds - *start) < 1e-3;
            }
            for (const scene::CameraShot& cut : facts.staged.cameras.shots) {
                atCut = atCut || std::abs(cut.startSeconds - *start) < 1e-3;
            }
            if (!atCut) {
                Issue& issue = c.warning(IssueCode::TimingConflict, p.key, at,
                                         fmt::format("the performance starts at {:.3f}s, not at a cut: {} will visibly jump "
                                                     "from where the simulation has them to the performance's mark",
                                                     *start, card->subject));
                issue.suggestions = {"start the performance with a shot of the performer"};
            }
        }
        // Jumps, checked on the path the compiler would fly (ADR-822): the same `compilePerformance`,
        // so what is refused is exactly what would have been compiled. Only when the path can be
        // placed: a start, and every target a place.
        const bool hasJump = std::any_of(p.beats.begin(), p.beats.end(), [](const PerformanceBeat& b) { return b.action == "jump"; });
        const bool placeable = std::all_of(p.beats.begin(), p.beats.end(), [&](const PerformanceBeat& b) {
            return b.target.empty() || facts.place(idOf(b.target)) != nullptr;
        });
        if (hasJump && start && placeable) {
            const CompiledPerformance probe = compilePerformance(plan, i, facts, v.times);
            for (const JumpOutcome& j : probe.jumps) {
                const std::string where = fmt::format("{}/beats/{}", at, j.beat);
                const bool refused = std::find(clearedStatically.begin(), clearedStatically.end(), j.beat) !=
                                     clearedStatically.end();
                const auto infeasible = [&](std::string message) -> Issue& {
                    Issue& issue = c.error(IssueCode::SpatialInfeasible, p.key, where, std::move(message));
                    issue.subject = card->subject;
                    issue.details = {{"apex", j.apex},        {"minimumApex", j.minimumApex}, {"apexLimit", j.apexLimit},
                                     {"distance", j.distance}, {"maxDistance", j.maxDistance}, {"obstacle", j.obstacle}};
                    return issue;
                };
                if (!j.groundKnown) {
                    infeasible("the ground under the jump is not known (this scene has no terrain), so its arc "
                               "cannot be placed or checked");
                    continue;
                }
                if (!j.arc) {
                    infeasible(fmt::format("no arc exists from where {} takes off to where it would land", card->subject));
                    continue;
                }
                if (!refused && j.apex > j.apexLimit + 1e-4f) {
                    Issue& issue = infeasible(fmt::format(
                        "on this path, clearing '{}' takes an arc peaking {:.2f} m above the take-off; {}'s highest "
                        "jump is {:.2f} m",
                        j.obstacle, j.apex, card->subject, j.apexLimit));
                    issue.suggestions = {"use a character with a larger jump", "take off closer to the obstacle",
                                         "take a different path"};
                }
                if (j.distance > j.maxDistance + 1e-4f) {
                    Issue& issue = infeasible(fmt::format("the leap is {:.2f} m long, and {}'s farthest is {:.2f} m",
                                                          j.distance, card->subject, j.maxDistance));
                    issue.suggestions = {"jump over something narrower", "use a character with a longer jump"};
                }
                if (!j.ground.clear) {
                    Issue& issue = infeasible(fmt::format(
                        "the arc meets the ground {:.2f} s after take-off, before it lands", j.ground.firstContact));
                    issue.suggestions = {"jump somewhere flatter", "jump in another direction"};
                }
            }
        }
        // One actor per character: another performance of the same subject in this plan, or an actor
        // already on this character that this plan did not make, would fight for the body.
        for (std::size_t o = 0; o < i; ++o) {
            if (plan.performances[o].subject == p.subject) {
                c.error(IssueCode::TimingConflict, p.key, at,
                        fmt::format("{} already has a performance in this plan ('{}'); combine the beats",
                                    card->subject, plan.performances[o].key));
            }
        }
        for (const seq::Actor& actor : facts.staged.sequence.actors) {
            const bool mine = std::find(ownActors.begin(), ownActors.end(), actor.id) != ownActors.end();
            if (!mine && actor.id == card->subject) {
                c.error(IssueCode::TimingConflict, p.key, at,
                        fmt::format("the sequence already has an actor '{}' that this plan did not make", actor.id))
                    .suggestions = {"remove that actor first", "revise the plan that made it"};
            }
        }
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
                if (source->mode != PerformanceMode::Scripted && !source->recording) {
                    c.add(plan.tier == Tier::Baked ? Severity::Error : Severity::Warning, IssueCode::NonDeterministic,
                          cue.key, at + "/on",
                          fmt::format("'{}' comes from a {} performance, so its time is only known live; a rendered "
                                      "effect on it would not reproduce",
                                      cue.on, performanceModeName(source->mode)));
                    // ADR-763: a change on a live event has nothing to apply it -- the engine bakes a
                    // parameter cue into keys at a known time, and a live trigger has none. Recording
                    // the performance gives it one.
                    Issue& issue = c.error(IssueCode::Unsupported, cue.key, at + "/on",
                                           fmt::format("a cue on the live event '{}' takes effect only once the "
                                                       "performance is recorded",
                                                       cue.on));
                    issue.suggestions = {"record the performance: its events then have times, and the cue bakes on them"};
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
        // ADR-823: slow motion is a reparameterisation of one performance's actor (`seq::retimeActor`)
        // -- its keys, clip cues and airborne spans -- never of the film's clock (spec §33).
        const PlanRetime& r = plan.retimes[i];
        const std::string at = fmt::format("/retimes/{}", i);
        const PlanPerformance* p = performance(plan, r.performance);
        if (p == nullptr) {
            Issue& issue = c.error(IssueCode::SchemaInvalid, r.key, at + "/performance",
                                   fmt::format("no performance '{}' to slow down", r.performance));
            issue.suggestions = {"retime a performance of this plan, by its key"};
            continue;
        }
        if (!(r.factor > 0.0)) {
            c.error(IssueCode::SchemaInvalid, r.key, at + "/factor", "a retime's factor must be greater than 0");
            continue;
        }
        const auto from = v.times.at(at + "/from");
        const auto until = v.times.at(at + "/until");
        if (!from || !until) {
            continue; // unplaceable: already reported
        }
        if (*until <= *from) {
            c.error(IssueCode::SchemaInvalid, r.key, at + "/until",
                    fmt::format("the window ends ({:.3f}s) before it starts ({:.3f}s)", *until, *from));
            continue;
        }
        for (std::size_t o = 0; o < i; ++o) {
            const PlanRetime& other = plan.retimes[o];
            const auto oFrom = v.times.at(fmt::format("/retimes/{}/from", o));
            const auto oUntil = v.times.at(fmt::format("/retimes/{}/until", o));
            if (other.performance == r.performance && oFrom && oUntil && *from < *oUntil && *until > *oFrom) {
                c.error(IssueCode::TimingConflict, r.key, at,
                        fmt::format("overlaps retime '{}' of the same performance; one stretch of time has one rate",
                                    other.key));
            }
        }
        if (v.isBlocked(p->key)) {
            Issue& issue = c.error(IssueCode::Blocked, r.key, at + "/performance",
                                   fmt::format("slows performance '{}', which is not possible as planned", p->key));
            issue.details = {{"dependsOn", p->key}};
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(p - plan.performances.data());
        if (p->mode != PerformanceMode::Scripted && !p->recording) {
            Issue& issue = c.error(IssueCode::Unsupported, r.key, at + "/performance",
                                   fmt::format("slowing '{}' needs its motion to be known, and a live performance's "
                                               "is not until it is recorded",
                                               p->key));
            issue.suggestions = {"record the performance first"};
            continue;
        }
        if (performanceStart(plan, index, v.times)) {
            const CompiledPerformance probe = compilePerformance(plan, index, facts, v.times);
            if (*until <= probe.from || *from >= probe.to) {
                c.error(IssueCode::TimingConflict, r.key, at,
                        fmt::format("the window {:.3f}-{:.3f}s misses performance '{}' ({:.3f}-{:.3f}s)", *from, *until,
                                    p->key, probe.from, probe.to));
            }
        }
    }
    return v;
}

} // namespace avgen::directing
