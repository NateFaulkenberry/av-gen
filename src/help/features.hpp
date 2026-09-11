#pragma once

// Feature metadata (§5): the row that ties a thing the application has to the topic that explains
// it, the key that invokes it and the menu it lives on.
//
// §5 is explicit that this must not become a reflection framework, and §52 is explicit about why it
// exists at all: a feature, its documentation, its keyboard and command representation and its AI
// capability should be one coherent record rather than five that drift. So this is a plain struct
// and a flat registry, and the interesting question is where the rows come from.
//
// Three answers, chosen per kind, and the rule is that a registry that already exists always wins:
//
//   * Panels are *derived*, at startup, from `ui::editorPanels()` -- the registry §5 calls "feature
//     metadata in embryo". Nothing about a panel is restated here. Add a panel there and Help knows
//     about it; that is `registerPanelFeatures()` in ui/help_panel.hpp.
//   * Commands and shortcuts are *authored*, in docs/help/features.json, because the application has
//     no command registry to derive them from -- they are inline ImGui and SDL calls. The validator
//     scans those call sites and reports anything authored that the source does not have, and
//     anything the source has that is not authored. When a command registry lands, these become
//     derived too and the JSON shrinks to nothing.
//   * Subsystems are authored, because they are a documentation concept -- "audio analysis" is not a
//     widget anywhere.

#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::help {

enum class FeatureKind : std::uint8_t {
    Panel,     // a dockable editor panel
    Command,   // a menu entry
    Shortcut,  // a key binding
    Subsystem, // an engine area with no single widget: audio analysis, modulation, rendering
};

[[nodiscard]] std::string_view featureKindName(FeatureKind kind);
[[nodiscard]] bool parseFeatureKind(std::string_view name, FeatureKind& out);

struct HelpFeature {
    std::string id;          // "panel.analysis", "command.file.open-audio", "subsystem.modulation"
    std::string name;        // what the UI calls it
    FeatureKind kind = FeatureKind::Subsystem;
    std::string category;    // groups the feature reference; usually the topic's category
    std::string summary;     // one line
    std::string documentId;  // the Help topic that explains it -- this is the "Learn more" target
    std::string shortcut;    // command id in the shortcut table, not the key itself
    std::string menuPath;    // "File > Open Audio..."
    std::string availability; // when it is usable: "always", "needs a track loaded", ...
    std::vector<std::string> related; // other feature ids
};

// §9. `command` is the stable id a topic and a feature refer to; `keys` is what the user presses.
struct HelpShortcut {
    std::string command;     // "transport.play"
    std::string keys;        // "Space"
    std::string description; // "Start or stop playback."
    std::string context;     // "General", "World Editor", "Sequencer", "Rendering", "Panels"
    std::string documentId;  // the topic that explains it
    bool configurable = false; // §9: say so if it is. Nothing in this build is.
    // Where the binding actually is, as "file:line". Not shown to users; it is what makes the
    // §35 check meaningful, because a shortcut that claims a call site can be checked for one.
    std::string callSite;
};

// Flat, small, and ordered as loaded. Lookup is linear because the whole table is a few dozen rows;
// a hash map here would be a structure built to answer a question nobody asks often enough.
class HelpFeatureTable {
public:
    void add(HelpFeature feature);
    void addShortcut(HelpShortcut shortcut);
    void clear();

    [[nodiscard]] const HelpFeature* find(std::string_view id) const;
    [[nodiscard]] const HelpShortcut* findShortcut(std::string_view command) const;
    [[nodiscard]] std::span<const HelpFeature> features() const { return features_; }
    [[nodiscard]] std::span<const HelpShortcut> shortcuts() const { return shortcuts_; }

    // Shortcuts grouped by `context`, in first-seen order, for §9's grouped reference.
    [[nodiscard]] std::vector<std::string> shortcutContexts() const;

    // Reads a features.json: `{"features": [...], "shortcuts": [...]}`. Both arrays optional.
    // Merges into whatever is already here, so derived rows can be added first and authored rows
    // second -- an authored row with an id that is already present replaces it, which is how a
    // derived panel row gets a documentId without the derivation having to know one.
    [[nodiscard]] Result<void> loadFile(const std::filesystem::path& file);

private:
    std::vector<HelpFeature> features_;
    std::vector<HelpShortcut> shortcuts_;
};

} // namespace avgen::help
