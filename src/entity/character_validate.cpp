#include "entity/character_validate.hpp"

#include "entity/decision.hpp"
#include "entity/mind.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <set>

namespace avgen::entity {
namespace {

std::vector<std::string> strings(const nlohmann::json& j, const char* key) {
    std::vector<std::string> out;
    if (j.contains(key) && j[key].is_array()) {
        for (const auto& v : j[key]) {
            if (v.is_string()) {
                out.push_back(v.get<std::string>());
            }
        }
    }
    return out;
}

void collectEvents(const std::vector<ActionDesc>& actions, std::set<std::string>& out) {
    for (const ActionDesc& a : actions) {
        if (!a.onComplete.empty()) {
            out.insert(a.onComplete);
        }
    }
}

} // namespace

std::vector<ValidationIssue>
validateCharacters(const std::vector<EntityDesc>& entities,
                   const std::vector<EntityWorld::EventProfile>& eventProfiles) {
    std::vector<ValidationIssue> issues;
    const auto error = [&](const EntityDesc& e, std::string m) {
        issues.push_back({ValidationIssue::Severity::Error, e.name, std::move(m)});
    };
    const auto warn = [&](const EntityDesc& e, std::string m) {
        issues.push_back({ValidationIssue::Severity::Warning, e.name, std::move(m)});
    };

    // The scene's vocabulary: every tag, every raisable event, every offered verb.
    std::set<std::string> tags = {"landmark", "character", "glow", "water", "vista"};
    std::set<std::string> events;
    for (const EntityWorld::EventProfile& p : eventProfiles) {
        events.insert(p.name);
    }
    for (const EntityDesc& e : entities) {
        tags.insert(e.tags.begin(), e.tags.end());
        collectEvents(e.actions, events);
        for (const ScheduleEntry& entry : e.schedule.entries) {
            collectEvents(entry.actions, events);
        }
        for (const InteractionDesc& i : e.interactions) {
            if (!i.onComplete.empty()) {
                events.insert(i.onComplete);
            }
        }
    }
    const auto knownKind = [](const std::string& kind) {
        const auto kinds = considererKinds();
        return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
    };
    const auto hasClip = [](const EntityDesc& e, const std::string& activity) {
        return std::any_of(e.clips.begin(), e.clips.end(),
                           [&](const auto& c) { return c.first == activity; });
    };

    for (const EntityDesc& e : entities) {
        for (const BehaviorDesc& b : e.behaviors) {
            if (b.kind != "decide" || !b.settings.is_object()) {
                continue;
            }
            const bool aware = b.settings.contains("mind");
            if (!b.settings.contains("considerers") || !b.settings["considerers"].is_array() ||
                b.settings["considerers"].empty()) {
                error(e, "decide has no considerers: it can never choose anything");
                continue;
            }
            for (const auto& c : b.settings["considerers"]) {
                const std::string kind = c.value("kind", "");
                const std::string name = c.value("name", kind);
                if (!knownKind(kind)) {
                    error(e, fmt::format("considerer '{}': unknown kind '{}'", name, kind));
                    continue;
                }
                const bool readsPercepts = kind == "investigate" || kind == "social" ||
                                           (kind == "interest" && c.value("source", "perceived") == "perceived") ||
                                           (kind == "route" && c.value("source", "perceived") == "perceived");
                if (readsPercepts && !e.perceives) {
                    error(e, fmt::format("considerer '{}' ({}) scores what the body perceives, and "
                                         "the body has no 'perception' block: it will never see anything",
                                         name, kind));
                }
                if ((kind == "react" || kind == "social") && !aware) {
                    error(e, fmt::format("considerer '{}' ({}) needs the decider's 'mind' block "
                                         "(events and personality); without it it never offers anything",
                                         name, kind));
                }
                for (const std::string& t : strings(c, "tags")) {
                    if (!tags.contains(t)) {
                        error(e, fmt::format("considerer '{}': tag '{}' is carried by nothing in this "
                                             "scene, so the filter matches nothing",
                                             name, t));
                    }
                }
                if (kind == "react") {
                    for (const std::string& ev : strings(c, "events")) {
                        if (!events.contains(ev)) {
                            error(e, fmt::format("considerer '{}': event '{}' is raised by nothing in "
                                                 "this scene",
                                                 name, ev));
                        }
                    }
                }
                if (c.contains("traits") && c["traits"].is_object()) {
                    for (const auto& [trait, value] : c["traits"].items()) {
                        (void)value;
                        if (!personalityTrait(trait)) {
                            error(e, fmt::format("considerer '{}': unknown personality trait '{}'",
                                                 name, trait));
                        }
                    }
                }
                const std::string affordance = c.value("affordance", "");
                if (!affordance.empty()) {
                    bool offered = false;
                    bool usable = false;
                    for (const EntityDesc& prop : entities) {
                        for (const InteractionDesc& i : prop.interactions) {
                            if (i.name != affordance) {
                                continue;
                            }
                            offered = true;
                            usable = usable || std::all_of(i.required.begin(), i.required.end(),
                                                           [&](const std::string& r) {
                                                               return std::find(e.capabilities.begin(),
                                                                                e.capabilities.end(), r) !=
                                                                      e.capabilities.end();
                                                           });
                        }
                    }
                    if (!offered) {
                        error(e, fmt::format("considerer '{}': affordance '{}' is offered by nothing "
                                             "in this scene",
                                             name, affordance));
                    } else if (!usable) {
                        warn(e, fmt::format("considerer '{}': every '{}' in this scene requires a "
                                            "capability this character lacks; it will observe instead",
                                            name, affordance));
                    }
                }
                for (const char* key : {"activity", "fleeActivity"}) {
                    const std::string activity = c.value(key, "");
                    if (!activity.empty() && !e.clips.empty() && !hasClip(e, activity)) {
                        warn(e, fmt::format("considerer '{}': activity '{}' maps to no clip on this "
                                            "character",
                                            name, activity));
                    }
                }
            }
        }
    }
    return issues;
}

} // namespace avgen::entity
