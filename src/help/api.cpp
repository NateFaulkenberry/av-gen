#include "help/api.hpp"

#include "help/markdown.hpp"

#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>

namespace avgen::help {
namespace {

using nlohmann::json;

constexpr std::array<ToolDescriptor, 5> kTools{{
    {"help.search",
     "Search AV Gen's own documentation. Use this before answering any question about what AV Gen "
     "can do, what a setting means, or how a workflow goes -- it is the same content the Help panel "
     "shows the user, so an answer drawn from it matches what they can read. Natural phrasing works "
     "(\"how do I make something react to the bass\"). Returns ranked topics with a summary and an "
     "excerpt; follow up with help.get for the full text.",
     R"({"type":"object","properties":{)"
     R"("query":{"type":"string","description":"what to look for; a phrase or a question"},)"
     R"("limit":{"type":"integer","minimum":1,"maximum":50,"default":8},)"
     R"("category":{"type":"string","description":"restrict to one category, e.g. \"Audio\""},)"
     R"("includeGaps":{"type":"boolean","default":true,"description":"include topics that declare themselves undocumented"})"
     R"(},"required":["query"]})",
     true},
    {"help.get",
     "Fetch one documentation topic in full by its id, as markdown. Ids come from help.search or "
     "from a topic's related list. A topic whose status is not \"stable\" is a placeholder naming a "
     "gap: say so rather than filling it in.",
     R"({"type":"object","properties":{)"
     R"("id":{"type":"string","description":"the topic id, e.g. \"audio/analysis\""},)"
     R"("format":{"type":"string","enum":["markdown","text"],"default":"markdown"})"
     R"(},"required":["id"]})",
     true},
    {"help.related",
     "The topics related to one topic, in both directions. Use it to widen an answer without "
     "another search, or to find the neighbouring concept a user is actually asking about.",
     R"({"type":"object","properties":{)"
     R"("id":{"type":"string","description":"the topic id"})"
     R"(},"required":["id"]})",
     true},
    {"help.getShortcut",
     "Look up a keyboard shortcut by its command id (\"transport.play\") or by the keys themselves "
     "(\"Space\"). Returns the keys, what they do, which context they apply in, and the topic that "
     "explains them. If a command is not here, AV Gen does not bind it -- do not guess a key.",
     R"({"type":"object","properties":{)"
     R"("command":{"type":"string","description":"the command id, e.g. \"transport.play\""},)"
     R"("keys":{"type":"string","description":"the keys, e.g. \"Space\"; used when command is absent"})"
     R"(},"required":[]})",
     true},
    {"help.getFeature",
     "Look up one feature -- a panel, a menu command or an engine subsystem -- by its id. Returns "
     "what it is, where it lives in the interface, when it is available, its shortcut if it has "
     "one, and every topic that documents it. Use it to answer \"where is X\" and \"can AV Gen do "
     "X\" without guessing at the interface.",
     R"({"type":"object","properties":{)"
     R"("id":{"type":"string","description":"the feature id, e.g. \"panel.analysis\""})"
     R"(},"required":["id"]})",
     true},
}};

json documentSummaryJson(const HelpDocument& doc) {
    return json{{"id", doc.id},
                {"title", doc.title},
                {"category", doc.category},
                {"summary", doc.summary},
                {"status", helpStatusName(doc.status)}};
}

json featureJson(const HelpFeature& feature) {
    json out{{"id", feature.id},
             {"name", feature.name},
             {"kind", featureKindName(feature.kind)},
             {"category", feature.category},
             {"summary", feature.summary},
             {"document", feature.documentId}};
    if (!feature.menuPath.empty()) {
        out["menu"] = feature.menuPath;
    }
    if (!feature.shortcut.empty()) {
        out["shortcut"] = feature.shortcut;
    }
    if (!feature.availability.empty()) {
        out["availability"] = feature.availability;
    }
    if (!feature.related.empty()) {
        out["related"] = feature.related;
    }
    return out;
}

json shortcutJson(const HelpShortcut& shortcut) {
    return json{{"command", shortcut.command},
                {"keys", shortcut.keys},
                {"description", shortcut.description},
                {"context", shortcut.context},
                {"document", shortcut.documentId},
                {"configurable", shortcut.configurable}};
}

json error(std::string message) { return json{{"error", std::move(message)}}; }

std::string stringArg(const json& args, std::string_view key) {
    if (!args.is_object()) {
        return {};
    }
    const auto it = args.find(key);
    if (it == args.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

} // namespace

std::span<const ToolDescriptor> tools() { return {kTools.data(), kTools.size()}; }

json search(const HelpDatabase& db, const json& args) {
    const std::string query = stringArg(args, "query");
    if (query.empty()) {
        return error("help.search needs a non-empty 'query'");
    }
    SearchOptions options;
    if (args.is_object()) {
        if (const auto it = args.find("limit"); it != args.end() && it->is_number_integer()) {
            options.limit = static_cast<std::size_t>(std::clamp(it->get<int>(), 1, 50));
        } else {
            options.limit = 8; // a tool result goes into a context window, not onto a page
        }
        if (const auto it = args.find("category"); it != args.end() && it->is_string()) {
            options.category = it->get<std::string>();
        }
        if (const auto it = args.find("includeGaps"); it != args.end() && it->is_boolean()) {
            options.includeGaps = it->get<bool>();
        }
    }

    json results = json::array();
    for (const SearchResult& result : db.search(query, options)) {
        results.push_back(json{{"id", result.documentId},
                               {"title", result.title},
                               {"category", result.category},
                               {"summary", result.summary},
                               {"score", result.score},
                               {"matchedTerms", result.matchedTerms},
                               {"excerpt", result.excerpt},
                               {"status", helpStatusName(result.status)}});
    }
    return json{{"query", query}, {"backend", db.searchBackendName()}, {"results", std::move(results)}};
}

json get(const HelpDatabase& db, const json& args) {
    const std::string id = stringArg(args, "id");
    if (id.empty()) {
        return error("help.get needs an 'id'");
    }
    const HelpDocument* doc = db.get(id);
    if (doc == nullptr) {
        // Not finding a topic is an ordinary outcome, so the failure carries the nearest matches
        // rather than a dead end: an agent that mistyped an id gets the right one back in the same
        // turn instead of spending another on a search.
        json suggestions = json::array();
        SearchOptions options;
        options.limit = 5;
        for (const SearchResult& result : db.search(id, options)) {
            suggestions.push_back(result.documentId);
        }
        json out = error("no Help topic with id '" + id + "'");
        out["suggestions"] = std::move(suggestions);
        return out;
    }

    const std::string format = stringArg(args, "format");
    std::string body = doc->markdown;
    if (format == "text") {
        // Blocks flattened: the same words with the markup removed, for a consumer that would only
        // strip it itself.
        body.clear();
        for (const HelpBlock& block : doc->body) {
            switch (block.kind) {
            case BlockKind::Code:
                body += block.text;
                break;
            case BlockKind::Table:
                for (const HelpTableRow& row : block.rows) {
                    for (std::size_t i = 0; i < row.cells.size(); ++i) {
                        if (i > 0) {
                            body += " | ";
                        }
                        body += flattenSpans(row.cells[i]);
                    }
                    body += '\n';
                }
                continue;
            case BlockKind::Rule:
                continue;
            default:
                body += flattenSpans(block.spans);
                break;
            }
            body += "\n\n";
        }
    }

    json out = documentSummaryJson(*doc);
    out["audience"] = helpAudienceName(doc->audience);
    out["version"] = doc->version;
    out["tags"] = doc->tags;
    out["keywords"] = doc->keywords;
    out["related"] = doc->related;
    out["features"] = doc->features;
    out["shortcuts"] = doc->shortcuts;
    out["body"] = std::move(body);
    if (doc->status != HelpStatus::Stable) {
        out["caution"] = "This topic declares itself incomplete. Do not fill the gap from general "
                         "knowledge: say that AV Gen's documentation does not cover it yet.";
    }
    return out;
}

json related(const HelpDatabase& db, const json& args) {
    const std::string id = stringArg(args, "id");
    if (id.empty()) {
        return error("help.related needs an 'id'");
    }
    if (db.get(id) == nullptr) {
        return error("no Help topic with id '" + id + "'");
    }
    json out = json::array();
    for (const HelpDocument* doc : db.related(id)) {
        out.push_back(documentSummaryJson(*doc));
    }
    return json{{"id", id}, {"related", std::move(out)}};
}

json getShortcut(const HelpDatabase& db, const json& args) {
    std::string key = stringArg(args, "command");
    if (key.empty()) {
        key = stringArg(args, "keys");
    }
    if (key.empty()) {
        return error("help.getShortcut needs a 'command' or 'keys'");
    }
    const HelpShortcut* shortcut = db.getShortcut(key);
    if (shortcut == nullptr) {
        json out = error("no shortcut for '" + key + "'");
        // The whole table is small, and an agent that guessed wrong should be able to see what does
        // exist rather than invent a plausible key -- which is the exact failure §34 is about.
        json known = json::array();
        for (const HelpShortcut& s : db.featureTable().shortcuts()) {
            known.push_back(json{{"command", s.command}, {"keys", s.keys}});
        }
        out["known"] = std::move(known);
        return out;
    }
    return shortcutJson(*shortcut);
}

json getFeature(const HelpDatabase& db, const json& args) {
    const std::string id = stringArg(args, "id");
    if (id.empty()) {
        return error("help.getFeature needs an 'id'");
    }
    const HelpFeature* feature = db.getFeature(id);
    if (feature == nullptr) {
        json out = error("no feature with id '" + id + "'");
        json known = json::array();
        for (const HelpFeature& f : db.featureTable().features()) {
            known.push_back(f.id);
        }
        out["known"] = std::move(known);
        return out;
    }
    json out = featureJson(*feature);
    // The feature's own topic first -- that is the "Learn more" target -- then every topic that
    // declares the feature in its front matter. Deduplicated, because the two usually overlap.
    json documents = json::array();
    std::vector<std::string> seen;
    const auto push = [&documents, &seen](const HelpDocument* doc) {
        if (doc == nullptr || std::ranges::find(seen, doc->id) != seen.end()) {
            return;
        }
        seen.push_back(doc->id);
        documents.push_back(documentSummaryJson(*doc));
    };
    push(db.get(feature->documentId));
    for (const HelpDocument* doc : db.documentsForFeature(id)) {
        push(doc);
    }
    out["documents"] = std::move(documents);
    if (const HelpShortcut* shortcut = db.getShortcut(feature->shortcut); shortcut != nullptr) {
        out["keys"] = shortcut->keys;
    }
    return out;
}

json index(const HelpDatabase& db) {
    json categories = json::array();
    for (const HelpCategory& category : db.categories()) {
        json topics = json::array();
        for (const HelpDocument* doc : category.documents) {
            topics.push_back(json{{"id", doc->id}, {"title", doc->title}, {"summary", doc->summary},
                                  {"status", helpStatusName(doc->status)}});
        }
        categories.push_back(json{{"name", category.name}, {"topics", std::move(topics)}});
    }
    json features = json::array();
    for (const HelpFeature& feature : db.featureTable().features()) {
        features.push_back(json{{"id", feature.id}, {"name", feature.name},
                                {"kind", featureKindName(feature.kind)}});
    }
    return json{{"topics", db.documents().size()},
                {"categories", std::move(categories)},
                {"features", std::move(features)},
                {"shortcuts", db.featureTable().shortcuts().size()},
                {"backend", db.searchBackendName()}};
}

json dispatch(const HelpDatabase& db, std::string_view toolName, const json& args) {
    // Names are matched with and without the `help.` prefix and in either case convention, because
    // a tool registry's naming is not something Help should have an opinion about.
    std::string_view name = toolName;
    if (name.starts_with("help.")) {
        name.remove_prefix(5);
    }
    if (name == "search") {
        return search(db, args);
    }
    if (name == "get") {
        return get(db, args);
    }
    if (name == "related") {
        return related(db, args);
    }
    if (name == "getShortcut" || name == "get_shortcut") {
        return getShortcut(db, args);
    }
    if (name == "getFeature" || name == "get_feature") {
        return getFeature(db, args);
    }
    if (name == "index") {
        return index(db);
    }
    return error("unknown Help tool '" + std::string(toolName) + "'");
}

} // namespace avgen::help
