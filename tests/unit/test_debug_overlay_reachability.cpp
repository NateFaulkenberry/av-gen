// ADR-421: every debug overlay switch is both drawable and reachable.
//
// **Why this test exists.** A census on 2026-09-20 of the twenty-eight `bool`s on
// `rendering::DebugViewOptions` found the same defect in both directions at once:
//
//   * `wind`, `vortex`, `lights` and `lightClusters` were fully implemented and read by
//     `debug_visualizer.cpp`, and had **no checkbox anywhere in the application**. They were
//     reachable only from `--debug-draw`, which is to say not from the application at all. The wind
//     arrow grid had been in that state since ADR-055.
//   * `culling` had a checkbox in `world_panel.cpp` that `debug_visualizer.cpp` read **nowhere**,
//     so it drew nothing. The application already knew -- `application.cpp` refuses to name it on
//     the command line because "a command-line name for an inert flag is a promise the application
//     does not keep" -- and nobody gave the panel the same treatment.
//
// Those are one defect: a switch and the thing it switches, kept in two files by hand. So this test
// reads all three files and requires them to agree, in the way `test_effect_conformance.cpp` reads
// `atmospherics.hpp` and `test_deformer_panel.cpp` reads `procedural.hpp` -- a test here may read
// the source tree to check a claim the source tree makes about itself.
//
// It reads source rather than calling anything because there is nothing to call: an ImGui checkbox
// leaves no trace a CPU test can query, and the visualiser's reads are `if (options.x)` inside a
// function that needs a device. What can be checked is that the three names line up, and that is
// exactly what went wrong.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::string readAll(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.good()) {
        return {};
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Every `bool <name> = <false|true>;` declared on the struct. Deliberately not a hand-written list:
// a hand-written list is the third place this would have to be remembered, which is the failure.
[[nodiscard]] std::vector<std::string> declaredFlags(const std::string& header) {
    std::vector<std::string> out;
    const std::regex re(R"(\n\s*bool\s+([A-Za-z][A-Za-z0-9]*)\s*=\s*(?:false|true)\s*;)");
    for (auto it = std::sregex_iterator(header.begin(), header.end(), re); it != std::sregex_iterator();
         ++it) {
        out.push_back((*it)[1].str());
    }
    return out;
}

[[nodiscard]] bool mentions(const std::string& text, const std::string& needle) {
    // Word-boundary match, so `lights` does not match `lightClusters` and `bounds` does not match
    // `entityBounds`. The whole value of this test is in telling those apart.
    const std::regex re("\\b" + needle + "\\b");
    return std::regex_search(text, re);
}

struct Sources {
    std::string header;
    std::string panel;
    std::string visualizer;
    std::string application;
    [[nodiscard]] bool ok() const {
        return !header.empty() && !panel.empty() && !visualizer.empty() && !application.empty();
    }
};

[[nodiscard]] Sources load() {
#ifdef AVGEN_SOURCE_DIR
    const std::filesystem::path root(AVGEN_SOURCE_DIR);
    return Sources{readAll(root / "src" / "rendering" / "debug_view_options.hpp"),
                   readAll(root / "src" / "ui" / "world_panel.cpp"),
                   readAll(root / "src" / "rendering" / "debug_visualizer.cpp"),
                   readAll(root / "src" / "app" / "application.cpp")};
#else
    return {};
#endif
}

// Switches that legitimately have no `if (options.x)` in the visualiser, each with the reason.
// A list of exceptions is a place defects hide, so it is short, every entry says where the flag IS
// consumed, and the test checks that claim rather than taking it.
struct Exception {
    const char* flag;
    const char* consumedIn; // a file the flag must appear in instead
    const char* why;
};
constexpr Exception kNotDrawnByTheVisualizer[] = {
    {"depthTest", "application.cpp",
     "not an overlay: it is how the overlays are drawn, handed to the renderer by "
     "`setDebugDepthTest`"},
    {"submittedOnly", "debug_visualizer.cpp",
     "a modifier on the entity overlays rather than an overlay, but it IS read there"},
};

[[nodiscard]] const Exception* exceptionFor(const std::string& flag) {
    for (const Exception& e : kNotDrawnByTheVisualizer) {
        if (flag == e.flag) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("every debug overlay switch has something that draws it", "[ui][debug][overlays]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const Sources src = load();
    REQUIRE(src.ok());
    const std::vector<std::string> flags = declaredFlags(src.header);
    REQUIRE(flags.size() > 20); // the struct has ~27; a regex that matched nothing must not pass

    for (const std::string& flag : flags) {
        INFO("flag: " << flag);
        if (const Exception* e = exceptionFor(flag); e != nullptr) {
            INFO("exception: " << e->why);
            const std::string& where =
                std::string(e->consumedIn) == "application.cpp" ? src.application : src.visualizer;
            // The exception's own claim, checked. An exception nobody verifies is a hole.
            CHECK(mentions(where, "options." + flag));
            continue;
        }
        CHECK(mentions(src.visualizer, "options." + flag));
    }
#endif
}

TEST_CASE("every debug overlay switch is reachable from the application", "[ui][debug][overlays]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const Sources src = load();
    REQUIRE(src.ok());
    const std::vector<std::string> flags = declaredFlags(src.header);
    REQUIRE(flags.size() > 20);

    // `--debug-draw` is not enough and that is the point. A headless render is the one context
    // where a checkbox does not exist, and a running application is the one context where a command
    // line does not. An overlay needs both; this case asserts the one that was missing.
    std::vector<std::string> unreachable;
    for (const std::string& flag : flags) {
        if (mentions(src.panel, "debug." + flag)) {
            continue;
        }
        unreachable.push_back(flag);
    }
    INFO("no checkbox in world_panel.cpp for: " << [&] {
        std::string joined;
        for (const std::string& f : unreachable) {
            joined += f + " ";
        }
        return joined;
    }());
    CHECK(unreachable.empty());
#endif
}

TEST_CASE("the overlay reachability checks can fail", "[ui][debug][overlays]") {
    // ADR-182. The two cases above are string searches over source, and a string search that never
    // matches passes a "nothing is missing" assertion perfectly. So: a name that is certainly not
    // in either file must be reported by the same predicates, and the extractor must find the flags
    // it is supposed to find.
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const Sources src = load();
    REQUIRE(src.ok());

    CHECK_FALSE(mentions(src.visualizer, "options.noSuchOverlayExists"));
    CHECK_FALSE(mentions(src.panel, "debug.noSuchOverlayExists"));
    // ...and the same predicates on names that are certainly there.
    CHECK(mentions(src.visualizer, "options.wind"));
    CHECK(mentions(src.panel, "debug.wind"));

    // The word boundary does the work the whole test rests on: without it, `lights` matches
    // `lightClusters` and an overlay with no switch of its own would pass on its neighbour's.
    CHECK_FALSE(mentions("options.lightClusters", "options.lights"));

    // The extractor finds the flags, and finds them by name.
    const std::vector<std::string> flags = declaredFlags(src.header);
    CHECK(std::find(flags.begin(), flags.end(), "wind") != flags.end());
    CHECK(std::find(flags.begin(), flags.end(), "vortex") != flags.end());
    CHECK(std::find(flags.begin(), flags.end(), "lightClusters") != flags.end());
    // ADR-421 deleted this one; if it comes back it needs a reader and a checkbox like everything
    // else, and this line failing is how somebody finds out.
    CHECK(std::find(flags.begin(), flags.end(), "culling") == flags.end());
#endif
}
