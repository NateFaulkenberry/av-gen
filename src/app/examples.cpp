#include "app/examples.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>

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
        out.push_back(std::move(info));
    }
    return out;
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
