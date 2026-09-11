#include "help/markdown.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace avgen::help {
namespace {

constexpr std::string_view kWhitespace = " \t\r\n";

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(kWhitespace);
    return s.substr(first, last - first + 1);
}

std::string_view trimRight(std::string_view s) {
    const auto last = s.find_last_not_of(kWhitespace);
    return last == std::string_view::npos ? std::string_view{} : s.substr(0, last + 1);
}

// Lines, with the terminator removed and a trailing \r tolerated so a file written on Windows
// parses the same as one written here.
std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto nl = text.find('\n', start);
        const auto end = nl == std::string_view::npos ? text.size() : nl;
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        if (nl == std::string_view::npos) {
            break;
        }
        start = nl + 1;
    }
    return lines;
}

std::vector<std::string> splitList(std::string_view value, char separator) {
    std::string_view v = trim(value);
    if (v.size() >= 2 && v.front() == '[' && v.back() == ']') {
        v = trim(v.substr(1, v.size() - 2));
    }
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= v.size()) {
        const auto sep = v.find(separator, start);
        const auto end = sep == std::string_view::npos ? v.size() : sep;
        const std::string_view item = trim(v.substr(start, end - start));
        if (!item.empty()) {
            out.emplace_back(item);
        }
        if (sep == std::string_view::npos) {
            break;
        }
        start = sep + 1;
    }
    return out;
}

int parseInt(std::string_view value, int fallback) {
    const std::string_view v = trim(value);
    int result = fallback;
    const auto* begin = v.data();
    const auto* end = begin + v.size();
    if (std::from_chars(begin, end, result).ec != std::errc{}) {
        return fallback;
    }
    return result;
}

std::size_t indentOf(std::string_view line) {
    std::size_t n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) {
        n += line[n] == '\t' ? 4 : 1;
    }
    return n;
}

bool isRule(std::string_view line) {
    const std::string_view t = trim(line);
    if (t.size() < 3) {
        return false;
    }
    const char c = t.front();
    if (c != '-' && c != '_' && c != '*') {
        return false;
    }
    return std::ranges::all_of(t, [c](char ch) { return ch == c; });
}

bool isTableRow(std::string_view line) {
    const std::string_view t = trim(line);
    return t.size() >= 2 && t.front() == '|';
}

// `|---|:--:|` -- the row that says the one above it was the header.
bool isTableDivider(std::string_view line) {
    const std::string_view t = trim(line);
    if (!isTableRow(t)) {
        return false;
    }
    bool sawDash = false;
    for (const char c : t) {
        if (c == '-') {
            sawDash = true;
        } else if (c != '|' && c != ':' && c != ' ') {
            return false;
        }
    }
    return sawDash;
}

std::vector<std::string_view> tableCells(std::string_view line) {
    std::string_view t = trim(line);
    if (!t.empty() && t.front() == '|') {
        t.remove_prefix(1);
    }
    if (!t.empty() && t.back() == '|') {
        t.remove_suffix(1);
    }
    std::vector<std::string_view> cells;
    std::size_t start = 0;
    while (start <= t.size()) {
        const auto bar = t.find('|', start);
        const auto end = bar == std::string_view::npos ? t.size() : bar;
        cells.push_back(trim(t.substr(start, end - start)));
        if (bar == std::string_view::npos) {
            break;
        }
        start = bar + 1;
    }
    return cells;
}

// `- ` / `* ` / `+ ` -> a bullet; `1. ` / `12) ` -> a numbered item. Returns the content after the
// marker, and reports which it was.
bool listMarker(std::string_view line, bool& numbered, std::string_view& content) {
    std::string_view t = line;
    const std::size_t indent = indentOf(t);
    t.remove_prefix(std::min(indent, t.size()));
    if (t.size() >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') && t[1] == ' ') {
        numbered = false;
        content = trim(t.substr(2));
        return true;
    }
    std::size_t digits = 0;
    while (digits < t.size() && std::isdigit(static_cast<unsigned char>(t[digits])) != 0) {
        ++digits;
    }
    if (digits > 0 && digits + 1 < t.size() && (t[digits] == '.' || t[digits] == ')') && t[digits + 1] == ' ') {
        numbered = true;
        content = trim(t.substr(digits + 2));
        return true;
    }
    return false;
}

// `> [!TIP]` and friends. A plain `>` quote becomes a Note, which is what a quote in a technical
// document almost always is.
bool calloutMarker(std::string_view content, CalloutKind& kind, std::string_view& rest) {
    const std::string_view t = trim(content);
    const auto match = [&](std::string_view tag, CalloutKind k) {
        if (t.size() >= tag.size()) {
            std::string upper;
            upper.reserve(tag.size());
            for (std::size_t i = 0; i < tag.size(); ++i) {
                upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(t[i]))));
            }
            if (upper == tag) {
                kind = k;
                rest = trim(t.substr(tag.size()));
                return true;
            }
        }
        return false;
    };
    return match("[!TIP]", CalloutKind::Tip) || match("[!WARNING]", CalloutKind::Warning) ||
           match("[!CAUTION]", CalloutKind::Warning) || match("[!IMPORTANT]", CalloutKind::Warning) ||
           match("[!NOTE]", CalloutKind::Note);
}

// `![alt](path)` or `![alt](path "caption")`, alone on a line.
bool imageLine(std::string_view line, std::string& alt, std::string& path, std::string& caption) {
    const std::string_view t = trim(line);
    if (t.size() < 5 || t[0] != '!' || t[1] != '[') {
        return false;
    }
    const auto close = t.find(']', 2);
    if (close == std::string_view::npos || close + 1 >= t.size() || t[close + 1] != '(' || t.back() != ')') {
        return false;
    }
    alt = std::string(t.substr(2, close - 2));
    std::string_view inner = trim(t.substr(close + 2, t.size() - close - 3));
    const auto quote = inner.find('"');
    if (quote != std::string_view::npos && inner.back() == '"') {
        caption = std::string(inner.substr(quote + 1, inner.size() - quote - 2));
        inner = trimRight(inner.substr(0, quote));
    } else {
        caption.clear();
    }
    path = std::string(trim(inner));
    return !path.empty();
}

void pushSpan(std::vector<InlineSpan>& out, InlineSpan::Style style, std::string text, std::string target = {},
              bool internal = false) {
    if (text.empty() && style != InlineSpan::Style::Link) {
        return;
    }
    if (style == InlineSpan::Style::Text && !out.empty() && out.back().style == InlineSpan::Style::Text) {
        out.back().text += text; // keep adjacent plain runs as one span
        return;
    }
    out.push_back({style, std::move(text), std::move(target), internal});
}

} // namespace

std::string documentIdFromLink(std::string_view target) {
    std::string_view t = trim(target);
    constexpr std::string_view kScheme = "help://";
    if (t.starts_with(kScheme)) {
        t.remove_prefix(kScheme.size());
    } else if (t.find("://") != std::string_view::npos || t.starts_with("mailto:") || t.starts_with("#")) {
        return {};
    }
    if (const auto hash = t.find('#'); hash != std::string_view::npos) {
        t = t.substr(0, hash);
    }
    t = trim(t);
    // A document id is a path of lowercase words: anything with a space or a dot is a file or a
    // sentence, not a topic, and is better shown than followed.
    if (t.empty() || t.find(' ') != std::string_view::npos || t.find('.') != std::string_view::npos) {
        return {};
    }
    return std::string(t);
}

std::vector<InlineSpan> parseInline(std::string_view text) {
    std::vector<InlineSpan> spans;
    std::string plain;
    const auto flush = [&] {
        if (!plain.empty()) {
            pushSpan(spans, InlineSpan::Style::Text, plain);
            plain.clear();
        }
    };

    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];

        if (c == '\\' && i + 1 < text.size()) { // \* and \` escape the marker that follows
            plain.push_back(text[i + 1]);
            i += 2;
            continue;
        }

        if (c == '`') {
            const auto end = text.find('`', i + 1);
            if (end != std::string_view::npos && end > i + 1) {
                flush();
                pushSpan(spans, InlineSpan::Style::Code, std::string(text.substr(i + 1, end - i - 1)));
                i = end + 1;
                continue;
            }
        }

        if (c == '*' && i + 1 < text.size() && text[i + 1] == '*') {
            const auto end = text.find("**", i + 2);
            if (end != std::string_view::npos && end > i + 2) {
                flush();
                pushSpan(spans, InlineSpan::Style::Strong, std::string(text.substr(i + 2, end - i - 2)));
                i = end + 2;
                continue;
            }
        }

        if (c == '*') {
            const auto end = text.find('*', i + 1);
            if (end != std::string_view::npos && end > i + 1) {
                flush();
                pushSpan(spans, InlineSpan::Style::Emphasis, std::string(text.substr(i + 1, end - i - 1)));
                i = end + 1;
                continue;
            }
        }

        if (c == '[') {
            const auto close = text.find(']', i + 1);
            if (close != std::string_view::npos && close + 1 < text.size() && text[close + 1] == '(') {
                const auto paren = text.find(')', close + 2);
                if (paren != std::string_view::npos) {
                    const std::string_view label = text.substr(i + 1, close - i - 1);
                    const std::string_view target = text.substr(close + 2, paren - close - 2);
                    const std::string id = documentIdFromLink(target);
                    flush();
                    pushSpan(spans, InlineSpan::Style::Link, std::string(label),
                             id.empty() ? std::string(trim(target)) : id, !id.empty());
                    i = paren + 1;
                    continue;
                }
            }
        }

        plain.push_back(c);
        ++i;
    }
    flush();
    return spans;
}

std::vector<HelpBlock> parseHelpBody(std::string_view markdown) {
    std::vector<HelpBlock> blocks;
    const std::vector<std::string_view> lines = splitLines(markdown);

    std::string paragraph;
    const auto flushParagraph = [&] {
        if (paragraph.empty()) {
            return;
        }
        HelpBlock block;
        block.kind = BlockKind::Paragraph;
        block.spans = parseInline(paragraph);
        blocks.push_back(std::move(block));
        paragraph.clear();
    };

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view raw = lines[i];
        const std::string_view line = trimRight(raw);

        if (trim(line).empty()) {
            flushParagraph();
            continue;
        }

        // Fenced code. An unterminated fence takes the rest of the file rather than erroring: the
        // text is still the best thing to show, and refusing to load a whole topic over a missing
        // fence is a worse outcome than a long code block.
        if (const std::string_view t = trim(line); t.starts_with("```")) {
            flushParagraph();
            HelpBlock block;
            block.kind = BlockKind::Code;
            block.language = std::string(trim(t.substr(3)));
            std::string body;
            ++i;
            for (; i < lines.size(); ++i) {
                if (trim(lines[i]).starts_with("```")) {
                    break;
                }
                body += lines[i];
                body.push_back('\n');
            }
            if (!body.empty() && body.back() == '\n') {
                body.pop_back();
            }
            block.text = std::move(body);
            blocks.push_back(std::move(block));
            continue;
        }

        if (isRule(line)) {
            flushParagraph();
            HelpBlock block;
            block.kind = BlockKind::Rule;
            blocks.push_back(std::move(block));
            continue;
        }

        if (const std::string_view t = trim(line); t.starts_with("#")) {
            std::size_t level = 0;
            while (level < t.size() && t[level] == '#') {
                ++level;
            }
            if (level <= 4 && level < t.size() && t[level] == ' ') {
                flushParagraph();
                HelpBlock block;
                block.kind = BlockKind::Heading;
                block.level = static_cast<int>(level);
                const std::string_view heading = trim(t.substr(level + 1));
                block.spans = parseInline(heading);
                block.text = std::string(heading);
                block.anchor = slugify(heading);
                blocks.push_back(std::move(block));
                continue;
            }
        }

        {
            std::string alt;
            std::string path;
            std::string caption;
            if (imageLine(line, alt, path, caption)) {
                flushParagraph();
                HelpBlock block;
                block.kind = BlockKind::Image;
                block.text = std::move(alt);
                block.target = std::move(path);
                if (!caption.empty()) {
                    block.spans = parseInline(caption);
                }
                blocks.push_back(std::move(block));
                continue;
            }
        }

        if (isTableRow(line)) {
            flushParagraph();
            HelpBlock block;
            block.kind = BlockKind::Table;
            for (; i < lines.size() && isTableRow(lines[i]); ++i) {
                if (isTableDivider(lines[i])) {
                    continue; // the row above was the header; nothing else to record
                }
                HelpTableRow row;
                for (const std::string_view cell : tableCells(lines[i])) {
                    row.cells.push_back(parseInline(cell));
                }
                block.rows.push_back(std::move(row));
            }
            --i; // the loop's ++i consumes the line that ended the table
            if (!block.rows.empty()) {
                blocks.push_back(std::move(block));
            }
            continue;
        }

        if (const std::string_view t = trim(line); t.starts_with(">")) {
            flushParagraph();
            HelpBlock block;
            block.kind = BlockKind::Callout;
            block.callout = CalloutKind::Note;
            std::string body;
            bool first = true;
            for (; i < lines.size(); ++i) {
                const std::string_view q = trim(lines[i]);
                if (!q.starts_with(">")) {
                    break;
                }
                std::string_view content = trim(q.substr(1));
                if (first) {
                    first = false;
                    CalloutKind kind = CalloutKind::Note;
                    std::string_view rest;
                    if (calloutMarker(content, kind, rest)) {
                        block.callout = kind;
                        content = rest;
                    }
                }
                if (content.empty()) {
                    continue;
                }
                if (!body.empty()) {
                    body.push_back(' ');
                }
                body += content;
            }
            --i;
            block.spans = parseInline(body);
            blocks.push_back(std::move(block));
            continue;
        }

        {
            bool numbered = false;
            std::string_view content;
            if (listMarker(line, numbered, content)) {
                flushParagraph();
                HelpBlock block;
                block.kind = numbered ? BlockKind::Numbered : BlockKind::Bullet;
                block.level = static_cast<int>(indentOf(line) / 2);
                // A wrapped list item: following lines indented past the marker and carrying no
                // marker of their own belong to this item.
                std::string body(content);
                while (i + 1 < lines.size()) {
                    const std::string_view next = trimRight(lines[i + 1]);
                    bool nextNumbered = false;
                    std::string_view nextContent;
                    if (trim(next).empty() || indentOf(next) <= indentOf(line) ||
                        listMarker(next, nextNumbered, nextContent)) {
                        break;
                    }
                    body.push_back(' ');
                    body += trim(next);
                    ++i;
                }
                block.spans = parseInline(body);
                blocks.push_back(std::move(block));
                continue;
            }
        }

        if (!paragraph.empty()) {
            paragraph.push_back(' ');
        }
        paragraph += trim(line);
    }
    flushParagraph();
    return blocks;
}

Result<HelpDocument> parseHelpMarkdown(std::string_view text, std::string_view sourceName) {
    const std::vector<std::string_view> lines = splitLines(text);
    std::size_t cursor = 0;
    while (cursor < lines.size() && trim(lines[cursor]).empty()) {
        ++cursor;
    }
    if (cursor >= lines.size() || trim(lines[cursor]) != "---") {
        return fail("{}: a Help topic must begin with a '---' front-matter block", sourceName);
    }
    ++cursor;

    HelpDocument doc;
    doc.sourceFile = std::string(sourceName);
    bool closed = false;
    for (; cursor < lines.size(); ++cursor) {
        const std::string_view line = lines[cursor];
        if (trim(line) == "---") {
            closed = true;
            ++cursor;
            break;
        }
        if (trim(line).empty()) {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            return fail("{}: front matter line {} is not 'key: value' -- '{}'", sourceName, cursor + 1,
                        std::string(trim(line)));
        }
        const std::string_view key = trim(line.substr(0, colon));
        const std::string_view value = trim(line.substr(colon + 1));

        if (key == "id") {
            doc.id = std::string(value);
        } else if (key == "title") {
            doc.title = std::string(value);
        } else if (key == "category") {
            doc.category = std::string(value);
        } else if (key == "summary") {
            doc.summary = std::string(value);
        } else if (key == "order") {
            doc.order = parseInt(value, doc.order);
        } else if (key == "version") {
            doc.version = parseInt(value, doc.version);
        } else if (key == "status") {
            if (!parseHelpStatus(value, doc.status)) {
                return fail("{}: unknown status '{}' (stable, partial or not-yet-documented)", sourceName,
                            std::string(value));
            }
        } else if (key == "audience") {
            if (!parseHelpAudience(value, doc.audience)) {
                return fail("{}: unknown audience '{}' (everyone, beginner or expert)", sourceName,
                            std::string(value));
            }
        } else if (key == "tags") {
            doc.tags = splitList(value, ',');
        } else if (key == "keywords") {
            // Semicolons, because a natural phrasing contains commas and splitting one on commas
            // turns "how do I make water look better, and cheaper?" into two useless fragments.
            doc.keywords = splitList(value, ';');
        } else if (key == "related") {
            doc.related = splitList(value, ',');
        } else if (key == "features") {
            doc.features = splitList(value, ',');
        } else if (key == "shortcuts") {
            doc.shortcuts = splitList(value, ',');
        } else if (key == "parameters") {
            doc.parameters = splitList(value, ',');
        } else {
            return fail("{}: unknown front-matter key '{}'", sourceName, std::string(key));
        }
    }
    if (!closed) {
        return fail("{}: the front-matter block is not closed with '---'", sourceName);
    }
    if (doc.id.empty()) {
        return fail("{}: front matter needs an 'id'", sourceName);
    }
    if (doc.title.empty()) {
        return fail("{}: '{}' needs a 'title'", sourceName, doc.id);
    }
    if (doc.category.empty()) {
        return fail("{}: '{}' needs a 'category'", sourceName, doc.id);
    }

    std::string body;
    for (std::size_t i = cursor; i < lines.size(); ++i) {
        body += lines[i];
        body.push_back('\n');
    }
    doc.markdown = std::string(trim(body));
    doc.body = parseHelpBody(doc.markdown);
    return doc;
}

} // namespace avgen::help
