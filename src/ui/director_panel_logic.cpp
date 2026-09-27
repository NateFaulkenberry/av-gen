#include "ui/director_panel_logic.hpp"

#include "directing/reactivity.hpp"
#include "directing/resolver.hpp"
#include "directing/setpieces.hpp"
#include "directing/validator.hpp"
#include "params/serialization.hpp"
#include "stage/setpiece.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cmath>

namespace avgen::ui {

const char* itemMarkName(ItemMark mark) {
    switch (mark) {
    case ItemMark::Ok: return "ok";
    case ItemMark::Warning: return "warning";
    case ItemMark::Blocked: return "blocked";
    }
    return "?";
}

std::vector<PlanItemRow> planItemRows(const directing::Plan& plan, const directing::Validation& validation) {
    std::vector<PlanItemRow> rows;
    const auto add = [&](std::string key, std::string kind, std::string label) {
        rows.push_back(PlanItemRow{std::move(key), std::move(kind), std::move(label), ItemMark::Ok, {}});
    };
    for (const directing::PlanShot& s : plan.shots) {
        add(s.key, "shot", s.name);
    }
    for (const directing::PlanPerformance& p : plan.performances) {
        add(p.key, "performance", p.subject);
    }
    for (const directing::PlanMarker& m : plan.markers) {
        add(m.key, "marker", m.name);
    }
    for (const directing::PlanCue& c : plan.cues) {
        std::string label = c.parameter;
        if (label.empty() && c.effect) {
            label = c.effect->id.empty() ? c.effect->owner + " " + c.effect->type : c.effect->id;
            if (!c.field.empty()) {
                label += "." + c.field;
            }
        }
        add(c.key, "cue", label);
    }
    for (const directing::PlanRetime& r : plan.retimes) {
        add(r.key, "retime", fmt::format("{} at {:.2f}x", r.performance, r.factor));
    }
    for (const directing::PlanSource& s : plan.sources) {
        add(s.key, "source", fmt::format("{} ({})", s.signal(), s.kind));
    }
    for (const directing::PlanRoute& r : plan.routes) {
        add(r.key, "route",
            fmt::format("{} {}: {} -> {}", directing::reactiveLevelName(r.level), r.layer.empty() ? r.route.source : r.layer,
                        r.route.source, r.route.target));
    }
    for (const directing::PlanSetPiece& sp : plan.setPieces) {
        add(sp.key, "set piece",
            fmt::format("{} by {}, {} at {}", sp.templateName, sp.craft,
                        sp.moment.empty() ? std::string("its moment") : sp.moment, sp.at.describe()));
    }
    PlanItemRow planWide{"", "plan", plan.title.empty() ? plan.id : plan.title, ItemMark::Ok, {}};

    for (PlanItemRow& row : rows) {
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        std::vector<std::string> notes; // Info: said, but it marks nothing (a locked shot keeping the frame)
        for (const directing::Issue& issue : validation.issues) {
            if (issue.item != row.key) {
                continue;
            }
            (issue.severity == directing::Severity::Error     ? errors
             : issue.severity == directing::Severity::Warning ? warnings
                                                              : notes)
                .push_back(issue.message);
        }
        if (validation.isBlocked(row.key) || !errors.empty()) {
            row.mark = ItemMark::Blocked;
        } else if (!warnings.empty()) {
            row.mark = ItemMark::Warning;
        }
        row.lines = std::move(errors);
        row.lines.insert(row.lines.end(), warnings.begin(), warnings.end());
        row.lines.insert(row.lines.end(), notes.begin(), notes.end());
    }
    for (const directing::Issue& issue : validation.issues) {
        const bool owned = std::any_of(rows.begin(), rows.end(), [&](const PlanItemRow& r) { return r.key == issue.item; });
        if (owned || issue.severity == directing::Severity::Info) {
            continue;
        }
        planWide.lines.push_back(issue.message);
        if (issue.severity == directing::Severity::Error) {
            planWide.mark = ItemMark::Blocked;
        } else if (planWide.mark == ItemMark::Ok) {
            planWide.mark = ItemMark::Warning;
        }
    }
    if (!planWide.lines.empty()) {
        rows.insert(rows.begin(), std::move(planWide));
    }
    return rows;
}

std::vector<ReactivityRow> reactivityRows(const directing::Plan& plan, const std::vector<params::ModRoute>& routes) {
    std::vector<ReactivityRow> rows;
    for (const directing::ReactiveLevel level :
         {directing::ReactiveLevel::Micro, directing::ReactiveLevel::Meso, directing::ReactiveLevel::Macro}) {
        for (const directing::PlanRoute& item : plan.routes) {
            if (item.level != level) {
                continue;
            }
            ReactivityRow row;
            row.key = item.key;
            row.level = directing::reactiveLevelName(item.level);
            row.what = fmt::format("{}: {}", item.layer.empty() ? item.route.source : item.layer,
                                   directing::describeRoute(item.route));
            row.reason = item.reason;
            const std::string id = directing::planItemId(plan.id, item.key);
            const auto live = std::find_if(routes.begin(), routes.end(), [&](const params::ModRoute& r) { return r.planItem == id; });
            const auto made = std::find_if(plan.produced.begin(), plan.produced.end(), [&](const directing::ContentRef& ref) {
                return ref.domain == directing::ContentDomain::ModRoute && ref.id == id;
            });
            if (live == routes.end()) {
                row.state = "not in the project";
            } else if (made != plan.produced.end() && !made->fingerprint.empty() &&
                       directing::fingerprint(params::routeToJson(*live)) != made->fingerprint) {
                row.state = "edited by hand";
            } else {
                row.state = "as made";
            }
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

std::string reactivityHeading(const directing::Plan& plan) {
    std::size_t micro = 0;
    std::size_t meso = 0;
    std::size_t macro = 0;
    for (const directing::PlanRoute& r : plan.routes) {
        (r.level == directing::ReactiveLevel::Micro ? micro : r.level == directing::ReactiveLevel::Meso ? meso : macro) += 1;
    }
    return fmt::format("{} route{}: micro {}, meso {}, macro {}", plan.routes.size(), plan.routes.size() == 1 ? "" : "s",
                       micro, meso, macro);
}

// ---- UFO set pieces (ADR-929) -------------------------------------------------------------------

namespace {

std::string compass(float degrees) {
    static constexpr const char* kNames[] = {"north", "north-east", "east", "south-east",
                                             "south", "south-west", "west", "north-west"};
    const float d = std::fmod(std::fmod(degrees, 360.0f) + 360.0f, 360.0f);
    return fmt::format("{} ({:.0f} deg)", kNames[static_cast<int>(std::lround(d / 45.0f)) % 8], d);
}

float slotOf(const directing::PlanSetPiece& sp, stage::SetPieceKind kind, std::string_view name) {
    for (auto it = sp.set.rbegin(); it != sp.set.rend(); ++it) {
        if (it->first == name) {
            return it->second;
        }
    }
    const stage::SetPieceSlot* slot = stage::findSetPieceSlot(kind, name);
    return slot != nullptr ? slot->value : 0.0f;
}

std::string colourName(const std::array<float, 3>& c) {
    const float m = std::max({c[0], c[1], c[2]});
    if (!(m > 0.0f)) {
        return "black";
    }
    const float r = c[0] / m;
    const float g = c[1] / m;
    const float b = c[2] / m;
    if (r > 0.8f && g > 0.8f && b > 0.8f) return "white";
    if (r > 0.8f && g < 0.5f && b < 0.5f) return "red";
    if (r > 0.8f && g >= 0.5f && b < 0.5f) return g > 0.8f ? "yellow" : "orange";
    if (g > 0.8f && r < 0.5f && b < 0.5f) return "green";
    if (b > 0.8f && r < 0.5f && g < 0.5f) return "blue";
    if (b > 0.8f && g > 0.6f && r < 0.5f) return "cyan";
    if (r > 0.6f && b > 0.6f && g < 0.5f) return "magenta";
    return fmt::format("({:.2f}, {:.2f}, {:.2f})", c[0], c[1], c[2]);
}

} // namespace

std::vector<SetPieceRow> setPieceRows(const directing::Plan& plan, const directing::SceneFacts& facts) {
    std::vector<SetPieceRow> rows;
    if (plan.setPieces.empty()) {
        return rows;
    }
    // Validated as it stands in the project, the way a revision would see it: its own previous
    // revision is itself, so what it made is its own and a hand edit is named.
    directing::Plan resolved = plan;
    const directing::Validation validation = directing::validatePlan(resolved, facts);
    for (std::size_t i = 0; i < plan.setPieces.size(); ++i) {
        const directing::PlanSetPiece& sp = plan.setPieces[i];
        SetPieceRow row;
        row.plan = plan.id;
        row.planTitle = plan.title.empty() ? plan.id : plan.title;
        row.key = sp.key;
        row.templateName = sp.templateName;
        row.craft = sp.craft;
        row.timeText = sp.at.describe();
        row.framingMetres = sp.framingMetres;
        row.beamColor = sp.beamColor;
        const auto kind = stage::setPieceKindFromName(sp.templateName);
        if (kind) {
            row.moments = stage::setPieceMoments(*kind);
        }
        row.moment = !sp.moment.empty() ? sp.moment : kind ? stage::defaultSetPieceMoment(*kind) : std::string();
        row.seconds = validation.times.at(fmt::format("/setPieces/{}/at", i));

        // ---- what, when, where, how: the words somebody describing the picture would use ----------
        if (!kind) {
            row.what = fmt::format("'{}' (not a set piece template)", sp.templateName);
        } else if (*kind == stage::SetPieceKind::Abduction) {
            row.namedAnimals = !sp.animals.empty();
            row.animals = row.namedAnimals ? static_cast<int>(sp.animals.size())
                                           : static_cast<int>(std::lround(slotOf(sp, *kind, "animals")));
            row.what = row.namedAnimals ? fmt::format("abduction: lifts {}", fmt::join(sp.animals, ", "))
                                        : fmt::format("abduction: lifts {} animal{}", row.animals, row.animals == 1 ? "" : "s");
        } else if (*kind == stage::SetPieceKind::Survey) {
            row.what = "survey: the beam sweeps a field and lifts nothing";
        } else {
            row.what = "flyby: crosses the sky";
        }
        row.what += fmt::format(", flown by {}", sp.craft);
        row.when = row.seconds ? fmt::format("{} at {}", row.moment, clockText(*row.seconds))
                               : fmt::format("{} at {} (cannot be placed in this song)", row.moment, row.timeText);
        if (row.seconds && sp.at.kind != directing::TimeRef::Kind::Seconds) {
            row.when += fmt::format(" ({})", row.timeText);
        }
        row.point = sp.where.point.has_value() && !sp.where.region;
        if (sp.where.point) {
            row.x = sp.where.point->first;
            row.z = sp.where.point->second;
        }
        const std::string centre = sp.where.point ? fmt::format("({:.0f}, {:.0f})", sp.where.point->first, sp.where.point->second)
                                                  : fmt::format("{}", sp.where.near);
        row.where = sp.where.region ? fmt::format("the nearest {} within {:.0f} m of {}", sp.tag.empty() ? "animal" : sp.tag,
                                                  sp.where.radius, centre)
                                    : sp.where.point ? fmt::format("over {}", centre) : fmt::format("near {}", centre);
        if (sp.where.offset.first != 0.0f || sp.where.offset.second != 0.0f) {
            row.where += fmt::format(", offset ({:.0f}, {:.0f})", sp.where.offset.first, sp.where.offset.second);
        }
        std::vector<std::string> how;
        if (kind == stage::SetPieceKind::Flyby) {
            row.bearing = slotOf(sp, *kind, "pathBearing");
            row.height = slotOf(sp, *kind, "altitude");
            how.push_back(fmt::format("flies toward the {}", compass(row.bearing)));
            how.push_back(fmt::format("{:.0f} m up", row.height));
        } else if (kind) {
            row.bearing = slotOf(sp, *kind, "approachBearing");
            row.height = slotOf(sp, *kind, "hoverHeight");
            how.push_back(fmt::format("comes in from the {}", compass(row.bearing)));
            how.push_back(fmt::format("hovers {:.0f} m up", row.height));
            how.push_back(sp.beamColor ? fmt::format("{} beam", colourName(*sp.beamColor)) : std::string("the beam as the scene has it"));
        }
        if (sp.framingMetres) {
            how.push_back(fmt::format("meant to be seen from about {:.0f} m", *sp.framingMetres));
        }
        row.how = fmt::format("{}", fmt::join(how, ", "));

        // ---- is what the project holds still what the plan made? --------------------------------
        const std::string scenario = stage::setPieceScenarioName(sp.key);
        bool present = false;
        bool edited = false;
        for (const directing::ContentRef& ref : plan.produced) {
            if (ref.item != sp.key) {
                continue;
            }
            const auto now = directing::contentOf(ref, facts.staged);
            if (ref.domain == directing::ContentDomain::StagingScenario && ref.id == scenario) {
                present = now.has_value();
            }
            if (now && !ref.fingerprint.empty() && directing::fingerprint(*now) != ref.fingerprint) {
                edited = true;
            }
        }
        if (!present) {
            row.state = "not in the project";
            row.whyNot = "its scenario is not in the project: install the plan again (a Director proposal, or avgen --plan)";
        } else if (edited) {
            row.state = "tuned by hand";
            row.whyNot = fmt::format("a slider under staging/{}/ (or one of its markers) was changed by hand, and a "
                                     "revision keeps hand edits: reset those sliders (right-click > Reset to default) "
                                     "to edit it here",
                                     scenario);
        } else {
            row.state = "as made";
            row.editable = kind.has_value();
        }
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        for (const directing::Issue& issue : validation.issues) {
            if (issue.item != sp.key || issue.code == directing::IssueCode::HandEdited) {
                continue; // the hand edit is the state above, said once
            }
            (issue.severity == directing::Severity::Error ? errors : warnings).push_back(issue.message);
        }
        row.lines = std::move(errors);
        row.lines.insert(row.lines.end(), warnings.begin(), warnings.end());
        rows.push_back(std::move(row));
    }
    return rows;
}

Result<directing::Plan> editSetPiece(const directing::Plan& plan, std::string_view key, const SetPieceEdit& edit) {
    directing::Plan out = plan;
    const auto it = std::find_if(out.setPieces.begin(), out.setPieces.end(),
                                 [&](const directing::PlanSetPiece& sp) { return sp.key == key; });
    if (it == out.setPieces.end()) {
        return fail("plan '{}' has no set piece '{}'", plan.id, key);
    }
    directing::PlanSetPiece& sp = *it;
    if (edit.templateName && *edit.templateName != sp.templateName) {
        const auto kind = stage::setPieceKindFromName(*edit.templateName);
        if (!kind) {
            return fail("'{}' is not a set piece template", *edit.templateName);
        }
        sp.templateName = *edit.templateName;
        // What the new template cannot take is dropped rather than refused: the person asked for the
        // template, and a slot of the old one has no meaning in it.
        std::erase_if(sp.set, [&](const auto& o) { return stage::findSetPieceSlot(*kind, o.first) == nullptr; });
        const std::vector<std::string> moments = stage::setPieceMoments(*kind);
        if (!sp.moment.empty() && std::find(moments.begin(), moments.end(), sp.moment) == moments.end()) {
            sp.moment.clear();
        }
        if (*kind != stage::SetPieceKind::Abduction) {
            sp.animals.clear();
            if (sp.where.region) { // only an abduction searches a region: it works over the centre
                sp.where.region = false;
                sp.where.radius = 0.0f;
            }
        }
        if (*kind == stage::SetPieceKind::Flyby) {
            sp.beamColor.reset();
        }
    }
    const auto kind = stage::setPieceKindFromName(sp.templateName);
    if (!kind) {
        return fail("set piece '{}' has no template the engine knows ('{}')", sp.key, sp.templateName);
    }
    if (edit.moment) {
        const std::vector<std::string> moments = stage::setPieceMoments(*kind);
        if (std::find(moments.begin(), moments.end(), *edit.moment) == moments.end()) {
            return fail("a {} has no moment '{}'", sp.templateName, *edit.moment);
        }
        sp.moment = *edit.moment == stage::defaultSetPieceMoment(*kind) ? std::string() : *edit.moment;
    }
    if (edit.seconds) {
        sp.at = directing::TimeRef::at(*edit.seconds);
    }
    if (edit.point) {
        sp.where.point = *edit.point;
        sp.where.near.clear();
        sp.where.offset = {0.0f, 0.0f};
    }
    for (const auto& [name, value] : edit.slots) {
        const stage::SetPieceSlot* slot = stage::findSetPieceSlot(*kind, name);
        if (slot == nullptr) {
            return fail("a {} has no slot '{}'", sp.templateName, name);
        }
        if (name == "animals" && !sp.animals.empty()) {
            return fail("set piece '{}' lifts named animals: change the plan's animals, not the count", sp.key);
        }
        const float v = std::clamp(value, slot->min, slot->max);
        std::erase_if(sp.set, [&](const auto& o) { return o.first == name; });
        sp.set.emplace_back(name, v);
    }
    if (edit.beamColor) {
        if (*edit.beamColor && *kind == stage::SetPieceKind::Flyby) {
            return fail("a flyby shows no beam, so it has no beam colour");
        }
        sp.beamColor = *edit.beamColor;
    }
    if (edit.framingMetres) {
        if (*edit.framingMetres > 0.0f) {
            sp.framingMetres = *edit.framingMetres;
        } else {
            sp.framingMetres.reset();
        }
    }
    return out;
}

Result<directing::Compilation> compileSetPieceEdit(const directing::Plan& plan, std::string_view key,
                                                   const SetPieceEdit& edit, const directing::SceneFacts& facts) {
    auto revised = editSetPiece(plan, key, edit);
    if (!revised) {
        return std::unexpected(revised.error());
    }
    const auto item = [&](const directing::Plan& p) {
        return std::find_if(p.setPieces.begin(), p.setPieces.end(), [&](const directing::PlanSetPiece& sp) { return sp.key == key; });
    };
    if (*item(*revised) == *item(plan)) {
        return fail("nothing would change");
    }
    directing::Compilation compiled = directing::compilePlan(std::move(*revised), facts);
    if (compiled.validation.isBlocked(std::string(key))) {
        std::string why = "it could not be built";
        for (const directing::Issue& issue : compiled.validation.issues) {
            if (issue.item == key && issue.severity == directing::Severity::Error) {
                why = issue.message;
                break;
            }
        }
        return fail("not applied: {}", why);
    }
    // A revision replaces every item it holds, so its diff always says something; what matters is
    // whether the set piece the person touched would be built differently (an override equal to the
    // template's default is the same set piece).
    const std::string scenario = stage::setPieceScenarioName(key);
    const auto find = [&](const stage::StagingDesc& staging) -> std::optional<nlohmann::json> {
        for (const stage::ScenarioDesc& sc : staging.scenarios) {
            if (sc.name == scenario) {
                return stage::scenarioToJson(sc);
            }
        }
        return std::nullopt;
    };
    if (!compiled.changesAnything() || find(compiled.staged.staging) == find(facts.staged.staging)) {
        return fail("nothing would change");
    }
    return compiled;
}

std::string setPieceEditLabel(std::string_view key, const SetPieceEdit& edit) {
    std::vector<std::string> what;
    if (edit.templateName) what.push_back("template");
    if (edit.moment) what.push_back("placed moment");
    if (edit.seconds) what.push_back("time");
    if (edit.point) what.push_back("place");
    for (const auto& [name, value] : edit.slots) {
        what.push_back(name);
    }
    if (edit.beamColor) what.push_back("beam colour");
    if (edit.framingMetres) what.push_back("framing");
    return fmt::format("UFO set piece '{}': {}", key, what.empty() ? std::string("edit") : fmt::format("{}", fmt::join(what, ", ")));
}

std::vector<ChangeGroup> changeGroups(const std::vector<directing::DiffLine>& diff) {
    std::vector<ChangeGroup> groups;
    for (const directing::DiffLine& line : diff) {
        if (line.sign == '!') {
            continue;
        }
        auto it = std::find_if(groups.begin(), groups.end(), [&](const ChangeGroup& g) { return g.item == line.item; });
        if (it == groups.end()) {
            groups.push_back(ChangeGroup{line.item, {}});
            it = groups.end() - 1;
        }
        it->lines.push_back(line);
    }
    return groups;
}

std::string clockText(double seconds) {
    const double s = std::max(0.0, seconds);
    const auto minutes = static_cast<int>(s / 60.0);
    return fmt::format("{:02d}:{:06.3f}", minutes, s - (minutes * 60.0));
}

ContextView contextView(const directing::Plan& resolved, const directing::MusicalContext& music, double playheadSeconds) {
    ContextView out;
    out.time = clockText(playheadSeconds);
    for (const directing::SectionRun& run : music.sections) {
        if (playheadSeconds >= run.startSeconds && playheadSeconds < run.endSeconds) {
            out.section = run.label.empty() ? fmt::format("{} {}", run.type, run.occurrence) : run.label;
            break;
        }
    }
    for (const directing::Subject& s : resolved.subjects) {
        out.subjects.push_back(s.id.empty()
                                   ? fmt::format("{} -> ? (unresolved: \"{}\")", s.alias, s.text)
                                   : fmt::format("{} -> {} ({})", s.alias, s.id, directing::subjectKindName(s.kind)));
    }
    return out;
}

PanelActions panelActions(const PanelState& state) {
    PanelActions a;
    const bool live = state.proposal && state.awaiting;
    const char* none = "no proposal is waiting";

    a.reject.enabled = live;
    a.reject.why = live ? "nothing is changed" : none;

    a.accept.enabled = live && state.changesAnything;
    a.accept.why = !live                    ? none
                   : !state.changesAnything ? "nothing to apply: every item is blocked"
                                            : "re-checked against the project, then applied as one undo";

    a.preview.enabled = live && state.changesAnything && !state.previewing;
    a.preview.why = !live                    ? none
                    : !state.changesAnything ? "nothing to preview"
                    : state.previewing       ? "already previewing"
                                             : "installs it as an ordinary edit so it can be played; undone after";

    a.endPreview.enabled = state.previewing && state.previewIsNewest;
    a.endPreview.why = !state.previewing        ? "not previewing"
                       : state.previewIsNewest ? "undoes the preview edit"
                                               : "later edits were made: undo the preview from the history";

    a.revertPreviewFirst = state.previewing && state.previewIsNewest;

    // Record: turns the waiting proposal into its recording, which is then approved like any other.
    // While it runs, nothing that would change the proposal under it is offered.
    a.record.enabled = live && state.liveToRecord && !state.recording && (!state.previewing || state.previewIsNewest);
    a.record.why = !live                  ? none
                   : state.recording      ? "recording..."
                   : !state.liveToRecord  ? "nothing live to record: every performance is already baked"
                   : state.previewing && !state.previewIsNewest
                       ? "later edits were made after the preview: undo it from the history first"
                       : "plays a scratch copy from zero, keeps what the characters did, checks it, and proposes that";
    // ADR-770: both replace the waiting proposal with a new task's, which ends at the same gate.
    a.modify.enabled = live && state.followUp && !state.recording;
    a.modify.why = !live            ? none
                   : state.recording ? "wait for the recording"
                   : !state.followUp ? "say what to change in the box first"
                                     : "revises this plan (same id) from what you wrote; you approve the revision";
    a.regenerate.enabled = live && !state.recording;
    a.regenerate.why = !live            ? none
                       : state.recording ? "wait for the recording"
                                         : "asks again from the original request; you approve what comes back";
    a.cancelRecording.enabled = state.recording;
    a.cancelRecording.why = state.recording ? "stops the recording; the proposal stays as it was" : "no recording is running";
    if (state.recording) {
        a.accept.enabled = false;
        a.accept.why = "wait for the recording";
        a.preview.enabled = false;
        a.preview.why = "wait for the recording";
    }
    return a;
}

} // namespace avgen::ui
