# ADR-2153: every client goes through one Session, with fixed permissions, one path policy and real transactions

- Status: Accepted (2026-11-25), eng/engine-api-s1 (Engine API stage S1; 113-engine-architecture, design in
  03-phase0-api-contracts/02-session-api.md)
- Code: `api/session.{hpp,cpp}` (`Session`, `Host`, `LocalHost`, `CallOptions`, `CallResult::toWire`,
  `CallArtifacts`, the live policy, `registerAllTools`), `api/session_tools.cpp` (`session.info`, `transaction.*`),
  `api/path_policy.{hpp,cpp}` (`SessionPermissions`, `PathPolicy`), `api/compensation.{hpp,cpp}`
  (`CompensationLog`, `FileClaims`), `ai/tool_api.{hpp,cpp}` (`PermissionDenied`, `FailedPrecondition`, `Busy`,
  `ToolError::details`, `ToolAnnotations::requirements`, `ToolRegistry::run`/`checkArguments`),
  `ai/tool_context.hpp` (`paths()`, `session()`, `snapshots()`, the file events; `projectsRoot`,
  `contentRoots` and `resolveContent` removed), `ai/engine_tools.cpp` and `ai/director_tools.cpp` (every path
  through `paths()`, the snapshot store from the context, requirements stated), `ai/orchestrator.*` (a Session
  client), `ai/control_plane.*` (holds the Session), `ai/transaction.hpp` (`rollbackPoint`, the header's
  snapshot domain corrected), `app/application.cpp` (`initControlPlane` builds the Session's permissions)
- Docs: `docs/api/transactions.md` (what each operation's rollback restores), `docs/ai-control-plane.md`
- Tests: `tests/unit/test_api_session.cpp` (`[api][session]`); the changed codes in `test_ai_tools.cpp`

## Context

The tool registry had one client, Studio's assistant, and the client did the API's work: the `Orchestrator`
built each call's `ToolContext`, copied the roots into it, opened the task's transaction by hand, and owned the
only transaction any call ever had. Everything else that drove the tools (the tests, `--ai-script`) built a
context by hand. Measured at 414d47790 and reported in 113-engine-architecture/01-evaluation:

- relative paths resolved against the process's working directory;
- writes were checked against no root at all: `asset.import` wrote beside whatever project was open;
- refusals were coded by each tool's author: outside the roots was `NotFound`, no root was `Unavailable`,
  "no project open" was `InvalidArguments`; an agent could not tell missing from forbidden;
- the editor added the project's folder to the read roots only if a project was open at start-up;
- `SnapshotStore` was a function-local static, so every session in a process shared rollback ids;
- only the orchestrator opened a transaction, and a non-undoable call inside one was silent;
- one tool (`asset.import`) cleaned up its own partial copies, and a rolled-back task left its imports behind.

## Decision

1. **`avgen::api::Session` is the one call path.** It holds the registry (engine, Director and session tools,
   immutable after `create`), the permissions, the path policy, its own snapshot store, the transaction sink
   and at most one explicit transaction, and marshals every call onto its `Host`'s queue. `call` looks up,
   validates and permission-checks on the caller's thread, then runs the body on the host thread (inline when
   the caller is the host thread). `CallResult::toWire` is the envelope of Part 1 section 5.3, every key on
   every call. A `Host` is `engine()`, `queue()`, `installHooks(ctx)` and the live-policy question
   (`showRunning`); render submission is stage S6 and not here. `LocalHost` is Studio's.
2. **Permissions are fixed and only narrowed** (`SessionPermissions`: read roots, write roots, the open
   project's folder rule, project access, gpu, subprocess, live control). Roots must be absolute.
3. **One path policy.** `PathPolicy::forRead`/`forWrite`/`projectWriteRefusal`: a relative path resolves
   against the open project's folder, else the first write root, never the working directory; containment is
   tested on the canonical path (`weakly_canonical`, the old `resolveContent` algorithm, moved); write roots
   are readable. `ToolContext::paths()` replaces `projectsRoot`, `contentRoots` and `resolveContent` in the same
   change, with no compatibility layer (ADR-441). `asset://` ids get `Unsupported` until stage S3
   (`PathPolicy::resolveAssetId` is the seam).
4. **The open project's folder is a rule, not a path.** Studio's session (and a headless `--ai-script` run's)
   has `openProjectFolder`: whichever project is open, its folder is readable and writable (owner decision 1).
   It is read at each call, so every way a project is opened refreshes it without a hook in each.
5. **Three error codes, and `details`.** `PermissionDenied`, `FailedPrecondition`, `Busy`, appended to the
   closed set; `ToolError::details` carries `pointer`, `path`, `field`, `findings` or `retryAfterMs`.
6. **Transactions.** A `mutatesProject` call with no transaction named runs in an implicit one that rolls back
   on failure (its rollback point is kept apart from the session's listed snapshots). `transaction.begin` /
   `commit` / `rollback` / `status` give one explicit transaction per session; while it is open, a mutating
   call that does not name it is refused. A non-undoable call inside it is refused unless
   `Irreversible::Accept`, which marks it partial; commit and rollback report what they cannot restore.
7. **A compensation log for files.** Tools report what they write (`noteCreatedFile`, `noteReplacedFile`,
   `noteOverwrittenFile`, `noteUsedFile`); the session undoes it on a failed call or a rollback. A file is not
   deleted if it existed before, if the project or scene as restored (or saved on disk) names it, if another
   transaction depends on it (claims are process-wide), or if it is a render output. `asset.import`'s own
   rollback is gone.
8. **The orchestrator is a client.** `invokeOnMainThread` is `session.call` (with `Irreversible::Accept`: a
   task may mix an edit with a save or an import, as it always could); the task's lazy transaction is
   `transaction.begin` on the first mutating tool and `commit`/`rollback` at the end, so it still opens lazily,
   is captured on the host thread, and the edit-history sink still gives one undo per task. The model's
   `transaction.*` calls are refused: the task owns its transaction. Director tools keep their proposal and
   deferred handles through `CallArtifacts`.
9. **The live policy** (owner decision 3). A `HostPolicy::Live` session whose host reports a show running
   refuses a client other than Studio's own assistant with `Busy` for `structural` and `job` calls, non-read
   `project.*` calls, seeks (`sequencer.set_playhead`) and `asset.import_audio` (it replaces what is playing),
   with `details.retryAfterMs` when the host knows the show's remaining time. In Studio a show is running
   while the transport plays. Reads stay available.
10. **Supported, available, permitted.** `ToolAnnotations::requirements` (closed vocabulary: `composition`,
    `performance`, `recording`, `watch`, `evaluation`, `profile`) states what the host must provide.
    `session.info` answers, per tool, `supported` (in the catalogue), `available` (the hook is installed in this
    host's context, a composition is loaded, the live policy lets it through) and `permitted` (the session's
    permissions), each answered from the running state, never from `supported`.

## Deliberately changed codes

| Call | Before | After |
|---|---|---|
| a path outside the roots (`asset.import`, `asset.import_audio`, `scene.create_node`) | `NOT_FOUND` | `PERMISSION_DENIED` |
| a name-based project tool with no write root (`project.list/create/save_as/open`) | `UNAVAILABLE` | `PERMISSION_DENIED`, recovery "start the session with --root <dir> or --write-root <dir>" |
| `project.save`, `asset.import`, `asset.import_audio` with no project open | `INVALID_ARGUMENTS` | `FAILED_PRECONDITION` |

## Alternatives considered

1. **Keep the roots in `ToolContext` and add the Session beside them.** Rejected: two ways to resolve a path,
   and the no-shims rule. Every tool now resolves through one policy.
2. **A process-wide registry shared by every session.** Rejected for now: a test (and a host) may add a tool,
   and the snapshot tools need a per-session store either way. Each session builds its own registry, which is
   immutable from `create` on.
3. **Refresh the project-folder root on every project open.** Rejected in favour of the rule (decision 4): the
   editor opens projects from a dozen places, and a missed refresh is a silent permission bug.
4. **Refuse irreversible calls in the orchestrator's transaction.** Rejected: run 09 (`parameter.set`, then
   `project.save`, in one task) would stop working. The orchestrator accepts, and the transaction says it is
   partial.

## Consequences

- Run 09 (`--ai-script` set, depth, save) saves the same project as before (the `app.version` stamp is 0.3.0
  by ADR-2150).
- The catalogue gains `session.info` and `transaction.*` (each with an output schema) and every tool's
  `requirements`; `docs/api/tools.md` and `errors.md` are regenerated.
- Studio's assistant may write into the open project's folder: a new write permission, by owner decision 1.
- A rollback after `project.open` or `project.create` inside the same transaction restores the old project's
  document into the new one; `docs/api/transactions.md` says not to do that.

## Revisit triggers

- Stage S1b: revisions and dirty state (a transaction records them at begin), and the time policy.
- Stage S2 (`avgen serve`): validation and permission checks already run on the caller's thread; the transport
  reader calls `Session::call` directly.
- Stage S3: `asset://` ids through `PathPolicy::resolveAssetId`; path-based `project.create`/`open`.
- Stage S6: `Host` gains render submission; `gpu` permission refusals become live.
- A second client on Studio's engine: the live policy then refuses real calls, and Studio needs a second
  Session on the same host.
