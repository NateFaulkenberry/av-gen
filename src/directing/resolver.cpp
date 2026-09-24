#include "directing/resolver.hpp"

#include "directing/text.hpp"
#include "entity/entity.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"
#include "world/hero.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <set>

namespace avgen::directing {
namespace {

// Words that say what KIND of thing is meant, and are not part of its name.
std::optional<SubjectKind> kindWord(const std::string& word) {
    if (word == "hero") {
        return SubjectKind::Hero;
    }
    if (word == "camera" || word == "cam") {
        return SubjectKind::Camera;
    }
    if (word == "character" || word == "entity") {
        return SubjectKind::Entity;
    }
    if (word == "node" || word == "object") {
        return SubjectKind::Node;
    }
    return std::nullopt;
}

bool kindMatches(const SubjectIdentity& identity, SubjectKind hint) {
    if (hint == SubjectKind::Unresolved) {
        return true;
    }
    if (identity.kind == hint || (hint == SubjectKind::Hero && identity.hero)) {
        return true;
    }
    // Asking for the node of an entity or a hero is asking for that same thing.
    return hint == SubjectKind::Node && !identity.node.empty();
}

std::string describe(const SubjectIdentity& identity) {
    if (identity.kind == SubjectKind::Camera && identity.name != identity.id) {
        return fmt::format("camera '{}' ({})", identity.name, identity.id);
    }
    return fmt::format("{} '{}'", subjectKindName(identity.kind), identity.id);
}

nlohmann::json candidatesJson(const std::vector<SubjectIdentity>& candidates) {
    nlohmann::json out = nlohmann::json::array();
    for (const SubjectIdentity& c : candidates) {
        nlohmann::json j{{"kind", subjectKindName(c.kind)}, {"id", c.id}};
        if (c.name != c.id) {
            j["name"] = c.name;
        }
        if (!c.node.empty() && c.node != c.id) {
            j["node"] = c.node;
        }
        out.push_back(std::move(j));
    }
    return out;
}

} // namespace

SubjectIndex SubjectIndex::fromComposition(const scene::Composition& composition) {
    SubjectIndex index;
    std::set<std::string> claimedNodes;
    for (const entity::EntityDesc& desc : composition.entities()) {
        SubjectIdentity id{SubjectKind::Entity, desc.name, desc.name, desc.node.empty() ? desc.name : desc.node};
        claimedNodes.insert(id.node);
        index.identities_.push_back(std::move(id));
    }
    for (const world::HeroPoint& hero : composition.heroes()) {
        // A hero names its node (ADR-107). When an entity drives that node the hero IS that entity,
        // starred: marked on it rather than listed again, or "Rook" would be ambiguous with itself.
        const auto driven = std::find_if(index.identities_.begin(), index.identities_.end(), [&](const SubjectIdentity& e) {
            return e.kind == SubjectKind::Entity && e.node == hero.name;
        });
        if (driven != index.identities_.end()) {
            driven->hero = true;
            continue;
        }
        claimedNodes.insert(hero.name);
        index.identities_.push_back(SubjectIdentity{SubjectKind::Hero, hero.name, hero.name, hero.name});
    }
    for (const auto& node : composition.nodes()) {
        if (node == nullptr || claimedNodes.contains(node->name)) {
            continue;
        }
        index.identities_.push_back(SubjectIdentity{SubjectKind::Node, node->name, node->name, node->name});
    }
    for (const scene::CameraRig& rig : composition.cameraDirection().cameras) {
        const std::string slug = rig.slug.empty() ? text::fold(rig.name) : rig.slug;
        index.identities_.push_back(SubjectIdentity{SubjectKind::Camera, slug, rig.name.empty() ? slug : rig.name, {}});
    }
    return index;
}

void SubjectIndex::addParameters(std::span<const std::string> paths) {
    parameters_.insert(parameters_.end(), paths.begin(), paths.end());
}

SubjectResult resolveSubject(const SubjectIndex& index, std::string_view rawText, SubjectKind hint,
                             std::string_view location) {
    SubjectResult out;
    const std::string original = text::trim(rawText);
    const auto fail = [&](SubjectResult::Status status, IssueCode code, std::string message) -> Issue& {
        out.status = status;
        Issue issue;
        issue.severity = Severity::Error;
        issue.code = code;
        issue.subject = original;
        issue.location = std::string(location);
        issue.message = std::move(message);
        out.issue = std::move(issue);
        return *out.issue;
    };

    // ---- the special subjects: the world, and parameter paths ----------------------------------
    if (hint == SubjectKind::World || text::lower(original) == "world" || text::lower(original) == "the world") {
        out.status = SubjectResult::Status::Resolved;
        out.identity = SubjectIdentity{SubjectKind::World, "world", "world", {}};
        return out;
    }
    if (hint == SubjectKind::Effect) {
        Issue& issue = fail(SubjectResult::Status::Unsupported, IssueCode::Unsupported,
                            fmt::format("effects cannot be named yet: '{}'", original));
        issue.cause = "effect instances (ADR-702) are not on this build's main line yet";
        issue.suggestions = {"name the effect's owner and type in a cue's `effect` field instead"};
        return out;
    }
    if (original.find('/') != std::string::npos || hint == SubjectKind::Parameter) {
        const auto& params = index.parameters();
        if (std::find(params.begin(), params.end(), original) != params.end()) {
            out.status = SubjectResult::Status::Resolved;
            out.identity = SubjectIdentity{SubjectKind::Parameter, original, original, {}};
            return out;
        }
        Issue& issue = fail(SubjectResult::Status::Unknown, IssueCode::UnknownSubject,
                            fmt::format("there is no parameter '{}'", original));
        issue.suggestions = text::nearest(original, params);
        return out;
    }

    // ---- words: drop articles, lift kind words into the hint -------------------------------------
    std::vector<std::string> words = text::words(original);
    std::erase_if(words, [](const std::string& w) { return w == "the" || w == "a" || w == "an"; });
    // A trailing kind word ("the Hero Free Roam camera") says what kind and leaves the name whole --
    // a camera may be called "Hero ...". Only without one do kind words inside the text act as hints
    // ("the Umbra hero mushroom").
    std::vector<std::string> nameWords;
    if (!words.empty() && kindWord(words.back())) {
        if (hint == SubjectKind::Unresolved) {
            hint = *kindWord(words.back());
        }
        nameWords.assign(words.begin(), words.end() - 1);
    } else {
        for (const std::string& w : words) {
            if (auto k = kindWord(w)) {
                if (hint == SubjectKind::Unresolved) {
                    hint = *k;
                }
                continue;
            }
            nameWords.push_back(w);
        }
    }
    if (nameWords.empty()) {
        fail(SubjectResult::Status::Unknown, IssueCode::UnknownSubject,
             fmt::format("'{}' does not name anything; it only says what kind of thing", original));
        return out;
    }
    std::string folded;
    for (const std::string& w : nameWords) {
        folded += w;
    }

    // ---- exact, then partial ----------------------------------------------------------------------
    const auto& all = index.identities();
    std::vector<SubjectIdentity> exact;
    std::vector<SubjectIdentity> partial;
    for (const SubjectIdentity& c : all) {
        if (text::fold(c.id) == folded || text::fold(c.name) == folded) {
            exact.push_back(c);
            continue;
        }
        const std::vector<std::string> cw = text::words(c.name + " " + c.id);
        if (std::find(cw.begin(), cw.end(), nameWords.front()) != cw.end()) {
            partial.push_back(c);
        }
    }
    const auto ofKind = [&](const std::vector<SubjectIdentity>& list) {
        std::vector<SubjectIdentity> kept;
        for (const SubjectIdentity& c : list) {
            if (kindMatches(c, hint)) {
                kept.push_back(c);
            }
        }
        return kept;
    };
    std::vector<SubjectIdentity> matches = ofKind(exact);
    if (matches.empty()) {
        matches = ofKind(partial);
    }

    if (matches.size() == 1) {
        out.status = SubjectResult::Status::Resolved;
        out.identity = matches.front();
        return out;
    }
    if (matches.size() > 1) {
        Issue& issue = fail(SubjectResult::Status::Ambiguous, IssueCode::AmbiguousReference,
                            fmt::format("'{}' could mean {} things", original, matches.size()));
        issue.details = {{"candidates", candidatesJson(matches)}};
        for (const SubjectIdentity& c : matches) {
            issue.suggestions.push_back(describe(c));
        }
        out.candidates = std::move(matches);
        return out;
    }

    // Nothing of that kind. If something of another kind answers to the name, say so -- "there is no
    // hero called rook, but there is a character" is the useful sentence.
    std::vector<SubjectIdentity> otherKinds = exact.empty() ? partial : exact;
    std::vector<std::string> names;
    for (const SubjectIdentity& c : all) {
        names.push_back(c.kind == SubjectKind::Camera ? c.name : c.id);
    }
    Issue& issue = fail(SubjectResult::Status::Unknown, IssueCode::UnknownSubject,
                        hint == SubjectKind::Unresolved
                            ? fmt::format("nothing in this scene is called '{}'", original)
                            : fmt::format("there is no {} called '{}'", subjectKindName(hint), original));
    if (!otherKinds.empty()) {
        issue.details = {{"otherKinds", candidatesJson(otherKinds)}};
        for (const SubjectIdentity& c : otherKinds) {
            issue.suggestions.push_back(describe(c));
        }
    } else {
        issue.suggestions = text::nearest(folded, names);
    }
    out.candidates = std::move(otherKinds);
    return out;
}

std::vector<Issue> resolvePlanSubjects(Plan& plan, const SubjectIndex& index) {
    std::vector<Issue> issues;
    for (std::size_t i = 0; i < plan.subjects.size(); ++i) {
        Subject& s = plan.subjects[i];
        const std::string location = fmt::format("/subjects/{}", i);
        if (s.kind != SubjectKind::Unresolved) {
            // Already chosen (by the model, from an earlier ambiguity's candidates, or by a person):
            // checked, never silently replaced.
            const SubjectResult check = resolveSubject(index, s.id, s.kind, location);
            if (check.status != SubjectResult::Status::Resolved || check.identity.id != s.id ||
                check.identity.kind != s.kind) {
                Issue issue;
                issue.code = IssueCode::UnknownSubject;
                issue.location = location;
                issue.subject = s.alias;
                issue.message = fmt::format("'{}' was resolved to {} '{}', which this scene does not have", s.alias,
                                            subjectKindName(s.kind), s.id);
                if (check.issue) {
                    issue.suggestions = check.issue->suggestions;
                }
                issues.push_back(std::move(issue));
            }
            continue;
        }
        SubjectResult r = resolveSubject(index, s.text.empty() ? s.alias : s.text, s.hint, location);
        if (r.status == SubjectResult::Status::Resolved) {
            s.kind = r.identity.kind;
            s.id = r.identity.id;
        } else if (r.issue) {
            r.issue->subject = s.alias;
            issues.push_back(std::move(*r.issue));
        }
    }
    return issues;
}

std::optional<double> PlanTimes::at(std::string_view location) const {
    for (const auto& [where, value] : seconds) {
        if (where == location) {
            return value;
        }
    }
    return std::nullopt;
}

PlanTimes resolvePlanTimes(const Plan& plan, const MusicalContext& context) {
    PlanTimes out;
    const auto place = [&](const TimeRef& ref, const std::string& location) {
        TimeResolution r = resolveTime(ref, context, location);
        if (r.seconds) {
            out.seconds.emplace_back(location, *r.seconds);
        }
        if (!r.explanation.empty()) {
            out.explanations.emplace_back(location, r.explanation);
        }
        for (Issue& i : r.issues) {
            out.issues.push_back(std::move(i));
        }
    };
    for (std::size_t i = 0; i < plan.shots.size(); ++i) {
        place(plan.shots[i].start, fmt::format("/shots/{}/start", i));
        for (std::size_t b = 0; b < plan.shots[i].camera.size(); ++b) {
            if (plan.shots[i].camera[b].at) {
                place(*plan.shots[i].camera[b].at, fmt::format("/shots/{}/camera/{}/at", i, b));
            }
        }
    }
    for (std::size_t i = 0; i < plan.markers.size(); ++i) {
        place(plan.markers[i].at, fmt::format("/markers/{}/at", i));
    }
    for (std::size_t i = 0; i < plan.performances.size(); ++i) {
        for (std::size_t b = 0; b < plan.performances[i].beats.size(); ++b) {
            if (plan.performances[i].beats[b].at) {
                place(*plan.performances[i].beats[b].at, fmt::format("/performances/{}/beats/{}/at", i, b));
            }
        }
    }
    for (std::size_t i = 0; i < plan.cues.size(); ++i) {
        if (plan.cues[i].at) {
            place(*plan.cues[i].at, fmt::format("/cues/{}/at", i));
        }
        if (plan.cues[i].until) {
            place(*plan.cues[i].until, fmt::format("/cues/{}/until", i));
        }
    }
    for (std::size_t i = 0; i < plan.retimes.size(); ++i) {
        place(plan.retimes[i].from, fmt::format("/retimes/{}/from", i));
        place(plan.retimes[i].until, fmt::format("/retimes/{}/until", i));
    }
    return out;
}

} // namespace avgen::directing
