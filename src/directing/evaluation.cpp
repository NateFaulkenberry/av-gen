#include "directing/evaluation.hpp"

#include "directing/scene_facts.hpp"
#include "directing/setpieces.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace avgen::directing {
namespace {

using json = nlohmann::json;

std::string str(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

double num(const json& j, const char* key, double fallback = 0.0) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}

json findingJson(const EvaluationFinding& f) {
    json o{{"id", f.id}, {"key", f.key}, {"severity", f.severity}, {"kind", f.kind}, {"title", f.title},
           {"start", f.start}, {"end", f.end}};
    if (!f.rule.empty()) o["rule"] = f.rule;
    if (!f.dimension.empty()) o["dimension"] = f.dimension;
    if (!f.message.empty()) o["message"] = f.message;
    if (!f.shot.empty()) o["shot"] = f.shot;
    if (f.confidence != 0.0) o["confidence"] = f.confidence;
    if (!f.items.empty()) o["items"] = f.items;
    if (!f.recommendations.empty()) o["recommendations"] = f.recommendations;
    return o;
}

EvaluationFinding findingFromJson(const json& o) {
    EvaluationFinding f;
    f.id = str(o, "id");
    f.key = str(o, "key");
    f.rule = str(o, "rule");
    f.dimension = str(o, "dimension");
    f.severity = str(o, "severity");
    f.kind = str(o, "kind");
    f.title = str(o, "title");
    f.message = str(o, "message");
    f.shot = str(o, "shot");
    f.start = num(o, "start");
    f.end = num(o, "end");
    f.confidence = num(o, "confidence");
    f.items = o.value("items", std::vector<std::string>{});
    f.recommendations = o.value("recommendations", std::vector<std::string>{});
    return f;
}

bool isIssue(const EvaluationFinding& f) { return f.kind.empty() || f.kind == "issue"; }

std::string nameOf(const EvaluationReport& r) {
    return r.label.empty() ? fmt::format("revision {}", r.revision) : fmt::format("'{}' (revision {})", r.label, r.revision);
}

} // namespace

json EvaluationReport::toJson() const {
    json j{{"planId", planId}, {"revision", revision}, {"from", from}, {"until", until}, {"mode", mode},
           {"evaluator", evaluator}, {"headline", headline}};
    if (!candidate.empty()) j["candidate"] = candidate;
    if (!label.empty()) j["label"] = label;
    if (!jobId.empty()) j["jobId"] = jobId;
    if (partial) j["partial"] = true;
    json dims = json::object();
    for (const auto& [name, score] : dimensions) {
        dims[name] = score;
    }
    j["dimensions"] = std::move(dims);
    if (!metrics.empty()) j["metrics"] = metrics;
    json list = json::array();
    for (const EvaluationFinding& f : findings) {
        list.push_back(findingJson(f));
    }
    j["findings"] = std::move(list);
    if (!reportPath.empty()) j["reportPath"] = reportPath;
    return j;
}

std::optional<EvaluationReport> EvaluationReport::fromJson(const json& j) {
    if (!j.is_object() || !j.contains("planId") || !j["planId"].is_string()) {
        return std::nullopt;
    }
    EvaluationReport r;
    r.planId = str(j, "planId");
    r.revision = static_cast<int>(num(j, "revision"));
    r.candidate = str(j, "candidate");
    r.label = str(j, "label");
    r.from = num(j, "from");
    r.until = num(j, "until");
    r.mode = str(j, "mode");
    r.evaluator = str(j, "evaluator");
    r.jobId = str(j, "jobId");
    r.partial = j.value("partial", false);
    r.headline = str(j, "headline");
    if (const auto d = j.find("dimensions"); d != j.end() && d->is_object()) {
        for (const auto& [name, score] : d->items()) {
            if (score.is_number()) {
                r.dimensions.emplace_back(name, score.get<double>());
            }
        }
    }
    r.metrics = j.value("metrics", json::object());
    if (const auto f = j.find("findings"); f != j.end() && f->is_array()) {
        for (const json& o : *f) {
            if (o.is_object()) {
                r.findings.push_back(findingFromJson(o));
            }
        }
    }
    r.reportPath = str(j, "reportPath");
    return r;
}

const EvaluationFinding* EvaluationReport::finding(std::string_view key) const {
    for (const EvaluationFinding& f : findings) {
        if (f.key == key) {
            return &f;
        }
    }
    return nullptr;
}

std::vector<ItemSpan> planItemSpans(const Plan& plan, const PlanTimes& times, const SceneFacts& facts) {
    std::vector<ItemSpan> out;
    for (std::size_t i = 0; i < plan.shots.size(); ++i) {
        if (const auto t = times.at(fmt::format("/shots/{}/start", i))) {
            out.push_back({plan.shots[i].key, *t, *t + plan.shots[i].durationSeconds});
        }
    }
    for (std::size_t i = 0; i < plan.markers.size(); ++i) {
        if (const auto t = times.at(fmt::format("/markers/{}/at", i))) {
            out.push_back({plan.markers[i].key, *t, *t});
        }
    }
    for (std::size_t i = 0; i < plan.cues.size(); ++i) {
        if (const auto t = times.at(fmt::format("/cues/{}/at", i))) {
            double end = *t + plan.cues[i].rampSeconds + plan.cues[i].holdSeconds;
            if (const auto u = times.at(fmt::format("/cues/{}/until", i))) {
                end = std::max(end, *u);
            }
            out.push_back({plan.cues[i].key, *t, end});
        }
    }
    for (std::size_t i = 0; i < plan.performances.size(); ++i) {
        std::optional<double> lo;
        std::optional<double> hi;
        for (std::size_t b = 0; b < plan.performances[i].beats.size(); ++b) {
            if (const auto t = times.at(fmt::format("/performances/{}/beats/{}/at", i, b))) {
                lo = lo ? std::min(*lo, *t) : *t;
                hi = hi ? std::max(*hi, *t + plan.performances[i].beats[b].seconds.value_or(0.0)) : *t;
            }
        }
        if (lo) {
            out.push_back({plan.performances[i].key, *lo, *hi});
        }
    }
    for (std::size_t i = 0; i < plan.retimes.size(); ++i) {
        const auto a = times.at(fmt::format("/retimes/{}/from", i));
        const auto b = times.at(fmt::format("/retimes/{}/until", i));
        if (a && b) {
            out.push_back({plan.retimes[i].key, *a, *b});
        }
    }
    for (std::size_t i = 0; i < plan.setPieces.size(); ++i) {
        std::vector<Issue> ignored;
        const ResolvedSetPiece r = resolveSetPiece(plan, i, facts, times, ignored);
        if (r.timeline) {
            out.push_back({plan.setPieces[i].key, r.timeline->start, r.timeline->end});
        }
    }
    return out;
}

void attributeFindings(EvaluationReport& report, const std::vector<ItemSpan>& spans) {
    for (EvaluationFinding& f : report.findings) {
        f.items.clear();
        if (!(f.end >= f.start) || (f.start == 0.0 && f.end == 0.0)) {
            continue;
        }
        for (const ItemSpan& s : spans) {
            // Closed intervals: a finding at the instant a marker marks is about that marker.
            if (s.start <= f.end + 1e-6 && s.end >= f.start - 1e-6 &&
                std::find(f.items.begin(), f.items.end(), s.item) == f.items.end()) {
                f.items.push_back(s.item);
            }
        }
    }
}

EvaluationReport reportFromCritic(const json& report, const json& submitted) {
    EvaluationReport out;
    out.evaluator = "creative-critic";
    const json summary = report.value("summary", json::object());
    out.headline = str(summary, "headline");
    if (out.headline.empty()) {
        out.headline = str(submitted, "headline");
    }
    const json completeness = report.value("completeness", summary.value("completeness", json::object()));
    out.partial = str(completeness, "status") == "partial" || out.headline.rfind("PARTIAL", 0) == 0;
    const json job = report.value("job", json::object());
    out.jobId = str(job, "id");
    if (out.jobId.empty()) {
        out.jobId = str(submitted, "job_id");
    }
    out.mode = str(job, "mode");
    out.reportPath = str(submitted, "report_json");
    if (const auto dims = report.find("dimensions"); dims != report.end() && dims->is_object()) {
        for (const auto& [name, d] : dims->items()) {
            if (d.is_object() && d.contains("score") && d["score"].is_number() && str(d, "status") != "not_evaluated") {
                out.dimensions.emplace_back(name, d["score"].get<double>());
            }
        }
    }
    json metrics = json::object();
    if (summary.contains("counts")) {
        metrics["counts"] = summary["counts"];
    }
    if (report.contains("coverage")) {
        metrics["coverage"] = report["coverage"];
    }
    if (completeness.contains("missing")) {
        metrics["missing"] = completeness["missing"];
    }
    if (submitted.contains("total_ms")) {
        metrics["totalMs"] = submitted["total_ms"];
    }
    out.metrics = std::move(metrics);
    const auto read = [&](const char* key, const char* kind) {
        if (const auto list = report.find(key); list != report.end() && list->is_array()) {
            for (const json& o : *list) {
                if (!o.is_object()) {
                    continue;
                }
                EvaluationFinding f;
                f.id = str(o, "id");
                f.key = str(o, "key");
                f.rule = str(o, "rule");
                f.dimension = str(o, "dimension");
                f.severity = str(o, "severity");
                f.kind = str(o, "kind").empty() ? std::string(kind) : str(o, "kind");
                f.title = str(o, "title");
                f.message = str(o, "message");
                f.shot = str(o, "shot");
                f.confidence = num(o, "confidence");
                if (const auto t = o.find("time"); t != o.end() && t->is_array() && t->size() == 2 && (*t)[0].is_number() &&
                                                   (*t)[1].is_number()) {
                    f.start = (*t)[0].get<double>();
                    f.end = (*t)[1].get<double>();
                }
                if (const auto recs = o.find("recommendations"); recs != o.end() && recs->is_array()) {
                    for (const json& rec : *recs) {
                        if (rec.is_object() && rec.contains("action") && rec["action"].is_string()) {
                            f.recommendations.push_back(rec["action"].get<std::string>());
                        } else if (rec.is_string()) {
                            f.recommendations.push_back(rec.get<std::string>());
                        }
                    }
                }
                if (f.key.empty()) {
                    f.key = f.rule.empty() ? f.id : f.rule + (f.shot.empty() ? std::string() : ":" + f.shot);
                }
                out.findings.push_back(std::move(f));
            }
        }
    };
    read("findings", "issue");
    read("strengths", "strength");
    read("observations", "observation");
    return out;
}

json EvaluationComparison::toJson() const {
    json dims = json::array();
    for (const Dimension& d : dimensions) {
        json o{{"name", d.name}, {"verdict", d.verdict}};
        if (d.a) o["a"] = *d.a;
        if (d.b) o["b"] = *d.b;
        if (d.a && d.b) o["delta"] = *d.b - *d.a;
        dims.push_back(std::move(o));
    }
    json itemsJson = json::array();
    for (const Item& i : items) {
        itemsJson.push_back({{"item", i.item}, {"a", i.a}, {"b", i.b}});
    }
    return {{"planId", planId},       {"a", a},
            {"b", b},                 {"dimensions", std::move(dims)},
            {"resolved", resolved},   {"new", appeared},
            {"persisting", persisting}, {"items", std::move(itemsJson)},
            {"summary", summary}};
}

EvaluationComparison compareEvaluations(const EvaluationReport& a, const EvaluationReport& b) {
    EvaluationComparison out;
    out.planId = b.planId;
    out.a = nameOf(a);
    out.b = nameOf(b);
    std::map<std::string, EvaluationComparison::Dimension> dims;
    for (const auto& [name, score] : a.dimensions) {
        dims[name].name = name;
        dims[name].a = score;
    }
    for (const auto& [name, score] : b.dimensions) {
        dims[name].name = name;
        dims[name].b = score;
    }
    int improved = 0;
    int degraded = 0;
    for (auto& [name, d] : dims) {
        if (d.a && d.b) {
            const double delta = *d.b - *d.a;
            d.verdict = delta > 0.02 ? "improved" : delta < -0.02 ? "degraded" : "unchanged";
            improved += delta > 0.02 ? 1 : 0;
            degraded += delta < -0.02 ? 1 : 0;
        } else {
            d.verdict = "not comparable";
        }
        out.dimensions.push_back(d);
    }
    std::set<std::string> inA;
    std::set<std::string> inB;
    for (const EvaluationFinding& f : a.findings) {
        if (isIssue(f)) inA.insert(f.key);
    }
    for (const EvaluationFinding& f : b.findings) {
        if (isIssue(f)) inB.insert(f.key);
    }
    for (const std::string& k : inA) {
        (inB.contains(k) ? out.persisting : out.resolved).push_back(k);
    }
    for (const std::string& k : inB) {
        if (!inA.contains(k)) out.appeared.push_back(k);
    }
    std::map<std::string, EvaluationComparison::Item> items;
    for (const EvaluationFinding& f : a.findings) {
        if (!isIssue(f)) continue;
        for (const std::string& item : f.items) {
            items[item].item = item;
            ++items[item].a;
        }
    }
    for (const EvaluationFinding& f : b.findings) {
        if (!isIssue(f)) continue;
        for (const std::string& item : f.items) {
            items[item].item = item;
            ++items[item].b;
        }
    }
    for (auto& [key, item] : items) {
        out.items.push_back(item);
    }
    out.summary = fmt::format("{} against {}: {} dimension(s) improved, {} degraded; {} finding(s) resolved, {} new, {} "
                              "persisting",
                              out.b, out.a, improved, degraded, out.resolved.size(), out.appeared.size(),
                              out.persisting.size());
    return out;
}

} // namespace avgen::directing
