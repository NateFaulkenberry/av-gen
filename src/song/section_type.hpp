#pragma once

// **Section types: what a passage *is*, as far as the person making the film is concerned** (ADR-247).
//
// A section type is not a musical label. It is allowed to be one -- `Chorus` is in the built-in list
// -- but the model does not know that and must not come to. The brief is explicit about this
// (section 14, marked Critical): "Do not encode the assumption that a section is always a musical
// structure." So `Ocean Ambience`, `Dream Sequence` and `Character Reveal` are not a second, lesser
// class of thing bolted on beside Verse and Chorus. They are the same class of thing, they take the
// same path through the code, and the only difference between `chorus` and `ocean_ambience` is that
// one ships with the engine and the other was typed in.
//
// The test that holds this honest is `tests/unit/test_section_type.cpp`'s custom-type suite: it runs
// the built-in assertions a second time against three types that did not exist when the engine was
// compiled. If anything anywhere ever special-cases the structural vocabulary, that suite fails.
//
// ## A type points at an intent; it does not contain one
//
// The brief asks whether a type should also carry visual preferences -- energy, movement, camera
// distance, cut frequency. The answer here is **no**, and it is a deliberate answer rather than an
// omission:
//
//   * Those preferences already exist, once, on `ShotIntent`. A type carrying its own copy would give
//     every question two places to look and two ways to disagree.
//   * The brief also says "do not expose a giant intimidating matrix of settings". A type with a name,
//     a description, a category and a default treatment is four fields. A type that also carried nine
//     dials would be the matrix.
//   * A user who wants different dials wants a different *treatment*, and can define one:
//     `ShotLanguage::defineIntent` is as available as `defineType`.
//
// The cost is that a custom look is two steps rather than one, and that cost is accepted and recorded
// here rather than discovered later.
//
// ## Repeats are not types
//
// `Verse`, `Verse 2` and `Verse 3` are one type and three occurrences. `Section::occurrence` carries
// the number and `ShotLanguage::displayName` composes it. Nothing needs three types and nothing gets
// them -- which is also what makes "the last chorus is a different shot from the first" derivable for
// a custom type nobody anticipated, rather than a special enumerator somebody had to add.

#include "core/error.hpp"
#include "song/shot_intent.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace avgen::song {

// A stable identifier, same rules as `ShotIntentId` (`validateId`).
using SectionTypeId = std::string;

// What sort of thing a type is. Used for grouping in a picker and for nothing else -- in particular,
// no behaviour anywhere switches on a category. It is a filing decision, not a semantic one, which is
// why `Custom` sits alongside the others rather than above them: a user-defined type may legitimately
// be a structural one, and one that calls itself Cinematic gets no different treatment.
enum class SectionCategory : std::uint8_t {
    Structural, // Intro, Verse, Chorus, Bridge: the shape of a song
    Energy,     // Build, Drop, Breakdown, Pause: what the arrangement is doing
    Texture,    // Ambient, Groove, Solo, Acapella: what it sounds like
    Cinematic,  // Dream Sequence, Montage, Character Reveal: what it is *for*, visually
    Custom,     // whatever the person filing it meant
};
[[nodiscard]] const char* sectionCategoryName(SectionCategory c);
[[nodiscard]] std::optional<SectionCategory> sectionCategoryFromName(std::string_view name);
[[nodiscard]] std::span<const SectionCategory> allSectionCategories();

// One entry in the vocabulary.
struct SectionType {
    SectionTypeId id;
    std::string name;        // "Pre-Chorus", "Ocean Ambience"
    std::string description; // one sentence
    SectionCategory category = SectionCategory::Custom;
    // The treatment a section of this type gets unless the person overrode it. Must name an intent
    // the `ShotLanguage` knows; `defineType` refuses one that does not, because a type whose default
    // points at nothing produces a section with no treatment and no error, which is the failure mode
    // that looks like the feature simply not working.
    ShotIntentId defaultShotIntent;

    bool builtIn = false;

    [[nodiscard]] Result<void> validate() const;
    friend bool operator==(const SectionType&, const SectionType&) = default;
};

[[nodiscard]] nlohmann::json sectionTypeToJson(const SectionType& type);
[[nodiscard]] Result<SectionType> sectionTypeFromJson(const nlohmann::json& j);

// ---- the built-in vocabulary --------------------------------------------------------------------
//
// The brief's four lists, implemented as given. Some entries are near-synonyms -- Build and Build-Up,
// Rise and Riser, Peak and Climax, Swell and Crescendo -- and they are all kept, because a person
// reaching for the word "Riser" should find it rather than be told the correct word is "Rise". Near
// synonyms share a default treatment; that is the whole of what being a synonym costs.

[[nodiscard]] std::span<const SectionType> builtInSectionTypes();
[[nodiscard]] const SectionType* builtInSectionType(std::string_view id);

// The type an unlabelled or unrecognised passage becomes: "Phrase", an ordinary passage.
//
// The same word `signals::MusicalSection::Phrase` uses, and for the same reason -- the analyzer is
// allowed to answer "I do not know" (`analysis::SectionFunction::Other`) and a film is not. This is
// what "I do not know" becomes once something has to point a camera. Nothing else maps to it, so a
// `phrase` section in a timeline always means exactly that.
[[nodiscard]] SectionTypeId neutralSectionTypeId();

} // namespace avgen::song
