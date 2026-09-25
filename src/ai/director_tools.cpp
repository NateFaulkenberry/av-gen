#include "ai/director_tools.hpp"

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "directing/resolver.hpp"
#include "directing/validator.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace avgen::ai {
namespace {

using json = nlohmann::json;

ToolAnnotations inspect() {
    ToolAnnotations a;
    a.readOnly = true;
    a.idempotent = true;
    return a;
}

void add(ToolRegistry& r, std::string name, std::string title, std::string description, json inputSchema,
         ToolAnnotations annotations, ToolFn fn) {
    Tool tool;
    tool.definition.name = std::move(name);
    tool.definition.title = std::move(title);
    tool.definition.description = std::move(description);
    tool.definition.inputSchema = std::move(inputSchema);
    tool.definition.annotations = annotations;
    tool.execute = std::move(fn);
    r.add(std::move(tool));
}

// The plan argument is an OPEN object: the Director Plan's own parser checks it, and reports far
// better than the tool layer's small schema subset can (a field it does not know is a warning that
// suggests the real one; see director.plan_schema). `schema::object` would close it.
json planArgument() {
    return json{{"type", "object"}, {"description", "A Director Plan (schemaVersion 1); see director.plan_schema"}};
}

json issuesJson(const std::vector<directing::Issue>& issues) {
    json out = json::array();
    for (const directing::Issue& i : issues) {
        out.push_back(i.toJson());
    }
    return out;
}

json identityJson(const directing::SubjectIdentity& i) {
    json j{{"kind", directing::subjectKindName(i.kind)}, {"id", i.id}};
    if (i.name != i.id) {
        j["name"] = i.name;
    }
    if (!i.node.empty() && i.node != i.id) {
        j["node"] = i.node;
    }
    if (i.hero) {
        j["hero"] = true;
    }
    return j;
}

// Parses a plan argument; on failure returns the issues as an ordinary (successful) result, because
// an invalid plan is information for the model to act on, not a broken tool call.
std::optional<directing::Plan> readPlan(const json& args, json& report) {
    directing::PlanParse parsed = directing::parsePlan(args.value("plan", json::object()));
    report["issues"] = issuesJson(parsed.issues);
    if (!parsed.plan) {
        report["accepted"] = false;
        return std::nullopt;
    }
    return std::move(*parsed.plan);
}

// ADR-767: a plan that places items on events and carries no observation of its own is given the
// one this task's watch returned -- so the model writes {"event": "abduction/beam"} and never copies
// times, or an observation, by hand.
bool usesEvents(const json& j) {
    if (j.is_object()) {
        if (j.contains("event") && j["event"].is_string()) {
            return true;
        }
        for (const auto& [k, v] : j.items()) {
            if (usesEvents(v)) {
                return true;
            }
        }
    } else if (j.is_array()) {
        for (const json& v : j) {
            if (usesEvents(v)) {
                return true;
            }
        }
    }
    return false;
}

void attachObservation(directing::Plan& plan, const ToolContext& ctx) {
    if (plan.observation || !ctx.observation().is_object() || !usesEvents(plan.toJson())) {
        return;
    }
    std::vector<directing::ObservedEvent> events;
    for (const json& e : ctx.observation().value("events", json::array())) {
        events.push_back(directing::ObservedEvent{e.value("name", std::string()), e.value("subject", std::string()),
                                                  e.value("seconds", 0.0), e.value("end", 0.0)});
    }
    plan.observation = std::make_pair(std::move(events), ctx.observation().value("until", 0.0));
}

json diffJson(const directing::Compilation& c) {
    json out = json::array();
    for (const directing::DiffLine& d : c.diff) {
        out.push_back(fmt::format("{} {}", d.sign, d.text));
    }
    return out;
}

} // namespace

void registerDirectorTools(ToolRegistry& registry) {
    add(registry, "director.inspect_scene", "Inspect the scene for directing",
        "What a plan can name and when things happen: every subject (characters, heroes, nodes, cameras, "
        "effects) by kind and id, the song's sections as they come round (\"chorus 2\" is the second "
        "time the chorus comes back), the piece's length, and the plans this project already holds "
        "(revise one by reusing its id). Prefer this over parameter-level inspection when directing.",
        schema::object({{"kinds", schema::array(schema::string("entity|hero|node|camera|effect"),
                                                "Only these subject kinds; omit for all")}}),
        inspect(), [](const json& args, ToolContext& ctx) -> ToolResult {
            const directing::SceneFacts facts = app::sceneFactsFor(ctx.engine());
            const std::vector<std::string> kinds = args.value("kinds", std::vector<std::string>{});
            json subjects = json::array();
            for (const directing::SubjectIdentity& i : facts.subjects.identities()) {
                const std::string kind = directing::subjectKindName(i.kind);
                if (kinds.empty() || std::find(kinds.begin(), kinds.end(), kind) != kinds.end()) {
                    subjects.push_back(identityJson(i));
                }
            }
            json sections = json::array();
            for (const directing::SectionRun& s : facts.music.sections) {
                sections.push_back({{"type", s.type}, {"occurrence", s.occurrence}, {"start", s.startSeconds},
                                    {"end", s.endSeconds}});
            }
            json plans = json::array();
            for (const directing::Plan& p : facts.plans) {
                plans.push_back({{"id", p.id}, {"revision", p.revision}, {"title", p.title}, {"produced", p.produced.size()}});
            }
            return ToolResult::ok(
                {{"subjects", std::move(subjects)},
                 {"sections", std::move(sections)},
                 {"sectionSource", facts.music.sectionSource},
                 {"durationSeconds", facts.music.durationSeconds},
                 {"tempoBpm", facts.music.tempoBpm},
                 {"plans", std::move(plans)},
                 {"cameraMoves", directing::cameraMoveNames()}},
                fmt::format("{} subject(s), {} section(s), {} plan(s)", facts.subjects.identities().size(),
                            facts.music.sections.size(), facts.plans.size()));
        });

    add(registry, "director.inspect_subject", "Inspect a subject",
        "Resolve a name as a person said it (\"Rook\", \"the Umbra hero mushroom\", \"the Valley Wide "
        "camera\") to one subject, or learn that it is ambiguous (with every candidate) or unknown (with "
        "the nearest names). Never guess between candidates: choose one only by writing its kind and id "
        "into the plan's subject. For a character, also returns what it can do.",
        schema::object({{"name", schema::string("The name as said")},
                        {"kind", schema::string("Optional hint", {"entity", "hero", "node", "camera", "effect", "parameter", "world"})}},
                       {"name"}),
        inspect(), [](const json& args, ToolContext& ctx) -> ToolResult {
            const directing::SceneFacts facts = app::sceneFactsFor(ctx.engine());
            const auto hint = directing::subjectKindFromName(args.value("kind", std::string{}))
                                  .value_or(directing::SubjectKind::Unresolved);
            const directing::SubjectResult r = directing::resolveSubject(facts.subjects, args.at("name").get<std::string>(), hint);
            json out;
            out["status"] = r.status == directing::SubjectResult::Status::Resolved    ? "resolved"
                            : r.status == directing::SubjectResult::Status::Ambiguous ? "ambiguous"
                            : r.status == directing::SubjectResult::Status::Unknown   ? "unknown"
                                                                                       : "unsupported";
            if (r.status == directing::SubjectResult::Status::Resolved) {
                out["subject"] = identityJson(r.identity);
                if (const directing::CharacterCard* card = facts.capabilities.character(r.identity.id)) {
                    out["capabilities"] = card->toJson();
                }
                if (const directing::Place* place = facts.place(r.identity.id)) {
                    out["place"] = {{"position", {place->position.x, place->position.y, place->position.z}},
                                    {"radius", place->radius}, {"height", place->height}};
                }
                json effects = json::array();
                for (const directing::EffectInstanceCapability& e : facts.capabilities.effects().instances) {
                    if (e.owner == (r.identity.node.empty() ? r.identity.id : r.identity.node)) {
                        effects.push_back({{"id", e.id}, {"type", e.type}, {"activation", e.activation}});
                    }
                }
                out["effects"] = std::move(effects);
            } else {
                json candidates = json::array();
                for (const directing::SubjectIdentity& c : r.candidates) {
                    candidates.push_back(identityJson(c));
                }
                out["candidates"] = std::move(candidates);
            }
            if (r.issue) {
                out["issue"] = r.issue->toJson();
            }
            return ToolResult::ok(out, fmt::format("'{}': {}", args.at("name").get<std::string>(), out["status"].get<std::string>()));
        });

    add(registry, "director.inspect_capabilities", "Inspect capabilities",
        "The capability registry, generated from the scene: each character's activities (semantic names; "
        "whether each exists on its loaded rig and how long it plays), locomotion speeds and jump "
        "envelope; the camera vocabulary; event kinds and whether each renders deterministically; effect "
        "types and instances. Ask before planning an action -- a capability not listed does not exist.",
        schema::object({{"subject", schema::string("A character's id; omit for everything")}}),
        inspect(), [](const json& args, ToolContext& ctx) -> ToolResult {
            const directing::SceneFacts facts = app::sceneFactsFor(ctx.engine());
            const std::string subject = args.value("subject", std::string{});
            if (subject.empty()) {
                return ToolResult::ok(facts.capabilities.toJson(), "the whole registry");
            }
            const directing::CharacterCard* card = facts.capabilities.character(subject);
            if (card == nullptr) {
                return ToolResult::failure(ToolErrorCode::NotFound, fmt::format("no character '{}'", subject),
                                           "director.inspect_subject resolves a name to an id");
            }
            return ToolResult::ok(card->toJson(), fmt::format("{}'s capabilities", subject));
        });

    add(registry, "director.resolve_time", "Resolve a musical time",
        "Place a time as a person said it -- \"1:30\", \"bar 64 beat 3\", \"the second chorus\", \"end of "
        "the bridge + 2s\" -- in seconds, against this song's sections and beat grid. Use it to check; in "
        "a plan, write the time as said and let the engine place it.",
        schema::object({{"text", schema::string("The time as said")}}, {"text"}), inspect(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            std::vector<directing::Issue> issues;
            const auto ref = directing::parseTime(args.at("text").get<std::string>(), issues);
            json out;
            if (ref) {
                const directing::TimeResolution r = directing::resolveTime(*ref, app::musicalContextFor(ctx.engine()));
                issues.insert(issues.end(), r.issues.begin(), r.issues.end());
                out["time"] = ref->toJson();
                if (r.seconds) {
                    out["seconds"] = *r.seconds;
                }
                if (!r.explanation.empty()) {
                    out["explanation"] = r.explanation;
                }
            }
            out["issues"] = issuesJson(issues);
            return ToolResult::ok(out, out.contains("seconds") ? fmt::format("{:.3f}s", out["seconds"].get<double>())
                                                               : std::string("not placed"));
        });

    add(registry, "director.plan_schema", "The Director Plan contract",
        "The fields and vocabularies of a Director Plan, generated from what the engine accepts.", schema::object(json::object()),
        inspect(), [](const json&, ToolContext&) -> ToolResult {
            return ToolResult::ok(directing::planSchema(), "Director Plan schema");
        });

    add(registry, "director.validate_plan", "Validate a Director Plan",
        "Check a plan against the scene without changing anything: names, times, capabilities, "
        "clearances, overlaps, camera precedence, determinism. Every finding names the plan item it "
        "concerns; an error means that item (and what depends on it) would not be built.",
        schema::object({{"plan", planArgument()}}, {"plan"}), inspect(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            json out;
            std::optional<directing::Plan> plan = readPlan(args, out);
            if (!plan) {
                return ToolResult::ok(out, "the plan is not well formed");
            }
            attachObservation(*plan, ctx);
            const directing::Validation v = directing::validatePlan(*plan, app::sceneFactsFor(ctx.engine()));
            out["accepted"] = true;
            out["issues"] = issuesJson(v.issues);
            out["blocked"] = v.blocked;
            out["subjects"] = plan->toJson()["subjects"];
            return ToolResult::ok(out, fmt::format("{} finding(s), {} item(s) blocked", v.issues.size(), v.blocked.size()));
        });

    ToolAnnotations proposes = inspect();
    proposes.idempotent = false;
    proposes.requiresApproval = true;
    proposes.deterministic = true;

    // ADR-765: the assistant may ASK for a recording; the host does it, and the person approves the
    // result. Nothing is applied by this tool.
    ToolAnnotations records = proposes;
    records.deterministic = false; // what the characters do while recorded is theirs
    add(registry, "director.record_plan", "Record a plan's live performances",
        "Bake a plan whose performances are live (mode goal): play the project from zero on a scratch "
        "copy, keep what each character actually did and when its events happened, check the recording "
        "plays back and scrubs exactly, and put the RECORDED plan in front of the person for approval. "
        "Nothing is applied until they approve. Takes seconds; use it when the person wants a live "
        "performance fixed, or wants cues on its events (which only a recording can time).",
        schema::object({{"plan", planArgument()}}, {"plan"}), records,
        [](const json& args, ToolContext& ctx) -> ToolResult {
            json out;
            std::optional<directing::Plan> plan = readPlan(args, out);
            if (!plan) {
                return ToolResult::ok(out, "the plan is not well formed; nothing recorded");
            }
            attachObservation(*plan, ctx);
            if (!ctx.recordingHook()) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "recording is not available in this session (the host installed no recorder)");
            }
            const directing::Compilation c = directing::compilePlan(*plan, app::sceneFactsFor(ctx.engine()));
            const bool live = std::any_of(c.plan.performances.begin(), c.plan.performances.end(),
                                          [&](const directing::PlanPerformance& p) {
                                              return p.mode != directing::PerformanceMode::Scripted && !p.recording &&
                                                     !c.validation.isBlocked(p.key);
                                          });
            if (!live) {
                out["issues"] = issuesJson(c.validation.issues);
                return ToolResult::ok(out, "nothing live to record: every performance is scripted, recorded or blocked");
            }
            auto handle = ctx.recordingHook()(ctx.engine(), c);
            if (!handle) {
                return ToolResult::failure(ToolErrorCode::Internal, "the recording did not start: " + handle.error().message);
            }
            ctx.deferProposal(std::move(*handle));
            out["recording"] = true;
            return ToolResult::ok(out, "recording started");
        });

    // ADR-767: what happens in the film on its own -- the characters' completions, the scenarios'
    // beats -- found by watching it, so a plan can put things ON them: {"event": "abduction/beam"}.
    add(registry, "director.watch_events", "Watch the film for what happens in it",
        "Play the project from zero on a scratch copy (audio off, nothing changed) and list every world "
        "event it raises -- a scenario's beat, a character's named completion, a goal's arrival -- with "
        "its time and who raised it. Then propose a plan whose times are {\"event\": name, \"subject\": "
        "who, \"occurrence\": n}: the observation is attached to it for you. Takes seconds.",
        schema::object({{"untilSeconds", {{"type", "number"}, {"description", "how far to watch; default: the whole piece, at most 240 s"}}}},
                       {}),
        inspect(), [](const json& args, ToolContext& ctx) -> ToolResult {
            if (!ctx.watchHook()) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "watching is not available in this session (the host installed no watcher)");
            }
            const double duration = app::musicalContextFor(ctx.engine()).durationSeconds;
            double until = args.value("untilSeconds", duration > 0.0 ? duration : 120.0);
            until = std::clamp(until, 1.0, 240.0);
            auto handle = ctx.watchHook()(ctx.engine(), until);
            if (!handle) {
                return ToolResult::failure(ToolErrorCode::Internal, "the watch did not start: " + handle.error().message);
            }
            ctx.deferResult(std::move(*handle));
            return ToolResult::ok(json{{"watching", true}, {"untilSeconds", until}}, "watching");
        });
    add(registry, "director.propose_plan", "Propose a Director Plan",
        "Compile a plan against the scene WITHOUT changing it, and put the result in front of the person: "
        "what will be added or replaced, and every finding. Nothing is applied until they approve; the "
        "task then ends waiting for them. Whatever cannot be built is left out and said, never "
        "substituted -- if you want a substitute (a jump for a backflip), propose it as a change and "
        "say so. Reuse an existing plan's id to revise it.",
        schema::object({{"plan", planArgument()}}, {"plan"}), proposes,
        [](const json& args, ToolContext& ctx) -> ToolResult {
            json out;
            std::optional<directing::Plan> plan = readPlan(args, out);
            if (!plan) {
                return ToolResult::ok(out, "the plan is not well formed; nothing proposed");
            }
            attachObservation(*plan, ctx);
            const directing::Compilation c = directing::compilePlan(*plan, app::sceneFactsFor(ctx.engine()));
            out["accepted"] = true;
            out["planId"] = c.plan.id;
            out["revision"] = c.plan.revision;
            out["diff"] = diffJson(c);
            out["issues"] = issuesJson(c.validation.issues);
            out["blocked"] = c.validation.blocked;
            out["proposed"] = c.changesAnything();
            if (!c.changesAnything()) {
                return ToolResult::ok(out, "nothing in this plan can be built; nothing proposed");
            }
            json document = args.at("plan");
            if (plan->observation && !document.contains("observation")) {
                document["observation"] = plan->toJson()["observation"]; // what its times were placed by
            }
            ctx.propose(ToolContext::Proposal{std::move(document), c.plan.id, c.diffText(), out["issues"]});
            return ToolResult::ok(out, fmt::format("proposed '{}' ({} change(s)); awaiting approval", c.plan.id,
                                                   std::count_if(c.diff.begin(), c.diff.end(),
                                                                 [](const directing::DiffLine& d) { return d.sign != '!'; })));
        });
}

Result<ToolContext::Proposal> proposalFor(app::Engine& engine, const directing::Plan& plan) {
    directing::Plan clean = plan;
    clean.produced.clear(); // provenance is the compiler's to write, on the project it installs into
    const directing::Compilation c = directing::compilePlan(clean, app::sceneFactsFor(engine));
    if (!c.changesAnything()) {
        return fail("nothing in this plan can be built");
    }
    json issues = issuesJson(c.validation.issues);
    return ToolContext::Proposal{clean.toJson(), c.plan.id, c.diffText(), std::move(issues)};
}

Result<CommitReport> commitProposal(app::Engine& engine, const ToolContext::Proposal& proposal) {
    directing::PlanParse parsed = directing::parsePlan(proposal.plan);
    if (!parsed.plan) {
        return fail("the proposed plan no longer parses");
    }
    const directing::Compilation c = directing::compilePlan(*parsed.plan, app::sceneFactsFor(engine));
    if (c.diffText() != proposal.diff) {
        return fail("the project changed after this plan was proposed, so it would no longer do what was shown; "
                    "propose it again");
    }
    if (!c.changesAnything()) {
        return fail("nothing in this plan can be built");
    }
    if (auto r = app::installCompilation(engine, c); !r) {
        return std::unexpected(r.error());
    }
    if (const std::vector<std::string> problems = app::verifyInstalled(engine, c.plan.id); !problems.empty()) {
        return fail("installed content does not match the plan: {}", problems.front());
    }
    return CommitReport{c.plan.id, c.plan.revision, c.plan.produced.size()};
}

} // namespace avgen::ai
