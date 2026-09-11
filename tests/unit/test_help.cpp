// The Help system (help-system spec §40): the content model, search, retrieval, the feature and
// shortcut tables, the source scan and the §35 validator.
//
// The tests fall into two halves. The first builds small documents by hand and pins behaviour the
// content happens not to exercise -- a malformed front matter, a duplicate id, a broken link. The
// second loads the **shipped** content from docs/help and asserts against it, because a Help system
// that passes its unit tests over three synthetic topics and ships an empty panel is exactly the
// failure this project keeps repeating.

#include "app/engine.hpp"
#include "help/api.hpp"
#include "help/app_surface.hpp"
#include "help/database.hpp"
#include "help/markdown.hpp"
#include "help/validation.hpp"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>
#include <ranges>
#include <nlohmann/json.hpp>

using namespace avgen;

namespace {

std::filesystem::path sourceRoot() { return help::configuredSourceDir(); }
std::filesystem::path contentDir() { return sourceRoot() / "docs" / "help"; }

help::HelpDocument makeDoc(std::string id, std::string title, std::string category, std::string body = "Body.") {
    help::HelpDocument doc;
    doc.id = std::move(id);
    doc.title = std::move(title);
    doc.category = std::move(category);
    doc.summary = "A summary.";
    doc.markdown = body;
    doc.body = help::parseHelpBody(doc.markdown);
    return doc;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The content model
// ---------------------------------------------------------------------------------------------

TEST_CASE("A Help topic parses its front matter and its body") {
    const std::string text = R"(---
id: audio/analysis
title: Audio Analysis
category: Audio
summary: What AV Gen measures.
order: 21
status: partial
audience: expert
version: 3
tags: fft, bands, onset
keywords: how do i react to the kick; make it pulse with the bass
related: audio/input, modulation/routes
features: panel.analysis
shortcuts: transport.play
parameters: audio/inputGain
---

# Heading

A paragraph with `code`, **strong**, *emphasis* and a [link](help://audio/input).

- first item
- second item

1. step one
2. step two

| a | b |
|---|---|
| 1 | 2 |

> [!WARNING]
> Careful.

```wgsl
let x = 1.0;
```

---
)";
    const auto parsed = help::parseHelpMarkdown(text, "memory");
    REQUIRE(parsed.has_value());
    const help::HelpDocument& doc = *parsed;

    CHECK(doc.id == "audio/analysis");
    CHECK(doc.title == "Audio Analysis");
    CHECK(doc.category == "Audio");
    CHECK(doc.order == 21);
    CHECK(doc.version == 3);
    CHECK(doc.status == help::HelpStatus::Partial);
    CHECK(doc.audience == help::HelpAudience::Expert);
    CHECK(doc.tags.size() == 3);
    // Keywords split on semicolons, not commas: a natural phrasing contains commas, and splitting
    // one on them turns a sentence into useless fragments.
    REQUIRE(doc.keywords.size() == 2);
    CHECK(doc.keywords[0] == "how do i react to the kick");
    CHECK(doc.related == std::vector<std::string>{"audio/input", "modulation/routes"});
    CHECK(doc.features == std::vector<std::string>{"panel.analysis"});
    CHECK(doc.parameters == std::vector<std::string>{"audio/inputGain"});

    const auto count = [&doc](help::BlockKind kind) {
        return std::ranges::count(doc.body, kind, &help::HelpBlock::kind);
    };
    CHECK(count(help::BlockKind::Heading) == 1);
    CHECK(count(help::BlockKind::Paragraph) == 1);
    CHECK(count(help::BlockKind::Bullet) == 2);
    CHECK(count(help::BlockKind::Numbered) == 2);
    CHECK(count(help::BlockKind::Table) == 1);
    CHECK(count(help::BlockKind::Callout) == 1);
    CHECK(count(help::BlockKind::Code) == 1);
    CHECK(count(help::BlockKind::Rule) == 1);

    const auto heading = std::ranges::find(doc.body, help::BlockKind::Heading, &help::HelpBlock::kind);
    CHECK(heading->anchor == "heading");

    const auto table = std::ranges::find(doc.body, help::BlockKind::Table, &help::HelpBlock::kind);
    REQUIRE(table->rows.size() == 2); // the divider row is consumed, not stored
    CHECK(help::flattenSpans(table->rows[0].cells[0]) == "a");
    CHECK(help::flattenSpans(table->rows[1].cells[1]) == "2");

    const auto callout = std::ranges::find(doc.body, help::BlockKind::Callout, &help::HelpBlock::kind);
    CHECK(callout->callout == help::CalloutKind::Warning);

    const auto code = std::ranges::find(doc.body, help::BlockKind::Code, &help::HelpBlock::kind);
    CHECK(code->language == "wgsl");
    CHECK(code->text == "let x = 1.0;");
}

TEST_CASE("A table cell may contain an escaped pipe") {
    // A reference table of command-line syntax contains `a\\|b`, and splitting the row on every raw
    // `|` grew the row an extra cell. ImGui then starts a new row on the cell the header has no
    // column for, so the whole table draws ragged -- which is what the shipped command-line
    // reference did before this.
    const auto blocks = help::parseHelpBody("| Flag | Does |\n|---|---|\n"
                                            "| `--output <d>[:full\\|:WxH]` | an output window |\n");
    REQUIRE(blocks.size() == 1);
    REQUIRE(blocks[0].kind == help::BlockKind::Table);
    REQUIRE(blocks[0].rows.size() == 2);
    CHECK(blocks[0].rows[0].cells.size() == 2);
    REQUIRE(blocks[0].rows[1].cells.size() == 2);
    // ...and the escape is resolved by the inline parser, not left as a stray backslash.
    CHECK(help::flattenSpans(blocks[0].rows[1].cells[0]) == "--output <d>[:full|:WxH]");
    CHECK(help::flattenSpans(blocks[0].rows[1].cells[1]) == "an output window");
}

TEST_CASE("Every shipped table row has the columns its header declared") {
    if (!std::filesystem::is_directory(contentDir())) {
        SKIP("docs/help is not present in this checkout");
    }
    help::HelpDatabase db;
    REQUIRE(db.loadDirectory(contentDir()).has_value());
    for (const help::HelpDocument& doc : db.documents()) {
        for (const help::HelpBlock& block : doc.body) {
            if (block.kind != help::BlockKind::Table || block.rows.empty()) {
                continue;
            }
            const std::size_t columns = block.rows.front().cells.size();
            for (std::size_t r = 0; r < block.rows.size(); ++r) {
                INFO(doc.id << " table row " << r << " has " << block.rows[r].cells.size()
                     << " cells, header declared " << columns);
                CHECK(block.rows[r].cells.size() == columns);
            }
        }
    }
}

TEST_CASE("Inline styles and links parse, and a link resolves to a topic id") {
    const auto spans = help::parseInline("plain `code` **strong** *em* [label](help://a/b#anchor) "
                                         "[out](https://example.com)");
    CHECK(help::flattenSpans(spans) == "plain code strong em label out");

    const auto link = std::ranges::find_if(spans, [](const help::InlineSpan& s) {
        return s.style == help::InlineSpan::Style::Link && s.text == "label";
    });
    REQUIRE(link != spans.end());
    CHECK(link->internal);
    CHECK(link->target == "a/b"); // the anchor is dropped; the id is what navigation needs

    const auto external = std::ranges::find_if(spans, [](const help::InlineSpan& s) {
        return s.style == help::InlineSpan::Style::Link && s.text == "out";
    });
    REQUIRE(external != spans.end());
    CHECK_FALSE(external->internal);

    CHECK(help::documentIdFromLink("help://x/y") == "x/y");
    CHECK(help::documentIdFromLink("x/y") == "x/y");
    CHECK(help::documentIdFromLink("https://example.com").empty());
    CHECK(help::documentIdFromLink("a file.txt").empty());
}

TEST_CASE("Malformed documentation is rejected with a message that names the problem") {
    CHECK_FALSE(help::parseHelpMarkdown("no front matter here", "f").has_value());
    CHECK_FALSE(help::parseHelpMarkdown("---\ntitle: T\ncategory: C\n---\n", "f").has_value());  // no id
    CHECK_FALSE(help::parseHelpMarkdown("---\nid: i\ncategory: C\n---\n", "f").has_value());     // no title
    CHECK_FALSE(help::parseHelpMarkdown("---\nid: i\ntitle: T\n---\n", "f").has_value());        // no category
    CHECK_FALSE(help::parseHelpMarkdown("---\nid: i\ntitle: T\ncategory: C\n", "f").has_value()); // unclosed
    CHECK_FALSE(help::parseHelpMarkdown("---\nid: i\ntitle: T\ncategory: C\nbogus: x\n---\n", "f")
                    .has_value()); // an unknown key is an error, so a typo is not silently dropped
    CHECK_FALSE(help::parseHelpMarkdown("---\nid: i\ntitle: T\ncategory: C\nstatus: soon\n---\n", "f")
                    .has_value());
}

// ---------------------------------------------------------------------------------------------
// The database and the §26 retrieval API
// ---------------------------------------------------------------------------------------------

TEST_CASE("HelpDatabase indexes, orders and refuses a duplicate id") {
    help::HelpDatabase db;
    help::HelpDocument a = makeDoc("a/one", "Alpha", "First");
    a.order = 10;
    help::HelpDocument b = makeDoc("b/two", "Beta", "Second");
    b.order = 5;
    REQUIRE(db.addDocument(std::move(a)));
    REQUIRE(db.addDocument(std::move(b)));
    CHECK_FALSE(db.addDocument(makeDoc("a/one", "Alpha again", "First")));

    REQUIRE(db.categories().size() == 2);
    // Category order follows the lowest-ordered topic, so an author shapes navigation from front
    // matter and never from a list in C++.
    CHECK(db.categories()[0].name == "Second");
    CHECK(db.get("a/one") != nullptr);
    CHECK(db.get("nope") == nullptr);
}

TEST_CASE("related() is symmetric even when only one author wrote the link") {
    help::HelpDatabase db;
    help::HelpDocument a = makeDoc("a", "A", "C");
    a.related = {"b"};
    REQUIRE(db.addDocument(std::move(a)));
    REQUIRE(db.addDocument(makeDoc("b", "B", "C")));

    const auto fromA = db.related("a");
    REQUIRE(fromA.size() == 1);
    CHECK(fromA[0]->id == "b");
    // b never mentioned a, and a reader of b still wants to know.
    const auto fromB = db.related("b");
    REQUIRE(fromB.size() == 1);
    CHECK(fromB[0]->id == "a");
    CHECK(db.related("missing").empty());
}

TEST_CASE("Feature and shortcut lookup") {
    help::HelpDatabase db;
    REQUIRE(db.addDocument(makeDoc("t", "T", "C")));
    db.addFeature({.id = "panel.analysis",
                   .name = "Analysis",
                   .kind = help::FeatureKind::Panel,
                   .category = "Audio",
                   .summary = "bands and onsets",
                   .documentId = "t"});
    db.addShortcut({.command = "transport.play", .keys = "Space", .description = "Play or pause.",
                    .context = "General", .documentId = "t"});

    REQUIRE(db.getFeature("panel.analysis") != nullptr);
    CHECK(db.getFeature("panel.analysis")->documentId == "t");
    CHECK(db.getFeature("panel.nope") == nullptr);

    REQUIRE(db.getShortcut("transport.play") != nullptr);
    // §38: looking up by the keys themselves works too, because that is what a person types.
    REQUIRE(db.getShortcut("Space") != nullptr);
    CHECK(db.getShortcut("Space")->command == "transport.play");
    CHECK(db.getShortcut("Cmd+S") == nullptr);
}

// ---------------------------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------------------------

TEST_CASE("Token folding is consistent between a query word and a document word") {
    // The property that matters is not linguistic correctness, it is that both sides fold the same.
    const auto same = [](std::string_view a, std::string_view b) {
        return help::foldToken(a) == help::foldToken(b);
    };
    CHECK(same("shadows", "shadow"));
    CHECK(same("bands", "band"));
    CHECK(same("reflections", "reflection"));
    CHECK(same("rendering", "render"));
    CHECK(same("settings", "setting")); // needs more than one fold pass; the regression this pins
    CHECK(same("matches", "match"));
    CHECK(same("passes", "pass"));
    CHECK(same("frequencies", "frequency"));
    CHECK(same("keys", "key"));

    // ...and that a short word is not mangled into a different one.
    CHECK(help::foldToken("bass") == "bass");
    CHECK(help::foldToken("less") == "less");
    CHECK(help::foldToken("ring") == "ring");
    CHECK(help::foldToken("using") == "using");
}

TEST_CASE("Query scaffolding is dropped, but never all of it") {
    const auto terms = help::queryTerms("how do I make water look better?");
    CHECK(std::ranges::find(terms, "water") != terms.end());
    CHECK(std::ranges::find(terms, "how") == terms.end());
    CHECK(std::ranges::find(terms, "do") == terms.end());
    // "make" is an intent verb and discriminates; it stays.
    CHECK(std::ranges::find(terms, "make") != terms.end());
    // A query that is nothing but scaffolding still searches for something.
    CHECK_FALSE(help::queryTerms("what is it").empty());
    CHECK(help::queryTerms("").empty());
}

TEST_CASE("Search ranks the title match above an incidental body match") {
    help::HelpDatabase db;
    help::HelpDocument target = makeDoc("r/water", "Water", "Rendering",
                                        "How to author water, and what it costs.");
    help::HelpDocument other = makeDoc("r/other", "Something Else", "Rendering",
                                       "water water water water water water water water water");
    REQUIRE(db.addDocument(std::move(target)));
    REQUIRE(db.addDocument(std::move(other)));

    const auto results = db.search("water");
    REQUIRE(results.size() == 2);
    CHECK(results[0].documentId == "r/water");
    // §7: a result explains itself.
    CHECK(std::ranges::find(results[0].matchedTerms, "water") != results[0].matchedTerms.end());
}

TEST_CASE("A placeholder topic never outranks a written one that also matched") {
    help::HelpDatabase db;
    help::HelpDocument gap = makeDoc("gaps/x", "Water", "Not Yet Documented", "water water water");
    gap.status = help::HelpStatus::NotYetDocumented;
    help::HelpDocument real = makeDoc("r/water", "Water Rendering", "Rendering", "water is authored here");
    REQUIRE(db.addDocument(std::move(gap)));
    REQUIRE(db.addDocument(std::move(real)));

    const auto results = db.search("water");
    REQUIRE(results.size() == 2);
    CHECK(results[0].documentId == "r/water");

    help::SearchOptions hideGaps;
    hideGaps.includeGaps = false;
    CHECK(db.search("water", hideGaps).size() == 1);
}

TEST_CASE("The search backend is replaceable without the database knowing which one it has") {
    class NullBackend final : public help::ISearchBackend {
    public:
        [[nodiscard]] std::string_view name() const override { return "null"; }
        void build(const std::vector<help::HelpDocument>&) override {}
        [[nodiscard]] std::vector<help::SearchResult> search(std::string_view,
                                                             const help::SearchOptions&) const override {
            return {};
        }
    };
    help::HelpDatabase db;
    REQUIRE(db.addDocument(makeDoc("a", "Alpha", "C", "alpha")));
    CHECK(db.searchBackendName() == "keyword");
    CHECK_FALSE(db.search("alpha").empty());

    db.setSearchBackend(std::make_unique<NullBackend>());
    CHECK(db.searchBackendName() == "null");
    CHECK(db.search("alpha").empty());

    db.setSearchBackend(nullptr);
    CHECK(db.searchBackendName() == "keyword");
}

// ---------------------------------------------------------------------------------------------
// The AI-facing JSON API (§25, §26, §39)
// ---------------------------------------------------------------------------------------------

TEST_CASE("The help.* tools are read-only and dispatch by either naming convention") {
    const auto tools = help::tools();
    REQUIRE(tools.size() == 5);
    for (const help::ToolDescriptor& tool : tools) {
        CHECK(tool.readOnly);
        CHECK(tool.name.starts_with("help."));
        CHECK_FALSE(tool.description.empty());
        // The schema is handed to a tool registry verbatim, so it has to be valid JSON.
        const auto schema = nlohmann::json::parse(tool.parametersSchema, nullptr, false);
        REQUIRE_FALSE(schema.is_discarded());
        CHECK(schema["type"] == "object");
    }
    const std::vector<std::string> names{"help.search", "help.get", "help.related", "help.getShortcut",
                                         "help.getFeature"};
    for (const std::string& name : names) {
        CHECK(std::ranges::any_of(tools, [&name](const help::ToolDescriptor& t) { return t.name == name; }));
    }
}

TEST_CASE("The retrieval API answers, and fails without throwing") {
    help::HelpDatabase db;
    help::HelpDocument doc = makeDoc("a/one", "Alpha", "First", "Alpha is about bass.");
    doc.related = {"a/two"};
    REQUIRE(db.addDocument(std::move(doc)));
    REQUIRE(db.addDocument(makeDoc("a/two", "Beta", "First", "Beta.")));
    db.addFeature({.id = "panel.x", .name = "X", .kind = help::FeatureKind::Panel, .documentId = "a/one"});
    db.addShortcut({.command = "transport.play", .keys = "Space", .documentId = "a/one"});

    const auto search = help::dispatch(db, "help.search", {{"query", "bass"}});
    CHECK(search["results"].size() == 1);
    CHECK(search["results"][0]["id"] == "a/one");

    const auto got = help::dispatch(db, "help.get", {{"id", "a/one"}});
    CHECK(got["title"] == "Alpha");
    CHECK(got["body"].get<std::string>().find("bass") != std::string::npos);
    CHECK_FALSE(got.contains("error"));

    CHECK(help::dispatch(db, "help.related", {{"id", "a/one"}})["related"].size() == 1);
    CHECK(help::dispatch(db, "help.getShortcut", {{"command", "transport.play"}})["keys"] == "Space");
    CHECK(help::dispatch(db, "help.get_shortcut", {{"keys", "Space"}})["command"] == "transport.play");
    CHECK(help::dispatch(db, "help.getFeature", {{"id", "panel.x"}})["documents"].size() == 1);

    // Every failure is a result object, not an exception: a tool that throws into an orchestrator's
    // loop ends the task.
    const auto missing = help::dispatch(db, "help.get", {{"id", "nope"}});
    CHECK(missing.contains("error"));
    CHECK(missing.contains("suggestions"));
    CHECK(help::dispatch(db, "help.search", nlohmann::json::object()).contains("error"));
    CHECK(help::dispatch(db, "help.nonsense", nlohmann::json::object()).contains("error"));
    // A shortcut miss hands back the whole table, so an agent sees what exists rather than guessing.
    CHECK(help::dispatch(db, "help.getShortcut", {{"command", "duplicate"}})["known"].size() == 1);

    CHECK(help::index(db)["topics"] == 2);
}

TEST_CASE("A topic that declares itself undocumented tells the AI not to fill the gap") {
    help::HelpDatabase db;
    help::HelpDocument gap = makeDoc("gaps/x", "X", "Not Yet Documented", "Not written.");
    gap.status = help::HelpStatus::NotYetDocumented;
    REQUIRE(db.addDocument(std::move(gap)));
    const auto got = help::dispatch(db, "help.get", {{"id", "gaps/x"}});
    CHECK(got["status"] == "not-yet-documented");
    REQUIRE(got.contains("caution"));
}

// ---------------------------------------------------------------------------------------------
// The source scan and the §35 validator
// ---------------------------------------------------------------------------------------------

TEST_CASE("The source scan finds the application's real panels, commands and key bindings") {
    const help::AppSurface surface = help::scanApplicationSource(sourceRoot());

    // Assert on coverage before anything else. A scan that quietly matched nothing would report a
    // clean bill of health for documentation it never checked, which is worse than no validator.
    INFO("missing files: " << surface.coverage.missing.size());
    CHECK(surface.coverage.filesMissing == 0);
    CHECK(surface.coverage.warnings.empty());
    CHECK(surface.coverage.filesRead == 3);

    // The registry rows, read out of editor_layout.cpp.
    CHECK(surface.panels.size() >= 12);
    REQUIRE(surface.findPanel("Analysis") != nullptr);
    CHECK(surface.findPanel("Analysis")->region == "Bottom");
    REQUIRE(surface.findPanel("Help") != nullptr);

    // The menu bar, read out of control_panel.cpp, with nesting.
    CHECK(surface.commands.size() >= 20);
    CHECK(std::ranges::any_of(surface.commands, [](const help::AppCommand& c) {
        return c.menuPath == "File > Open Audio...";
    }));
    CHECK(std::ranges::any_of(surface.commands, [](const help::AppCommand& c) {
        return c.menuPath == "Camera > Direct to Music";
    }));
    // The View menu is generated from the panel registry, so its entries are collected from there.
    CHECK(std::ranges::any_of(surface.commands, [](const help::AppCommand& c) {
        return c.menuPath == "View > Analysis";
    }));

    // The key bindings, read out of application.cpp.
    CHECK(surface.hasShortcutKeys("Space"));
    CHECK(surface.hasShortcutKeys("Left"));
    CHECK(surface.hasShortcutKeys("O"));
    CHECK_FALSE(surface.hasShortcutKeys("Cmd+S"));
}

TEST_CASE("The validator reports documentation that refers to things that are not there") {
    help::HelpDatabase db;
    help::HelpDocument doc = makeDoc("a", "A", "C", "See [there](help://nowhere).");
    doc.related = {"also-nowhere"};
    doc.features = {"feature.nope"};
    doc.shortcuts = {"command.nope"};
    doc.parameters = {"not/a/parameter"};
    REQUIRE(db.addDocument(std::move(doc)));
    db.addShortcut({.command = "transport.play", .keys = "F13", .documentId = "a"});
    db.addFeature({.id = "feature.orphan", .name = "Orphan", .kind = help::FeatureKind::Subsystem});

    help::AppSurface surface;
    surface.coverage.filesRead = 1; // pretend a scan happened, so "stale" is a real claim
    surface.shortcuts.push_back({.keys = "Space"});
    surface.parameterPaths = {"a/real/parameter"};

    const auto findings = help::validate(db, surface);
    const auto has = [&findings](std::string_view code) {
        return std::ranges::any_of(findings, [code](const help::Finding& f) { return f.code == code; });
    };
    CHECK(has("broken-link"));          // both the inline link and the related entry
    CHECK(has("unknown-feature"));
    CHECK(has("unknown-shortcut"));
    CHECK(has("unknown-parameter"));
    CHECK(has("stale-shortcut"));       // F13 is documented and nothing binds it
    CHECK(has("feature-without-doc"));
    CHECK(has("undocumented-shortcut")); // Space is bound and undocumented
    CHECK(help::summarize(findings).errors > 0);
}

TEST_CASE("The validator does not claim a shortcut is stale when nothing was scanned") {
    help::HelpDatabase db;
    REQUIRE(db.addDocument(makeDoc("a", "A", "C")));
    db.addShortcut({.command = "transport.play", .keys = "Space", .documentId = "a"});

    help::AppSurface nothingScanned; // filesRead == 0
    const auto findings = help::validate(db, nothingScanned);
    CHECK(std::ranges::none_of(findings, [](const help::Finding& f) { return f.code == "stale-shortcut"; }));
}

TEST_CASE("The validator reports a panel or a command that no topic documents") {
    help::HelpDatabase db;
    REQUIRE(db.addDocument(makeDoc("a", "A", "C", "Nothing relevant here.")));

    help::AppSurface surface;
    surface.coverage.filesRead = 3;
    surface.panels.push_back({.id = "Chronoscope", .label = "Chronoscope", .region = "Left"});
    surface.commands.push_back({.menuPath = "File > Defenestrate", .label = "Defenestrate"});

    const auto findings = help::validate(db, surface);
    CHECK(std::ranges::any_of(findings, [](const help::Finding& f) {
        return f.code == "undocumented-panel" && f.subject == "Chronoscope";
    }));
    CHECK(std::ranges::any_of(findings, [](const help::Finding& f) {
        return f.code == "undocumented-command";
    }));
}

// ---------------------------------------------------------------------------------------------
// The shipped content. §41: do not ship an empty shell.
// ---------------------------------------------------------------------------------------------

TEST_CASE("The shipped Help content loads, and is not a shell") {
    if (!std::filesystem::is_directory(contentDir())) {
        SKIP("docs/help is not present in this checkout");
    }
    help::HelpDatabase db;
    const auto report = db.loadDirectory(contentDir());
    REQUIRE(report.has_value());
    INFO("load errors: " << report->errors.size());
    CHECK(report->errors.empty());

    CHECK(report->documents >= 40);
    CHECK(report->features >= 25);
    // A floor, like every other count in this test. It was an equality when six was the whole set
    // AV Gen bound; the world editor then landed seventeen more, and an exact count here would make
    // documenting them a failure in the smoke test that only asks whether content loaded. That the
    // table matches the binaries exactly, in *both* directions, is checked properly by "Every
    // shortcut in the reference is bound, and every binding is in the reference".
    CHECK(report->shortcuts >= 6);
    CHECK(db.categories().size() >= 8);

    // Every category a user is told to expect actually exists.
    const auto hasCategory = [&db](std::string_view name) {
        return std::ranges::any_of(db.categories(),
                                   [name](const help::HelpCategory& c) { return c.name == name; });
    };
    for (const char* name : {"Getting Started", "Audio", "Modulation", "Sequencer", "Rendering",
                             "Shaders", "Performance", "Troubleshooting", "Reference"}) {
        INFO("category " << name);
        CHECK(hasCategory(name));
    }

    // The topics the rest of the system links to by name have to exist, or a contextual link and a
    // menu entry go nowhere.
    for (const char* id : {"start/welcome", "start/how-it-works", "start/interface",
                           "reference/keyboard-shortcuts", "modulation/recipes",
                           "troubleshooting/index", "performance/diagnosis",
                           "rendering/emission-and-bloom"}) {
        INFO("topic " << id);
        CHECK(db.get(id) != nullptr);
    }

    // Not a shell: every stable topic has real body text.
    for (const help::HelpDocument& doc : db.documents()) {
        if (doc.status != help::HelpStatus::Stable) {
            continue;
        }
        INFO(doc.id);
        CHECK(doc.markdown.size() > 400);
        CHECK_FALSE(doc.summary.empty());
    }
}

TEST_CASE("The shipped content answers the questions the acceptance criteria name") {
    if (!std::filesystem::is_directory(contentDir())) {
        SKIP("docs/help is not present in this checkout");
    }
    help::HelpDatabase db;
    REQUIRE(db.loadDirectory(contentDir()).has_value());

    // §51: a new user should be able to answer these without leaving the application. Each is
    // phrased the way somebody would actually type it.
    const std::vector<std::pair<std::string, std::string>> cases{
        {"how do i make something glow", "rendering/emission-and-bloom"},
        {"make the glow illuminate nearby objects", "rendering/emission-and-bloom"},
        {"how do i make it react to the bass", "modulation/recipes"},
        {"why is my scene slow", "performance/diagnosis"},
        {"render offline", "rendering/offline-render"},
        {"keyboard shortcuts", "reference/keyboard-shortcuts"},
        {"what is a parameter", "modulation/parameters"},
        {"how does av gen work", "start/how-it-works"},
        {"animate a camera", "sequencer/camera-direction"},
        {"add a keyframe", "sequencer/timeline-automation"},
        {"what does a rendering setting do", "rendering/overview"},
        {"shader uniforms", "shaders/uniforms"},
        {"midi controller", "modulation/external-control"},
    };
    for (const auto& [query, expected] : cases) {
        INFO("query: " << query << " -- expected " << expected);
        const auto results = db.search(query);
        REQUIRE_FALSE(results.empty());
        // The expected topic is in the top three. Insisting on first place would pin the scorer's
        // tuning rather than its usefulness.
        const bool found = std::ranges::any_of(results | std::views::take(3),
                                               [&want = expected](const help::SearchResult& r) {
                                                   return r.documentId == want;
                                               });
        CHECK(found);
    }

    // The areas that are deliberately not written must still answer, and must answer honestly.
    for (const auto& [query, expected] : std::vector<std::pair<std::string, std::string>>{
             {"water", "gaps/terrain-water-navigation"},
             {"how do i select an object", "gaps/world-editor"},
             {"what can the ai do", "gaps/ai-director"}}) {
        INFO("gap query: " << query);
        const auto results = db.search(query);
        REQUIRE_FALSE(results.empty());
        const bool found = std::ranges::any_of(results | std::views::take(3),
                                               [&want = expected](const help::SearchResult& r) {
                                                   return r.documentId == want;
                                               });
        CHECK(found);
        CHECK(db.get(expected)->status == help::HelpStatus::NotYetDocumented);
    }
}

TEST_CASE("The shipped content and the running application do not disagree") {
    if (!std::filesystem::is_directory(contentDir())) {
        SKIP("docs/help is not present in this checkout");
    }
    help::HelpDatabase db;
    REQUIRE(db.loadDirectory(contentDir()).has_value());

    const help::AppSurface surface = help::scanApplicationSource(sourceRoot());
    REQUIRE(surface.coverage.ok());

    const auto findings = help::validate(db, surface);
    const help::ValidationSummary summary = help::summarize(findings);

    // Errors are documentation saying something untrue. There must be none.
    if (summary.errors > 0) {
        // Print the whole report, not the count: a failure here should tell whoever sees it exactly
        // what to fix, in the same words the lint tool uses.
        FAIL(help::formatReport(db, surface, findings));
    }
    CHECK(summary.errors == 0);

    // Warnings are coverage gaps, and are expected while the world-editor and AI passes are in
    // flight. The number is not pinned -- pinning it would make landing a new panel a test failure
    // in Help rather than a report. But the *kinds* are checked, so a new kind of drift is visible.
    for (const help::Finding& finding : findings) {
        INFO(finding.code << ": " << finding.subject << " -- " << finding.message);
        CHECK((finding.code == "advertised-unbound" || finding.code == "undocumented-panel" ||
               finding.code == "undocumented-command" || finding.code == "undocumented-shortcut" ||
               finding.code == "feature-without-doc" || finding.code == "known-gap" ||
               finding.code == "orphan-topic" || finding.code == "scan-incomplete"));
    }

    // The gap register is a deliverable: the three areas this pass deliberately did not document
    // must be reported, so they cannot be quietly forgotten.
    const auto gaps = std::ranges::count(findings, std::string("known-gap"), &help::Finding::code);
    CHECK(gaps >= 3);
}

TEST_CASE("Every shortcut in the reference is bound, and every binding is in the reference") {
    if (!std::filesystem::is_directory(contentDir())) {
        SKIP("docs/help is not present in this checkout");
    }
    help::HelpDatabase db;
    REQUIRE(db.loadDirectory(contentDir()).has_value());
    const help::AppSurface surface = help::scanApplicationSource(sourceRoot());
    REQUIRE(surface.coverage.ok());

    // This is the pair of checks that makes the short §9 reference safe to ship. If a later pass
    // removes a key, the first half fails; if it adds one, the second half does. Both failures are
    // the point, so both messages say exactly what to do about them.
    for (const help::HelpShortcut& shortcut : db.featureTable().shortcuts()) {
        INFO("docs/help/features.json documents '" << shortcut.command << "' as '" << shortcut.keys
             << "', and the source scan found no handler that binds it. Either the binding was "
                "removed -- delete the row -- or the scan can no longer see it, which "
                "help::scanApplicationSource needs teaching about.");
        CHECK(surface.hasShortcutKeys(shortcut.keys));
    }
    for (const help::AppShortcut& bound : surface.shortcuts) {
        INFO("'" << bound.keys << "' is bound at " << bound.sourceRef
             << " and is in no Help topic. Add a row to docs/help/features.json under "
                "\"shortcuts\" (command, keys, context, description, document) and list it in "
                "docs/help/reference-shortcuts.md. This check is how a new binding gets documented "
                "instead of going unmentioned.");
        CHECK(db.getShortcut(bound.keys) != nullptr);
    }
}

// ---------------------------------------------------------------------------------------------
// §34, enforced on the one thing a source scan cannot see
// ---------------------------------------------------------------------------------------------

TEST_CASE("Every parameter path the documentation cites is one the engine registers") {
    if (!std::filesystem::is_directory(contentDir())) {
        SKIP("docs/help is not present in this checkout");
    }
    help::HelpDatabase db;
    REQUIRE(db.loadDirectory(contentDir()).has_value());

    // A scan of the source cannot know these: most parameters are registered when a scene loads,
    // so the only honest source is a running engine. The union of what two engines register --
    // one on the built-in orb scene, one on a composition -- is the set a topic may cite. Two
    // rather than one because the groups are disjoint: `orb/` exists only in the first and
    // `env/`, `scene/volume*` and the per-node groups only in the second.
    help::AppSurface surface;
    surface.coverage.filesRead = 1;
    const auto collect = [&surface](app::Engine& engine) {
        for (const params::IParameter* param : engine.params().ordered()) {
            if (std::ranges::find(surface.parameterPaths, param->path()) == surface.parameterPaths.end()) {
                surface.parameterPaths.push_back(param->path());
            }
        }
    };
    {
        app::Engine engine(app::EngineMode::Offline);
        engine.loadOrbScene();
        collect(engine);
    }
    {
        app::Engine engine(app::EngineMode::Offline);
        const auto scene = sourceRoot() / "examples" / "chamber" / "chamber.scene.json";
        if (std::filesystem::exists(scene)) {
            const auto loaded = engine.loadComposition(scene);
            if (!loaded) {
                FAIL(loaded.error().message);
            }
            collect(engine);
        }
    }
    REQUIRE(surface.parameterPaths.size() > 40);

    std::string missing;
    for (const help::HelpDocument& doc : db.documents()) {
        for (const std::string& path : doc.parameters) {
            if (std::ranges::find(surface.parameterPaths, path) == surface.parameterPaths.end()) {
                missing += "\n  " + doc.id + " cites '" + path + "'";
            }
        }
    }
    INFO("paths the engine does not register:" << missing);
    CHECK(missing.empty());
}
