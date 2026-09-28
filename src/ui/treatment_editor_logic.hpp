#pragma once

// A section's treatment, edited where it is chosen (ADR-937).
//
// The Sequence panel's section inspector has a "shot" picker listing every treatment -- the built-ins
// and the project's own, which is where Glowmere Valley 3's seven appear by name ("GV3: the riser's
// roll") -- and, since ADR-921, a line saying what the chosen one does to the cutting. What it did not
// have was any way to change one. A custom treatment's dials (who it is about, the shot sizes, the
// movement, the cut rate, the arc...) could only be edited in the project file, so the thing that sets
// how a section is cut was visible in the picture and unreachable from the app.
//
// Everything the inspector decides about that is here, without a window, as the Effects and Lights
// panels' decisions are: which dials a treatment has and what they are called, which treatments can be
// edited in place, how many sections an edit reaches, the edit itself, and "edit a copy" for a built-in.
// `sequence_panel.cpp` draws the answers; tests/unit/test_treatment_editor.cpp checks them.
//
// **Custom is data, built-in is code** (ADR-247). A custom treatment is the project's: an edit replaces
// its definition in the project's shot language (`ShotLanguage::defineIntent`), so every section using
// it follows, and the project's save writes it (`Sequence::toJson` writes the customs). A built-in is
// compiled in, so it is shown and not edited; "Edit a copy" defines a custom copy and gives it to the
// section, which is the whole of how a built-in becomes editable without editing the engine.

#include "core/error.hpp"
#include "song/section_timeline.hpp"
#include "song/shot_intent.hpp"
#include "song/shot_language.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace avgen::ui {

enum class TreatmentDialKind : std::uint8_t {
    Amount,  // one of the 0..1 floats; `amount` names it
    Focus,   // `focus`, one of `allSubjectFocuses()`
    Framing, // `framing`: the tightest and the widest shot size
    Cameras, // `cameras`: the fewest and the most (0 = as many as there are)
    Arc,     // `arc`, one of `allArcs()`
};

// One of a treatment's dials, as the inspector draws it. `label` is what an artist reads beside it (the
// shape of the cut, in words); `tip` what it does, said where it is hovered.
struct TreatmentDial {
    const char* key = "";   // the field's name in the project file's shot language
    const char* label = "";
    const char* tip = "";
    TreatmentDialKind kind = TreatmentDialKind::Amount;
    float song::ShotIntent::*amount = nullptr; // Amount dials only
};

// Every dial a treatment has, in the order the inspector draws them. Every field of `ShotIntent` a
// director reads is here (the test counts them), so a dial added to the struct and not to this list is
// a failing test rather than a setting only the file can reach.
[[nodiscard]] std::span<const TreatmentDial> treatmentDials();

// What each choice of an enumerated dial is called in the inspector.
[[nodiscard]] const char* focusLabel(song::SubjectFocus focus);
[[nodiscard]] const char* framingLabel(song::Framing framing);
[[nodiscard]] const char* arcLabel(song::Arc arc);

// Whether `id` is one of the project's own treatments (a custom definition, including one that shadows
// a built-in's id), which the inspector edits in place. A built-in or an unknown id is not.
[[nodiscard]] bool treatmentEditable(const song::ShotLanguage& language, std::string_view id);

// How many sections of `timeline` resolve to `id` (by their own choice or their type's default), so
// the inspector can say how far an edit reaches before it is made.
[[nodiscard]] std::size_t treatmentUsers(const song::SectionTimeline& timeline, const song::ShotLanguage& language,
                                         std::string_view id);

// Replaces one of the project's own treatments with `edited` (matched by its id; its `builtIn` is
// ignored, as `defineIntent` ignores it). Refused, and nothing changed, for a built-in or an unknown id
// -- "edit a copy" is the way to a built-in -- and for dials out of range: ADR-225's rule, refused and
// not clamped, as `ShotIntent::validate` says.
Result<void> editTreatment(song::ShotLanguage& language, const song::ShotIntent& edited);

// "Edit a copy": the treatment section `index` resolves to, defined as one of the project's own under a
// new id ("<id>_custom", then "_2", "_3"... while taken) and name ("<name> (custom)"), and chosen for that
// section. Returns the new id. Refused for an index outside the timeline.
Result<song::ShotIntentId> copyTreatmentForSection(song::ShotLanguage& language, song::SectionTimeline& timeline,
                                                   std::size_t index);

} // namespace avgen::ui
