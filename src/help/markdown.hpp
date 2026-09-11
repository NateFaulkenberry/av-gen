#pragma once

// Reading a Help topic off disk: front matter, then a deliberately small subset of markdown.
//
// "Markdown-like source files are acceptable" (§6), and a subset is the right amount of format.
// The alternative -- a full markdown implementation -- would buy inline HTML and reference links
// that no topic needs, and cost a dependency and a class of rendering bugs in a panel that has to
// draw the result with ImGui primitives. What is here is what technical documentation uses:
// headings, paragraphs, lists, tables, code, callouts, links and a rule.
//
// The parser is strict about metadata and forgiving about prose. A file missing an id, a title or
// a category is an error, because those three are what navigation, search and every cross-reference
// are keyed on and a topic without them fails silently later. A malformed emphasis marker inside a
// sentence is not an error: it is shown as the characters the author typed.

#include "core/error.hpp"
#include "help/document.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace avgen::help {

// Parses one topic. `sourceName` appears in error messages and in `HelpDocument::sourceFile`.
//
// The format:
//
//   ---
//   id: audio/analysis          (required)
//   title: Audio Analysis       (required)
//   category: Audio             (required)
//   summary: one sentence
//   order: 20
//   status: stable | partial | not-yet-documented
//   audience: everyone | beginner | expert
//   version: 1
//   tags: fft, bands, onset
//   keywords: how do i react to the kick; make something pulse with the bass
//   related: audio/input, modulation/signals
//   features: panel.analysis
//   shortcuts: transport.play
//   parameters: response/bassGain
//   ---
//
//   # A heading
//   Prose, `code`, **strong**, *emphasis* and [links](help://audio/input).
//
// `keywords` splits on semicolons, because a natural phrasing contains commas; everything else
// splits on commas. Both accept an optional `[ ... ]` wrapper.
[[nodiscard]] Result<HelpDocument> parseHelpMarkdown(std::string_view text, std::string_view sourceName);

// The body half alone, for content assembled in code (tests, built-in fallbacks) where the
// metadata is set directly.
[[nodiscard]] std::vector<HelpBlock> parseHelpBody(std::string_view markdown);

// One line of prose into styled runs. Exposed because summaries and tooltips want it too.
[[nodiscard]] std::vector<InlineSpan> parseInline(std::string_view text);

// A link target as written -> the document id it names, or empty when it names something else.
// Accepts `help://id`, `help://id#anchor` and a bare `id`. The anchor is dropped.
[[nodiscard]] std::string documentIdFromLink(std::string_view target);

} // namespace avgen::help
