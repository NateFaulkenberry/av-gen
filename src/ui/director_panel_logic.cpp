#include "ui/director_panel_logic.hpp"

#include <fmt/format.h>

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
