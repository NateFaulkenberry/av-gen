#pragma once

// Naming things (spec §14, ADR-755): "Rook", "the Umbra hero mushroom", "the Valley Wide camera" ->
// a canonical identity, or a refusal that says exactly why.
//
// The rules, in order, and the reason for each:
//
//   1. **One identity per thing.** An entity drives a node, and a hero names a node; those are one
//      subject, not two or three. The entity (else the hero) is the identity and carries its node, so
//      "Rook" is one candidate, not "the entity rook, the hero rook and the node rook".
//   2. **Kind words are hints.** "hero", "camera", "character", "node" in the text (or an explicit
//      hint) narrow the candidates to that kind. A trailing one leaves the name whole ("the Hero Free
//      Roam camera"); otherwise they are lifted out ("the Umbra hero mushroom"). Other words
//      ("mushroom") are description and play no part beyond the first significant word.
//   3. **Exact before partial.** The whole folded text equal to a name wins outright. Otherwise a
//      candidate matches when its name contains the text's first significant word ("umbra" ->
//      umbra-cap, umbra-stem, ...).
//   4. **Never choose silently.** More than one identity left is AMBIGUOUS_REFERENCE with every
//      candidate listed; none is UNKNOWN_SUBJECT with the nearest names. The model may pick -- by
//      writing the candidate's id -- but the resolver never does.
//
// Effects resolve once ADR-702's instance list is on main; until then an effect hint is UNSUPPORTED
// rather than a guess.

#include "directing/issue.hpp"
#include "directing/plan.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {
class Composition;
}

namespace avgen::directing {

struct SubjectIdentity {
    SubjectKind kind = SubjectKind::Unresolved;
    std::string id;    // canonical: entity name, hero name, node name, camera slug, parameter path
    std::string name;  // what a person calls it: the camera's display name; otherwise the id
    std::string node;  // the composition node it is, or drives; empty for cameras and parameters
    // An entity whose node is also a declared hero: one thing, which answers to "Rook" and to "the
    // Rook hero" alike. Heroes on nodes no entity drives are identities of kind Hero instead.
    bool hero = false;
    friend bool operator==(const SubjectIdentity&, const SubjectIdentity&) = default;
};

class SubjectIndex {
public:
    // Entities, heroes, remaining nodes, and cameras, from the composition as it is now.
    [[nodiscard]] static SubjectIndex fromComposition(const scene::Composition& composition);
    // Parameter paths are addressable too, but only by exact path (they are the escape hatch).
    void addParameters(std::span<const std::string> paths);

    [[nodiscard]] const std::vector<SubjectIdentity>& identities() const { return identities_; }
    [[nodiscard]] const std::vector<std::string>& parameters() const { return parameters_; }

private:
    std::vector<SubjectIdentity> identities_;
    std::vector<std::string> parameters_;
};

struct SubjectResult {
    enum class Status : std::uint8_t { Resolved, Ambiguous, Unknown, Unsupported };
    Status status = Status::Unknown;
    SubjectIdentity identity;                // when Resolved
    std::vector<SubjectIdentity> candidates; // when Ambiguous (or the wrong-kind matches when Unknown)
    std::optional<Issue> issue;              // for every status but Resolved
};

// `text` as the request wrote it; `hint` from the plan (Unresolved when none). `location` is where in
// the plan the reference sits, for the issue.
[[nodiscard]] SubjectResult resolveSubject(const SubjectIndex& index, std::string_view text,
                                           SubjectKind hint = SubjectKind::Unresolved, std::string_view location = {});

// Resolves every subject a plan declares, in place (kind + id), and returns the issues. A subject
// that already carries a resolution is checked, not re-resolved: an id the model chose from an
// ambiguity's candidates must still exist.
[[nodiscard]] std::vector<Issue> resolvePlanSubjects(Plan& plan, const SubjectIndex& index);

// Every time in a plan, placed: the seconds each TimeRef resolves to, by JSON pointer, plus the
// issues. The plan keeps its TimeRefs (the request); these are what the compiler and the diff read.
struct PlanTimes {
    std::vector<std::pair<std::string, double>> seconds; // (location, seconds), in plan order
    std::vector<std::pair<std::string, std::string>> explanations; // (location, "chorus 2 runs ...")
    std::vector<Issue> issues;
    [[nodiscard]] std::optional<double> at(std::string_view location) const;
};
[[nodiscard]] PlanTimes resolvePlanTimes(const Plan& plan, const MusicalContext& context);

} // namespace avgen::directing
