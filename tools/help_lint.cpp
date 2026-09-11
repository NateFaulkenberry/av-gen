// avgen_help_lint -- the development-time documentation validation report (help-system spec §35).
//
// Loads the Help content, scans the application's source for what it actually has, compares the
// two and prints what it finds. Exit status is 0 unless there are errors, so it can gate a commit;
// warnings and notes are information, not failure, because the gap list is a deliverable.
//
//   avgen_help_lint                          # content from the source tree, scan the source tree
//   avgen_help_lint --content docs/help      # a different content directory
//   avgen_help_lint --source /path/to/repo   # a different source tree to scan
//   avgen_help_lint --list                   # every topic, id and status, one per line
//   avgen_help_lint --search "make water look better"
//   avgen_help_lint --tools                  # the AI tool descriptors, as JSON
//   avgen_help_lint --strict                 # exit non-zero on warnings too

#include "help/api.hpp"
#include "help/app_surface.hpp"
#include "help/database.hpp"
#include "help/validation.hpp"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

using namespace avgen;

int usage(int code) {
    std::cerr << "usage: avgen_help_lint [--content <dir>] [--source <dir>] [--list] "
                 "[--search <query>] [--tools] [--strict]\n";
    return code;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path contentDir;
    std::filesystem::path sourceDir = help::configuredSourceDir();
    std::string query;
    bool list = false;
    bool printTools = false;
    bool strict = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << what << " needs a value\n";
                std::exit(usage(2));
            }
            return argv[++i];
        };
        if (arg == "--content") {
            contentDir = next("--content");
        } else if (arg == "--source") {
            sourceDir = next("--source");
        } else if (arg == "--search") {
            query = next("--search");
        } else if (arg == "--list") {
            list = true;
        } else if (arg == "--tools") {
            printTools = true;
        } else if (arg == "--strict") {
            strict = true;
        } else if (arg == "--help" || arg == "-h") {
            return usage(0);
        } else {
            std::cerr << "unknown argument '" << arg << "'\n";
            return usage(2);
        }
    }

    help::HelpDatabase db;
    if (!contentDir.empty()) {
        auto loaded = db.loadDirectory(contentDir);
        if (!loaded) {
            std::cerr << loaded.error().message << "\n";
            return 2;
        }
    } else {
        const auto dirs = help::HelpDatabase::searchDirs(std::filesystem::path(argv[0]));
        const help::HelpLoadReport report = db.loadFirstAvailable(dirs);
        if (report.documents == 0) {
            std::cerr << "no Help content found. Looked in:\n";
            for (const auto& dir : report.searched) {
                std::cerr << "  " << dir.string() << "\n";
            }
            return 2;
        }
    }

    if (printTools) {
        nlohmann::json out = nlohmann::json::array();
        for (const help::ToolDescriptor& tool : help::tools()) {
            // The non-throwing overload: a typo in a schema literal should be a message, not a
            // terminate, and this is the one place those literals are parsed.
            auto schema = nlohmann::json::parse(tool.parametersSchema, nullptr, false);
            if (schema.is_discarded()) {
                std::cerr << "tool '" << tool.name << "' has an unparseable parameter schema\n";
                return 2;
            }
            out.push_back({{"name", tool.name},
                           {"description", tool.description},
                           {"parameters", std::move(schema)},
                           {"readOnly", tool.readOnly}});
        }
        std::cout << out.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
        return 0;
    }

    if (list) {
        for (const help::HelpCategory& category : db.categories()) {
            std::cout << category.name << "\n";
            for (const help::HelpDocument* doc : category.documents) {
                std::cout << "  " << doc->id << "  [" << help::helpStatusName(doc->status) << "]  "
                          << doc->title << "\n";
            }
        }
        return 0;
    }

    if (!query.empty()) {
        const nlohmann::json result = help::search(db, {{"query", query}, {"limit", 10}});
        std::cout << result.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
        return 0;
    }

    const help::AppSurface surface = help::scanApplicationSource(sourceDir);
    const std::vector<help::Finding> findings = help::validate(db, surface);
    std::cout << help::formatReport(db, surface, findings);

    const help::ValidationSummary summary = help::summarize(findings);
    if (summary.errors > 0) {
        return 1;
    }
    return strict && summary.warnings > 0 ? 1 : 0;
}
