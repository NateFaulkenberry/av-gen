#pragma once

// A Director Plan from a file, compiled into the loaded project headless (ADR-929).
//
// GV3 writes its film from Python and consumes the engine through JSON: its generator writes a plan
// (set pieces among its items), has the engine compile it into the project, saves the project, and
// renders the saved file. This is that step, shared by `avgen --plan P.json` and
// `avgen_cast_trace --plan P.json` so the two cannot compile a plan differently:
//
//   parse -> compile against the project as loaded -> install (one plan, its content and its
//   provenance, exactly as an approved proposal installs) -> verify the installed content against
//   the plan's own fingerprints -> a report a generator can read.
//
// A plan with an error in any item is still installed for the items that can be built -- that is the
// Director's rule (spec §34) -- but `blocked` names every item that was not, and the command-line
// callers exit non-zero on it: a generator that asked for four abductions and got three must hear so.

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "core/error.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "directing/setpieces.hpp"
#include "stage/setpiece.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace avgen::app {

struct PlanFileReport {
    std::string planId;
    int revision = 0;
    std::vector<directing::Issue> issues;
    std::vector<std::string> blocked;
    std::vector<std::string> diff;
    // Each set piece as compiled: its scenario, craft, where and when -- the NOMINAL times (the placed
    // moment is exact to a frame; the others follow at the authored durations; `avgen_cast_trace`
    // measures where they land).
    nlohmann::json setPieces = nlohmann::json::array();

    [[nodiscard]] nlohmann::json toJson() const {
        nlohmann::json issuesJson = nlohmann::json::array();
        for (const directing::Issue& i : issues) {
            issuesJson.push_back(i.toJson());
        }
        return {{"planId", planId},     {"revision", revision},   {"issues", std::move(issuesJson)},
                {"blocked", blocked},   {"diff", diff},           {"setPieces", setPieces}};
    }
};

// Parses and compiles `document` against `engine`'s project and installs it. Fails when the document
// is not a plan, when nothing in it can be built, or when the install does not verify; otherwise the
// report, with every finding and every blocked item.
[[nodiscard]] inline Result<PlanFileReport> applyPlanDocument(Engine& engine, const nlohmann::json& document) {
    directing::PlanParse parsed = directing::parsePlan(document);
    PlanFileReport report;
    report.issues = parsed.issues;
    if (!parsed.plan) {
        std::string why;
        for (const directing::Issue& i : parsed.issues) {
            if (i.severity == directing::Severity::Error) {
                why += fmt::format("\n  {} {}: {}", i.location, directing::issueCodeName(i.code), i.message);
            }
        }
        return fail("the plan does not parse:{}", why);
    }
    const directing::SceneFacts facts = sceneFactsFor(engine);
    const directing::Compilation compiled = directing::compilePlan(*parsed.plan, facts);
    report.planId = compiled.plan.id;
    report.revision = compiled.plan.revision;
    report.issues.insert(report.issues.end(), compiled.validation.issues.begin(), compiled.validation.issues.end());
    report.blocked.assign(compiled.validation.blocked.begin(), compiled.validation.blocked.end());
    for (const directing::DiffLine& d : compiled.diff) {
        report.diff.push_back(fmt::format("{} {}", d.sign, d.text));
    }
    for (std::size_t i = 0; i < compiled.plan.setPieces.size(); ++i) {
        const directing::PlanSetPiece& sp = compiled.plan.setPieces[i];
        std::vector<directing::Issue> ignored;
        const directing::ResolvedSetPiece r =
            directing::resolveSetPiece(compiled.plan, i, facts, compiled.validation.times, ignored);
        nlohmann::json one{{"key", sp.key},
                           {"template", sp.templateName},
                           {"craft", sp.craft},
                           {"scenario", stage::setPieceScenarioName(sp.key)},
                           {"built", !compiled.validation.isBlocked(sp.key)}};
        if (r.spec && r.timeline) {
            nlohmann::json moments = nlohmann::json::object();
            for (const auto& [name, t] : r.timeline->moments) {
                moments[name] = t;
            }
            const auto xz = [](glm::vec2 p) { return nlohmann::json::array({p.x, p.y}); };
            one["placedMoment"] = r.spec->moment.empty() ? stage::defaultSetPieceMoment(r.spec->kind) : r.spec->moment;
            one["moments"] = std::move(moments);
            one["start"] = r.timeline->start;
            one["end"] = r.timeline->end;
            one["station"] = xz(r.timeline->station);
            one["entry"] = xz(r.timeline->entry);
            one["exit"] = xz(r.timeline->exit);
            one["animals"] = stage::setPieceAnimalCount(*r.spec);
            if (!r.spec->animals.empty()) {
                one["namedAnimals"] = r.spec->animals;
            }
            if (r.spec->kind != stage::SetPieceKind::Flyby) {
                one["hoverHeight"] = stage::setPieceValue(*r.spec, "hoverHeight");
            }
        }
        if (sp.framingMetres) {
            one["framingMetres"] = *sp.framingMetres;
        }
        report.setPieces.push_back(std::move(one));
    }
    if (!compiled.changesAnything()) {
        std::string why;
        for (const directing::Issue& i : compiled.validation.issues) {
            if (i.severity == directing::Severity::Error) {
                why += fmt::format("\n  [{}] {}: {}", i.item, directing::issueCodeName(i.code), i.message);
            }
        }
        return fail("nothing in plan '{}' can be built:{}", compiled.plan.id, why);
    }
    if (auto r = installCompilation(engine, compiled); !r) {
        return fail("plan '{}' did not install: {}", compiled.plan.id, r.error().message);
    }
    if (const std::vector<std::string> problems = verifyInstalled(engine, compiled.plan.id); !problems.empty()) {
        return fail("plan '{}' installed but does not verify: {}", compiled.plan.id, problems.front());
    }
    return report;
}

[[nodiscard]] inline Result<PlanFileReport> applyPlanFile(Engine& engine, const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) {
        return fail("cannot open plan {}", file.string());
    }
    nlohmann::json document = nlohmann::json::parse(in, nullptr, false);
    if (document.is_discarded()) {
        return fail("plan {} is not JSON", file.string());
    }
    auto report = applyPlanDocument(engine, document);
    if (!report) {
        return fail("{}: {}", file.string(), report.error().message);
    }
    return report;
}

} // namespace avgen::app
