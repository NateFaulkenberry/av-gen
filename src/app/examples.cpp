#include "app/examples.hpp"

#include "app/project_paths.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <fstream>
#include <system_error>

namespace avgen::app {

std::vector<std::filesystem::path> exampleSearchDirs(const std::filesystem::path& executablePath) {
    std::vector<std::filesystem::path> dirs;
    if (const char* env = std::getenv("AVGEN_EXAMPLES_DIR")) {
        dirs.emplace_back(env);
    }
    const auto exeDir = executablePath.parent_path();
    dirs.push_back(exeDir / "examples");
    dirs.push_back(exeDir / ".." / "examples");
    dirs.push_back(exeDir / ".." / ".." / "examples");
    dirs.push_back(exeDir / ".." / "Resources" / "examples");
#ifdef AVGEN_SOURCE_DIR
    dirs.push_back(std::filesystem::path(AVGEN_SOURCE_DIR) / "examples");
#endif
    return dirs;
}

namespace {

// A project's `assets.scene.path` and `assets.audio.path`, resolved as the project loader resolves them
// (app/project_paths.hpp: relative to the project's folder, or "~/" for the user's home). A scene or recipe
// file, or a project that cannot be parsed, names neither; opening it reports its own error.
void readProjectAssets(ExampleInfo& info) {
    std::ifstream in(info.file);
    if (!in) {
        return;
    }
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object() || !doc.contains("assets") || !doc["assets"].is_object()) {
        return;
    }
    const nlohmann::json& assets = doc["assets"];
    const auto dir = info.file.parent_path();
    if (assets.contains("scene") && assets["scene"].is_object()) {
        const nlohmann::json& path = assets["scene"].value("path", nlohmann::json{});
        const nlohmann::json& inner = path.is_object() ? path.value("path", nlohmann::json{}) : path;
        if (inner.is_string()) {
            info.required.push_back(resolveFrom(inner.get<std::string>(), dir));
        }
    }
    if (assets.contains("audio") && assets["audio"].is_object()) {
        const nlohmann::json& path = assets["audio"].value("path", nlohmann::json{});
        if (path.is_string() && !path.get<std::string>().empty()) {
            info.audio = resolveFrom(path.get<std::string>(), dir);
        }
    }
}

bool present(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

} // namespace

ExampleStatus exampleStatus(const ExampleInfo& example) {
    ExampleStatus status;
    if (!present(example.file)) {
        status.missing.push_back(example.file);
    }
    for (const auto& file : example.required) {
        if (!present(file)) {
            status.missing.push_back(file);
        }
    }
    if (!status.missing.empty()) {
        status.readiness = ExampleReadiness::Unavailable;
    }
    if (!example.audio.empty() && !present(example.audio)) {
        status.missing.push_back(example.audio);
        if (status.readiness == ExampleReadiness::Ready) {
            status.readiness = ExampleReadiness::NoAudio;
        }
    }
    return status;
}

std::string exampleLabel(const ExampleInfo& example, const ExampleStatus& status) {
    switch (status.readiness) {
    case ExampleReadiness::Ready: return example.name;
    case ExampleReadiness::NoAudio: return example.name + "  (no audio)";
    case ExampleReadiness::Unavailable:
        return example.name + (example.generate.empty() ? "  (missing files)" : "  (needs generating)");
    }
    return example.name;
}

std::string exampleTooltip(const ExampleInfo& example, const ExampleStatus& status) {
    std::string text = example.description;
    if (status.missing.empty()) {
        return text;
    }
    if (!text.empty()) {
        text += "\n\n";
    }
    text += status.readiness == ExampleReadiness::NoAudio ? "Opens without its audio; not on this machine:"
                                                          : "Cannot open: not on this machine:";
    for (const auto& file : status.missing) {
        text += "\n  " + file.string();
    }
    if (status.readiness == ExampleReadiness::Unavailable && !example.generate.empty()) {
        text += fmt::format("\n\nGenerate it from the repository root:\n  {}", example.generate);
    }
    return text;
}

Result<std::vector<ExampleInfo>> loadExampleIndex(const std::filesystem::path& indexFile) {
    std::ifstream in(indexFile);
    if (!in) {
        return fail("cannot open '{}'", indexFile.string());
    }
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object() || !doc.contains("examples") || !doc["examples"].is_array()) {
        return fail("'{}' is not an examples index (object with an 'examples' array)", indexFile.string());
    }
    const auto dir = indexFile.parent_path();
    std::vector<ExampleInfo> out;
    for (const auto& e : doc["examples"]) {
        if (!e.is_object() || !e.contains("name") || !e["name"].is_string()) {
            return fail("examples index: every entry needs a 'name'");
        }
        ExampleInfo info;
        info.name = e["name"].get<std::string>();
        info.description = e.value("description", std::string());
        info.category = e.value("category", std::string("Examples"));
        if (e.contains("live") && !e["live"].is_boolean()) {
            return fail("examples index: '{}': 'live' must be true or false", info.name);
        }
        info.live = e.value("live", false);
        if (e.contains("hidden") && !e["hidden"].is_boolean()) {
            return fail("examples index: '{}': 'hidden' must be true or false", info.name);
        }
        info.hidden = e.value("hidden", false);
        // Three kinds of entry, opened the same way: `Application::openAny` sniffs a recipe from a
        // project by content, so the key is documentation rather than routing. A recipe says so
        // rather than calling itself a project, since what it opens is a world that gets generated.
        std::string file = e.value("project", std::string());
        if (file.empty()) {
            file = e.value("scene", std::string());
        }
        if (file.empty()) {
            file = e.value("recipe", std::string());
        }
        if (file.empty()) {
            return fail("examples index: '{}' needs a 'project', 'scene' or 'recipe'", info.name);
        }
        info.file = (dir / file).lexically_normal();
        if (e.contains("generate") && !e["generate"].is_string()) {
            return fail("examples index: '{}': 'generate' must be a command string", info.name);
        }
        info.generate = e.value("generate", std::string());
        readProjectAssets(info);
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<ExampleSection> exampleSections(const std::vector<ExampleInfo>& examples, bool live) {
    std::vector<ExampleSection> sections;
    for (std::size_t i = 0; i < examples.size(); ++i) {
        if (examples[i].live != live || examples[i].hidden) {
            continue;
        }
        auto it = std::find_if(sections.begin(), sections.end(),
                               [&](const ExampleSection& s) { return s.category == examples[i].category; });
        if (it == sections.end()) {
            sections.push_back(ExampleSection{examples[i].category, {}});
            it = std::prev(sections.end());
        }
        it->entries.push_back(i);
    }
    return sections;
}

Result<std::vector<ExampleInfo>> loadExamples(const std::vector<std::filesystem::path>& searchDirs) {
    for (const auto& dir : searchDirs) {
        const auto index = dir / "index.json";
        std::error_code ec;
        if (std::filesystem::exists(index, ec)) {
            return loadExampleIndex(index);
        }
    }
    return std::vector<ExampleInfo>{};
}

} // namespace avgen::app
