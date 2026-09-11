# ADR-095: The Help system, staged around "Help should never lie"

Status: Accepted
Date: 2026-09-11

## Context

`docs/help-system-spec.md` asks for a professional, searchable, context-aware in-application
documentation system covering essentially every user-facing feature, which is also the knowledge
source for the AI control plane.

Two of its requirements collide with the state of the repository. §4 says document the actual
application; §34 says that where documentation and implementation disagree, the implementation
wins, and that an uncertain author should document the limitation rather than invent behaviour.
When this work started, six agents were concurrently rewriting the world editor (selection, gizmos,
brush, groups, undo), water, terrain, navigation and the AI tool surface. Writing the world-editor
reference today would produce documentation that is stale on arrival — which is precisely what §34
forbids.

## Decision

**Build the framework and the §35 validator now; document only what is stable; register the rest as
gaps a tool reports.**

### Content lives in markdown, not in C++

`docs/help/*.md`, one topic per file, front matter plus a small markdown subset. `src/help/` parses
them into a `HelpDocument` — metadata plus a list of blocks — and `src/ui/help_panel.cpp` draws
whatever it is given. Adding, rewriting or reorganising a topic never touches UI code, and the
navigation tree's shape comes from each topic's `order` and `category`, not from a list in C++.

The raw markdown is kept alongside the parsed blocks. The panel wants blocks; an AI asking
`help.get` wants the text a person would have read. One store, two forms.

Content is found at runtime through a search path — `$AVGEN_HELP_DIR`, beside the executable, then
`AVGEN_SOURCE_DIR/docs/help` — the same shape as `app::exampleSearchDirs` and the shader library. A
second convention for finding runtime content would be a second thing to get wrong.

### One knowledge layer, two consumers

`help::HelpDatabase` exposes exactly the five calls §26 names: `search`, `get`, `related`,
`getShortcut`, `getFeature`. `src/ui/help_panel.cpp` calls them; `src/help/api.hpp` wraps the same
five as JSON-in, JSON-out tool descriptors with schemas, ready for a tool registry to register.
There is no second knowledge base for the AI to drift away from, because there is no second store.

Every tool is read-only, needs no main thread and needs no undo record, and says so in its
descriptor.

### Feature metadata is derived where a registry exists and authored where none does

§5 warns against building a reflection framework and points at the panel registry in
`ui::editor_layout.cpp` as the metadata already present. So:

* **Panels are derived** from `ui::editorPanels()` at startup. `docs/help/features.json` carries only
  an id and a topic for each; the name, the description and the dock region come from the registry
  that owns them. Adding a panel there makes it visible to Help, to search and to the AI with no
  second edit.
* **Commands and shortcuts are authored**, because the application has no command registry — they
  are inline ImGui and SDL calls. The validator scans those call sites and reports drift in both
  directions. When a command registry lands these become derived too and the file shrinks.

### Validation reads the implementation, not a copy

`help::scanApplicationSource` reads `src/ui/editor_layout.cpp`, `src/ui/control_panel.cpp` and
`src/app/application.cpp` as text and extracts the panels, the menu commands and the key bindings.
A validator that compared documentation against a hand-maintained feature list would be comparing
two documents, and two documents drift together.

The obvious failure of scraping is a scan that matches nothing and reports a clean bill of health,
so every scan carries its own coverage and `ScanCoverage::ok()` is false when a file that should
have yielded rows yielded none. The test asserts on coverage *before* it asserts on anything else:
a restructure of `control_panel.cpp` fails the build rather than silently emptying the inventory.

The validator answers two different questions. **Is the documentation wrong** — dead links,
unknown feature ids, shortcuts with no call site, parameter paths nothing registers — reported as
errors. **Does the application have things the documentation does not cover** — panels, commands
and key bindings with no topic — reported as warnings. The second is what fires when somebody
else's pass lands, and it is what makes the staging safe rather than a promise.

### A status on every topic

`stable`, `partial` or `not-yet-documented`. A placeholder states the gap and claims nothing about
behaviour; the validator reports every one as a known gap on every run; search penalises them so a
placeholder never outranks a written topic; and `help.get` returns a `caution` field telling an AI
not to fill the gap from general knowledge.

## Consequences

**Positive.** Documentation is editable without a rebuild of the UI and without touching C++. The
gap list is a tool's output rather than a note in a commit message. The AI and the human panel
cannot disagree. The scan already found a real defect: the File menu advertises `Cmd+S` for Save
Project and nothing binds it — AV Gen has no modifier-aware key handling, so `Cmd+S` reaches the
plain `S` handler and opens the Open Scene dialog.

**Negative.** The source scan is regular expressions over C++ and will need updating when the
editor's shape changes; the coverage assertions are what makes that a loud failure rather than a
quiet one. Dear ImGui has one font, so "bold" in an article is a brighter colour rather than a
weight. Nine areas of the application are deliberately undocumented.

**Rejected.** Generating an embedded C++ string table from the markdown at build time: it would
make every content edit a reconfigure, and loose runtime files are already this repository's
convention for shaders and examples. A full markdown implementation: it buys inline HTML and
reference links that no topic needs, at the cost of a dependency and a class of rendering bugs.

## Follow-ups

The world editor, terrain, water, navigation and the AI Director, once those passes land. The
command inventory in `docs/help/features.json` should become derived from a command registry when
one exists. `avgen_help_lint --strict` is ready to gate a commit once the coverage warnings are
closed.
