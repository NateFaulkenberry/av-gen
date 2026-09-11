# AV Gen — Integrated Help System

The brief for the in-application help system, kept in the repository so the work can be picked up
without the conversation it arrived in. Reproduced as written, tightened where the original
enumerated at length.

Not a static About dialog: a professional, searchable, context-aware, maintainable in-application
documentation system covering essentially every user-facing feature. A new user should be able to
learn AV Gen without leaving it; an experienced user should be able to look up a command, parameter,
workflow or shortcut fast. **It should also become a knowledge source for the AI Control Plane.**

---

## §1–§3 Goal, native panel, navigation

Answer real questions: *How do I move around the world? Place an asset? Select multiple objects?
Duplicate? Edit terrain? Create water? Make something glow — and make the glow actually illuminate
nearby objects? Add a camera shot? Keyframe a parameter? Connect bass energy to a material property?
Render offline? Optimise a scene? What does this rendering setting do? How do I undo an AI-generated
modification? What can the AI agent actually control?*

**§2 — a dedicated native panel** reachable from primary navigation, following existing UI
conventions. **Not a browser, not an external website.** Core documentation must work offline.

**§3** Organise for discoverability: Getting Started, World Editor, Scene System, Audio, Modulation,
Sequencer, Rendering, Shaders, AI Director, Keyboard Shortcuts, Command Reference, Troubleshooting,
Performance Guide. **Adapt to the actual application; do not create categories for features that do
not exist.**

## §4 Document the actual application *(critical)*

**Do not write generic documentation about a hypothetical 3D engine.** Before writing, inspect the
source, UI, menus, commands, shortcuts, settings, panels, engine systems, existing documentation,
tests and configuration. **The implementation is the source of truth.** If a feature does not exist,
do not document it. If it is partial, describe its actual behaviour.

## §5–§6 Metadata and a real content model

Build around metadata rather than duplicated hardcoded information: feature ID, display name,
category, description, documentation topic, keyboard shortcut, menu location, availability, related
features. **Do not build an enormous reflection framework for this** — use existing metadata systems.

Do not store pages as giant hardcoded C++ strings. A `HelpDocument` carries id, title, category,
summary, body, tags, keywords, related documents, related features, shortcuts, version.
Markdown-like source files are acceptable. **The requirement: documentation must be editable and
maintainable without rewriting UI code.**

## §7–§8 Search

Fast full-text search. Typing `water` surfaces overview, materials, reflections, transparency,
environment integration, performance and troubleshooting. Natural phrasing — *"how do I make water
look better?"* — should surface relevant topics. **Local keyword search is fine initially; design
the abstraction so semantic search can be added later.** Results show title, category, short
description and matching terms; clicking navigates.

## §9–§10 Shortcut and world-editor command reference

Enumerate **every actual shortcut discovered in the application**, grouped by General, World Editor,
Sequencer, Rendering and Panels. **Do not invent shortcuts** — read them from the implementation. If
configurable, document defaults and say so.

The world editor deserves a compact cheat-sheet: camera navigation (orbit, pan, zoom, fly, focus,
frame selected, speed, modifiers), selection, transform (translate/rotate/scale, local/world,
snapping, numeric entry), object manipulation (create, duplicate, delete, parent, hide, isolate),
terrain, and asset placement.

## §11–§13 Contextual help, tooltips, two audiences

A reusable mechanism — `Help::open("rendering/hdr")`, `Help::open("world-editor/navigation")` — so
settings, materials, lighting, sequencer controls, audio, modulation and AI controls can link
straight to the right topic.

Tooltips stay short: what it does, plus a *Learn more* link for how and when. **Do not turn every
tooltip into a paragraph.**

Serve beginners (*"a parameter is a value that can be changed over time or controlled by another
signal"*) and experts (type, range, whether it supports modulation, automation, MIDI, OSC) — both
where relevant.

## §14–§21 Content areas

**§14 How AV Gen works** — `audio → analysis → signals → parameters → modulation → scene → renderer`,
explained without requiring programming knowledge, then in technical detail.

**§15 Audio** — live and file input, FFT, bands, spectral centroid and flux, onsets, beats, tempo,
confidence, offline and realtime analysis, signals. **Teach users to build effects, not merely
define terms**: *"to make a material pulse with the kick, use an onset or low-frequency signal
rather than mapping the entire spectrum."*

**§16 Modulation** — `signal → modulator → parameter → engine property`, with worked examples:
bass-reactive glow, beat-synchronised camera movement.

**§17 Sequencer** — timeline, tracks, clips, keyframes, curves, interpolation, automation, parameter
binding, camera shots, beats versus seconds, tempo sync, with concrete workflows.

**§18 Rendering** — realtime and offline, resolution, HDR, exposure, tonemapping, bloom, shadows,
reflections, volumetrics, particles, lighting, environment, post, GPU diagnostics. Explain
interactions: *"increasing emission alone does not necessarily illuminate nearby objects."*

**§19 Performance guide** — CPU versus GPU cost, draw calls, visible objects, shadows, lights,
particles, ecology, volumetrics, resolution, post, culling, LOD, asset and shader cost. **Teach
diagnosis, not "turn things off"**: baseline → inspect frame timing → isolate subsystem → disable
suspect → measure → identify bottleneck → optimise → restore quality. Document the actual profiler.

**§20 Troubleshooting** — objects disappearing, shadow popping, glow not illuminating, water looking
disconnected, FPS drops. **Only document troubleshooting that reflects actual behaviour.**

**§21 Shaders** — files, lifecycle, inputs, uniforms, material parameters, audio inputs, the shader
contract, compilation errors, debugging. **Do not invent APIs.**

## §22–§24 AI Director documentation

Explain what the AI can do, how to prompt it, what it modifies and inspects, how tool calling works,
what "working" means, how plans work, cancellation, undoing AI changes, choosing a model, where
credentials live, which operations are destructive, how the AI knows what AV Gen supports.

A practical prompting guide covering simple commands, multi-property requests, creative direction,
sequencer direction, audio direction and complex production direction — teaching that **users do not
need to know underlying parameter names.**

A technical reference of AI tool capabilities for advanced users: what each tool does and when it is
useful. Also a debugging resource.

## §25–§26 Help as an AI resource *(major architectural requirement)*

The AI Control Plane must be able to query the same documentation. **The AI should not need a
separate knowledge base** — that is how documentation drifts away from behaviour. One database, two
consumers: the human UI and AI retrieval.

A reusable internal API: `help.search(query)`, `help.get(documentId)`, `help.related(documentId)`,
`help.getShortcut(command)`, `help.getFeature(featureId)`.

## §27–§33 Versioning, UI, integration

Document version metadata so obsolete content can be distinguished later — **do not overengineer
this initially.** The panel wants search, category navigation, a document tree, article view,
back/forward/home, related topics, the shortcut and command references, and contextual links;
bookmarks, recently viewed and "copy topic ID" are useful additions. **Do not sacrifice simplicity
for feature count.**

Integrate with a command palette **if one exists**; if not, note it as future work rather than
building a command system solely for Help. Every document carries search keywords. Support images in
the content model even if the first set is text — **do not create fake screenshots.** Design the
content model so examples could eventually be applied to the project (*"Bass-Reactive Glow — Try
Example"*), but **do not implement that unless it is safe.** Provide *Learn more →* from features.

## §34 Help should never lie *(highest priority)*

**If documentation and implementation disagree, the implementation wins.** Do not preserve stale
documentation because it reads better. Inspect code, UI, command registration, settings, tool
definitions and tests, then document reality. **If uncertain, document the limitation rather than
inventing behaviour.**

## §35 Documentation validation

Lightweight validation to detect staleness: if documentation references a tool that no longer exists,
or a shortcut the command registry no longer has, flag it. **Even a development-time validation
report is valuable.**

## §36–§38 Sources, style, quick reference

Distil the repository's own material — README, architecture documents, ADRs, research notes, source
comments, tests, UI labels, command and settings definitions. **Do not expose internal engineering
documents verbatim; translate implementation concepts into usable instructions.**

Professional technical-writing style: short paragraphs, headings, bullets, numbered procedures,
tables, examples, warnings, tips, shortcut formatting. **Avoid marketing fluff, verbosity, vague
language and unexplained internal terminology.**

Provide compact reference pages. Searching *"duplicate"* should immediately give the shortcut, a
one-line description and related topics.

## §39 One knowledge layer

Help content feeds both the human UI and AI retrieval from the same source. **This prevents
duplicated knowledge.**

## §40–§43 Testing, initial content, settings and menu audits

Test document loading, IDs, search, keyword matching, navigation, related documents, shortcut and
feature lookup, missing documents, malformed documentation, stale references and AI retrieval, plus
integration tests over representative workflows.

**Do not ship an empty Help shell.** Populate it across the current major feature set.

**§42** Audit the Settings UI — every meaningful user-facing setting documented with what it
controls, units, practical effect, useful range, interactions and performance implications. **Not
every internal debug variable.**

**§43** Audit every menu. Every user-facing command should be documented, self-explanatory, or
intentionally excluded as internal. **Create a command inventory** — it also feeds future
command-palette work.

## §44–§46 World editor, sequencer and AI audits

Inventory the world editor exhaustively: mouse and keyboard controls, modifiers, gizmos, snapping,
selection, hierarchy, inspector, asset placement, terrain, water, lighting, camera, environment,
physics, navigation, animation, deletion, duplication, grouping, visibility, focus, viewport.

Audit the sequencer: every track and clip type, keyframe and timeline controls, interpolation,
automation, parameter binding, camera sequencing, audio sync, playback, editing, snapping, looping.
**Only document functionality that actually exists.**

Audit the AI system once it exists. Every AI tool should carry a description, a Help topic, examples,
limitations and related concepts, with tool metadata and documentation **linked**:
`material.set_emission → help://materials/emission`. That bridges engine capability, AI capability
and human documentation.

## §47–§49 Future assistant, native UI, accessibility

Design for an assistant that answers *"what is the difference between emission and bloom?"* from the
same Help system, and can then offer to do the thing.

**Do not build a static website inside AV Gen** — no Electron, no embedded browser, no documentation
server, no React site. AV Gen remains a native C++ application.

Keyboard navigation, readable text, sensible sizing, clear hierarchy, high-contrast controls and
predictable navigation. **Help should be usable without a mouse.**

## §50 Deliverables

Help panel; navigation; search; content system; keyboard shortcut reference; world editor quick
reference; command reference; contextual links; documentation metadata; retrieval API;
AI-accessible retrieval; initial comprehensive documentation; tests; architecture notes.

## §51–§52 Acceptance and the architectural goal

A new user should answer, without leaving the application: how to move around, duplicate an object,
make something glow, make the glow respond to bass, animate a camera, add a keyframe, make a scene
audio-reactive, render offline, why the scene is slow, what a rendering setting does, what the world
editor shortcuts are, how to configure the AI and what the AI can change. An experienced user should
be able to use Help as fast reference rather than tutorial.

The long-term shape: the engine, the Help system and the AI Control Plane sharing one documentation
database, with the human UI and AI retrieval as its two consumers, and everything reachable through
the Tool API. **The goal is a coherent relationship between a feature, its documentation, its
keyboard and command representation, its AI capability and its machine-readable metadata — so they
do not become five independent systems that slowly drift apart.**

Build Help so it can become **the knowledge layer of AV Gen**: the user learns from it, the AI learns
from it, and the application uses its metadata to keep both synchronised with the actual engine.

---

## Notes added when this was filed

**§34 and §4 collide with work in flight, and §34 wins.** When this brief arrived, five agents were
concurrently rewriting the world editor (selection, gizmos, brush, groups, undo), water, terrain,
navigation and the AI tool surface. Documenting those today would produce documentation that is
stale on arrival — exactly what §34 forbids.

Measured at that moment: **there are zero keyboard-shortcut call sites in the application.** §9's
shortcut reference and §10/§44's world-editor command reference therefore have nothing truthful to
say yet, and the editor pass is what will create them. The AI tool list (§22–§24, §46) did not exist
either.

So this work is staged deliberately:

- **Now:** the framework — content model, panel, navigation, search, the `help.*` retrieval API,
  feature metadata, contextual links, and **the §35 validation that detects drift.** Plus content for
  the parts of the engine that are stable: core concepts, how AV Gen works, projects, audio analysis,
  parameters, signals, modulation, MIDI/OSC, the sequencer (ADR-089), rendering, shaders, performance
  and troubleshooting, distilled from 57 documents and 90 ADRs.
- **After the world-authoring and AI passes land:** the world editor reference, shortcuts, the
  command inventory, water, terrain, navigation and the AI Director sections.

The validation in §35 is what makes that staging safe rather than a promise to come back: a topic
that references something the application does not have should be *flagged by a tool*, not
discovered by a user.

**A useful seed already exists.** The panel registry in `src/ui/editor_layout.cpp` already carries an
id, a label, a dock region and a one-line description per panel — §5's feature metadata in embryo,
and the right thing to extend rather than duplicate.
