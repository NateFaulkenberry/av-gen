# ADR-762: A minimal Director panel, with Preview as an ordinary edit

**Status:** Accepted
**Date:** 2026-09-25
**Related:** ADR-752 (one undo), ADR-753 (preview isolation), ADR-757 (the approval gate); spec §35
**Implemented by:**
- `src/ui/director_panel.*` (drawing);
- `src/ui/director_panel_logic.*` (the decisions);
- the "Director" entry in `editor_layout.cpp`;
- `Application::runAiTask`, which now stops at a proposal.

**Tests:** `tests/unit/test_director_panel_logic.cpp` (`[directing][panel]`)
**Evidence:** `~/Desktop/av-gen-review/15-director-panel/director-panel-benchmark-proposal.png`, a
headless capture of the benchmark proposal. The proposal came from a scripted provider:
`tests/data/directing/benchmark_proposal.ai-script.json`.

## Decision

- **A "Director" panel, next to the AI panel.** The AI panel keeps the chat. The Director panel is
  a view of what the chat proposed, in four parts:
  - **Conversation, beside Context.** The request and the model's words, next to the playhead
    time, the song section there, the project, and every subject as it resolved (e.g. `rook -> rook
    (entity)`).
  - **The plan's items.** One per shot, performance, marker, cue or retime. Each is marked ok,
    warning or blocked, with the validator's messages under it, errors first. The marks are drawn
    as shapes because the UI font is ASCII.
  - **Proposed changes.** The diff's `+ - ~` lines, grouped by item. The `!` findings belong to the
    item rows.
  - **Preview / Accept / Reject.** Modify and Regenerate are left out: the conversation already
    covers them.
- **The panel shows the live dry run.** It re-compiles the proposal against the project as it is
  now, whenever the proposal or the history changes. That is what approval re-checks (ADR-757).
- **Preview is an ordinary, labelled edit.** It uses `applyCompilation`, so the person can play the
  proposal where it will be.
  - It ends by undoing that edit, and only while the edit is still the newest one. After later
    edits, undoing would take them instead, so the panel says the preview is still in the history.
  - Accept and Reject end a newest preview before they act. Accept then goes through the approval
    gate unchanged.
  - We rejected a scratch-session preview. ADR-753's file copy loads in seconds and cannot be
    played in the editor's viewport. We also rejected a hidden temporary install: it would bypass
    the history and could be saved by accident.
- **A batch AI run stops at a proposal.** `--ai-script` / `--ai-prompt` return 0 when a proposal is
  waiting for approval. Before this, they waited out the deadline and cancelled it, so a proposal
  could not be captured.

## Consequences

- Every decision the panel makes can be tested: the marks, the grouping, the context and the
  buttons. Making the preview-revert rule ignore "newest" turns a test red.
- Not verified in a running session: pressing Preview, Accept or Reject. The capture shows the
  waiting state only.
