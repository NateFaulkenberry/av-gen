# ADR-931: The Director evaluates in scratch, and proposes only the winner

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-094 (the AI tool layer), ADR-753/764 (scratch copies), ADR-757 (the approval gate),
ADR-765/767 (host hooks that answer off the main thread), ADR-770 (Modify and Regenerate), ADR-755
(plan revisions and `produced`)
**Found by:** the GV3 revision's Director audit (`reports/director.md` §4 "Can it take feedback from
an external evaluator? Not today", recommendation 4), and the owner's brief §15 ("the Director should
actively use the evaluator during iteration ... not a final inspection tool only") and §17
**Implemented by:** `directing::EvaluationReport`, `EvaluationFinding`, `reportFromCritic`,
`planItemSpans`, `attributeFindings`, `compareEvaluations`, `EvaluationComparison`
(`src/directing/evaluation.*`); `Engine::directingEvaluations` and the project's
`directingEvaluations` key (`src/app/engine.*`); `ai::EvaluationRequest`, `EvaluationHook`,
`DeferredResult::settle`, `ToolContext::setEvaluationHook` (`src/ai/tool_context.hpp`); the
orchestrator's settle and summary (`src/ai/orchestrator.cpp`), `ControlPlane::setEvaluationHook`;
`director.evaluate` and `director.compare` (`src/ai/director_tools.cpp`); `app::EvaluatorOptions`,
`evaluatorOptionsFrom`, `resolveEvaluatorOptions`, `runProcess`, `criticSubmitCommand`,
`readCriticOutcome`, `evaluateFromCopy`, `makeEvaluationHook` (`src/app/directing_evaluate.*`);
`avgen --critic` / `--critic-url` (`src/app/application.*`)
**Tests:** `tests/unit/test_directing_evaluate.cpp` (`[evaluate]`: a missing Critic is a clear error;
child processes and cancellation; every answer the Critic can give; the whole pipeline end to end on
stand-in programs laid out as the real ones are; the tools' refusals; evaluations kept, saved and
compared); `tests/integration/test_directing_evaluate_agent.cpp` (the ScriptedProvider loop: evaluate
-> propose -> Modify -> evaluate -> compare -> propose -> approve); `test_directing_agent.cpp` (the
tools declare themselves honestly: twelve, and the two new ones' annotations);
`tests/integration/test_directing_evaluate_live.cpp` (hidden `[.live]`: the hook against a running
Critic, below)

## Context

The owner's loop is: revise the directives, render representative shots, evaluate, find concrete
weaknesses, revise, render again, compare with the last iteration, repeat. The evaluator is the
Creative Critic, an independent local program with a CLI and a daemon, which reads a render plus a
generic scene and intent -- written from AV Gen's output files by its own adapter -- and returns
findings with evidence. The Director could not take part: nothing rendered a plan the person had not
approved, nothing called the Critic, nothing remembered what it said, and the approval gate (ADR-757)
would have stopped every iteration.

## Decision

**Two tools.**

- **`director.evaluate {plan | planId, from, until, mode, label}`** renders the span [from, until]
  (at most 60 s) of the film with the plan in it and asks the Critic. `plan` is a candidate nobody has
  approved; `planId` a revision the project holds, rendered as it is. The candidate is compiled into a
  **scratch copy**; the person's project is not touched and nothing is proposed. It answers with the
  report and keeps it.
- **`director.compare {planId, a, b}`** diffs two of a plan's kept evaluations, named by label or
  "revision N" (default: the last two): each scored dimension's verdict (improved / degraded /
  unchanged, the Critic's own +-0.02 threshold), the findings resolved, new and persisting (matched on
  the Critic's stable keys, "repeated_composition:s03"), and the issues per plan item in each.

**The autonomy policy: iterate in scratch, propose the winner.** Evaluating neither proposes nor
installs, so the assistant can try several candidates, compare them, and `director.propose_plan` only
the one that won: the approval gate stops once per proposal, not once per iteration. Evaluations are
records, not content: no gate stands in front of one and no undo takes one back (the Critic keeps its
own session history for the same reason -- an iteration that could not be remembered could not be
compared). The tool is annotated not read-only (it keeps a record), session-mutating, expensive, and
not needing approval.

**The report, `EvaluationReport`**: the plan's id, the revision evaluated (a candidate's is the one it
would become), the candidate document's fingerprint (two candidates of one revision differ), the label,
the span, the mode, the evaluator and its job id, **`partial`** (a core check did not run: never
mistaken for a full report), the headline, the scored dimensions, counts and coverage, and every
finding -- issues first, then strengths and observations -- **keyed by the film-time span it is about
and by the plan items whose spans overlap it** (`planItemSpans`: a shot from its start for its
duration, a set piece from when its craft is taken until it is let go, a marker or a cue at its time,
a performance across its beats). A finding with no span is attributed to nothing rather than to
everything. Stored in order in `Engine::directingEvaluations()` and saved under `directingEvaluations`
beside `directingPlans` -- only when there is one, so every other project keeps its bytes.

**The host's pipeline** (`makeEvaluationHook`), each step on a thread of its own after the copy:

1. the project written to a scratch copy on the main thread (it reads the engine);
2. the candidate compiled and installed on a scratch engine loaded from the copy, saved as a project
   and a scene, with a shot list aligned to the scene's camera shots (the adapter's contract);
3. `[prefix] avgen --project copy --render clip.mov --range a:b --size WxH` -- the prefix is
   `AVGEN_RENDER_PREFIX`, e.g. `tools/gpu-lock.sh` where agents share one GPU;
4. `avgen_cast_trace --project copy --start a --seconds b-a --hz 20` for where the bodies were;
5. the Critic's `adapters/avgen/avgen_adapter.py` with the clip and its range, writing `inputs.json`;
6. `critic [--url U] submit --inputs inputs.json --video-start a --mode M --session S --track
   <plan>@a-b --label L --wait --json --strict --quiet --no-autostart --timeout T`;
7. the Critic's `critic.report/1` read from the `report_json` its JSON line names, into the report,
   and the findings attributed.

**The Critic is configurable and never silently absent.** `--critic <path>` or `AVGEN_CRITIC` names
its CLI (`<repo>/.venv/bin/critic`); the adapter and its Python are found beside it
(`AVGEN_CRITIC_ADAPTER`, `AVGEN_CRITIC_PYTHON` override), the renderer is this executable
(`AVGEN_RENDERER`) and the tracer is beside its build (`AVGEN_CAST_TRACE`); `--critic-url` /
`AVGEN_CRITIC_URL` says where it listens. No Critic configured: `director.evaluate` fails with "no
evaluator is configured: start AV Gen with --critic ..., or set AVGEN_CRITIC". The Critic's exit codes
are read exactly: 0 a report; **5 (PARTIAL under `--strict`) a report marked partial**; **4 "the
Creative Critic is not reachable at <url>: start it with `critic start --daemon`"**; 2 the job failed;
3 cancelled; anything else, or output that is not the Critic's JSON, an error. `--no-autostart` always:
the engine never starts an evaluator on anybody's behalf, and never the default one on :8765.

## Consequences

- **The Director's tool count is twelve** (`test_directing_agent.cpp` re-baselined from ten: ADR-931
  adds two).
- **The ScriptedProvider loop runs end to end** (`test_directing_evaluate_agent.cpp`): a candidate
  abduction is evaluated before it is proposed (the stub Critic finds "the abduction is framed like
  the valley's last one", attributed to the set piece), proposed, Modified by the person ("vary it"),
  the revision evaluated and compared -- the repetition resolved, the stale hold persisting,
  composition improved -- and only then proposed; approval lands it as one undo. The project held no
  plan until the approval; two evaluations were kept.
- **The pipeline is checked without a GPU or a Critic** on stand-ins that record their command lines:
  the renderer is handed a copy with the candidate's `setpiece/west` in it while the person's project
  holds no plan and the same staging; every command is the guide's, in order, the renderer behind the
  prefix; the Critic down, a PARTIAL job, a failed one and nonsense each read as they must.
- **Live smoke test** on a private Critic passed on its second run, after it found the `NaN` defect
  (see the section below).
- **What the Critic's adapter should change (not done here: the Critic's repository is not ours).**
  `adapters/avgen/avgen_adapter.py` hard-codes GV3's saucer beats as events (28.2 s "flyby", 148.45
  "over the rim", 170.35 "beam lights", 177.70 "horse taken", 181.4 "leaves the station", all on
  entity `visitor`). It should read the cast trace's `setPieces` section instead: one event per beat
  in `beats` (`type` "ufo", `label` "<id> <beat>", `t` the beat's time, `position` the trace's
  `craftAt[beat]`, `entity` the piece's `craft`), plus one per animal `retired`. Then every set piece a
  plan places is evaluated for event variety (`measurements.events.types.ufo`) with no adapter edit
  per film.
- **Not done.** An evaluation renders at the previews' size (960x540 unless `AVGEN_EVALUATE_SIZE`
  says otherwise); the Director panel does not show kept evaluations yet (they are in the project and
  in `director.compare`). The route on/off render the audit asked for (the third level of reactivity
  evidence) is the Critic's to request through a span render of each arm; this hook renders what the
  plan makes.

## Live smoke test

`tests/integration/test_directing_evaluate_live.cpp` (hidden, `[.live]`), run by hand against a
**private** Critic -- `CRITIC_HOME=<scratch>/critic-home CRITIC_PORT=8791 critic --url
http://127.0.0.1:8791 start --daemon`, stopped afterwards with `CRITIC_HOME=<scratch>/critic-home critic
stop`; the coordinator's evaluator on :8765 (pid 35807) was listening before and after and was never
addressed. The lab with one static camera on the field station, the red-beam two-cow abduction as a
candidate plan, the span 58-66 s, preview mode, `AVGEN_RENDER_PREFIX=tools/gpu-lock.sh`:

- **First run:** the copy, the render (73.4 s, most of it waiting for the GPU lock another agent held),
  the trace, the adapter and the Critic's preview job all ran; then the reader refused the report:
  Python's `json` writes an undefined measurement as a bare `NaN` (`"median_offset_ms": NaN`), which
  strict JSON refuses. Fixed: outside strings `NaN`, `Infinity` and `-Infinity` are read as `null`, and
  the stand-in reports now carry all three.
- **Second run: passed** (exit 0; 5.2 s with the lock free: render 3.8 s, trace 0.2 s, adapter and
  evaluation 1.0 s). The report came back **PARTIAL** -- the lab has no audio, so the Critic's audio
  analysis and audio reactivity did not run, and `--strict` exited 5, which the engine keeps as
  `partial: true` -- with 16 scored dimensions (lighting 0.947, colour 0.973, environment 0.973, the
  rest 1.0) and one issue, "Pale and washed out for a night scene" (`pale_for_night:s01`, lighting,
  58-66 s), **attributed to the plan item `field`** whose span it overlaps. It was kept in
  `directingEvaluations`; the person's project held no plan afterwards. The Critic also said "s01: no
  primary subject could be matched": the host names a shot's subject from its rig's aim or follow node,
  and a static rig has neither -- a generator's shot plan (GV3's) names them.
