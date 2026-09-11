#pragma once

// Help search (§7, §8, §30).
//
// Two requirements pull in different directions. Search has to be fast and offline, which says
// keyword index; and it has to cope with "how do I make water look better?", which is a sentence,
// not a keyword. The answer is not to be clever: strip the question scaffolding, fold the words to
// a common form, score what remains against weighted fields, and let authors add the phrasings they
// expect in the `keywords` front matter when the prose does not contain them.
//
// §8 asks for the abstraction that lets semantic search replace this later, so the ranking sits
// behind `ISearchBackend` and `HelpDatabase` never names the keyword implementation. An embedding
// backend would implement the same interface over the same documents; nothing above it changes.

#include "help/document.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace avgen::help {

struct SearchOptions {
    std::size_t limit = 20;
    std::string category;      // restrict to one category; empty means all
    bool includeGaps = true;   // include topics whose status is not-yet-documented
    float minimumScore = 0.0f; // drop hits below this; 0 keeps everything that matched at all
};

struct SearchResult {
    std::string documentId;
    std::string title;
    std::string category;
    std::string summary;
    // §7: the terms that actually matched, so a result can explain itself rather than appearing by
    // magic. These are the folded forms, which is what matched.
    std::vector<std::string> matchedTerms;
    std::string excerpt; // a line of body text around the strongest match; may be empty
    float score = 0.0f;
    HelpStatus status = HelpStatus::Stable;
};

// Lowercased, punctuation dropped, and a light suffix fold so "reflections" finds "reflection" and
// "rendering" finds "render". Not a stemmer: it stops well short of anything that would collapse
// two words a reader would distinguish.
[[nodiscard]] std::string foldToken(std::string_view token);

// The words of a phrase, folded. Question scaffolding ("how do I ...") is *not* removed here --
// that is a query concern, and indexing has no reason to throw words away.
[[nodiscard]] std::vector<std::string> tokenize(std::string_view text);

// The query's meaningful words. Drops the words that carry no signal in a documentation search
// ("how", "do", "i", "the", "my"), unless they are all there is, in which case they are kept:
// a search for "how" should find something rather than nothing.
[[nodiscard]] std::vector<std::string> queryTerms(std::string_view query);

[[nodiscard]] bool isSearchStopWord(std::string_view foldedToken);

// The ranking strategy. Swapping in a semantic backend means implementing this and handing it to
// HelpDatabase; nothing above the interface knows which one it has.
class ISearchBackend {
public:
    virtual ~ISearchBackend() = default;
    [[nodiscard]] virtual std::string_view name() const = 0;
    // Rebuilt whenever the document set changes. `documents` outlives every call that follows.
    virtual void build(const std::vector<HelpDocument>& documents) = 0;
    [[nodiscard]] virtual std::vector<SearchResult> search(std::string_view query,
                                                           const SearchOptions& options) const = 0;
};

// The offline default: an inverted index over weighted fields.
//
// Field weights, and why they are what they are. A word in a title is a much stronger signal than
// the same word in the middle of a paragraph, because titles are written to be searched for. An
// authored `keyword` outranks even a title, because a keyword exists for exactly one reason -- the
// author knew somebody would type this and the prose does not contain it.
class KeywordSearch final : public ISearchBackend {
public:
    struct Weights {
        float keyword = 12.0f;
        float title = 10.0f;
        float tag = 6.0f;
        float identifier = 5.0f; // the document id, which is a phrase people do type
        float summary = 4.0f;
        float heading = 3.0f;
        float body = 1.0f;
        // Scores multiplied by this when the topic is a placeholder, so a gap never outranks a
        // written topic that also matched.
        float gapPenalty = 0.25f;
    };

    // Two constructors rather than one with a default argument: a default argument of `{}`
    // needs Weights complete, and it is not yet complete inside its own enclosing class.
    KeywordSearch() = default;
    explicit KeywordSearch(Weights weights) : weights_(weights) {}

    [[nodiscard]] std::string_view name() const override { return "keyword"; }
    void build(const std::vector<HelpDocument>& documents) override;
    [[nodiscard]] std::vector<SearchResult> search(std::string_view query,
                                                   const SearchOptions& options) const override;

    [[nodiscard]] std::size_t vocabularySize() const { return postings_.size(); }
    [[nodiscard]] std::size_t documentCount() const { return documents_.size(); }

private:
    struct Posting {
        std::uint32_t document = 0;
        float weight = 0.0f; // summed field weight of every occurrence in that document
    };
    struct Candidate {
        float score = 0.0f;
        std::vector<std::string> terms;
    };

    void index(std::uint32_t document, std::string_view text, float weight);
    [[nodiscard]] float inverseDocumentFrequency(const std::vector<Posting>& postings) const;
    // Terms that begin with `prefix`, for the unfinished word at the end of a query.
    [[nodiscard]] std::vector<const std::vector<Posting>*> prefixPostings(std::string_view prefix) const;

    Weights weights_{};
    std::vector<HelpDocument> documents_;
    std::unordered_map<std::string, std::vector<Posting>> postings_;
    // The title of each document, folded and joined, so an exact-title query can be recognised.
    std::vector<std::string> foldedTitles_;
};

// A line of the document containing one of `terms`, trimmed to roughly `budget` characters with
// the match inside it. Empty when nothing matched in the body.
[[nodiscard]] std::string excerptFor(const HelpDocument& doc, const std::vector<std::string>& terms,
                                     std::size_t budget = 180);

} // namespace avgen::help
