#include "app/recent_files.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

using namespace avgen;
using avgen::app::RecentFiles;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

namespace {
// A fresh, empty scratch directory per test case.
struct TempDir {
    fs::path root;
    TempDir()
        : root(fs::temp_directory_path() / "avgen_recent_files_test") {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TempDir() { fs::remove_all(root); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

fs::path touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << "{}\n";
    return path;
}
} // namespace

TEST_CASE("Recent files: add normalises, moves to front, dedupes and trims", "[recent_files]") {
    TempDir tmp;
    RecentFiles recent(tmp.root / "recent.json", 3);
    CHECK(recent.storeFile() == tmp.root / "recent.json");
    CHECK(recent.maxEntries() == 3);
    CHECK(recent.entries().empty());

    const fs::path a = tmp.root / "a.json";
    const fs::path b = tmp.root / "b.json";
    const fs::path c = tmp.root / "c.json";
    const fs::path e = tmp.root / "e.json";
    recent.add(a);
    recent.add(b);
    REQUIRE(recent.entries().size() == 2);
    CHECK(recent.entries()[0] == b.lexically_normal()); // newest first
    CHECK(recent.entries()[1] == a.lexically_normal());

    // The same file spelled differently is one entry and moves to the front.
    recent.add(tmp.root / "sub" / ".." / "a.json");
    REQUIRE(recent.entries().size() == 2);
    CHECK(recent.entries()[0] == a.lexically_normal());
    CHECK(recent.entries()[1] == b.lexically_normal());
    for (const auto& entry : recent.entries()) {
        CHECK(entry.is_absolute());
        CHECK(entry == entry.lexically_normal());
    }

    // A relative path becomes absolute (relative to the current directory).
    recent.add("relative.json");
    REQUIRE(recent.entries().size() == 3);
    CHECK(recent.entries()[0].is_absolute());
    CHECK(recent.entries()[0] == (fs::current_path() / "relative.json").lexically_normal());

    // Over the limit: the oldest entry drops off.
    recent.add(c);
    recent.add(e);
    REQUIRE(recent.entries().size() == 3);
    CHECK(recent.entries()[0] == e.lexically_normal());
    CHECK(recent.entries()[1] == c.lexically_normal());
    CHECK(recent.entries()[2] == (fs::current_path() / "relative.json").lexically_normal());

    recent.add(""); // ignored
    CHECK(recent.entries().size() == 3);
}

TEST_CASE("Recent files: remove and clear", "[recent_files]") {
    TempDir tmp;
    RecentFiles recent(tmp.root / "recent.json");
    const fs::path a = tmp.root / "a.json";
    const fs::path b = tmp.root / "b.json";
    recent.add(a);
    recent.add(b);
    CHECK_FALSE(recent.remove(tmp.root / "missing.json"));
    CHECK(recent.remove(tmp.root / "x" / ".." / "a.json")); // normalised before matching
    REQUIRE(recent.entries().size() == 1);
    CHECK(recent.entries()[0] == b.lexically_normal());
    CHECK_FALSE(recent.remove(a));
    recent.clear();
    CHECK(recent.entries().empty());
}

TEST_CASE("Recent files: save creates parent directories and load round-trips", "[recent_files]") {
    TempDir tmp;
    const fs::path store = tmp.root / "nested" / "deeper" / "recent.json";
    const fs::path a = tmp.root / "a.json";
    const fs::path b = tmp.root / "b.json";
    {
        RecentFiles recent(store, 5);
        recent.add(a);
        recent.add(b);
        REQUIRE(recent.save().has_value());
    }
    REQUIRE(fs::exists(store));
    {
        std::ifstream in(store);
        const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
        REQUIRE(doc.is_object());
        CHECK(doc["format"] == "avgen-recent");
        CHECK(doc["version"] == 1);
        REQUIRE(doc["entries"].is_array());
        REQUIRE(doc["entries"].size() == 2);
        CHECK(doc["entries"][0] == b.lexically_normal().string());
        CHECK(doc["entries"][1] == a.lexically_normal().string());
    }
    RecentFiles loaded(store, 5);
    REQUIRE(loaded.load().has_value());
    REQUIRE(loaded.entries().size() == 2);
    CHECK(loaded.entries()[0] == b.lexically_normal());
    CHECK(loaded.entries()[1] == a.lexically_normal());

    // A smaller limit at load time trims the stored list.
    RecentFiles smaller(store, 1);
    REQUIRE(smaller.load().has_value());
    REQUIRE(smaller.entries().size() == 1);
    CHECK(smaller.entries()[0] == b.lexically_normal());
}

TEST_CASE("Recent files: a missing store file loads as an empty list", "[recent_files]") {
    TempDir tmp;
    RecentFiles recent(tmp.root / "does-not-exist" / "recent.json");
    recent.add(tmp.root / "stale.json");
    REQUIRE(recent.load().has_value());
    CHECK(recent.entries().empty());
}

TEST_CASE("Recent files: a malformed store is an error and leaves entries alone", "[recent_files]") {
    TempDir tmp;
    const fs::path store = tmp.root / "recent.json";
    const fs::path keep = tmp.root / "keep.json";

    auto check = [&](const std::string& content, const char* what) {
        INFO(what);
        {
            std::ofstream out(store);
            out << content;
        }
        RecentFiles recent(store);
        recent.add(keep);
        auto result = recent.load();
        CHECK_FALSE(result.has_value());
        REQUIRE(recent.entries().size() == 1);
        CHECK(recent.entries()[0] == keep.lexically_normal());
    };
    check("{ not json", "invalid JSON");
    check("[]", "not an object");
    check(R"({"format":"avgen-project","version":1,"entries":[]})", "wrong format");
    check(R"({"format":"avgen-recent","entries":[]})", "missing version");
    check(R"({"format":"avgen-recent","version":2,"entries":[]})", "newer version");
    check(R"({"format":"avgen-recent","version":1})", "missing entries");
    check(R"({"format":"avgen-recent","version":1,"entries":{}})", "entries not an array");
    check(R"({"format":"avgen-recent","version":1,"entries":["/x", 3]})", "entry not a string");

    {
        std::ofstream out(store);
        out << R"({"format":"avgen-recent","version":2,"entries":[]})";
    }
    RecentFiles recent(store);
    auto result = recent.load();
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message, ContainsSubstring("version"));
}

TEST_CASE("Recent files: pruneMissing drops entries whose file is gone", "[recent_files]") {
    TempDir tmp;
    RecentFiles recent(tmp.root / "recent.json");
    const fs::path present = touch(tmp.root / "present.json");
    const fs::path alsoPresent = touch(tmp.root / "sub" / "also.json");
    const fs::path gone = tmp.root / "gone.json";
    recent.add(gone);
    recent.add(present);
    recent.add(alsoPresent);
    REQUIRE(recent.entries().size() == 3);
    recent.pruneMissing();
    REQUIRE(recent.entries().size() == 2);
    CHECK(recent.entries()[0] == alsoPresent.lexically_normal());
    CHECK(recent.entries()[1] == present.lexically_normal());

    fs::remove(present);
    recent.pruneMissing();
    REQUIRE(recent.entries().size() == 1);
    CHECK(recent.entries()[0] == alsoPresent.lexically_normal());
}
