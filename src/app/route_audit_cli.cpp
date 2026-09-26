#include "app/route_audit_cli.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"

#include <fmt/format.h>

#include <cstdio>
#include <fstream>

namespace avgen::app {

Result<nlohmann::json> auditProjectFile(const std::filesystem::path& project, double fps) {
    Engine engine(EngineMode::Offline);
    engine.setLiveControl(false); // an audit is a function of the file, not of the OSC port
    if (auto r = engine.loadProject(project); !r) {
        return std::unexpected(r.error());
    }
    if (fps > 0.0) {
        engine.renderSettings().fps = fps;
    }
    const scene::RouteAudit audit = engine.auditRoutes();
    return scene::routeAuditToJson(audit, engine.livenessInputs(), project.string());
}

int runRouteAuditCommand(const std::filesystem::path& project, const std::filesystem::path& out, double fps) {
    if (project.empty()) {
        log::error("--audit-routes needs a project: pass --project <file.json>");
        return 2;
    }
    auto report = auditProjectFile(project, fps);
    if (!report) {
        log::error("--audit-routes: {}", report.error().message);
        return 3;
    }
    const std::string text = report->dump(2);
    if (out == "-") {
        std::fwrite(text.data(), 1, text.size(), stdout);
        std::fputc('\n', stdout);
    } else {
        std::ofstream file(out);
        file << text << '\n';
        if (!file) {
            log::error("--audit-routes: cannot write '{}'", out.string());
            return 4;
        }
    }
    // The summary goes to stderr when the report itself is on stdout, so the JSON stays parseable.
    std::FILE* sink = out == "-" ? stderr : stdout;
    const nlohmann::json& s = (*report)["summary"];
    for (const char* list : {"routes", "tracks", "effectDefaultRoutes", "effects"}) {
        const nlohmann::json& l = s[list];
        std::fprintf(sink, "%-20s %3d total  %3d live  %3d dead  %3d hazard\n", list, l.value("total", 0),
                     l.value("live", 0), l.value("dead", 0), l.value("hazard", 0));
    }
    for (const auto& [rule, count] : s["findingsByRule"].items()) {
        std::fprintf(sink, "  %-26s %d\n", rule.c_str(), count.get<int>());
    }
    if (out != "-") {
        std::fprintf(sink, "report: %s\n", out.string().c_str());
    }
    return 0;
}

} // namespace avgen::app
