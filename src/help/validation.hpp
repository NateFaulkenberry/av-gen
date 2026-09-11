#pragma once

// Documentation validation (§35).
//
// This is the piece that makes staging the content safe. The Help system ships today describing the
// stable half of the engine and openly declining to describe the half that six concurrent passes
// are rewriting. That is only defensible if something *tells* us when the rewrite lands -- if the
// list of undocumented features is a tool's output rather than a promise in a commit message.
//
// So the validator answers two questions, and they are different:
//
//   1. Does the documentation refer to things that do not exist? A dead link, a feature id nothing
//      registers, a shortcut with no call site, a parameter path nothing declares. This is §34's
//      "Help should never lie", enforced.
//   2. Does the application have things the documentation does not cover? A panel, a menu command
//      or a key binding that no topic mentions. This is the one that fires when somebody else's
//      pass lands, and it is why the gaps are safe to leave open today.
//
// Both are findings, not failures. `Severity::Error` is for a Help system that is actively wrong;
// `Warning` for coverage; `Info` for the gap register -- topics that say, in their own front
// matter, that they are placeholders.

#include "help/app_surface.hpp"
#include "help/database.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace avgen::help {

enum class Severity { Error, Warning, Info };

[[nodiscard]] std::string_view severityName(Severity severity);

struct Finding {
    Severity severity = Severity::Warning;
    // A stable machine-readable code, so a report can be filtered and a test can assert on a class
    // of finding without matching prose.
    std::string code;
    std::string subject; // the document id, feature id, panel or command the finding is about
    std::string message; // what is wrong, in a sentence, with enough detail to act on
};

// Every check, and what it means. Codes are stable; the prose is not.
//
//   broken-link            a [link](help://x) or `related:` entry naming no topic       Error
//   duplicate-id           two topics share an id (only reachable via addDocument)      Error
//   missing-summary        a stable topic with no summary                              Error
//   empty-body             a stable topic with no body                                 Error
//   unknown-feature        a topic's `features:` names nothing in the feature table     Error
//   unknown-shortcut       a topic's `shortcuts:` names nothing in the shortcut table   Error
//   unknown-parameter      a topic cites a parameter path the application does not have Error
//   stale-shortcut         a documented shortcut with no call site in the source        Error
//   stale-feature-doc      a feature points at a topic that does not exist              Error
//   advertised-unbound     a menu advertises a key that no handler binds                Warning
//   undocumented-panel     a registered panel no topic documents                        Warning
//   undocumented-command   a menu command no topic mentions                             Warning
//   undocumented-shortcut  a key binding in the source that the shortcut table lacks    Warning
//   feature-without-doc    a feature with no documentId                                 Warning
//   orphan-topic           a stable topic nothing links to and no category lists first  Info
//   known-gap              a topic declaring itself partial or not-yet-documented       Info
//   scan-incomplete        the source scan could not read what it needed                Warning
[[nodiscard]] std::vector<Finding> validate(const HelpDatabase& db, const AppSurface& surface);

struct ValidationSummary {
    std::size_t errors = 0;
    std::size_t warnings = 0;
    std::size_t infos = 0;
    [[nodiscard]] std::size_t total() const { return errors + warnings + infos; }
};

[[nodiscard]] ValidationSummary summarize(const std::vector<Finding>& findings);

// The findings as a report a person reads: grouped by severity, one line each, with a header that
// says what was checked against what. This is what `avgen_help_lint` prints and what a failing test
// dumps, so the same words appear in both places.
[[nodiscard]] std::string formatReport(const HelpDatabase& db, const AppSurface& surface,
                                       const std::vector<Finding>& findings);

} // namespace avgen::help
