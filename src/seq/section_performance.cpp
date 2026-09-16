#include "seq/section_performance.hpp"

#include "seq/song_structure.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <set>
#include <utility>

namespace avgen::seq {

SectionPerformanceTable defaultSectionPerformanceTable() {
    return [](signals::MusicalSection) -> std::optional<SectionPerformance> { return std::nullopt; };
}

GeneratedDirection generatePerformanceEvents(const analysis::SongStructure& structure,
                                          const GenerationOptions& options) {
    GeneratedDirection out;
    if (!options.table) {
        out.warnings.emplace_back(
            "no section-direction table: generation needs the Director decision layer");
        return out;
    }
    const std::vector<signals::MusicalSection> kinds =
        sectionKindsFor(structure, options.finalFraction);

    // One event per distinct *name*, in the order the names first appear. A name rather than a kind
    // because that is what `TriggerKind::Section` matches on, and first-appearance order rather than
    // alphabetical because a sequence a person reads should run in the order the piece does.
    std::set<std::string> seen;
    std::set<signals::MusicalSection> declined;
    for (std::size_t i = 0; i < structure.sections.size(); ++i) {
        const std::string name = sectionDisplayName(structure.sections[i]);
        if (name.empty() || !seen.insert(name).second) {
            continue;
        }
        const signals::MusicalSection kind = kinds[i];
        const std::optional<SectionPerformance> direction = options.table(kind);
        if (!direction) {
            declined.insert(kind);
            continue;
        }
        SequenceEvent event;
        event.id = fmt::format("{}.{}", options.idPrefix, name);
        event.when.kind = TriggerKind::Section;
        event.when.name = name;
        event.when.repeat = 0; // every occurrence of this name, which is the point of a table
        event.when.delaySeconds = direction->delaySeconds;
        event.what.kind = EventActionKind::EntityAction;
        event.what.target = direction->subject;
        event.what.value = direction->verb;
        event.what.argument = direction->argument;
        event.priority = direction->priority;
        out.events.push_back(std::move(event));
    }
    for (const signals::MusicalSection kind : declined) {
        out.warnings.push_back(
            fmt::format("no direction for '{}'", signals::musicalSectionName(kind)));
    }
    return out;
}

} // namespace avgen::seq
