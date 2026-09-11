#include "help/app_surface.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace avgen::help {
namespace {

constexpr std::string_view kPanelRegistryFile = "src/ui/editor_layout.cpp";
constexpr std::string_view kMenuFile = "src/ui/control_panel.cpp";
constexpr std::string_view kKeyFile = "src/app/application.cpp";

struct SourceFile {
    bool found = false;
    std::string relative;
    std::vector<std::string> lines;
};

SourceFile readSource(const std::filesystem::path& root, std::string_view relative) {
    SourceFile file;
    file.relative = std::string(relative);
    std::ifstream in(root / relative);
    if (!in) {
        return file;
    }
    file.found = true;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        file.lines.push_back(std::move(line));
    }
    return file;
}

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// The C++ string literals on a line, in order, with escapes left as written. Good enough for
// scraping ImGui calls, which take plain literals; a concatenated or computed label simply yields
// nothing, and yielding nothing is the honest answer for a label this cannot read.
std::vector<std::string> stringLiterals(std::string_view line) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            break; // a trailing comment; its prose is not a label
        }
        if (line[i] != '"') {
            continue;
        }
        std::string literal;
        ++i;
        while (i < line.size() && line[i] != '"') {
            if (line[i] == '\\' && i + 1 < line.size()) {
                ++i;
                switch (line[i]) {
                case 'n':
                    literal.push_back('\n');
                    break;
                case 't':
                    literal.push_back('\t');
                    break;
                default:
                    literal.push_back(line[i]);
                    break;
                }
            } else {
                literal.push_back(line[i]);
            }
            ++i;
        }
        out.push_back(std::move(literal));
    }
    return out;
}

std::string sourceRef(const SourceFile& file, std::size_t lineIndex) {
    return file.relative + ":" + std::to_string(lineIndex + 1);
}

// "SDLK_SPACE" -> "Space", "SDLK_O" -> "O", "ImGuiKey_LeftArrow" -> "LeftArrow".
std::string prettyKeyName(std::string_view symbol) {
    for (const std::string_view prefix : {"SDL_SCANCODE_", "SDLK_", "ImGuiKey_"}) {
        if (symbol.starts_with(prefix)) {
            symbol.remove_prefix(prefix.size());
            break;
        }
    }
    if (symbol.empty()) {
        return {};
    }
    if (symbol.size() == 1) {
        return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(symbol[0]))));
    }
    // ALL-CAPS symbols become Titlecase; a symbol that is already mixed case (ImGuiKey style) is
    // left as written, because "LeftArrow" is already the name a reader wants.
    const bool allCaps = std::ranges::none_of(symbol, [](char c) { return std::islower(static_cast<unsigned char>(c)) != 0; });
    std::string out;
    out.reserve(symbol.size());
    bool startOfWord = true;
    for (const char c : symbol) {
        if (c == '_') {
            out.push_back(' ');
            startOfWord = true;
            continue;
        }
        if (!allCaps) {
            out.push_back(c);
            continue;
        }
        out.push_back(startOfWord ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                                  : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        startOfWord = false;
    }
    return out;
}

bool mentionsModifier(std::string_view line) {
    for (const std::string_view token : {"KMOD_", "SDL_KMOD_", "ImGuiMod_", "KeyCtrl", "KeyShift", "KeyAlt",
                                         "KeySuper", "event.key.mod"}) {
        if (line.find(token) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

// The call the branch makes, for the report. The first `foo()` or `foo->bar()` after the condition.
std::string actionAfter(const std::vector<std::string>& lines, std::size_t from) {
    for (std::size_t i = from; i < lines.size() && i < from + 4; ++i) {
        const std::string_view line = trim(lines[i]);
        const auto call = line.find("();");
        if (call != std::string_view::npos) {
            std::size_t start = call;
            while (start > 0 && (std::isalnum(static_cast<unsigned char>(line[start - 1])) != 0 ||
                                 line[start - 1] == '_' || line[start - 1] == '.' || line[start - 1] == '>' ||
                                 line[start - 1] == '-' || line[start - 1] == ':')) {
                --start;
            }
            return std::string(line.substr(start, call - start + 2));
        }
        if (line.find('(') != std::string_view::npos && line.find(';') != std::string_view::npos &&
            !line.starts_with("if") && !line.starts_with("}")) {
            return std::string(line);
        }
    }
    return {};
}

void scanPanels(const SourceFile& file, AppSurface& surface) {
    // The registry rows look like:
    //   {"World Builder", "World Builder", DockRegion::Left, true, "recipe, Generate World, ..."},
    bool inArray = false;
    for (std::size_t i = 0; i < file.lines.size(); ++i) {
        const std::string& line = file.lines[i];
        if (line.find("kPanels") != std::string::npos && line.find("EditorPanel") != std::string::npos) {
            inArray = true;
            continue;
        }
        if (!inArray) {
            continue;
        }
        if (trim(line).starts_with("}}")) {
            break;
        }
        if (line.find("DockRegion::") == std::string::npos) {
            // A row may wrap; the region token is on the first line of every row in this registry,
            // so a line without it is a continuation and its literals belong to the row above.
            if (!surface.panels.empty() && !stringLiterals(line).empty()) {
                surface.panels.back().hint += stringLiterals(line).front();
            }
            continue;
        }
        const std::vector<std::string> literals = stringLiterals(line);
        if (literals.empty()) {
            continue;
        }
        AppPanel panel;
        panel.id = literals[0];
        panel.label = literals.size() > 1 ? literals[1] : literals[0];
        if (literals.size() > 2) {
            panel.hint = literals[2];
        }
        const auto region = line.find("DockRegion::");
        std::string_view rest = std::string_view(line).substr(region + 12);
        panel.region = std::string(rest.substr(0, rest.find_first_of(", ")));
        panel.openByDefault = line.find(", true,") != std::string::npos;
        surface.panels.push_back(std::move(panel));
        (void)i;
    }
}

void scanMenus(const SourceFile& file, AppSurface& surface) {
    // Menu nesting is tracked per function body, because the View menu's contents live in a
    // function of their own that the File menu's Begin/End pairs know nothing about. A function
    // whose name says it draws a menu starts inside that menu.
    std::vector<std::string> menuStack;
    bool insideMenuFunction = false;

    for (std::size_t i = 0; i < file.lines.size(); ++i) {
        const std::string& line = file.lines[i];

        if (!line.empty() && line[0] != ' ' && line[0] != '\t' && line[0] != '}' &&
            line.find("::") != std::string::npos && line.find('(') != std::string::npos &&
            line.back() == '{') {
            menuStack.clear();
            insideMenuFunction = line.find("drawMenuBar") != std::string::npos ||
                                 line.find("drawViewMenu") != std::string::npos;
            if (line.find("drawViewMenu") != std::string::npos) {
                menuStack.emplace_back("View"); // drawViewMenu is only ever called inside View
            }
            continue;
        }
        if (line == "}") {
            menuStack.clear();
            insideMenuFunction = false;
            continue;
        }
        if (!insideMenuFunction) {
            continue;
        }

        if (line.find("ImGui::BeginMenu(") != std::string::npos) {
            const std::vector<std::string> literals = stringLiterals(line);
            menuStack.push_back(literals.empty() ? std::string("?") : literals.front());
            continue;
        }
        if (line.find("ImGui::EndMenu()") != std::string::npos) {
            if (!menuStack.empty()) {
                menuStack.pop_back();
            }
            continue;
        }
        if (line.find("ImGui::MenuItem(") == std::string::npos || menuStack.empty()) {
            continue;
        }
        const std::vector<std::string> literals = stringLiterals(line);
        if (literals.empty()) {
            continue; // a computed label: nothing truthful to record
        }
        AppCommand command;
        command.label = literals[0];
        // The second literal of a MenuItem is its shortcut column -- the string the menu shows the
        // user. Whether anything binds it is exactly the question §35 wants asked.
        if (literals.size() > 1) {
            command.shortcut = literals[1];
        }
        command.menuPath.clear();
        for (const std::string& menu : menuStack) {
            if (!command.menuPath.empty()) {
                command.menuPath += " > ";
            }
            command.menuPath += menu;
        }
        command.menuPath += " > " + command.label;
        command.sourceRef = sourceRef(file, i);
        surface.commands.push_back(std::move(command));
    }
}

void scanShortcuts(const SourceFile& file, AppSurface& surface) {
    for (std::size_t i = 0; i < file.lines.size(); ++i) {
        const std::string& line = file.lines[i];
        std::string symbol;

        // SDL: `event.key.key == SDLK_SPACE`, or a scancode comparison.
        if (const auto sdl = line.find("SDLK_"); sdl != std::string::npos) {
            symbol = line.substr(sdl);
        } else if (const auto scan = line.find("SDL_SCANCODE_"); scan != std::string::npos) {
            symbol = line.substr(scan);
        } else if (const auto imgui = line.find("ImGuiKey_"); imgui != std::string::npos) {
            // ImGui: `ImGui::IsKeyPressed(ImGuiKey_G)` or `ImGui::Shortcut(...)`. Included so that
            // a pass which introduces shortcuts the ImGui way is seen by this scan without anyone
            // remembering to extend it -- which is the whole point of scanning.
            if (line.find("IsKeyPressed") == std::string::npos && line.find("IsKeyDown") == std::string::npos &&
                line.find("Shortcut(") == std::string::npos && line.find("IsKeyChordPressed") == std::string::npos) {
                continue;
            }
            symbol = line.substr(imgui);
        } else {
            continue;
        }

        std::size_t end = 0;
        while (end < symbol.size() &&
               (std::isalnum(static_cast<unsigned char>(symbol[end])) != 0 || symbol[end] == '_')) {
            ++end;
        }
        symbol.resize(end);
        std::string keys = prettyKeyName(symbol);
        if (keys.empty()) {
            continue;
        }

        AppShortcut shortcut;
        shortcut.keys = std::move(keys);
        shortcut.modified = mentionsModifier(line);
        shortcut.sourceRef = sourceRef(file, i);
        shortcut.action = actionAfter(file.lines, i);
        surface.shortcuts.push_back(std::move(shortcut));
    }
}

} // namespace

const AppPanel* AppSurface::findPanel(std::string_view id) const {
    const auto it = std::ranges::find(panels, id, &AppPanel::id);
    return it == panels.end() ? nullptr : &*it;
}

bool AppSurface::hasShortcutKeys(std::string_view keys) const {
    return std::ranges::any_of(shortcuts, [keys](const AppShortcut& s) { return s.keys == keys; });
}

std::filesystem::path configuredSourceDir() {
#ifdef AVGEN_SOURCE_DIR
    return std::filesystem::path(AVGEN_SOURCE_DIR);
#else
    return {};
#endif
}

AppSurface scanApplicationSource(const std::filesystem::path& sourceDir) {
    AppSurface surface;
    const auto take = [&surface, &sourceDir](std::string_view relative) {
        SourceFile file = readSource(sourceDir, relative);
        if (!file.found) {
            ++surface.coverage.filesMissing;
            surface.coverage.missing.emplace_back(relative);
        } else {
            ++surface.coverage.filesRead;
        }
        return file;
    };

    const SourceFile panels = take(kPanelRegistryFile);
    const SourceFile menus = take(kMenuFile);
    const SourceFile keys = take(kKeyFile);

    if (panels.found) {
        scanPanels(panels, surface);
        if (surface.panels.empty()) {
            surface.coverage.warnings.push_back(
                std::string(kPanelRegistryFile) +
                ": found no panel registry rows. The registry has moved or changed shape, and the "
                "Help validator is now checking documentation against nothing.");
        }
    }
    if (menus.found) {
        scanMenus(menus, surface);
        if (surface.commands.empty()) {
            surface.coverage.warnings.push_back(
                std::string(kMenuFile) +
                ": found no menu commands. The menu bar has moved or changed shape, and the command "
                "inventory is now checking documentation against nothing.");
        }
        // The View menu is generated from the panel registry rather than written out, so its
        // entries never appear as literals. Record them from the panels, and notice if the loop
        // that generates them goes away.
        const bool generated = std::ranges::any_of(
            menus.lines, [](const std::string& line) { return line.find("editorPanels()") != std::string::npos; });
        if (generated) {
            for (const AppPanel& panel : surface.panels) {
                AppCommand command;
                command.label = panel.label;
                command.menuPath = "View > " + panel.label;
                command.sourceRef = std::string(kMenuFile) + " (generated from the panel registry)";
                surface.commands.push_back(std::move(command));
            }
        } else if (!surface.panels.empty()) {
            surface.coverage.warnings.push_back(
                std::string(kMenuFile) +
                ": the View menu no longer iterates editorPanels(), so panel entries are not being "
                "collected. Check how the View menu is built.");
        }
    }
    if (keys.found) {
        scanShortcuts(keys, surface);
        // No warning when this finds nothing: an application with no key bindings is a real state,
        // and it is the state this one was in when the Help brief was written. The validator
        // reports the count rather than assuming a number.
    }
    return surface;
}

} // namespace avgen::help
