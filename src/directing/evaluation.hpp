#pragma once

// What an evaluator said about a plan revision, kept beside the plans (ADR-931).
//
// The Director's loop the owner asked for (brief §15) is: revise, render representative shots,
// evaluate, find concrete weaknesses, revise, render again, compare, repeat. The evaluator is the
// Creative Critic, an independent local program that reads a render plus a generic scene and intent
// and returns findings with evidence (its `critic.report/1`). This file is the engine's side of that
// conversation, as plain data -- nothing here renders, spawns a process or reads the engine:
//
//   * `EvaluationReport` -- one evaluation of one plan revision (or candidate): the evaluator's
//     headline, dimension scores, measurements, and its findings, each **keyed by the film-time span
//     it is about and by the plan items that span overlaps**, so "the second abduction's lift reads
//     as the first's" points at the item a revision should change.
//   * `reportFromCritic` -- a `critic.report/1` document into that shape.
//   * `compareEvaluations` -- two reports of one plan, diffed: dimensions up and down, findings
//     resolved, new and persisting (by the evaluator's stable keys), and per item.
//
// Reports are stored in the project under `directingEvaluations` (`Engine::directingEvaluations`),
// in the order they were made. They are records, not content: no approval gate stands in front of
// one and no undo takes one back, for the reason the Critic keeps its own session history -- an
// iteration that could not be remembered could not be compared.

#include "directing/plan.hpp"
#include "directing/resolver.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace avgen::directing {

struct SceneFacts;

struct EvaluationFinding {
    std::string id;        // the evaluator's, "F007"
    std::string key;       // stable across runs, "competing_elements:s01": what a comparison matches on
    std::string rule;
    std::string dimension;
    std::string severity;  // critical | high | medium | low | info
    std::string kind;      // issue | strength | observation
    std::string title;
    std::string message;
    std::string shot;      // the evaluator's shot id, when it was about one
    double start = 0.0;    // film seconds
    double end = 0.0;
    double confidence = 0.0;
    std::vector<std::string> items;           // plan item keys whose span overlaps [start, end]
    std::vector<std::string> recommendations; // the evaluator's actions, as it wrote them
    friend bool operator==(const EvaluationFinding&, const EvaluationFinding&) = default;
};

struct EvaluationReport {
    std::string planId;
    int revision = 0;      // the revision evaluated; a candidate's is the one it would become
    std::string candidate; // fingerprint of the plan document evaluated (two candidates of one revision differ)
    std::string label;     // what the caller called this iteration
    double from = 0.0;     // the film-time span rendered and evaluated
    double until = 0.0;
    std::string mode;      // fast | preview | deep
    std::string evaluator; // "creative-critic", or a stub's name
    std::string jobId;     // the evaluator's job, when it has one
    bool partial = false;  // a core check did not run (the Critic's PARTIAL): never mistaken for a full one
    std::string headline;
    std::vector<std::pair<std::string, double>> dimensions; // scored dimensions only, name -> 0..1
    nlohmann::json metrics = nlohmann::json::object();     // counts and measurements, as the evaluator gave them
    std::vector<EvaluationFinding> findings;
    std::string reportPath; // the evaluator's full report on disk, when there is one

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static std::optional<EvaluationReport> fromJson(const nlohmann::json& j);
    [[nodiscard]] const EvaluationFinding* finding(std::string_view key) const;
    friend bool operator==(const EvaluationReport&, const EvaluationReport&) = default;
};

// Every plan item's film-time span, from its placed times: a shot from its start for its duration, a
// set piece from when its craft is taken until it is let go, a marker or a cue at its time (a cue
// until its end when it has one), a performance across its beats. What a finding is attributed by.
struct ItemSpan {
    std::string item;
    double start = 0.0;
    double end = 0.0;
};
[[nodiscard]] std::vector<ItemSpan> planItemSpans(const Plan& plan, const PlanTimes& times, const SceneFacts& facts);

// Sets each finding's `items` to the plan items whose span overlaps it. A finding with no span (a
// whole-film judgement) is attributed to nothing rather than to everything.
void attributeFindings(EvaluationReport& report, const std::vector<ItemSpan>& spans);

// A `critic.report/1` document -- and, when given, the `critic submit --json` line that pointed at it
// -- into a report: headline, completeness, scored dimensions, counts, findings (issues first, as the
// evaluator ordered them; strengths and observations after). The plan fields are the caller's.
[[nodiscard]] EvaluationReport reportFromCritic(const nlohmann::json& report, const nlohmann::json& submitted = {});

struct EvaluationComparison {
    std::string planId;
    std::string a; // "revision 2" / the label
    std::string b;
    struct Dimension {
        std::string name;
        std::optional<double> a;
        std::optional<double> b;
        std::string verdict; // improved | degraded | unchanged | not comparable
    };
    std::vector<Dimension> dimensions;
    std::vector<std::string> resolved;   // finding keys in a and not in b
    std::vector<std::string> appeared;   // in b and not in a
    std::vector<std::string> persisting; // in both
    struct Item {
        std::string item;
        int a = 0; // issues attributed to it in each
        int b = 0;
    };
    std::vector<Item> items;
    std::string summary;
    [[nodiscard]] nlohmann::json toJson() const;
};
// Two evaluations, `a` the baseline and `b` the candidate. Dimensions within +-0.02 are unchanged (the
// Critic's own threshold). Only issues are counted per item; strengths are not problems to resolve.
[[nodiscard]] EvaluationComparison compareEvaluations(const EvaluationReport& a, const EvaluationReport& b);

} // namespace avgen::directing
