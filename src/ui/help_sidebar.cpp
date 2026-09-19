// The Help sidebar's navigation, as data rather than as draw calls (ADR-361).
//
// Deliberately ImGui-free and in its own translation unit, for the reason `output_preview.cpp` and
// `world_edit.cpp` are: the property this file exists to hold -- that no two sidebar rows share an
// identity -- is checkable without a window, a device or a frame, and a rule that can only be
// checked by looking at the screen is a rule nobody checks. `help_panel.cpp` draws these groups
// and decides nothing about them.

#include "ui/help_panel.hpp"

#include <span>
#include <string>
#include <utility>
#include <vector>

namespace avgen::ui {

std::vector<HelpSidebarGroup> helpSidebarGroups(const help::HelpDatabase& db,
                                                std::span<const std::string> recent) {
    std::vector<HelpSidebarGroup> groups;

    HelpSidebarGroup recentGroup;
    recentGroup.scope = "recent";
    recentGroup.title = "Recently viewed";
    recentGroup.separatorAfter = true;
    for (const std::string& id : recent) {
        if (const help::HelpDocument* doc = db.get(id); doc != nullptr) {
            recentGroup.documents.push_back(doc);
        }
    }
    if (!recentGroup.documents.empty()) {
        groups.push_back(std::move(recentGroup));
    }

    for (const help::HelpCategory& category : db.categories()) {
        HelpSidebarGroup group;
        // The scope is prefixed rather than being the bare category name, so a category that one
        // day is called "recent" cannot collide with the list above.
        group.scope = "cat/" + category.name;
        group.title = category.name;
        // Getting Started open, the rest closed: a tree that is entirely expanded is a list, and a
        // list of forty-six is not navigation.
        group.defaultOpen = category.name == "Getting Started";
        group.indented = true;
        group.documents.assign(category.documents.begin(), category.documents.end());
        groups.push_back(std::move(group));
    }
    return groups;
}

} // namespace avgen::ui
