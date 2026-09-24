# ADR-757: A proposed plan waits for the person; approval re-checks, installs in one transaction, and verifies

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-094 (the control plane), ADR-752 (one undo; the history sink), ADR-755/756 (the Plan,
validation and compilation), ADR-750 (the module boundary); spec §19, §45–§47; commit 13bc030f (a
task's state is published with release and read with acquire)
**Implemented by:** `src/ai/director_tools.*` (seven `director.*` tools, `commitProposal`);
`ToolContext::Proposal`; `TaskState::{AwaitingApproval, Committing, Rejected}`, `AgentTask::proposal`,
`Orchestrator::approve/reject`; `ControlPlane::approveCurrentTask/rejectCurrentTask`;
`ToolAnnotations::{requiresApproval, deterministic}`; `app::installCompilation`,
`app::verifyInstalled`; the AI panel's Approve/Reject
**Tests:** `tests/integration/test_directing_agent.cpp` (`[directing][agent]`)

## Context

The orchestrator went straight from model output to mutation. The spec asks for plan → validate →
preview → approve → commit → verify. A rejected plan must change nothing, and no mutating Director
operation may bypass approval.

## Decision

**Semantic tools, none of which changes the project.** They are thin wrappers over
`src/directing/`:
- `director.inspect_scene`
- `inspect_subject`
- `inspect_capabilities`
- `resolve_time`
- `plan_schema`: the contract, generated from the vocabularies
- `validate_plan`
- `propose_plan`

`propose_plan` compiles against a staging copy and hands the result, with its diff, to the task as
a `Proposal`. It is annotated `requiresApproval`. An invalid plan is an ordinary result carrying
issues, not a failed call: it is information the model acts on. The low-level tools stay as the
escape hatch.

**There is no apply tool.** Applying is the person's act. This is the only way to make "no
mutating Director operation may bypass approval" structural rather than a rule a model is asked to
follow. Spec §45 lists `apply_plan`; its role is `ControlPlane::approveCurrentTask`, a host API. A
future MCP surface may expose the same approve call to a *person's* client, never to the model.

**The state machine.**

```
Preparing -> WaitingForModel <-> ExecutingTools -> Validating -> Completed
                                                \-> AwaitingApproval -> Committing -> Completed | Failed
                                                                     \-> Rejected
```

- A task that proposed a plan does not finish. `run` holds the model-phase outcome and publishes
  `AwaitingApproval` last, through `setState` (release), so a reader that sees the state also sees
  the proposal.
- New requests are refused while a task waits.
- Cancel while waiting is reject.

**Approval** (main thread only):
1. Re-compile the proposed plan against the project as it is *now*.
2. **If the diff differs from the one the person saw, refuse.** "The project changed after this
   plan was proposed; propose it again." Approving must never apply something other than what was
   shown.
3. Open a `Transaction` over the task's sink. With the history sink, the install becomes one
   command labelled with the request.
4. `installCompilation`.
5. `verifyInstalled` (spec §19's ValidateCommittedState): every `produced` entry exists and matches
   its fingerprint.
6. Commit, and record the history state in the outcome, so "Undo this task" works (ADR-752). Any
   failure along the way rolls back.

**Rejection** changes nothing and ends the task `Rejected`.

**The UI** (spec §35, not overbuilt): the proposal's diff is shown as the task's Plan activity,
with Approve and Reject beneath it.

## Consequences

- A model cannot change the project through the Director without a person's approval, and the
  person approves exactly what they were shown.
- The benchmark end to end:
  1. inspect: Rook is resolved; Umbra is ambiguous, and "the Umbra hero mushroom" resolves;
  2. resolve 1:30;
  3. propose, with the backflip refused;
  4. wait, with nothing changed;
  5. approve, as one undo labelled with the request;
  6. undo, which restores everything.
- Tested: reject, cancel-while-waiting, a stale proposal refused after the project changed under it,
  and a plan with nothing buildable (which is never proposed).
- Known limits:
  - Only one task at a time may wait. "Modify" and "Regenerate" (spec §35) are a new request after
    a reject, not yet a revision of the waiting proposal.
  - Approval re-compiles; it does not re-validate spatial facts against the *simulation* (entity
    positions), because plans read authored places only (ADR-756).
  - Not verified in the running UI: the Approve/Reject buttons.
