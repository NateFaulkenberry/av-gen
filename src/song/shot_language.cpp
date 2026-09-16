#include "song/shot_language.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace avgen::song {
namespace {
using nlohmann::json;

// The treatment everything falls back to. A function-local static so the reference `intentFor`
// returns outlives every caller, and so a language that has somehow lost its neutral built-in still
// answers with a usable treatment rather than dereferencing nothing.
const ShotIntent& neutralIntent() {
    static const ShotIntent kFallback = [] {
        if (const ShotIntent* built = builtInShotIntent(neutralShotIntentId())) {
            return *built;
        }
        ShotIntent i;
        i.id = "steady_coverage";
        i.name = "Steady Coverage";
        i.builtIn = true;
        return i;
    }();
    return kFallback;
}
} // namespace

ShotLanguage::ShotLanguage() = default;

// ---- lookup -------------------------------------------------------------------------------------

const ShotIntent* ShotLanguage::findCustomIntent(std::string_view id) const {
    const auto it = std::find_if(customIntents_.begin(), customIntents_.end(),
                                 [id](const ShotIntent& i) { return i.id == id; });
    return it == customIntents_.end() ? nullptr : &*it;
}

const SectionType* ShotLanguage::findCustomType(std::string_view id) const {
    const auto it = std::find_if(customTypes_.begin(), customTypes_.end(),
                                 [id](const SectionType& t) { return t.id == id; });
    return it == customTypes_.end() ? nullptr : &*it;
}

const ShotIntent* ShotLanguage::intent(std::string_view id) const {
    if (const ShotIntent* custom = findCustomIntent(id)) {
        return custom;
    }
    return builtInShotIntent(id);
}

const SectionType* ShotLanguage::type(std::string_view id) const {
    if (const SectionType* custom = findCustomType(id)) {
        return custom;
    }
    return builtInSectionType(id);
}

// ---- enumeration --------------------------------------------------------------------------------

std::vector<const ShotIntent*> ShotLanguage::intents() const {
    std::vector<const ShotIntent*> out;
    out.reserve(builtInShotIntents().size() + customIntents_.size());
    for (const ShotIntent& built : builtInShotIntents()) {
        // A shadowed built-in appears once, in the built-in's position, carrying the custom's
        // definition -- so defining one does not make a picker reshuffle.
        const ShotIntent* custom = findCustomIntent(built.id);
        out.push_back(custom != nullptr ? custom : &built);
    }
    for (const ShotIntent& custom : customIntents_) {
        if (builtInShotIntent(custom.id) == nullptr) {
            out.push_back(&custom);
        }
    }
    return out;
}

std::vector<const SectionType*> ShotLanguage::types() const {
    std::vector<const SectionType*> out;
    out.reserve(builtInSectionTypes().size() + customTypes_.size());
    for (const SectionType& built : builtInSectionTypes()) {
        const SectionType* custom = findCustomType(built.id);
        out.push_back(custom != nullptr ? custom : &built);
    }
    for (const SectionType& custom : customTypes_) {
        if (builtInSectionType(custom.id) == nullptr) {
            out.push_back(&custom);
        }
    }
    return out;
}

std::vector<const SectionType*> ShotLanguage::typesInCategory(SectionCategory category) const {
    std::vector<const SectionType*> out;
    for (const SectionType* t : types()) {
        if (t->category == category) {
            out.push_back(t);
        }
    }
    return out;
}

// ---- definition ---------------------------------------------------------------------------------

Result<void> ShotLanguage::defineIntent(ShotIntent value) {
    value.builtIn = false; // a definition made here is the person's, whatever it claims
    if (auto ok = value.validate(); !ok) {
        return ok;
    }
    const auto it = std::find_if(customIntents_.begin(), customIntents_.end(),
                                 [&value](const ShotIntent& i) { return i.id == value.id; });
    if (it != customIntents_.end()) {
        *it = std::move(value);
    } else {
        customIntents_.push_back(std::move(value));
    }
    return {};
}

Result<void> ShotLanguage::defineType(SectionType value) {
    value.builtIn = false;
    if (auto ok = value.validate(); !ok) {
        return ok;
    }
    if (!hasIntent(value.defaultShotIntent)) {
        // A type whose default points at nothing produces a section with no treatment and no error,
        // which is indistinguishable from the feature simply not working. Refused at the door.
        return fail("section type '{}': its default shot intent '{}' is not defined", value.id,
                    value.defaultShotIntent);
    }
    const auto it = std::find_if(customTypes_.begin(), customTypes_.end(),
                                 [&value](const SectionType& t) { return t.id == value.id; });
    if (it != customTypes_.end()) {
        *it = std::move(value);
    } else {
        customTypes_.push_back(std::move(value));
    }
    return {};
}

Result<void> ShotLanguage::removeCustomIntent(std::string_view id) {
    const auto it = std::find_if(customIntents_.begin(), customIntents_.end(),
                                 [id](const ShotIntent& i) { return i.id == id; });
    if (it == customIntents_.end()) {
        return fail("no custom shot intent '{}' to remove", id);
    }
    // Removing an intent that shadows a built-in restores the built-in, so nothing can be left
    // dangling by that. Removing one that nothing shadows must not leave a type pointing at nothing.
    if (builtInShotIntent(id) == nullptr) {
        for (const SectionType& t : customTypes_) {
            if (t.defaultShotIntent == id) {
                return fail("shot intent '{}' is still the default for section type '{}'", id, t.id);
            }
        }
    }
    customIntents_.erase(it);
    return {};
}

bool ShotLanguage::removeCustomType(std::string_view id) {
    const auto it = std::find_if(customTypes_.begin(), customTypes_.end(),
                                 [id](const SectionType& t) { return t.id == id; });
    if (it == customTypes_.end()) {
        return false;
    }
    customTypes_.erase(it);
    return true;
}

// ---- resolution ---------------------------------------------------------------------------------

ShotLanguage::Resolution ShotLanguage::resolutionOf(const Section& section) const {
    if (section.shotIntent && intent(*section.shotIntent) != nullptr) {
        return Resolution::Override;
    }
    const SectionType* t = type(section.type);
    if (t == nullptr) {
        return Resolution::MissingType;
    }
    if (intent(t->defaultShotIntent) == nullptr) {
        return Resolution::MissingIntent;
    }
    // An override naming an intent nobody defined also lands here: the type's default is what
    // actually answered, and the UI needs to be able to say the chosen one is missing.
    return section.shotIntent ? Resolution::MissingIntent : Resolution::TypeDefault;
}

const ShotIntent& ShotLanguage::intentFor(const Section& section) const {
    if (section.shotIntent) {
        if (const ShotIntent* chosen = intent(*section.shotIntent)) {
            return *chosen;
        }
    }
    if (const SectionType* t = type(section.type)) {
        if (const ShotIntent* fromType = intent(t->defaultShotIntent)) {
            return *fromType;
        }
    }
    return neutralIntent();
}

ShotIntentId ShotLanguage::intentIdFor(const Section& section) const {
    return intentFor(section).id;
}

std::string ShotLanguage::typeName(std::string_view typeId) const {
    if (const SectionType* t = type(typeId)) {
        return t->name;
    }
    // The raw id rather than nothing: a name that is wrong is diagnosable and a blank one is not.
    return std::string(typeId);
}

std::string ShotLanguage::displayName(const Section& section) const {
    if (!section.label.empty()) {
        return section.label;
    }
    const std::string base = typeName(section.type);
    if (section.occurrence <= 0) {
        return base;
    }
    // "Verse", "Verse 2", "Verse 3": one type, three occurrences, no stored strings.
    return fmt::format("{} {}", base, section.occurrence + 1);
}

// ---- persistence --------------------------------------------------------------------------------

json ShotLanguage::toJson() const {
    json out = json::object();
    if (!customIntents_.empty()) {
        json intents = json::array();
        for (const ShotIntent& i : customIntents_) {
            intents.push_back(shotIntentToJson(i));
        }
        out["intents"] = std::move(intents);
    }
    if (!customTypes_.empty()) {
        json types = json::array();
        for (const SectionType& t : customTypes_) {
            types.push_back(sectionTypeToJson(t));
        }
        out["types"] = std::move(types);
    }
    return out;
}

Result<ShotLanguage> ShotLanguage::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("a shot language must be an object");
    }
    ShotLanguage out;
    // Intents first: a type is refused if its default is not defined yet, and a project's own types
    // routinely point at that project's own intents.
    if (const auto it = j.find("intents"); it != j.end()) {
        if (!it->is_array()) {
            return fail("shot language: 'intents' must be an array");
        }
        for (const json& e : *it) {
            auto parsed = shotIntentFromJson(e);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            if (auto ok = out.defineIntent(std::move(*parsed)); !ok) {
                return std::unexpected(ok.error());
            }
        }
    }
    if (const auto it = j.find("types"); it != j.end()) {
        if (!it->is_array()) {
            return fail("shot language: 'types' must be an array");
        }
        for (const json& e : *it) {
            auto parsed = sectionTypeFromJson(e);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            if (auto ok = out.defineType(std::move(*parsed)); !ok) {
                return std::unexpected(ok.error());
            }
        }
    }
    return out;
}

} // namespace avgen::song
