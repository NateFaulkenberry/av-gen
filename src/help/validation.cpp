#include "help/validation.hpp"

#include "help/search.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <unordered_set>

namespace avgen::help {
namespace {

// The folded word sequence of every topic a reader would actually learn something from. Built once
// per validation run: the coverage checks ask this question a few dozen times, and re-tokenising
// the whole corpus for each of them was most of the tool's running time.
//
// Placeholders are excluded. A topic that names a feature in order to say it is not documented is
// not coverage of it, and counting it would make the gap register hide the gaps.
std::vector<std::vector<std::string>> corpusOf(const HelpDatabase& db) {
    std::vector<std::vector<std::string>> corpus;
    for (const HelpDocument& doc : db.documents()) {
        if (doc.status == HelpStatus::NotYetDocumented) {
            continue;
        }
        std::string text = doc.title;
        text += ' ';
        text += doc.summary;
        text += ' ';
        text += doc.markdown;
        for (const std::string& keyword : doc.keywords) {
            text += ' ';
            text += keyword;
        }
        corpus.push_back(tokenize(text));
    }
    return corpus;
}

// Does any topic actually talk about this thing? A mention in the prose counts, not just a
// `features:` declaration, because the question the coverage checks ask is "would a reader find out
// about this from Help" rather than "did somebody fill in the metadata".
//
// Matched as a **phrase**, contiguously. An unordered bag of words is close to vacuous over a corpus
// this size -- "Save Project" would be satisfied by any topic that happens to contain "save" and
// "project" in unrelated sentences, which quietly turns the coverage half of the validator off.
bool corpusMentions(const std::vector<std::vector<std::string>>& corpus, std::string_view phrase) {
    const std::vector<std::string> wanted = tokenize(phrase);
    if (wanted.empty()) {
        return true; // a label with no words in it is not something coverage can be claimed about
    }
    for (const std::vector<std::string>& topic : corpus) {
        if (topic.size() < wanted.size()) {
            continue;
        }
        if (std::ranges::search(topic, wanted).begin() != topic.end()) {
            return true;
        }
    }
    return false;
}

void collectLinks(const std::vector<InlineSpan>& spans, std::vector<std::string>& out) {
    for (const InlineSpan& span : spans) {
        if (span.style == InlineSpan::Style::Link && span.internal && !span.target.empty()) {
            out.push_back(span.target);
        }
    }
}

std::vector<std::string> internalLinksOf(const HelpDocument& doc) {
    std::vector<std::string> out;
    for (const HelpBlock& block : doc.body) {
        collectLinks(block.spans, out);
        for (const HelpTableRow& row : block.rows) {
            for (const std::vector<InlineSpan>& cell : row.cells) {
                collectLinks(cell, out);
            }
        }
    }
    return out;
}

} // namespace

std::string_view severityName(Severity severity) {
    switch (severity) {
    case Severity::Error:
        return "error";
    case Severity::Warning:
        return "warning";
    case Severity::Info:
        return "info";
    }
    return "info";
}

std::vector<Finding> validate(const HelpDatabase& db, const AppSurface& surface) {
    std::vector<Finding> findings;
    const auto add = [&findings](Severity severity, std::string code, std::string subject, std::string message) {
        findings.push_back({severity, std::move(code), std::move(subject), std::move(message)});
    };

    for (const std::string& warning : surface.coverage.warnings) {
        add(Severity::Warning, "scan-incomplete", "source scan", warning);
    }
    for (const std::string& missing : surface.coverage.missing) {
        add(Severity::Warning, "scan-incomplete", missing,
            fmt::format("'{}' was not found, so nothing it defines could be checked", missing));
    }

    // ---- 1. Does the documentation refer to things that are not there? -------------------------

    std::unordered_set<std::string> linkTargets;
    for (const HelpDocument& doc : db.documents()) {
        if (doc.status == HelpStatus::Stable && doc.summary.empty()) {
            add(Severity::Error, "missing-summary", doc.id,
                "a stable topic with no summary: search results and category pages will show a "
                "blank line for it");
        }
        if (doc.status == HelpStatus::Stable && doc.body.empty()) {
            add(Severity::Error, "empty-body", doc.id,
                "a stable topic with an empty body. Either write it or mark it not-yet-documented, "
                "which is what the status is for");
        }

        for (const std::string& id : doc.related) {
            linkTargets.insert(id);
            if (db.get(id) == nullptr) {
                add(Severity::Error, "broken-link", doc.id,
                    fmt::format("`related: {}` names no topic", id));
            }
        }
        for (const std::string& target : internalLinksOf(doc)) {
            linkTargets.insert(target);
            if (db.get(target) == nullptr) {
                add(Severity::Error, "broken-link", doc.id,
                    fmt::format("links to help://{}, which does not exist", target));
            }
        }
        for (const std::string& feature : doc.features) {
            if (db.getFeature(feature) == nullptr) {
                add(Severity::Error, "unknown-feature", doc.id,
                    fmt::format("declares feature '{}', which nothing registers", feature));
            }
        }
        for (const std::string& command : doc.shortcuts) {
            if (db.getShortcut(command) == nullptr) {
                add(Severity::Error, "unknown-shortcut", doc.id,
                    fmt::format("declares shortcut '{}', which is not in the shortcut table", command));
            }
        }
        if (!surface.parameterPaths.empty()) {
            for (const std::string& path : doc.parameters) {
                if (std::ranges::find(surface.parameterPaths, path) == surface.parameterPaths.end()) {
                    add(Severity::Error, "unknown-parameter", doc.id,
                        fmt::format("cites parameter '{}', which this build does not register", path));
                }
            }
        }
        if (doc.status != HelpStatus::Stable) {
            add(Severity::Info, "known-gap", doc.id,
                fmt::format("{}: {}", helpStatusName(doc.status),
                            doc.summary.empty() ? std::string("no summary given") : doc.summary));
        }
    }

    // A documented shortcut must have a call site. This is the check that catches a key binding
    // being removed or renamed under documentation that still claims it.
    for (const HelpShortcut& shortcut : db.featureTable().shortcuts()) {
        if (!shortcut.documentId.empty() && db.get(shortcut.documentId) == nullptr) {
            add(Severity::Error, "stale-feature-doc", shortcut.command,
                fmt::format("points at topic '{}', which does not exist", shortcut.documentId));
        }
        if (surface.shortcuts.empty() && surface.coverage.filesRead == 0) {
            continue; // nothing was scanned; saying "stale" would be a lie about a lie
        }
        if (!surface.hasShortcutKeys(shortcut.keys)) {
            add(Severity::Error, "stale-shortcut", shortcut.command,
                fmt::format("documented as '{}', but no key handler in the scanned source binds it",
                            shortcut.keys));
        }
    }

    for (const HelpFeature& feature : db.featureTable().features()) {
        if (feature.documentId.empty()) {
            add(Severity::Warning, "feature-without-doc", feature.id,
                "has no Help topic, so a 'Learn more' from it goes nowhere");
        } else if (db.get(feature.documentId) == nullptr) {
            add(Severity::Error, "stale-feature-doc", feature.id,
                fmt::format("points at topic '{}', which does not exist", feature.documentId));
        }
    }

    // ---- 2. Does the application have things the documentation does not cover? ------------------

    const std::vector<std::vector<std::string>> corpus = corpusOf(db);

    for (const AppPanel& panel : surface.panels) {
        const std::string featureId = "panel." + slugify(panel.id);
        const bool documented =
            !db.documentsForFeature(featureId).empty() || corpusMentions(corpus, panel.label);
        if (!documented) {
            add(Severity::Warning, "undocumented-panel", panel.id,
                fmt::format("the '{}' panel ({}, {}) is registered but no topic documents it", panel.label,
                            panel.region, panel.hint.empty() ? "no hint" : panel.hint));
        }
    }

    for (const AppCommand& command : surface.commands) {
        if (!corpusMentions(corpus, command.label)) {
            add(Severity::Warning, "undocumented-command", command.menuPath,
                fmt::format("no topic mentions this command ({})", command.sourceRef));
        }
        // A menu that advertises a key the application does not bind is a lie the *application*
        // tells, and Help repeating it would make it two. Reported here because this is the tool
        // that can see both halves.
        if (!command.shortcut.empty() && !surface.shortcuts.empty() &&
            !surface.hasShortcutKeys(command.shortcut)) {
            add(Severity::Warning, "advertised-unbound", command.menuPath,
                fmt::format("the menu advertises '{}' but no key handler binds it ({})", command.shortcut,
                            command.sourceRef));
        }
    }

    for (const AppShortcut& shortcut : surface.shortcuts) {
        const bool documented = std::ranges::any_of(
            db.featureTable().shortcuts(), [&shortcut](const HelpShortcut& s) { return s.keys == shortcut.keys; });
        if (!documented) {
            add(Severity::Warning, "undocumented-shortcut", shortcut.keys,
                fmt::format("bound at {} ({}) and not in the shortcut reference", shortcut.sourceRef,
                            shortcut.action.empty() ? "action unread" : shortcut.action));
        }
    }

    // ---- 3. Reachability -----------------------------------------------------------------------

    for (const HelpCategory& category : db.categories()) {
        for (std::size_t i = 0; i < category.documents.size(); ++i) {
            const HelpDocument* doc = category.documents[i];
            if (doc->status != HelpStatus::Stable || i == 0) {
                continue; // the first topic of a category is reachable by navigating to it
            }
            if (!linkTargets.contains(doc->id) && db.related(doc->id).empty()) {
                add(Severity::Info, "orphan-topic", doc->id,
                    "nothing links to this topic and it declares no related topics; it is reachable "
                    "only through its category list and search");
            }
        }
    }

    return findings;
}

ValidationSummary summarize(const std::vector<Finding>& findings) {
    ValidationSummary summary;
    for (const Finding& finding : findings) {
        switch (finding.severity) {
        case Severity::Error:
            ++summary.errors;
            break;
        case Severity::Warning:
            ++summary.warnings;
            break;
        case Severity::Info:
            ++summary.infos;
            break;
        }
    }
    return summary;
}

std::string formatReport(const HelpDatabase& db, const AppSurface& surface,
                         const std::vector<Finding>& findings) {
    const ValidationSummary summary = summarize(findings);
    std::string out;

    out += "AV Gen Help -- documentation validation (spec 35)\n";
    out += "================================================\n\n";
    out += fmt::format("content:  {} topic(s) in {} categor(y/ies){}\n", db.documents().size(),
                       db.categories().size(),
                       db.report().loadedFrom.empty() ? std::string()
                                                      : fmt::format(", loaded from {}",
                                                                    db.report().loadedFrom.string()));
    out += fmt::format("metadata: {} feature(s), {} shortcut(s)\n", db.featureTable().features().size(),
                       db.featureTable().shortcuts().size());
    out += fmt::format("source:   {} file(s) read, {} missing; found {} panel(s), {} command(s), "
                       "{} key binding(s)\n",
                       surface.coverage.filesRead, surface.coverage.filesMissing, surface.panels.size(),
                       surface.commands.size(), surface.shortcuts.size());
    if (!db.report().errors.empty()) {
        out += fmt::format("loading:  {} file(s) failed to parse\n", db.report().errors.size());
        for (const std::string& error : db.report().errors) {
            out += "          " + error + "\n";
        }
    }
    out += fmt::format("findings: {} error(s), {} warning(s), {} note(s)\n\n", summary.errors,
                       summary.warnings, summary.infos);

    const auto section = [&out, &findings](Severity severity, std::string_view heading, std::string_view blurb) {
        std::vector<const Finding*> matching;
        for (const Finding& finding : findings) {
            if (finding.severity == severity) {
                matching.push_back(&finding);
            }
        }
        if (matching.empty()) {
            return;
        }
        out += std::string(heading) + "\n";
        out += std::string(heading.size(), '-') + "\n";
        if (!blurb.empty()) {
            out += std::string(blurb) + "\n";
        }
        // Grouped by code, so a class of finding reads as one item rather than forty.
        std::vector<std::string> codes;
        for (const Finding* finding : matching) {
            if (std::ranges::find(codes, finding->code) == codes.end()) {
                codes.push_back(finding->code);
            }
        }
        for (const std::string& code : codes) {
            out += fmt::format("\n  [{}]\n", code);
            for (const Finding* finding : matching) {
                if (finding->code == code) {
                    out += fmt::format("    {} -- {}\n", finding->subject, finding->message);
                }
            }
        }
        out += "\n";
    };

    section(Severity::Error, "Errors: Help is saying something untrue",
            "Every one of these is documentation referring to something the application does not have.");
    section(Severity::Warning, "Warnings: the application has things Help does not cover",
            "These are the coverage gaps a later pass should close. A new one appearing here is the "
            "point of this tool.");
    section(Severity::Info, "Notes: the gap register and reachability",
            "Topics that declare themselves incomplete, and topics reachable only through their "
            "category and search.");

    if (findings.empty()) {
        out += "No findings.\n";
    }
    return out;
}

} // namespace avgen::help
