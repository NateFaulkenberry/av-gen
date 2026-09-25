# ADR-770: Modify and Regenerate end at the same approval gate

**Status:** Accepted (the owner's ruling, 2026-09-25: spec 35's Modify and Regenerate, yes)
**Date:** 2026-09-25
**Related:** ADR-757 (the approval gate), ADR-762 (the panel), ADR-755 (plan identity); spec §35–§36

**Implemented by:**
- `ControlPlane::modifyCurrentTask` and `regenerateCurrentTask`;
- `AgentTask::briefing` and `originalRequest`;
- the panel's follow-up box, and its Modify / Regenerate buttons and rules in `director_panel_logic`.

**Tests:**
- the `[directing][agent][modify]` ScriptedProvider test;
- the panel logic cases;
- the `director-modify` UI-script arm (12 checks).

## Decision

- **Both close the waiting task** (declined, as superseded) and start a new one, which ends at the
  same approval gate. Nothing is applied by either.
- **Modify** sends the person's follow-up, briefed with the waiting plan document and told to keep
  its id. So the revision revises the same plan rather than starting a second one.
- **Regenerate** sends the original request of the chain again: the first request, not a
  follow-up. It is briefed that the last proposal was not wanted.
- **The briefing reaches the model only.** The task's request and its undo label stay the person's
  own words.

## Consequences

- **Checked through the pointer:**
  - propose;
  - type a follow-up, then Modify: the first task is superseded, and the revision keeps plan id
    `rook-run-past` with the shot 7 s instead of 5;
  - Regenerate: the original request is asked again;
  - nothing is applied until approval.

  The ScriptedProvider test ends by approving: one undo, labelled with the request.
- The panel shows the task that is still thinking, not the proposal it superseded.
- **Known:** one run of the UI arm missed the Regenerate click, and two reruns passed. The likely
  cause is the panel re-laying out between the frame the arm read the button's position on and the
  click. This is not fixed.
