#include "app/reactivity_cli.hpp"

#include "analysis/span_profile.hpp"
#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "core/log.hpp"
#include "directing/compiler.hpp"
#include "directing/reactivity.hpp"
#include "directing/reactivity_proposer.hpp"
#include "params/serialization.hpp"
#include "scene/route_liveness.hpp"

#include <fmt/format.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <set>

namespace avgen::app {
namespace {

using json = nlohmann::json;

json issuesFor(const directing::Validation& v, const std::string& key) {
    json out = json::array();
    for (const directing::Issue& issue : v.issues) {
        if (issue.item == key) {
            out.push_back(issue.toJson());
        }
    }
    return out;
}

} // namespace

Result<json> proposeReactivityForProject(const std::filesystem::path& project, double fps) {
    Engine engine(EngineMode::Offline);
    engine.setLiveControl(false); // a proposal is a function of the file, not of the OSC port
    if (auto r = engine.loadProject(project); !r) {
        return std::unexpected(r.error());
    }
    if (fps > 0.0) {
        engine.renderSettings().fps = fps;
    }

    // ---- propose, validate, compile ------------------------------------------------------------------
    directing::ReactivityProposal proposal;
    directing::Compilation compiled;
    directing::ReactiveCatalog catalog;
    std::vector<params::ModRoute> authoredBefore;
    {
        const directing::SceneFacts facts = sceneFactsFor(engine);
        catalog = facts.capabilities.reactive();
        proposal = directing::proposeReactivity(catalog, facts.music);
        compiled = directing::compilePlan(proposal.plan, facts);
        authoredBefore = facts.staged.routes;
    }

    // ---- install into this scratch engine, and audit it as --audit-routes would ----------------------
    if (auto r = installCompilation(engine, compiled); !r) {
        return fail("the proposal could not be installed in a scratch copy: {}", r.error().message);
    }
    const scene::RouteAudit audit = engine.auditRoutes();
    std::map<std::string, const scene::AuditEntry*> auditByItem;
    const auto& routes = engine.modulator().routes();
    for (const scene::AuditEntry& entry : audit.routes) {
        if (entry.index < routes.size() && !routes[entry.index].planItem.empty()) {
            auditByItem[routes[entry.index].planItem] = &entry;
        }
    }
    const directing::Plan* stored = nullptr;
    for (const directing::Plan& p : engine.directingPlans()) {
        if (p.id == compiled.plan.id) {
            stored = &p;
        }
    }

    // ---- the document ----------------------------------------------------------------------------------
    json doc;
    doc["format"] = "avgen-reactivity-proposal";
    doc["version"] = 1;
    doc["project"] = project.string();
    doc["frameRate"] = engine.renderSettings().fps > 0.0 ? engine.renderSettings().fps : 60.0;
    doc["durationSeconds"] = engine.durationSeconds();
    doc["hasAudio"] = engine.track() != nullptr && !engine.track()->empty();

    const directing::MusicalContext music = musicalContextFor(engine);
    json sectionsJson = json::array();
    for (const directing::SectionRun& s : music.sections) {
        json o{{"type", s.type}, {"start", s.startSeconds}, {"end", s.endSeconds}, {"energy", s.energy}};
        if (s.audio.measured()) {
            o["audio"] = {{"energy", s.audio.energy},
                          {"onsetRate", s.audio.onsetRate},
                          {"kickRate", s.audio.kickRate},
                          {"snareRate", s.audio.snareRate},
                          {"hatRate", s.audio.hatRate}};
        }
        sectionsJson.push_back(std::move(o));
    }
    doc["music"] = {{"tempoBpm", music.tempoBpm}, {"sections", std::move(sectionsJson)}};

    std::size_t live = 0;
    std::size_t dead = 0;
    std::size_t hazard = 0;
    json items = json::array();
    json installRoutes = json::array();
    for (const directing::PlanRoute& item : compiled.plan.routes) {
        params::ModRoute route = item.route;
        route.planItem = directing::planItemId(compiled.plan.id, item.key);
        json o{{"key", item.key},
               {"level", directing::reactiveLevelName(item.level)},
               {"group", item.group},
               {"owner", item.owner},
               {"layer", item.layer},
               {"reason", item.reason},
               {"route", params::routeToJson(route)},
               {"compiled", !compiled.validation.isBlocked(item.key)},
               {"issues", issuesFor(compiled.validation, item.key)}};
        if (const auto a = auditByItem.find(route.planItem); a != auditByItem.end()) {
            const scene::AuditEntry& entry = *a->second;
            o["verdict"] = params::liveness::verdictName(entry.verdict);
            o["verdictReason"] = scene::auditReason(entry);
            json findings = json::array();
            for (const params::liveness::Finding& f : entry.findings) {
                findings.push_back({{"rule", f.rule}, {"verdict", params::liveness::verdictName(f.verdict)}, {"reason", f.reason}});
            }
            o["findings"] = std::move(findings);
            (entry.verdict == params::liveness::Verdict::Live ? live
             : entry.verdict == params::liveness::Verdict::Dead ? dead
                                                                  : hazard) += 1;
            installRoutes.push_back(params::routeToJson(route));
        } else {
            o["verdict"] = "not installed";
        }
        items.push_back(std::move(o));
    }
    doc["items"] = std::move(items);

    json sourcesJson = json::array();
    json installSources = json::array();
    json installParameters = json::object();
    for (const directing::PlanSource& src : compiled.plan.sources) {
        json parameters = json::object();
        for (const auto& [leaf, value] : src.parameters) {
            const std::string path = "sources/" + src.name + "/" + leaf;
            if (const params::IParameter* p = engine.params().find(path)) {
                parameters[path] = params::parameterToJson(*p);
            }
        }
        const json source{{"kind", src.kind}, {"name", src.name}, {"settings", src.settings}};
        sourcesJson.push_back({{"key", src.key},
                               {"reason", src.reason},
                               {"source", source},
                               {"parameters", parameters},
                               {"compiled", !compiled.validation.isBlocked(src.key)},
                               {"issues", issuesFor(compiled.validation, src.key)}});
        if (!compiled.validation.isBlocked(src.key)) {
            installSources.push_back(source);
            installParameters.update(parameters);
        }
    }
    doc["sources"] = std::move(sourcesJson);

    json validation = json::array();
    std::size_t errors = 0;
    std::size_t warnings = 0;
    for (const directing::Issue& issue : compiled.validation.issues) {
        validation.push_back(issue.toJson());
        errors += issue.severity == directing::Severity::Error ? 1 : 0;
        warnings += issue.severity == directing::Severity::Warning ? 1 : 0;
    }
    doc["validation"] = std::move(validation);
    json diff = json::array();
    for (const directing::DiffLine& l : compiled.diff) {
        diff.push_back(fmt::format("{} {}", l.sign, l.text));
    }
    doc["diff"] = std::move(diff);

    json summary = proposal.summaryJson();
    summary["validation"] = {{"errors", errors}, {"warnings", warnings}};
    summary["audit"] = {{"live", live}, {"dead", dead}, {"hazard", hazard}};
    doc["summary"] = std::move(summary);
    doc["catalog"] = catalog.toJson();

    // Authored routes that move what the plan animates: keep them only if both are meant.
    std::set<std::string> planOwners;
    for (const directing::PlanRoute& item : compiled.plan.routes) {
        if (!compiled.validation.isBlocked(item.key)) {
            for (const std::string& o : directing::targetOwners(catalog, item.route.target)) {
                if (o != "world") {
                    planOwners.insert(o);
                }
            }
        }
    }
    json overlaps = json::array();
    for (const params::ModRoute& r : authoredBefore) {
        if (!r.planItem.empty()) {
            continue;
        }
        std::vector<std::string> owners;
        for (const std::string& o : directing::targetOwners(catalog, r.target)) {
            if (planOwners.contains(o)) {
                owners.push_back(o);
            }
        }
        if (owners.empty()) {
            continue;
        }
        const directing::ReactiveTarget* t = catalog.find(r.target);
        const bool shared = t != nullptr && t->group == directing::ReactiveGroup::MaterialEmission;
        overlaps.push_back({{"source", r.source},
                            {"target", r.target},
                            {"owners", owners},
                            {"note", shared ? fmt::format("a shared material: '{}' moves {} surfaces in lockstep, "
                                                          "which the plan's per-node lanes now answer one by one; "
                                                          "remove it, or keep it only as a common base",
                                                          r.source, t->sharedBy.size())
                                            : "the plan also routes this entity; keep both only if both are meant"}});
    }
    doc["overlaps"] = std::move(overlaps);
    doc["install"] = {{"routes", std::move(installRoutes)},
                      {"sources", std::move(installSources)},
                      {"parameters", std::move(installParameters)},
                      {"directingPlan", stored != nullptr ? stored->toJson() : compiled.plan.toJson()}};
    return doc;
}

int runProposeReactivityCommand(const std::filesystem::path& project, const std::filesystem::path& out, double fps) {
    if (project.empty()) {
        log::error("--propose-reactivity needs a project: pass --project <file.json>");
        return 2;
    }
    auto doc = proposeReactivityForProject(project, fps);
    if (!doc) {
        log::error("--propose-reactivity: {}", doc.error().message);
        return 3;
    }
    const std::string text = doc->dump(2);
    if (out == "-") {
        std::fwrite(text.data(), 1, text.size(), stdout);
        std::fputc('\n', stdout);
    } else {
        std::ofstream file(out);
        file << text << '\n';
        if (!file) {
            log::error("--propose-reactivity: cannot write '{}'", out.string());
            return 4;
        }
    }
    std::FILE* sink = out == "-" ? stderr : stdout;
    const json& s = (*doc)["summary"];
    std::fprintf(sink, "routes %d, sources %d; audit: %d live, %d dead, %d hazard; validation: %d error(s), %d warning(s)\n",
                 s.value("routes", 0), s.value("sources", 0), s["audit"].value("live", 0), s["audit"].value("dead", 0),
                 s["audit"].value("hazard", 0), s["validation"].value("errors", 0), s["validation"].value("warnings", 0));
    for (const char* list : {"byLevel", "byGroup", "bySource"}) {
        std::fprintf(sink, "  %s:", list);
        for (const auto& [name, count] : s[list].items()) {
            std::fprintf(sink, " %s %d", name.c_str(), count.get<int>());
        }
        std::fprintf(sink, "\n");
    }
    for (const auto& note : s["notes"]) {
        std::fprintf(sink, "  note: %s\n", note.get<std::string>().c_str());
    }
    if (out != "-") {
        std::fprintf(sink, "proposal: %s\n", out.string().c_str());
    }
    return 0;
}

} // namespace avgen::app
