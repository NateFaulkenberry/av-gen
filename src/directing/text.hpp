#pragma once

// The few text rules the resolver and the time parser share, in one place so they cannot drift.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::directing::text {

[[nodiscard]] inline std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] inline std::string trim(std::string_view s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])) != 0) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])) != 0) {
        --b;
    }
    return std::string(s.substr(a, b - a));
}

// Splits on anything that is not a letter or a digit: "Umbra-Cap" -> {"umbra", "cap"}. Lowercased.
[[nodiscard]] inline std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (!current.empty()) {
            out.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

// The letters and digits only, lowercased: "Rook", "rook", "ROOK!" all fold to "rook".
[[nodiscard]] inline std::string fold(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

// Levenshtein distance, for "did you mean". Small strings only.
[[nodiscard]] inline std::size_t editDistance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) {
        row[j] = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

// Candidates within a distance that grows with the length of what was typed (1 for up to 4 letters,
// 2 above), nearest first. Suggestions, never a silent correction.
[[nodiscard]] inline std::vector<std::string> nearest(std::string_view typed, const std::vector<std::string>& candidates,
                                                      std::size_t limit = 3) {
    const std::string key = fold(typed);
    const std::size_t allowed = key.size() <= 4 ? 1 : 2;
    std::vector<std::pair<std::size_t, std::string>> scored;
    for (const std::string& c : candidates) {
        const std::size_t d = editDistance(key, fold(c));
        if (d <= allowed) {
            scored.emplace_back(d, c);
        }
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<std::string> out;
    for (auto& [d, c] : scored) {
        if (out.size() >= limit) {
            break;
        }
        if (std::find(out.begin(), out.end(), c) == out.end()) {
            out.push_back(std::move(c));
        }
    }
    return out;
}

} // namespace avgen::directing::text
