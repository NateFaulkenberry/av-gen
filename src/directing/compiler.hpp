#pragma once

// The Director's compiler: a validated plan -> ordinary native content, in a staging copy, with a
// human-readable diff (spec §20-§21, ADR-756).
//
// It is deterministic -- the same plan against the same facts produces the same content, byte for
// byte -- and it writes nothing the person could not have made by hand:
//
//   shot            a `seq::Shot` (the framing) AND a `scene::CameraShot` (which camera is live), so
//                   the two shot types of ADR-245 are written together and cannot disagree
//   framing move    the shot's `seq::ShotCamera` move against the place (wide, close, reveal, ...):
//                   baked to keys at install, editable in the shot inspector as that move
//   follow / chase  a `scene::CameraRig` following the character's node, cut to by the CameraShot:
//                   evaluated per frame, editable in the Cameras panel
//   marker          a `seq::Marker` of kind Cue
//   parameter cue   a `seq::SequenceEvent` SetParameter on a Time trigger: baked, scrub-safe
//
// Performances, retimes, effect cues and time-varying camera moves are not compiled in Slice 1; the
// validator reports each, and the diff says so.
//
// **Revisions.** When the project already holds a plan with this id, its `produced` content is taken
// out of the staging copy first -- unless its fingerprint no longer matches, which means the person
// edited it by hand; the validator has then blocked the item and the content stays exactly as they
// left it. The new plan's `produced` is what this compilation wrote.

#include "directing/plan.hpp"
#include "directing/scene_facts.hpp"
#include "directing/validator.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace avgen::directing {

struct DiffLine {
    // '+' added, '-' removed, '~' replaced (a revision), '!' a finding the person must see
    char sign = '+';
    std::string item; // the plan key, empty for plan-wide lines
    std::string text;
    friend bool operator==(const DiffLine&, const DiffLine&) = default;
};

struct Compilation {
    Plan plan;                       // resolved; revision and `produced` set as an apply would store it
    Validation validation;
    seq::Sequence sequence;          // the staging copy, compiled into
    scene::CameraDirection cameras;  // the staging copy, compiled into
    std::vector<DiffLine> diff;
    [[nodiscard]] bool changesAnything() const;
    // The diff as text, one line per entry: "+ Shot "rook-umbra" 01:30.000-01:35.000 ...".
    [[nodiscard]] std::string diffText() const;
};

[[nodiscard]] Compilation compilePlan(Plan plan, const SceneFacts& facts);

// ---- provenance: finding and fingerprinting native content --------------------------------------

// The native content a `produced` entry names, as JSON, or nothing when it is gone.
[[nodiscard]] std::optional<nlohmann::json> contentOf(const ContentRef& ref, const seq::Sequence& sequence,
                                                      const scene::CameraDirection& cameras);
// A stable fingerprint of a document (FNV-1a 64 over its canonical dump).
[[nodiscard]] std::string fingerprint(const nlohmann::json& content);

} // namespace avgen::directing
