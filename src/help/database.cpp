#include "help/database.hpp"

#include "help/markdown.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace avgen::help {

struct HelpDatabase::Impl {
    std::vector<HelpDocument> documents;
    std::vector<HelpCategory> categories;
    HelpFeatureTable features;
    HelpLoadReport report;
    std::unique_ptr<ISearchBackend> backend;
    bool indexDirty = true;

    void rebuild() {
        // `documents` itself is never reordered -- `get()` and `related()` hand out pointers into
        // it, and a sort under a caller holding one is a use-after-move waiting for a second
        // consumer to exist. Ordering happens over a vector of pointers instead.
        std::vector<const HelpDocument*> sorted;
        sorted.reserve(documents.size());
        for (const HelpDocument& doc : documents) {
            sorted.push_back(&doc);
        }
        std::ranges::stable_sort(sorted, [](const HelpDocument* a, const HelpDocument* b) {
            if (a->order != b->order) {
                return a->order < b->order;
            }
            return a->title < b->title;
        });

        // Category order is the order in which each category's first topic appears once topics are
        // in `order` sequence -- so a content author shapes the navigation tree by setting `order`
        // in front matter and never by editing a list in C++. Getting Started sits at 0, the
        // reference pages in the hundreds.
        categories.clear();
        std::unordered_map<std::string, std::size_t> index;
        for (const HelpDocument* doc : sorted) {
            auto it = index.find(doc->category);
            if (it == index.end()) {
                it = index.emplace(doc->category, categories.size()).first;
                categories.push_back({doc->category, {}});
            }
            categories[it->second].documents.push_back(doc);
        }
        if (backend) {
            backend->build(documents);
        }
        indexDirty = false;
    }
};

HelpDatabase::HelpDatabase() : impl_(std::make_unique<Impl>()) {
    impl_->backend = std::make_unique<KeywordSearch>();
}
HelpDatabase::~HelpDatabase() = default;
HelpDatabase::HelpDatabase(HelpDatabase&&) noexcept = default;
HelpDatabase& HelpDatabase::operator=(HelpDatabase&&) noexcept = default;

std::vector<std::filesystem::path> HelpDatabase::searchDirs(const std::filesystem::path& executablePath) {
    std::vector<std::filesystem::path> dirs;
    if (const char* env = std::getenv("AVGEN_HELP_DIR"); env != nullptr && *env != '\0') {
        dirs.emplace_back(env);
    }
    const auto exeDir = executablePath.parent_path();
    if (!exeDir.empty()) {
        dirs.push_back(exeDir / "help");
        dirs.push_back(exeDir / ".." / "help");
        dirs.push_back(exeDir / ".." / "Resources" / "help");
    }
#ifdef AVGEN_SOURCE_DIR
    dirs.push_back(std::filesystem::path(AVGEN_SOURCE_DIR) / "docs" / "help");
#endif
    return dirs;
}

Result<HelpLoadReport> HelpDatabase::loadDirectory(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return fail("'{}' is not a directory", dir.string());
    }

    HelpLoadReport report;
    report.loadedFrom = dir;

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".md") {
            files.push_back(entry.path());
        }
    }
    if (ec) {
        return fail("cannot walk '{}': {}", dir.string(), ec.message());
    }
    // A stable order, so two runs load the same content in the same sequence and a duplicate id is
    // always reported against the same file.
    std::ranges::sort(files);

    for (const std::filesystem::path& file : files) {
        std::ifstream in(file);
        if (!in) {
            report.errors.push_back(fmt::format("cannot open '{}'", file.string()));
            continue;
        }
        std::ostringstream text;
        text << in.rdbuf();
        auto parsed = parseHelpMarkdown(text.str(), file.string());
        if (!parsed) {
            // One bad file must not take the Help system down with it: the reader of the other
            // forty topics is not helped by an empty panel, and the validator is where a malformed
            // topic is meant to be noticed.
            report.errors.push_back(parsed.error().message);
            continue;
        }
        if (auto added = addDocument(std::move(*parsed)); !added) {
            report.errors.push_back(added.error().message);
            continue;
        }
        ++report.documents;
    }

    const auto featureFile = dir / "features.json";
    if (std::filesystem::exists(featureFile, ec)) {
        if (auto loaded = impl_->features.loadFile(featureFile); !loaded) {
            report.errors.push_back(loaded.error().message);
        }
    }
    report.features = impl_->features.features().size();
    report.shortcuts = impl_->features.shortcuts().size();

    impl_->report = report;
    // Built here rather than on the first query. The lazy rebuild mutates through a const method,
    // which is fine for one thread and a data race for two -- and two is the whole point of §25:
    // the panel and the control plane share one database. Building it while loading, on the thread
    // that loaded, means every const call afterwards really is const.
    impl_->rebuild();
    return report;
}

HelpLoadReport HelpDatabase::loadFirstAvailable(std::span<const std::filesystem::path> dirs) {
    HelpLoadReport report;
    for (const std::filesystem::path& dir : dirs) {
        report.searched.push_back(dir);
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }
        auto loaded = loadDirectory(dir);
        if (!loaded) {
            report.errors.push_back(loaded.error().message);
            continue;
        }
        if (loaded->documents == 0) {
            // A directory that exists but holds no topics is not the content directory; keep
            // looking rather than declaring victory over an empty folder.
            continue;
        }
        HelpLoadReport merged = *loaded;
        merged.searched = report.searched;
        merged.errors.insert(merged.errors.begin(), report.errors.begin(), report.errors.end());
        impl_->report = merged;
        return merged;
    }
    impl_->report = report;
    return report;
}

Result<void> HelpDatabase::addDocument(HelpDocument doc) {
    if (doc.id.empty()) {
        return fail("a Help topic needs an id");
    }
    if (const auto it = std::ranges::find(impl_->documents, doc.id, &HelpDocument::id);
        it != impl_->documents.end()) {
        return fail("duplicate Help topic id '{}' (already loaded from '{}', now '{}')", doc.id,
                    it->sourceFile.empty() ? "a built-in" : it->sourceFile,
                    doc.sourceFile.empty() ? "a built-in" : doc.sourceFile);
    }
    impl_->documents.push_back(std::move(doc));
    impl_->indexDirty = true;
    return {};
}

void HelpDatabase::addFeature(HelpFeature feature) { impl_->features.add(std::move(feature)); }
void HelpDatabase::addShortcut(HelpShortcut shortcut) { impl_->features.addShortcut(std::move(shortcut)); }

void HelpDatabase::setSearchBackend(std::unique_ptr<ISearchBackend> backend) {
    impl_->backend = backend ? std::move(backend) : std::make_unique<KeywordSearch>();
    impl_->indexDirty = true;
}

std::string_view HelpDatabase::searchBackendName() const {
    return impl_->backend ? impl_->backend->name() : std::string_view{"none"};
}

std::vector<SearchResult> HelpDatabase::search(std::string_view query, const SearchOptions& options) const {
    if (impl_->indexDirty) {
        impl_->rebuild();
    }
    if (!impl_->backend) {
        return {};
    }
    return impl_->backend->search(query, options);
}

const HelpDocument* HelpDatabase::get(std::string_view documentId) const {
    const auto it = std::ranges::find(impl_->documents, documentId, &HelpDocument::id);
    return it == impl_->documents.end() ? nullptr : &*it;
}

std::vector<const HelpDocument*> HelpDatabase::related(std::string_view documentId) const {
    std::vector<const HelpDocument*> out;
    const auto push = [&out, documentId](const HelpDocument* doc) {
        if (doc == nullptr || doc->id == documentId) {
            return;
        }
        if (std::ranges::find(out, doc) == out.end()) {
            out.push_back(doc);
        }
    };
    if (const HelpDocument* doc = get(documentId); doc != nullptr) {
        for (const std::string& id : doc->related) {
            push(get(id));
        }
    }
    // The other direction too. An author who wrote "modulation is related to signals" has said
    // something true about signals as well, and making them write it twice is how one of the two
    // ends up missing.
    for (const HelpDocument& other : impl_->documents) {
        if (std::ranges::find(other.related, documentId) != other.related.end()) {
            push(&other);
        }
    }
    return out;
}

const HelpShortcut* HelpDatabase::getShortcut(std::string_view command) const {
    if (const HelpShortcut* direct = impl_->features.findShortcut(command); direct != nullptr) {
        return direct;
    }
    // §38: searching "duplicate" should give the key straight away, so a lookup by the words a
    // person would use -- the description or the keys themselves -- also resolves.
    for (const HelpShortcut& shortcut : impl_->features.shortcuts()) {
        if (shortcut.keys == command) {
            return &shortcut;
        }
    }
    return nullptr;
}

const HelpFeature* HelpDatabase::getFeature(std::string_view featureId) const {
    return impl_->features.find(featureId);
}

std::span<const HelpDocument> HelpDatabase::documents() const { return impl_->documents; }

std::span<const HelpCategory> HelpDatabase::categories() const {
    if (impl_->indexDirty) {
        impl_->rebuild();
    }
    return impl_->categories;
}

const HelpFeatureTable& HelpDatabase::featureTable() const { return impl_->features; }

const HelpLoadReport& HelpDatabase::report() const { return impl_->report; }

std::vector<const HelpDocument*> HelpDatabase::documentsForFeature(std::string_view featureId) const {
    std::vector<const HelpDocument*> out;
    for (const HelpDocument& doc : impl_->documents) {
        if (std::ranges::find(doc.features, featureId) != doc.features.end()) {
            out.push_back(&doc);
        }
    }
    return out;
}

} // namespace avgen::help
