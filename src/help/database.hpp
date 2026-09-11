#pragma once

// The Help database: one knowledge layer, two consumers (§25, §39).
//
// Everything the panel draws and everything the AI control plane retrieves comes through this
// object. That is the architectural requirement, and it is worth being precise about why: the
// failure mode §25 names is an AI with its own knowledge base, which drifts away from behaviour
// silently because nothing ever compares the two. There is no second store here to drift.
//
// The five methods §26 asks for -- search, get, related, getShortcut, getFeature -- are the public
// surface, named to match. ui/help_panel.cpp calls them; help/api.hpp wraps the same five as JSON
// tools for the control plane. Neither consumer can see anything the other cannot.

#include "core/error.hpp"
#include "help/document.hpp"
#include "help/features.hpp"
#include "help/search.hpp"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::help {

// A navigation group (§3, §28): the categories, in the order the panel lists them, each with its
// topics already sorted.
struct HelpCategory {
    std::string name;
    std::vector<const HelpDocument*> documents;
};

// Where content was looked for and what was found. Reported by the panel, because the way a Help
// system fails in practice is by finding no files and saying nothing about it -- which looks
// exactly like a Help system nobody wrote any content for.
struct HelpLoadReport {
    std::vector<std::filesystem::path> searched;
    std::filesystem::path loadedFrom;
    std::size_t documents = 0;
    std::size_t features = 0;
    std::size_t shortcuts = 0;
    std::vector<std::string> errors; // one per file that failed to parse; loading continues
};

// Threading: once a load has finished, every const method here is safe to call from several
// threads at once -- the search index is built during the load rather than on the first query, so
// nothing is mutated behind a const call. The mutating methods (`loadDirectory`, `addDocument`,
// `addFeature`, `addShortcut`, `setSearchBackend`) are setup, and are not safe against a concurrent
// reader.
class HelpDatabase {
public:
    HelpDatabase();
    ~HelpDatabase();
    HelpDatabase(HelpDatabase&&) noexcept;
    HelpDatabase& operator=(HelpDatabase&&) noexcept;
    HelpDatabase(const HelpDatabase&) = delete;
    HelpDatabase& operator=(const HelpDatabase&) = delete;

    // The directories a build looks in, most specific first: $AVGEN_HELP_DIR, then beside the
    // executable, then the source tree. Same shape as app::exampleSearchDirs, deliberately: a
    // second convention for finding runtime content is a second thing to get wrong.
    [[nodiscard]] static std::vector<std::filesystem::path> searchDirs(const std::filesystem::path& executablePath);

    // Loads every *.md under `dir` as a topic and `features.json` beside them as the feature table.
    // A file that fails to parse is recorded in the report and skipped: one malformed topic must
    // not take the whole Help system down, and the validator is where a malformed topic gets
    // noticed. Returns an error only when the directory itself cannot be read.
    [[nodiscard]] Result<HelpLoadReport> loadDirectory(const std::filesystem::path& dir);

    // Tries each of `dirs` in turn and loads the first that contains any *.md. Always returns a
    // report, even when nothing was found, so the caller can say where it looked.
    HelpLoadReport loadFirstAvailable(std::span<const std::filesystem::path> dirs);

    // For tests, and for content assembled in code. Adding a topic invalidates pointers previously
    // returned by get(), related(), categories() and documentsForFeature(), the way any vector
    // does; in practice loading happens once at startup and queries follow.
    [[nodiscard]] Result<void> addDocument(HelpDocument doc);
    void addFeature(HelpFeature feature);
    void addShortcut(HelpShortcut shortcut);

    // Swap the ranking strategy (§8). Rebuilds the index. Null restores the keyword default.
    void setSearchBackend(std::unique_ptr<ISearchBackend> backend);
    [[nodiscard]] std::string_view searchBackendName() const;

    // ---- the retrieval API (§26) -------------------------------------------------------------

    [[nodiscard]] std::vector<SearchResult> search(std::string_view query, const SearchOptions& options = {}) const;
    [[nodiscard]] const HelpDocument* get(std::string_view documentId) const;
    // The topics `documentId` names in its front matter, plus the ones that name it: relatedness is
    // symmetric to a reader even when an author only wrote it in one direction. Deduplicated, and
    // never includes `documentId` itself.
    [[nodiscard]] std::vector<const HelpDocument*> related(std::string_view documentId) const;
    [[nodiscard]] const HelpShortcut* getShortcut(std::string_view command) const;
    [[nodiscard]] const HelpFeature* getFeature(std::string_view featureId) const;

    // ---- navigation --------------------------------------------------------------------------

    [[nodiscard]] std::span<const HelpDocument> documents() const;
    [[nodiscard]] std::span<const HelpCategory> categories() const;
    [[nodiscard]] const HelpFeatureTable& featureTable() const;
    [[nodiscard]] const HelpLoadReport& report() const;
    [[nodiscard]] bool empty() const { return documents().empty(); }

    // Every topic that declares `featureId` in its `features` front matter, for "Learn more" from a
    // feature that has no single canonical topic.
    [[nodiscard]] std::vector<const HelpDocument*> documentsForFeature(std::string_view featureId) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace avgen::help
