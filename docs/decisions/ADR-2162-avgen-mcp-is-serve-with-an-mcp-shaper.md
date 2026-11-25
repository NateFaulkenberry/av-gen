# ADR-2162: `avgen mcp` is `avgen serve` with an MCP result shaper and a name table

- Status: Accepted (2026-11-25), eng/engine-api-s9 (Engine API stage S9; 113-engine-architecture, design in
  03-phase0-api-contracts/02-session-api.md section 10 and 01-api-contracts.md section 5.3, stage plan in
  04-poc-plan.md; owner's spec section 4 S9 and decision 7)
- Code: `api/mcp_shaper.{hpp,cpp}` (the pinned revision, `McpNameTable`, `mcpInitializeResult`, `mcpTool`,
  `mcpOutputSchema`, `mcpCallToolResult`, `mcpProgressParams`, the method table, `mcpMarkdown`),
  `api/jsonrpc_server.{hpp,cpp}` (`ServeProtocol`, the MCP dispatch beside the JSON-RPC one in the same `Server`),
  `app/main.cpp` (`avgen mcp`), `api/describe.cpp` (`docs/api/mcp.md` is a generated artefact)
- Docs: `docs/api/mcp.md` (generated), `docs/help/reference-command-line.md`
- Tests: `tests/unit/test_api_mcp.cpp` (`[api][mcp]`), fixtures `tests/data/mcp/*.json`

## Context

Every client reaches the engine through one `api::Session` (ADR-2153), and `avgen serve --stdio` (ADR-2155) puts
it on a stream as JSON-RPC 2.0. MCP clients (coding agents, chat clients) speak a different dialect of the same
JSON-RPC: an `initialize` handshake with a protocol revision, `tools/list` with annotation hints, `tools/call`
answering a `CallToolResult`, progress tied to a token. The owner's rule (decision 7): MCP is a thin adapter over
the canonical Session, never a second implementation of engine functionality, and equivalent operations through
Session, JSON-RPC and MCP must give equivalent results.

## Decision

1. **One server, two protocols.** `ServeOptions::protocol` (`ServeProtocol::JsonRpc | Mcp`) selects the method
   table and the shape of the answers inside the existing `Server`; the reader, the writer, the host thread, the
   argument and option parsing, the dry run and `Session::call` are shared. `avgen mcp [--stdio] [serve's flags]`
   is `runServeCommand` with `Mcp`. The session's client kind is `mcp`.
2. **Pinned revision `2025-11-25`** (`api::kMcpProtocolRevision`, the one constant): the newest revision that opens
   with `initialize`, checked against modelcontextprotocol.io on 2026-11-25. The current revision there,
   2026-07-28, removed the handshake (per-request `_meta` and `server/discover`). The brief requires the
   `initialize` lifecycle, and a dual-era client that probes `server/discover` here gets `-32601`, a non-modern
   error, and falls back to `initialize`, which is that revision's stdio compatibility rule. `initialize` always
   answers the pinned revision.
3. **Lifecycle.** `ping` at any time; anything else before `initialize` is `-32600`; a second `initialize` is
   `-32600`; `notifications/initialized` marks the client ready (log notifications start only then);
   `notifications/cancelled` is accepted and ignored; `logging/setLevel` maps RFC 5424 levels to the engine's;
   batches are `-32600` (the revision has none); end of input closes the session as `serve` does. `shutdown` is
   not an MCP method.
4. **`initialize`**: `{protocolVersion, capabilities: {tools: {listChanged: false}, logging: {}}, serverInfo (from
   api::version()), instructions, _meta["avgen/server"]}`; the last is `serve`'s own answer (API level, versions,
   session, permissions, `gpu`, `render`) without the JSON-RPC protocol's name and capabilities.
5. **The name table.** `McpNameTable` is built from the catalogue's generated `mcpName` fields and refuses a
   duplicate. `tools/call` maps the MCP name back through it, never by reversing the string; a name not in it is
   a protocol error `-32602` "Unknown tool: <name>" with `data {name, recovery}`, as MCP specifies for unknown
   tools. (Over `serve` the same mistake is a `NOT_FOUND` tool result; this is the one deliberate difference.)
6. **`tools/list`**: each catalogue entry as an MCP Tool: `name` = `mcpName`, `title`, `description`,
   `inputSchema` unchanged, `annotations {title, readOnlyHint, destructiveHint, idempotentHint, openWorldHint:
   false}` straight from the registry's annotations, `_meta {avgen/tool, avgen/executionClass, avgen/permissions,
   avgen/since, avgen/availability}`. One page.
7. **`outputSchema` describes the envelope.** `structuredContent` is the envelope, and MCP requires it to conform
   to `outputSchema`, so each tool's MCP `outputSchema` is the envelope schema with `result: anyOf [the tool's
   outputSchema, null]` (null: a failure), or `result: {}` for a tool that has none yet. Every tool gets one.
8. **`CallToolResult`**: `structuredContent` = `CallResult::toWire()` unchanged; `isError = !success`;
   `content[0]` = a text block, the summary, a newline and the compact envelope (the revision's "SHOULD also
   return the serialized JSON"). Captures and renders are file paths in the job record; no image block (deferred).
9. **Call options in `_meta`.** MCP's `tools/call` has no field for them, so `_meta["avgen/transaction"]`,
   `["avgen/irreversible"]` and `["avgen/dryRun"]` carry what `serve` takes as `transaction`, `irreversible` and
   `dryRun`, parsed by the same function.
10. **Progress** goes to the call's `_meta.progressToken` only while that call runs: the tool's own `onProgress`,
    and for `job.wait {id}` that job's events (`state: phase`). `total` is 1; a value that does not increase is
    nudged up by 1e-4 when its message is new and not sent when it repeats; the token is cleared before the
    response is written (the progress mutex is taken before the writer's), so nothing follows the response.
11. **`docs/api/mcp.md` is generated** from the shaper's tables and the catalogue (methods, shapes, the
    `mcpServers` configuration, the full name table), as an artefact of `avgen --describe all` held by the T1 drift
    test.

## Alternatives considered

- **A separate MCP server loop or library.** Rejected by the owner's rule and by size: everything but four answer
  shapes would be duplicated, and two loops drift.
- **Pin 2026-07-28 (stateless).** Rejected for this stage: no `initialize`, which the brief requires, and the MCP
  clients this is for still open with the handshake. A dual-era server is the revisit.
- **Unknown tool as `isError: true`** (serve's behaviour). Rejected: the revision names unknown tools as a protocol
  error.
- **The tool's own outputSchema as MCP's `outputSchema`.** Rejected: `structuredContent` is the envelope, which
  would not conform; making `structuredContent` the bare `result` would break P1's "the same envelope everywhere".
- **Transaction options as extra tool arguments.** Rejected: it would change every tool's `inputSchema` for MCP
  only; the catalogue stays the one contract.

## Consequences

- P1 (CPU suite): project.create, asset.import, scene.create_node, document.patch, parameter.set,
  modulation.create, project.validate, transaction.begin, a parameter.set inside it, transaction.rollback and a
  parameter.get give byte-identical envelopes through `Session::call`, `serve` and `mcp`.
- Acceptance: a stdlib Python MCP client ran `initialize`, `tools/list` and project.create, asset.import,
  scene.create_node, parameter.set, render.capture, job.wait (with a progress token) against `avgen mcp --root`;
  the capture's frame hash equals the same calls through `avgen serve --stdio` (stage report).
- A model-driven MCP client that cannot set `_meta` cannot join an explicit transaction; if it opens one, its
  mutating calls are refused `FAILED_PRECONDITION` until it commits or rolls back. Calls outside one run in their
  own implicit transactions, as for every client. `docs/api/mcp.md` says so.

## Revisit triggers

- MCP clients move to 2026-07-28: answer `server/discover` and per-request `_meta` beside `initialize` (dual-era).
- An agent needs to see a capture: an image content block when the client asks and the PNG is small.
- Explicit transactions from model-driven MCP clients: a session-level "join the open transaction" option.
- Cancellation (`notifications/cancelled` to `CallOptions::cancel`) once the server reads while a call runs.
