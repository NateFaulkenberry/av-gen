#include "help/search.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace avgen::help {
namespace {

// The words that a documentation query is mostly made of and that tell you nothing about which
// topic is wanted. Kept short on purpose: every word removed here is a word a user typed that the
// search then ignores, and over-aggressive stopping is how "what is a signal" stops finding
// anything. Verbs that name an intent -- make, set, add, use -- are *not* here, because they do
// discriminate: "make water" and "water" want different topics.
constexpr std::array<std::string_view, 34> kStopWords{
    "a",   "an",   "and",  "are",  "as",  "at",  "be",  "but",  "by",   "can",  "do",   "doe",
    "for", "from", "how",  "i",    "if",  "in",  "is",  "it",   "my",   "of",   "on",   "or",
    "the", "to",   "what", "when", "why", "with", "you", "your", "doing", "there"};

bool isWordChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_';
}

} // namespace

bool isSearchStopWord(std::string_view foldedToken) {
    return std::ranges::find(kStopWords, foldedToken) != kStopWords.end();
}

std::string foldToken(std::string_view token) {
    std::string out;
    out.reserve(token.size());
    for (const char c : token) {
        if (isWordChar(c)) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }

    // A light suffix fold. It is not a stemmer and does not try to be linguistically right; it
    // tries to be *consistent*, because over-merging only ever costs a false positive while folding
    // a query word and a document word differently costs the hit entirely. Every rule is guarded by
    // how much word is left, which is what keeps "bass" from becoming "bas" and "ring" from
    // becoming "r".
    //
    // Applied repeatedly until it stops changing, which is the part that is easy to get wrong:
    // "settings" folds to "setting" on the first pass, and a document saying "setting" folds to
    // "sett". One pass each and they never meet.
    const auto cut = [&out](std::size_t suffix, std::size_t minimumRemaining, std::string_view replacement = {}) {
        if (out.size() < suffix + minimumRemaining) {
            return false;
        }
        out.resize(out.size() - suffix);
        out += replacement;
        return true;
    };
    const auto foldOnce = [&out, &cut] {
        const std::string_view sv = out;
        if (sv.ends_with("ies")) {
            return cut(3, 3, "y"); // frequencies -> frequency, bodies -> body
        }
        if (sv.ends_with("sses")) {
            return cut(2, 4); // passes -> pass
        }
        if (sv.ends_with("shes") || sv.ends_with("ches") || sv.ends_with("xes") || sv.ends_with("zes")) {
            return cut(2, 3); // matches -> match, boxes -> box
        }
        if (sv.ends_with("ss")) {
            return false; // bass, class, gloss, less: never touched, and this must precede the "s" rule
        }
        if (sv.ends_with("s")) {
            return cut(1, 3); // shadows -> shadow, bands -> band, keys -> key
        }
        if (sv.ends_with("ing")) {
            return cut(3, 3); // rendering -> render; ring and using are left alone
        }
        if (sv.ends_with("ed")) {
            return cut(2, 4); // rendered -> render; used, based and speed are left alone
        }
        return false;
    };
    for (int pass = 0; pass < 3 && foldOnce(); ++pass) {
    }
    return out;
}

std::vector<std::string> tokenize(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (!isWordChar(text[i])) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < text.size() && isWordChar(text[i])) {
            ++i;
        }
        std::string folded = foldToken(text.substr(start, i - start));
        if (!folded.empty()) {
            out.push_back(std::move(folded));
        }
    }
    return out;
}

std::vector<std::string> queryTerms(std::string_view query) {
    std::vector<std::string> all = tokenize(query);
    std::vector<std::string> kept;
    for (const std::string& term : all) {
        if (!isSearchStopWord(term)) {
            kept.push_back(term);
        }
    }
    // Every word was scaffolding. Search for the scaffolding rather than for nothing: a user who
    // typed "what is it" deserves a result list, even a poor one, over an empty panel.
    return kept.empty() ? all : kept;
}

void KeywordSearch::index(std::uint32_t document, std::string_view text, float weight) {
    for (const std::string& token : tokenize(text)) {
        std::vector<Posting>& postings = postings_[token];
        if (!postings.empty() && postings.back().document == document) {
            postings.back().weight += weight;
        } else {
            postings.push_back({document, weight});
        }
    }
}

void KeywordSearch::build(const std::vector<HelpDocument>& documents) {
    documents_ = documents;
    postings_.clear();
    foldedTitles_.clear();
    foldedTitles_.reserve(documents_.size());

    for (std::uint32_t i = 0; i < documents_.size(); ++i) {
        const HelpDocument& doc = documents_[i];
        index(i, doc.title, weights_.title);
        index(i, doc.id, weights_.identifier);
        index(i, doc.summary, weights_.summary);
        for (const std::string& tag : doc.tags) {
            index(i, tag, weights_.tag);
        }
        for (const std::string& keyword : doc.keywords) {
            index(i, keyword, weights_.keyword);
        }
        for (const HelpBlock& block : doc.body) {
            if (block.kind == BlockKind::Heading) {
                index(i, block.text, weights_.heading);
            }
        }
        // The body once, as written. Headings are therefore counted twice -- once at heading
        // weight above and once at body weight here -- which is the intent.
        index(i, doc.markdown, weights_.body);

        std::string folded;
        for (const std::string& token : tokenize(doc.title)) {
            if (!folded.empty()) {
                folded.push_back(' ');
            }
            folded += token;
        }
        foldedTitles_.push_back(std::move(folded));
    }
}

float KeywordSearch::inverseDocumentFrequency(const std::vector<Posting>& postings) const {
    const auto total = static_cast<float>(std::max<std::size_t>(documents_.size(), 1));
    const auto hits = static_cast<float>(std::max<std::size_t>(postings.size(), 1));
    return std::log(1.0f + total / hits);
}

std::vector<const std::vector<KeywordSearch::Posting>*> KeywordSearch::prefixPostings(std::string_view prefix) const {
    std::vector<const std::vector<Posting>*> out;
    if (prefix.size() < 3) {
        return out;
    }
    for (const auto& [term, postings] : postings_) {
        if (term.size() > prefix.size() && std::string_view(term).starts_with(prefix)) {
            out.push_back(&postings);
            if (out.size() >= 24) { // a prefix that matches half the vocabulary is not a search
                break;
            }
        }
    }
    return out;
}

std::vector<SearchResult> KeywordSearch::search(std::string_view query, const SearchOptions& options) const {
    const std::vector<std::string> terms = queryTerms(query);
    if (terms.empty() || documents_.empty()) {
        return {};
    }

    std::unordered_map<std::uint32_t, Candidate> candidates;
    const auto credit = [&candidates](std::uint32_t doc, float score, const std::string& term) {
        Candidate& c = candidates[doc];
        c.score += score;
        if (std::ranges::find(c.terms, term) == c.terms.end()) {
            c.terms.push_back(term);
        }
    };

    for (std::size_t t = 0; t < terms.size(); ++t) {
        const std::string& term = terms[t];
        if (const auto it = postings_.find(term); it != postings_.end()) {
            const float idf = inverseDocumentFrequency(it->second);
            for (const Posting& p : it->second) {
                // Saturating weight: the tenth mention of "water" in a topic about water says
                // little the first three did not, and without this the longest topic always wins.
                credit(p.document, idf * (p.weight / (p.weight + 6.0f)) * 10.0f, term);
            }
        }
        // The last word of a query is the one still being typed, so it gets prefix expansion.
        // Earlier words do not: expanding them turns "render setting" into a match on every topic
        // containing "rendered", "renderer" and "settings" at once.
        if (t + 1 == terms.size()) {
            for (const std::vector<Posting>* postings : prefixPostings(term)) {
                const float idf = inverseDocumentFrequency(*postings);
                for (const Posting& p : *postings) {
                    credit(p.document, 0.35f * idf * (p.weight / (p.weight + 6.0f)) * 10.0f, term);
                }
            }
        }
    }

    // A topic that matched every word of the query beats one that matched a single word many
    // times. Without this "audio analysis" ranks a topic that says "audio" forty times above the
    // one actually called Audio Analysis.
    std::string foldedQuery;
    for (const std::string& term : terms) {
        if (!foldedQuery.empty()) {
            foldedQuery.push_back(' ');
        }
        foldedQuery += term;
    }

    std::vector<SearchResult> results;
    results.reserve(candidates.size());
    for (const auto& [docIndex, candidate] : candidates) {
        const HelpDocument& doc = documents_[docIndex];
        if (!options.category.empty() && doc.category != options.category) {
            continue;
        }
        if (!options.includeGaps && doc.status == HelpStatus::NotYetDocumented) {
            continue;
        }
        float score = candidate.score;
        const float coverage = static_cast<float>(candidate.terms.size()) / static_cast<float>(terms.size());
        score *= 0.5f + 0.5f * coverage;
        if (candidate.terms.size() == terms.size() && terms.size() > 1) {
            score *= 1.5f;
        }
        const std::string& title = foldedTitles_[docIndex];
        if (title == foldedQuery) {
            score *= 3.0f;
        } else if (!foldedQuery.empty() && title.find(foldedQuery) != std::string::npos) {
            score *= 1.8f;
        }
        if (doc.status == HelpStatus::NotYetDocumented) {
            score *= weights_.gapPenalty;
        }
        if (score < options.minimumScore) {
            continue;
        }

        SearchResult result;
        result.documentId = doc.id;
        result.title = doc.title;
        result.category = doc.category;
        result.summary = doc.summary;
        result.matchedTerms = candidate.terms;
        result.status = doc.status;
        result.score = score;
        result.excerpt = excerptFor(doc, candidate.terms);
        results.push_back(std::move(result));
    }

    std::ranges::sort(results, [](const SearchResult& a, const SearchResult& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.documentId < b.documentId; // a stable order, so a run is reproducible
    });
    if (results.size() > options.limit) {
        results.resize(options.limit);
    }
    return results;
}

std::string excerptFor(const HelpDocument& doc, const std::vector<std::string>& terms, std::size_t budget) {
    if (terms.empty()) {
        return {};
    }
    const std::unordered_set<std::string> wanted(terms.begin(), terms.end());
    for (const HelpBlock& block : doc.body) {
        if (block.kind != BlockKind::Paragraph && block.kind != BlockKind::Bullet &&
            block.kind != BlockKind::Numbered && block.kind != BlockKind::Callout) {
            continue;
        }
        const std::string line = flattenSpans(block.spans);
        if (line.empty()) {
            continue;
        }
        std::size_t hitAt = std::string::npos;
        std::size_t i = 0;
        while (i < line.size() && hitAt == std::string::npos) {
            if (!isWordChar(line[i])) {
                ++i;
                continue;
            }
            const std::size_t start = i;
            while (i < line.size() && isWordChar(line[i])) {
                ++i;
            }
            if (wanted.contains(foldToken(std::string_view(line).substr(start, i - start)))) {
                hitAt = start;
            }
        }
        if (hitAt == std::string::npos) {
            continue;
        }
        if (line.size() <= budget) {
            return line;
        }
        const std::size_t half = budget / 2;
        std::size_t from = hitAt > half ? hitAt - half : 0;
        while (from > 0 && isWordChar(line[from])) { // start on a word boundary, not mid-word
            --from;
        }
        // ...and on a character boundary. These offsets are bytes, the prose is UTF-8, and an em
        // dash is three of them: cutting between two of those three produces a string that is not
        // valid UTF-8, which the JSON serialiser refuses outright. A search result must not be able
        // to take the retrieval API down because of where a dash happened to fall.
        const auto isContinuation = [&line](std::size_t i) {
            return (static_cast<unsigned char>(line[i]) & 0xC0u) == 0x80u;
        };
        while (from > 0 && from < line.size() && isContinuation(from)) {
            --from;
        }
        std::size_t end = std::min(from + budget, line.size());
        while (end > from && end < line.size() && isContinuation(end)) {
            --end;
        }
        std::string out = line.substr(from, end - from);
        if (from > 0) {
            out.insert(0, "...");
        }
        if (end < line.size()) {
            out += "...";
        }
        return out;
    }
    return {};
}

} // namespace avgen::help
