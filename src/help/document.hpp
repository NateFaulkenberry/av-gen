#pragma once

// The Help content model (spec §6): what a documentation topic *is*, once it has been read off
// disk and before anything draws it.
//
// The shape of this type is the whole argument for the design. A topic is not a string of ImGui
// calls and it is not a blob of markup the renderer re-parses every frame: it is metadata plus a
// list of blocks. Metadata is what search, the retrieval API and the validator work on; blocks are
// what the panel draws. Writing a new topic therefore means adding a file, never editing UI code
// -- which is the requirement §6 actually states, and the one that decides whether documentation
// stays current after the people who wrote it have moved on.
//
// The raw markdown survives alongside the parsed blocks on purpose. The panel wants blocks; an AI
// asking `help.get` wants the text it would have read itself, not a tree it has to flatten. Both
// consumers get the form they want out of one stored document (§25, §39).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::help {

// A run of text inside a paragraph, heading, list item, callout or table cell. Deliberately few
// styles: the ones a technical document needs and an ImGui renderer can honour without a layout
// engine. Anything richer belongs in a browser, and §48 says there isn't one.
struct InlineSpan {
    enum class Style : std::uint8_t {
        Text,     // plain
        Code,     // `identifier`
        Strong,   // **emphasis**
        Emphasis, // *emphasis*
        Link,     // [label](target)
    };

    Style style = Style::Text;
    std::string text;
    // Link only. A `help://audio/analysis` or bare `audio/analysis` target is stored as the
    // document id, so the panel navigates rather than shelling out and the validator can check the
    // id exists. Anything else is kept verbatim and shown but not followed.
    std::string target;
    // Link only: true when `target` names a Help document rather than something external.
    bool internal = false;
};

enum class BlockKind : std::uint8_t {
    Paragraph,
    Heading,  // level 1..4, with an anchor slug for deep links
    Bullet,   // one item; `level` is the indent depth, runs of them form a list
    Numbered, // one item; `level` is the indent depth
    Code,     // `text` holds the body verbatim, `language` the fence tag
    Table,    // `rows`, the first of which is the header
    Callout,  // Note / Tip / Warning
    Image,    // `target` is the path, `text` the alt text, `spans` the caption (§31: none ship yet)
    Rule,
};

enum class CalloutKind : std::uint8_t { Note, Tip, Warning };

struct HelpTableRow {
    std::vector<std::vector<InlineSpan>> cells;
};

struct HelpBlock {
    BlockKind kind = BlockKind::Paragraph;
    int level = 0; // Heading: 1..4. Bullet/Numbered: indent depth from 0.
    CalloutKind callout = CalloutKind::Note;
    std::string anchor;   // Heading only
    std::string language; // Code only
    std::string text;     // Code body, or Image alt text
    std::string target;   // Image path
    std::vector<InlineSpan> spans;
    std::vector<HelpTableRow> rows; // Table only; rows.front() is the header
};

// How far a topic is to be trusted. The distinction exists because of §34: a Help system that is
// allowed to say nothing is safer than one that is obliged to say something, and the staging this
// pass works under needs somewhere honest to record "this area is not written yet".
//
// The validator reports every Partial and NotYetDocumented topic as a known gap, which is how the
// gap list becomes a tool's output rather than a promise in a commit message.
enum class HelpStatus : std::uint8_t {
    Stable,           // describes shipped behaviour, checked against the implementation
    Partial,          // true as far as it goes, and says where it stops
    NotYetDocumented, // a placeholder that names the gap and claims nothing about behaviour
};

[[nodiscard]] std::string_view helpStatusName(HelpStatus status);
[[nodiscard]] bool parseHelpStatus(std::string_view name, HelpStatus& out);

// §13: the same subject serves someone meeting it for the first time and someone checking a range.
// A topic declares who it is for so the panel can mark it and search can prefer one; most topics
// are Everyone, and say the beginner's version first and the expert's second.
enum class HelpAudience : std::uint8_t { Everyone, Beginner, Expert };

[[nodiscard]] std::string_view helpAudienceName(HelpAudience audience);
[[nodiscard]] bool parseHelpAudience(std::string_view name, HelpAudience& out);

struct HelpDocument {
    std::string id;       // "audio/analysis" -- stable, quotable, and what help://... refers to
    std::string title;    // "Audio Analysis"
    std::string category; // "Audio" -- the navigation group (§3)
    std::string summary;  // one sentence, shown in search results and on category pages
    int order = 100;      // position within the category; ties break on title
    HelpStatus status = HelpStatus::Stable;
    HelpAudience audience = HelpAudience::Everyone;
    int version = 1; // §27: content version, so obsolete topics can be told apart later

    std::vector<std::string> tags;       // short subject words
    std::vector<std::string> keywords;   // §30: natural phrasings a user might actually type
    std::vector<std::string> related;    // document ids (§28 related topics)
    std::vector<std::string> features;   // feature ids this topic documents (§5)
    std::vector<std::string> shortcuts;  // command ids whose keys this topic documents (§9)
    std::vector<std::string> parameters; // parameter paths cited, so the validator can check them

    std::vector<HelpBlock> body; // what the panel draws
    std::string markdown;        // what an AI reads, and what the file said

    std::string sourceFile; // absolute path it was loaded from; empty for a built-in

    [[nodiscard]] bool empty() const { return body.empty(); }
};

// Heading text -> the slug used for in-page anchors and deep links: lowercased, runs of
// non-alphanumerics collapsed to single hyphens, ends trimmed.
[[nodiscard]] std::string slugify(std::string_view text);

// The spans of a block joined back into plain text, for search excerpts and for anything that
// wants a line rather than a tree.
[[nodiscard]] std::string flattenSpans(const std::vector<InlineSpan>& spans);

} // namespace avgen::help
