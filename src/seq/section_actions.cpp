#include "seq/section_actions.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace avgen::seq {
namespace {

std::string knownVerbs() {
    std::string out;
    for (const entity::ActionKind kind :
         {entity::ActionKind::Wait, entity::ActionKind::Move, entity::ActionKind::Face,
          entity::ActionKind::Pose, entity::ActionKind::Interact, entity::ActionKind::Equip,
          entity::ActionKind::Unequip, entity::ActionKind::Set}) {
        if (!out.empty()) {
            out += ", ";
        }
        out += entity::actionKindName(kind);
    }
    return out;
}

} // namespace

const SectionPerformance* SectionPerformanceSet::find(signals::MusicalSection kind) const {
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [&](const SectionPerformanceEntry& e) { return e.kind == kind; });
    return it == entries.end() ? nullptr : &it->direction;
}

Result<void> validate(const SectionPerformanceSet& set) {
    for (const SectionPerformanceEntry& entry : set.entries) {
        const char* kindName = signals::musicalSectionName(entry.kind);
        if (entry.direction.subject.empty()) {
            return fail("section direction for '{}': needs a subject to act on", kindName);
        }
        if (!entity::actionKindFromName(entry.direction.verb)) {
            return fail("section direction for '{}': '{}' is not a verb ({})", kindName,
                        entry.direction.verb, knownVerbs());
        }
        if (entry.direction.delaySeconds < 0.0) {
            return fail("section direction for '{}': a delay of {} s is before the section begins",
                        kindName, entry.direction.delaySeconds);
        }
    }
    return {};
}

nlohmann::json toJson(const SectionPerformanceSet& set) {
    nlohmann::json out = nlohmann::json::array();
    for (const SectionPerformanceEntry& entry : set.entries) {
        nlohmann::json row;
        row["section"] = signals::musicalSectionName(entry.kind);
        row["subject"] = entry.direction.subject;
        row["verb"] = entry.direction.verb;
        // Written only when they carry something, so a hand-edited file stays readable and a
        // round-trip does not grow four defaults per row.
        if (!entry.direction.argument.empty()) {
            row["argument"] = entry.direction.argument;
        }
        if (entry.direction.priority != 0) {
            row["priority"] = entry.direction.priority;
        }
        if (entry.direction.delaySeconds != 0.0) {
            row["delay"] = entry.direction.delaySeconds;
        }
        out.push_back(std::move(row));
    }
    return out;
}

Result<SectionPerformanceSet> sectionPerformanceSetFromJson(const nlohmann::json& doc) {
    SectionPerformanceSet set;
    if (doc.is_null()) {
        return set;
    }
    if (!doc.is_array()) {
        return fail("sectionPerformance must be an array of rows");
    }
    for (const nlohmann::json& row : doc) {
        if (!row.is_object()) {
            return fail("sectionPerformance: every row must be an object");
        }
        SectionPerformanceEntry entry;
        const std::string section = row.value("section", std::string());
        const auto kind = signals::musicalSectionFromName(section);
        if (!kind) {
            return fail("sectionPerformance: '{}' is not a section kind", section);
        }
        entry.kind = *kind;
        entry.direction.subject = row.value("subject", std::string());
        entry.direction.verb = row.value("verb", std::string());
        entry.direction.argument = row.value("argument", std::string());
        entry.direction.priority = row.value("priority", 0);
        entry.direction.delaySeconds = row.value("delay", 0.0);
        set.entries.push_back(std::move(entry));
    }
    if (auto ok = validate(set); !ok) {
        return std::unexpected(ok.error());
    }
    return set;
}

SectionPerformanceTable tableFrom(SectionPerformanceSet set) {
    return [set = std::move(set)](signals::MusicalSection kind) -> std::optional<SectionPerformance> {
        if (const SectionPerformance* found = set.find(kind)) {
            return *found;
        }
        return std::nullopt;
    };
}

Result<DirectedAction> actionFromEvent(std::string_view subject, std::string_view verb,
                                       std::string_view argument) {
    if (subject.empty()) {
        return fail("a section action needs a subject");
    }
    const auto kind = entity::actionKindFromName(verb);
    if (!kind) {
        return fail("'{}' is not a verb ({})", verb, knownVerbs());
    }
    DirectedAction out;
    out.entity = std::string(subject);
    out.action.kind = *kind;
    out.action.name = fmt::format("section.{}", entity::actionKindName(*kind));
    // What the argument means is the verb's business.
    //
    // `move` and `face` travel toward or turn toward something, so their argument is another entity
    // -- `EntityRef` rather than `Node`, because the point of directing a character at a *character*
    // is that it tracks where that character is now rather than where its node was authored.
    //
    // `pose` names an activity, not a place, so it goes in `activity` and not in the target at all;
    // that distinction is the whole of `ActionDesc`'s comment about activities never being clips.
    //
    // `interact` names a verb the prop published, which `ActionTarget` carries as `member` with the
    // prop in `name` -- so an argument of "lantern.light" is the only place a dot means anything
    // here, and an argument without one is an interaction with no verb, which the action system
    // refuses on its own terms rather than this file guessing a default.
    switch (*kind) {
    case entity::ActionKind::Move:
    case entity::ActionKind::Face:
        if (!argument.empty()) {
            out.action.target.kind = entity::TargetKind::EntityRef;
            out.action.target.name = std::string(argument);
        }
        break;
    case entity::ActionKind::Pose:
        out.action.activity = std::string(argument);
        break;
    case entity::ActionKind::Interact: {
        const auto dot = argument.find('.');
        out.action.target.kind = entity::TargetKind::Interaction;
        out.action.target.name =
            std::string(dot == std::string_view::npos ? argument : argument.substr(0, dot));
        if (dot != std::string_view::npos) {
            out.action.target.member = std::string(argument.substr(dot + 1));
        }
        break;
    }
    case entity::ActionKind::Equip:
    case entity::ActionKind::Unequip:
        if (!argument.empty()) {
            out.action.target.kind = entity::TargetKind::Node;
            out.action.target.name = std::string(argument);
        }
        break;
    case entity::ActionKind::Wait:
    case entity::ActionKind::Set:
        // Neither takes a target from a section row: a wait is a duration and a set is a property
        // write the authored action system already has a spelling for.
        break;
    }
    return out;
}

} // namespace avgen::seq
