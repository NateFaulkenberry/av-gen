// Guards against damage that lands in the tree and survives, because nothing looks for it.
//
// Both cases here happened. A merge-resolution script reported "conflicts resolved: N" for the two
// source files it was pointed at, and `docs/decisions/README.md` -- which also conflicted -- kept
// its markers and was committed. It then nested inside the next merge. A script's own report is not
// evidence that the tree is clean, and neither suite would have failed: conflict markers are legal
// text inside a Markdown table.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {

std::vector<std::filesystem::path> filesUnder(const std::filesystem::path& root,
                                              const std::set<std::string>& extensions) {
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::exists(root)) {
        return out;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (extensions.count(entry.path().extension().string()) != 0) {
            out.push_back(entry.path());
        }
    }
    return out;
}

} // namespace

TEST_CASE("no source or document carries a merge conflict marker", "[hygiene]") {
    const std::filesystem::path root(AVGEN_SOURCE_DIR);
    std::vector<std::filesystem::path> files;
    for (const char* dir : {"src", "tests", "docs", "shaders", "tools"}) {
        auto found = filesUnder(root / dir, {".cpp", ".hpp", ".wgsl", ".md", ".sh", ".py", ".json"});
        files.insert(files.end(), found.begin(), found.end());
    }
    REQUIRE(files.size() > 100); // the walk found the tree, not an empty directory

    std::vector<std::string> offenders;
    for (const auto& file : files) {
        std::ifstream in(file);
        std::string line;
        int number = 0;
        while (std::getline(in, line)) {
            ++number;
            // Anchored at column 0 and length-exact, so prose about conflicts does not trip it.
            const bool marker = line.rfind("<<<<<<< ", 0) == 0 || line.rfind(">>>>>>> ", 0) == 0 ||
                                line == "=======";
            if (marker) {
                offenders.push_back(std::filesystem::relative(file, root).string() + ":" +
                                    std::to_string(number));
            }
        }
    }
    INFO("files carrying conflict markers: " << offenders.size());
    for (const auto& o : offenders) {
        INFO("  " << o);
    }
    CHECK(offenders.empty());
}

TEST_CASE("every ADR is indexed and every indexed ADR exists", "[hygiene]") {
    const std::filesystem::path decisions = std::filesystem::path(AVGEN_SOURCE_DIR) / "docs" / "decisions";
    REQUIRE(std::filesystem::exists(decisions / "README.md"));

    std::set<int> onDisk;
    const std::regex fileRe(R"(ADR-(\d+))");
    for (const auto& entry : std::filesystem::directory_iterator(decisions)) {
        std::smatch m;
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && std::regex_search(name, m, fileRe)) {
            onDisk.insert(std::stoi(m[1]));
        }
    }
    REQUIRE(onDisk.size() > 100); // the directory was actually read

    std::set<int> indexed;
    std::ifstream in(decisions / "README.md");
    std::string line;
    const std::regex rowRe(R"(^\|\s*\[(\d+)\])");
    while (std::getline(in, line)) {
        std::smatch m;
        if (std::regex_search(line, m, rowRe)) {
            indexed.insert(std::stoi(m[1]));
        }
    }

    std::vector<int> missingFromIndex;
    for (const int n : onDisk) {
        if (indexed.count(n) == 0) {
            missingFromIndex.push_back(n);
        }
    }
    std::vector<int> danglingRows;
    for (const int n : indexed) {
        if (onDisk.count(n) == 0) {
            danglingRows.push_back(n);
        }
    }
    INFO("ADR files not listed in README: " << missingFromIndex.size());
    INFO("README rows with no ADR file: " << danglingRows.size());
    CHECK(missingFromIndex.empty());
    CHECK(danglingRows.empty());
}
