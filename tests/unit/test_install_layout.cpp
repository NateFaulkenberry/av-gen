// ADR-2160: the package's install rules, read from what CMake generated (cmake_install.cmake and every one it
// includes), so the check sees exactly what `cmake --install` would copy -- including a rule some later
// add_subdirectory brings in. The repository's assets/ holds commercial and third-party files and the repo is
// public: no rule may install anything from assets/ or examples/, and every rule must land inside the
// documented layout (cmake/Install.cmake). tools/package_poc.sh checks the staged files themselves as well.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct InstallRule {
    std::string destination; // relative to the install prefix ("" is the prefix itself)
    std::string type;        // EXECUTABLE, FILE, DIRECTORY, ...
    std::vector<std::string> sources;
    std::string script;      // which cmake_install.cmake it came from
};

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::vector<std::string> quoted(const std::string& text) {
    std::vector<std::string> out;
    static const std::regex q("\"([^\"]*)\"");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), q); it != std::sregex_iterator(); ++it) {
        out.push_back((*it)[1].str());
    }
    return out;
}

void collect(const fs::path& script, std::vector<InstallRule>& rules, std::set<std::string>& seen) {
    if (!seen.insert(script.string()).second) {
        return;
    }
    const std::string text = readFile(script);
    static const std::regex install(R"re(file\(INSTALL DESTINATION "([^"]*)" TYPE (\w+)([^\n]*)\))re");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), install); it != std::sregex_iterator(); ++it) {
        InstallRule rule;
        std::string destination = (*it)[1].str();
        const std::string prefix = "${CMAKE_INSTALL_PREFIX}";
        if (destination.starts_with(prefix)) {
            destination.erase(0, prefix.size());
        }
        while (destination.starts_with("/")) {
            destination.erase(0, 1);
        }
        while (destination.ends_with("/.") || destination == ".") {
            destination.erase(destination.size() - (destination == "." ? 1 : 2));
        }
        rule.destination = destination;
        rule.type = (*it)[2].str();
        const std::string rest = (*it)[3].str();
        const std::size_t files = rest.find("FILES ");
        if (files != std::string::npos) {
            const std::size_t end = rest.find(" FILES_MATCHING", files);
            rule.sources = quoted(rest.substr(files, end == std::string::npos ? std::string::npos : end - files));
        }
        rule.script = script.string();
        rules.push_back(std::move(rule));
    }
    static const std::regex include(R"re(include\("([^"]*cmake_install\.cmake)"\))re");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), include); it != std::sregex_iterator(); ++it) {
        collect(fs::path((*it)[1].str()), rules, seen);
    }
}

std::vector<InstallRule> installRules() {
    std::vector<InstallRule> rules;
    std::set<std::string> seen;
    collect(fs::path(AVGEN_BINARY_DIR) / "cmake_install.cmake", rules, seen);
    return rules;
}

bool under(const std::string& path, const fs::path& dir) {
    const std::string d = dir.lexically_normal().string();
    const std::string p = fs::path(path).lexically_normal().string();
    return p == d || p.starts_with(d.ends_with("/") ? d : d + "/");
}

} // namespace

TEST_CASE("the install rules stay inside the package layout and install no assets", "[api][package]") {
    const fs::path source = fs::path(AVGEN_SOURCE_DIR).lexically_normal();
    const std::vector<InstallRule> rules = installRules();

    // Parsing found the rules at all: the executable, the three folders and the generated files.
    bool executable = false;
    std::set<std::string> directories;
    for (const InstallRule& rule : rules) {
        if (rule.type == "EXECUTABLE" && rule.destination == "bin") {
            executable = true;
        }
        if (rule.type == "DIRECTORY") {
            for (const std::string& s : rule.sources) {
                directories.insert(fs::path(s).lexically_normal().string());
            }
        }
    }
    CHECK(executable);
    const std::set<std::string> allowedDirectories = {
        (source / "shaders" / "").string(), (source / "schemas" / "").string(), (source / "docs" / "api" / "").string()};
    CHECK(directories == allowedDirectories);

    const std::set<std::string> allowedDestinations = {"",
                                                       "bin",
                                                       "bin/shaders",
                                                       "share/avgen/schemas",
                                                       "share/avgen/docs",
                                                       "share/avgen/docs/api"};
    for (const InstallRule& rule : rules) {
        INFO(rule.script << ": file(INSTALL DESTINATION \"" << rule.destination << "\" TYPE " << rule.type << ")");
        CHECK(allowedDestinations.contains(rule.destination));
        CHECK_FALSE(rule.sources.empty());
        for (const std::string& s : rule.sources) {
            INFO("source " << s);
            CHECK_FALSE(under(s, source / "assets"));
            CHECK_FALSE(under(s, source / "examples"));
            CHECK_FALSE(under(s, source / "docs" / "help"));
            CHECK_FALSE(under(s, source / "prototypes"));
            // Nothing installs the whole tree, or a folder above one of the allowed ones.
            CHECK(fs::path(s).lexically_normal() != source);
            CHECK(fs::path(s).lexically_normal() != source / "");
            CHECK(fs::path(s).lexically_normal() != source / "docs" / "");
            if (rule.type == "DIRECTORY") {
                CHECK(allowedDirectories.contains(fs::path(s).lexically_normal().string()));
            }
        }
    }
}

TEST_CASE("the folders the package installs from hold no asset files", "[api][package]") {
    // A DIRECTORY rule copies by pattern, so the folders themselves must not hold an asset that a pattern could
    // match one day: the shader folder holds shaders, the schema folder JSON, the API docs Markdown.
    const fs::path source(AVGEN_SOURCE_DIR);
    const std::vector<std::pair<fs::path, std::set<std::string>>> folders = {
        {source / "shaders", {".wgsl"}}, {source / "schemas", {".json"}}, {source / "docs" / "api", {".md"}}};
    for (const auto& [folder, extensions] : folders) {
        REQUIRE(fs::is_directory(folder));
        for (const auto& entry : fs::recursive_directory_iterator(folder)) {
            if (!entry.is_regular_file() || entry.path().filename() == ".DS_Store") {
                continue;
            }
            INFO(entry.path().string());
            CHECK(extensions.contains(entry.path().extension().string()));
        }
    }
}
