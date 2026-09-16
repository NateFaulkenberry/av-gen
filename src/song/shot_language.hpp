#pragma once

// **The shot language**: the vocabulary of section types and shot intents a project is working in,
// built-ins and the person's own together (ADR-247).
//
// One object, because the two registries are only ever useful together: a type's default treatment
// names an intent, and refusing a type whose intent does not exist is the check that stops a section
// resolving to nothing. Splitting them would mean that check had to be performed by whoever happened
// to hold both.
//
// ## Built-ins are code; customs are data
//
// `toJson` writes **only** what a person defined. The built-in library is compiled in, so writing it
// would mean a project saved today could resurrect a definition the engine has since improved -- the
// same argument that stops the beat markers being saved. A project that never defined anything
// serializes to nothing at all and its file is unchanged, which is what makes this safe to add to
// every sequence.
//
// ## Shadowing
//
// A custom definition may take a built-in's id, and then it wins for that project. This is the
// type-level half of the brief's "the user must be able to override them": the per-section half is
// `Section::shotIntent`, and this is for a person who wants *every* Intro in this piece treated as an
// extreme close-up rather than choosing it eleven times. Removing the custom restores the built-in,
// so a shadow is reversible and nothing is destroyed by making one.

#include "core/error.hpp"
#include "song/section_timeline.hpp"
#include "song/section_type.hpp"
#include "song/shot_intent.hpp"

#include <nlohmann/json_fwd.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::song {

class ShotLanguage {
public:
    // Starts with the built-in vocabulary and nothing else.
    ShotLanguage();

    // ---- lookup ---------------------------------------------------------------------------------
    // Customs first, then built-ins. Null when nothing has that id.
    [[nodiscard]] const ShotIntent* intent(std::string_view id) const;
    [[nodiscard]] const SectionType* type(std::string_view id) const;
    [[nodiscard]] bool hasIntent(std::string_view id) const { return intent(id) != nullptr; }
    [[nodiscard]] bool hasType(std::string_view id) const { return type(id) != nullptr; }

    // ---- enumeration, for a picker --------------------------------------------------------------
    // Built-in order first, then customs in definition order, with a shadowed built-in appearing
    // once, in the built-in's position, carrying the custom's definition. A stable order, so a list
    // does not reshuffle when somebody defines something.
    [[nodiscard]] std::vector<const ShotIntent*> intents() const;
    [[nodiscard]] std::vector<const SectionType*> types() const;
    [[nodiscard]] std::vector<const SectionType*> typesInCategory(SectionCategory category) const;

    // ---- definition (the brief's section 4) -----------------------------------------------------
    //
    // A custom type is a first-class citizen and takes exactly the same path through every other
    // function in this namespace as a built-in does.
    //
    // `defineType` refuses a type whose `defaultShotIntent` this language does not know, and refuses
    // a malformed id. `defineIntent` refuses a malformed id and out-of-range dials. Neither clamps:
    // ADR-225's rule, because a clamped value is indistinguishable from an authored one forever after.
    Result<void> defineIntent(ShotIntent value);
    Result<void> defineType(SectionType value);

    // Removes a custom definition. Removing one that shadows a built-in restores the built-in.
    // Refuses to remove a custom intent that a custom type still names as its default, because that
    // would leave the type pointing at nothing -- which is the state `defineType` exists to prevent.
    Result<void> removeCustomIntent(std::string_view id);
    bool removeCustomType(std::string_view id);

    [[nodiscard]] std::span<const ShotIntent> customIntents() const { return customIntents_; }
    [[nodiscard]] std::span<const SectionType> customTypes() const { return customTypes_; }
    // Whether this language differs from a freshly constructed one. What `Sequence::toJson` asks
    // before writing anything.
    [[nodiscard]] bool customized() const {
        return !customIntents_.empty() || !customTypes_.empty();
    }

    // ---- resolution -----------------------------------------------------------------------------
    //
    // The three questions everything downstream asks. `intentFor` is the one the director uses, and
    // it is the only one it needs: a director that has an intent has everything the section can tell
    // it, and never learns what type produced it.

    // The section's own override, else its type's default, else the neutral intent. Never null, so a
    // director need not carry a fallback of its own -- a section naming a type nobody defined still
    // gets a treatment, and `resolutionOf` is how a UI finds out that happened.
    //
    // The reference is into this object and is invalidated by any later `defineIntent`, exactly as a
    // reference into a `std::vector` is. Anything that outlives the call should take a copy --
    // `SectionCue` does, which is why a cue sheet is safe to hold across an edit.
    [[nodiscard]] const ShotIntent& intentFor(const Section& section) const;
    [[nodiscard]] ShotIntentId intentIdFor(const Section& section) const;

    // Where `intentFor` got its answer. Exists so a UI can mark a section whose type is missing --
    // which happens when a project defined a custom type and the definition was later removed -- and
    // so a test can tell "it fell back" from "it resolved".
    enum class Resolution : std::uint8_t {
        Override,     // the person chose this treatment for this section
        TypeDefault,  // the section's type supplied it
        MissingIntent,// the type named an intent nobody defined; the neutral one was used
        MissingType,  // the section names a type nobody defined; the neutral one was used
    };
    [[nodiscard]] Resolution resolutionOf(const Section& section) const;

    // The section's name for a person: its label when it has one, otherwise its type's name with the
    // occurrence appended -- "Verse", "Verse 2", "Verse 3". A section whose type is missing reads as
    // its raw type id rather than as nothing, because a name that is wrong is diagnosable and a name
    // that is blank is not.
    [[nodiscard]] std::string displayName(const Section& section) const;
    // The type's display name, or the id itself when it is unknown.
    [[nodiscard]] std::string typeName(std::string_view typeId) const;

    // ---- persistence ----------------------------------------------------------------------------
    // Writes customs only; a language with none serializes to an empty object.
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<ShotLanguage> fromJson(const nlohmann::json& j);

    friend bool operator==(const ShotLanguage& a, const ShotLanguage& b) {
        return a.customIntents_ == b.customIntents_ && a.customTypes_ == b.customTypes_;
    }

private:
    [[nodiscard]] const ShotIntent* findCustomIntent(std::string_view id) const;
    [[nodiscard]] const SectionType* findCustomType(std::string_view id) const;

    std::vector<ShotIntent> customIntents_;
    std::vector<SectionType> customTypes_;
};

} // namespace avgen::song
