# ADR-765: A recording is started by a button or the assistant, and approved by the person

**Status:** Accepted
**Date:** 2026-09-25
**Related:** ADR-763 (a goal is live until it is recorded), ADR-757 (the approval gate), ADR-762 (the
panel), ADR-094 (the AI control plane); the owner's ruling of 2026-09-25 ("yes to both")

**Implemented by:**
- **The recorder:** `app::writeRecordingCopy`, `app::recordFromCopy`, `app::RecordingJob` and
  `app::makeRecordingHook` (`src/app/directing_record.*`).
- **The core seam:** `ai::RecordingHandle` and `ai::RecordingHook`, with
  `ToolContext::deferProposal`, `Orchestrator`'s off-main-thread wait, `ControlPlane::setRecordingHook`
  and `ControlPlane::reviseCurrentProposal`.
- **The tool and proposal helper:** `director.record_plan` and `ai::proposalFor`.
- **The panel:** the "Record" button and `PanelActions::record`.

**Tests:**
- the `[directing][agent][record]` ScriptedProvider test, with and without a recorder;
- `[directing][panel]` for the Record button's rules;
- the `director-record` UI script arm (18 checks).

## Decision

- **One gate.** A recording is never applied by whoever starts it. It becomes the waiting
  proposal, previewed and then accepted or rejected like any other.
  - Accept installs the recorded, baked plan as one undo labelled with the request.
  - There is no blanket approval.
- **The panel's Record button.**
  - It records the waiting proposal's live performances on the recorder's own thread, and the
    panel shows the phase.
  - When it finishes, `reviseCurrentProposal` replaces the waiting proposal with the recording.
  - While it runs, Accept and Preview are held, because what they would act on is about to change.
    Reject stays available.
- **The assistant's `director.record_plan`.**
  - It compiles the plan and refuses one with nothing live to record.
  - It asks the host's hook to start a recording and hands the handle to the orchestrator.
  - The orchestrator waits on the task's worker thread, never the main thread. It reports the
    phases as tool progress, and on success proposes the recorded plan through `proposalFor`.
  - A session whose host installed no recorder gets `UNAVAILABLE`, and nothing is proposed.
- **The recorder stays application code.**
  - The core library sees only the handle and the hook.
  - The application installs `makeRecordingHook()` on its control plane, and tests install the same.
  - The copy is written on the main thread, because it reads the live engine. Everything else
    touches only that file and scratch engines.

## Consequences

- **Measured, headless, benchmark (Rook's goal to the Lantern):**
  - The Record button's recording replaced the proposal at frame 923. That was about 2.6 s of
    recording and 9.6 s of checks (0.0000 m played back, 0.0000 m scrubbed), with the editor
    drawing throughout.
  - The flash cue, blocked while the goal was live, is placed at the recorded arrival (00:14.417).
- **Proven red:** without the orchestrator's `setProposal` for a deferred recording, the agent
  test never reaches AwaitingApproval. The UI arm fails its checks if the recorded proposal is not
  baked or its Accept does not install the recording.
- Not done: cancelling a recording from the panel (the job can be cancelled; there is no button).
